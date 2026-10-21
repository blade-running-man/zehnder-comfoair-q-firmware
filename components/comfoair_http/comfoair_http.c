// comfoair_http: the server's lifecycle and route table, the state the handlers share, and the
// helpers every handler answers through. Read comfoair_http.h first; the component's shape is in
// README.md, Implementation.
#include "comfoair_http.h"
#include "comfoair_http_internal.h"
#include "comfoair_http_sockets.h"

#include <stdio.h>

#include "comfoair_api.h"
#include "comfoair_net.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "http";


httpd_handle_t g_http_server;

// One document does not fit a stack frame: the state object is one field per entity and the
// entity list is several times that. Static rather than heap so the cost is visible in the map
// file and cannot fail at the worst moment.
// Sized against the real documents, not guessed: the entity list is the big one and it
// grows with pdo_table.py. It was 23 bytes from its old 16 KB when a counter-review caught
// it, and turning "no range" from 0 into null immediately pushed it over -- which the
// renderer now reports rather than serving truncated. comfoair_http_start() logs both
// document sizes at boot so the headroom is a number somebody can watch, not a hope.
#define SCRATCH_SIZE COMFOAIR_API_DOC_BUFFER_SIZE
char g_http_scratch[SCRATCH_SIZE];

TaskHandle_t g_http_ws_task;

// --- handlers ---------------------------------------------------------------------------

esp_err_t comfoair_http_send_json(httpd_req_t *req, size_t len)
{
    httpd_resp_set_type(req, "application/json");
    // The UI is served from this device, so there is no cross-origin case to support. Not
    // sending the header is the decision: "*" is the obvious value and it lets any page in
    // the browser read this device's state.
    return httpd_resp_send(req, g_http_scratch, len);
}

// Reads the whole request body, or answers and returns false. httpd_req_recv can return short,
// so this loops -- a single call is the usual way a body arrives truncated and is then parsed
// as valid JSON that happens to be missing its tail.
bool comfoair_http_read_body(httpd_req_t *req, char *out, size_t cap)
{
    if (req->content_len <= 0 || (size_t)req->content_len >= cap) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"body too large\"}");
        return false;
    }
    size_t got = 0;
    while (got < (size_t)req->content_len) {
        const int n = httpd_req_recv(req, out + got, (size_t)req->content_len - got);
        if (n <= 0) {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"error\":\"truncated body\"}");
            return false;
        }
        got += (size_t)n;
    }
    out[got] = '\0';
    return true;
}

esp_err_t comfoair_http_send_error(httpd_req_t *req, const char *status,
                                   const char *message)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    char body[256];
    // The message is always a LITERAL, from comfoair_wire_strerror() or from a call site here.
    // Nothing the client submitted is ever formatted into it: this body is also what goes in the
    // log ring, and GET /api/log hands that ring to whoever can reach the device -- which on the
    // setup access point is everybody.
    snprintf(body, sizeof body, "{\"error\":\"%s\"}", message);
    return httpd_resp_sendstr(req, body);
}

// --- lifecycle --------------------------------------------------------------------------

esp_err_t comfoair_http_start(const comfoair_http_config_t *config)
{
    if (g_http_server != NULL)
        return ESP_ERR_INVALID_STATE;
    if (config == NULL)
        return ESP_ERR_INVALID_ARG;

    static const httpd_uri_t routes[] = {
        {.uri = "/api/state",      .method = HTTP_GET, .handler = comfoair_http_state_get},
        {.uri = "/api/entities",   .method = HTTP_GET, .handler = comfoair_http_entities_get},
        {.uri = "/api/entities/*", .method = HTTP_GET, .handler = comfoair_http_entity_get},
        {.uri = "/api/log",        .method = HTTP_GET, .handler = comfoair_http_log_get},
        {.uri = "/api/status",     .method = HTTP_GET, .handler = comfoair_http_status_get},
        {.uri = "/api/config",     .method = HTTP_GET, .handler = comfoair_http_config_get},
        {.uri = "/api/config",     .method = HTTP_POST, .handler = comfoair_http_config_post},
        {.uri = "/api/wifi/scan",  .method = HTTP_GET, .handler = comfoair_http_wifi_scan_get},
        {.uri = "/api/provision",  .method = HTTP_POST, .handler = comfoair_http_provision_post},
        // Phase 7. Both are registered BEFORE the wildcard write gate below: esp_http_server
        // matches in registration order, so a route listed after it would never be reached.
        {.uri = "/api/entities/*", .method = HTTP_POST, .handler = comfoair_http_entity_post},
        {.uri = "/api/ops/*",      .method = HTTP_POST, .handler = comfoair_http_operation_post},
        // The RMI probe (comfoair_http_probe.c). POST is before the write gate for the same
        // reason as the two above; GET before the "/*" GET fallback, or it would be the SPA's.
        {.uri = "/api/probe",      .method = HTTP_GET,  .handler = comfoair_http_probe_get},
        {.uri = "/api/probe",      .method = HTTP_POST, .handler = comfoair_http_probe_post},
        // The system summary (comfoair_http_system.c); before the "/*" GET fallback, like the GET
        // above, or the SPA shell would answer it.
        {.uri = "/api/system",     .method = HTTP_GET,  .handler = comfoair_http_system_get},
        // Firmware upload (comfoair_http_ota.c); before the write gate like every POST above.
        {.uri = "/api/ota",        .method = HTTP_POST, .handler = comfoair_http_ota_post},
        {.uri = "/ws", .method = HTTP_GET, .handler = comfoair_http_ws_get, .is_websocket = true},
        // Catch-all for writes, so the policy is applied to every one of them rather than
        // to the ones that happen to have a handler.
        {.uri = "/*",              .method = HTTP_POST,   .handler = comfoair_http_write_gate},
        {.uri = "/*",              .method = HTTP_PUT,    .handler = comfoair_http_write_gate},
        {.uri = "/*",              .method = HTTP_DELETE, .handler = comfoair_http_write_gate},
        {.uri = "/*",              .method = HTTP_PATCH,  .handler = comfoair_http_write_gate},
        // Last: the wildcard would otherwise shadow the API routes above.
        // Before the SPA fallback: a probe path answered with the shell is what breaks the
        // phone's captive-portal detection. Unknown paths fall through to asset_get from here.
        {.uri = "/*",              .method = HTTP_GET,    .handler = comfoair_http_captive_probe_get},
    };

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    // NOT the default of 8. There are more routes than that, and the first registration past the
    // limit returns
    // ESP_ERR_HTTPD_HANDLERS_FULL (httpd_uri.c:196), which ESP_ERROR_CHECK turns into an
    // abort -- a device that reboots every second or two and never serves anything. Derived
    // from the array so adding a route cannot reintroduce it.
    cfg.max_uri_handlers = sizeof routes / sizeof routes[0];
    cfg.server_port      = config->port;
    cfg.lru_purge_enable = true;
    // The client sockets share lwIP's one socket table with httpd's internals and every other
    // socket user, and the assertion is the whole budget -- comfoair_http_sockets.h names each
    // term. DO NOT raise the client count without raising CONFIG_LWIP_MAX_SOCKETS: a short table
    // fails accept() with ENFILE before the LRU purge can run, and the web UI stops answering.
    cfg.max_open_sockets = COMFOAIR_HTTP_CLIENT_SOCKETS;
    _Static_assert(COMFOAIR_HTTP_CLIENT_SOCKETS + COMFOAIR_HTTPD_INTERNAL_SOCKETS
                           + COMFOAIR_OTHER_LWIP_SOCKETS + COMFOAIR_LWIP_SOCKET_SPARE
                       <= CONFIG_LWIP_MAX_SOCKETS,
                   "lwIP's socket table cannot hold every socket user -- see comfoair_http_sockets.h");
    // NOT the IDF default of 1024 (esp_http_server/Kconfig, CONFIG_HTTPD_MAX_REQ_HDR_LEN).
    // A browser overruns a kilobyte of request headers without trying: measured against the
    // firmware on the bench, a request carrying a realistic Chrome header set plus 800 bytes of
    // padding is refused with 431 while 600 bytes still passes, and the real difference between
    // those two numbers is Sec-CH-UA, a long User-Agent and whatever cookies the origin has
    // collected. The owner saw it as "431 Request Header Fields Too Large" on the settings page,
    // that is, on the one screen the device exists to offer.
    //
    // This is a CEILING, not an allocation: httpd grows ra->scratch on demand from NULL and
    // caps it here (httpd_parse.c:221, :239, :712). Raising it costs nothing until a request
    // actually arrives with headers that big, and the httpd task serves sockets one at a time,
    // so there is one such buffer at a time rather than max_open_sockets of them.
    //
    // Set here rather than in sdkconfig.defaults deliberately: the generated sdkconfig is
    // .gitignore'd (sdkconfig.* with sdkconfig.defaults excepted), so a Kconfig value would be
    // reproducible only through a file nobody reads, while this line sits beside the reason.
    cfg.max_req_hdr_len = 2048;
    // NOT the default 4096. POST /api/config is the deepest call in this firmware: a 2 KB request
    // body, the config document and the patch storage, one on top of another. Counter-review
    // measured that within a few hundred bytes of the default, and a stack overflow on ESP-IDF is
    // a panic and a reboot -- on the one operation the owner performs standing in front of the
    // unit, and one that would then repeat. That measurement also counted
    // comfoair_mqtt_reconfigure() underneath all of it; since 2026-09-05 that call is two writes
    // and a notify, so this number now has more headroom than it was given. It is not lowered on
    // that reasoning alone -- the figure came from a measurement and only another measurement
    // should move it.
    cfg.stack_size = 8192;
    // MAX_WS_CLIENTS is sized against this number and must stay >= it -- see the comment there.
    _Static_assert(MAX_WS_CLIENTS >= COMFOAIR_HTTP_CLIENT_SOCKETS, "the client-list array must hold every open socket");
    // Wildcard matching, so one handler can serve every client-side route.
    cfg.uri_match_fn = httpd_uri_match_wildcard;

    const esp_err_t err = httpd_start(&g_http_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err));
        g_http_server = NULL;
        return err;
    }

    // LOGGED, never ESP_ERROR_CHECK'd. The count above is derived from the array, so
    // ESP_ERR_HTTPD_HANDLERS_FULL cannot happen -- but that was true of the version that shipped
    // nine routes against a limit of eight, and it aborted on every boot. A device that serves
    // fifteen routes out of sixteen is one the owner can reach to fix; a device that calls
    // abort() in app_main reboots every second or two for ever and cannot be reached at all
    // (decisions, "Прошивка не загружалась вообще").
    for (size_t i = 0; i < sizeof routes / sizeof routes[0]; i++) {
        const esp_err_t reg = httpd_register_uri_handler(g_http_server, &routes[i]);
        if (reg != ESP_OK)
            ESP_LOGE(TAG, "route %s did not register: %s -- it will answer 404", routes[i].uri,
                     esp_err_to_name(reg));
    }

    if (xTaskCreate(comfoair_http_ws_task, "comfoair_ws", 4096, NULL, 4, &g_http_ws_task) != pdPASS) {
        ESP_LOGE(TAG, "push task did not start; the API still serves over HTTP");
        g_http_ws_task = NULL;
    }

    // Both documents rendered once, so their size is in the boot log. A renderer that
    // returns 0 has not fitted, and that is a defect to see now rather than the first time
    // a browser asks.
    const size_t state_len    = comfoair_api_render_state(g_http_scratch, sizeof g_http_scratch);
    const size_t entities_len = comfoair_api_render_entities(g_http_scratch, sizeof g_http_scratch);
    if (state_len == 0 || entities_len == 0)
        ESP_LOGE(TAG, "a JSON document does not fit in %u B -- raise COMFOAIR_API_DOC_BUFFER_SIZE (comfoair_api.h)",
                 (unsigned)SCRATCH_SIZE);
    else
        ESP_LOGI(TAG, "documents: state %u B, entities %u B, buffer %u B",
                 (unsigned)state_len, (unsigned)entities_len, (unsigned)SCRATCH_SIZE);

    // Asked, not remembered -- the same call the policy makes on every request.
    ESP_LOGI(TAG, "listening on :%u (%s)", config->port,
             comfoair_net_is_provisioned() ? "network" : "access point");
    return ESP_OK;
}

esp_err_t comfoair_http_stop(void)
{
    if (g_http_ws_task != NULL) {
        vTaskDelete(g_http_ws_task);
        g_http_ws_task = NULL;
    }
    if (g_http_server == NULL)
        return ESP_OK;
    const esp_err_t err = httpd_stop(g_http_server);
    g_http_server = NULL;
    return err;
}
