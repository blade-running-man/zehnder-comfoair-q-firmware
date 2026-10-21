// comfoair_http: the access check every handler calls first, the path normalisation every
// comparison uses, and the device identity the documents report. Read comfoair_http.h first;
// the component's shape is in README.md, Implementation.
#include "comfoair_http.h"
#include "comfoair_http_internal.h"

#include <string.h>

#include "comfoair_auth.h"
#include "comfoair_net.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// req->uri is the whole request target the client sent -- "/app.js?v=2", and in
// absolute-form even "http://host/app.js". esp_http_server routes on the path component
// alone (httpd_uri.c:307-310) but hands the handler the raw string, so every comparison
// here has to strip the query first. Without this the policy's exact matches simply never
// fire on a client that appends a parameter, and /app.js?v=2 fell through to the SPA shell.
const char *comfoair_http_request_path(httpd_req_t *req, char *out, size_t cap)
{
    const char *uri = req->uri;
    // Absolute-form: skip past the authority so the path starts at its slash.
    if (strncmp(uri, "http://", 7) == 0) {
        const char *slash = strchr(uri + 7, '/');
        uri = slash != NULL ? slash : "/";
    }
    size_t n = 0;
    while (uri[n] && uri[n] != '?' && uri[n] != '#' && n + 1 < cap) {
        out[n] = uri[n];
        n++;
    }
    out[n] = '\0';
    // A trailing slash on anything but the root names the same resource; folding it keeps
    // "/api/config/" from quietly missing the rule written for "/api/config".
    if (n > 1 && out[n - 1] == '/')
        out[n - 1] = '\0';
    return out;
}

// Ignores `stored` on purpose: the record lives in comfoair_net and is checked there, in
// constant time. This exists so the hash has no reason to travel.
static bool verify_against_device(const char *stored, const char *candidate)
{
    (void)stored;
    return comfoair_net_check_password(candidate);
}

// --- policy -----------------------------------------------------------------------------

static comfoair_http_method_t method_of(const httpd_req_t *req)
{
    switch (req->method) {
    case HTTP_GET:  return COMFOAIR_HTTP_GET;
    case HTTP_POST: return COMFOAIR_HTTP_POST;
    default:        return COMFOAIR_HTTP_OTHER;
    }
}

// Who this device is, for any document that reports it. ONE builder, because the two callers
// (comfoair_http_config.c, comfoair_http_read.c) would otherwise be two chances for the identity
// to be spelled differently -- and it is already spelled once more, in comfoair_mqtt_client.c,
// from these same helpers.
//
// The two buffers are the CALLER'S: comfoair_wire_device_t borrows its strings and does not copy
// them, so returning a struct pointing at this function's own stack would be a dangling read.
comfoair_wire_device_t comfoair_http_this_device(char id_out[COMFOAIR_CONFIG_DEVICE_ID_LEN + 1],
                                                 char mac_out[18])
{
    comfoair_net_device_id(id_out);
    comfoair_net_mac_string(mac_out);
    const esp_app_desc_t *app = esp_app_get_description();
    return (comfoair_wire_device_t){
        .device_id  = id_out,
        .mac        = mac_out,
        .sw_version = app != NULL ? app->version : "unknown",
    };
}

// Every handler goes through this. There is no second door: a handler that forgets to call
// it is the bug this arrangement exists to make obvious.
bool comfoair_http_allowed(httpd_req_t *req)
{
    char path[128];
    comfoair_http_request_path(req, path, sizeof path);

    // Somebody is talking to us. On the access point this extends the setup window: it closing
    // while the owner is in the next room reading the password off their router is the most
    // annoying failure in the design, and it is not an attack.
    comfoair_net_note_client();

    // Generous but bounded: "Basic " plus base64 of a 128-byte password and a name. A header
    // longer than this is not a credential, and reading it into a fixed buffer is the whole
    // reason comfoair_auth parses rather than trusting.
    char        header_buf[512];
    const char *header =
        httpd_req_get_hdr_value_str(req, "Authorization", header_buf, sizeof header_buf) == ESP_OK
            ? header_buf
            : NULL;

    const bool password_set = comfoair_net_password_set();
    // The stored record NEVER reaches this layer. comfoair_auth wants a `stored` string only so
    // it can tell "nothing is set" from "something is"; the comparison itself goes through the
    // verifier below, which hands the candidate to comfoair_net and gets back a yes or no. A
    // caller that could read the record could also log it, and the log is served by /api/log.
    const comfoair_auth_outcome_t auth =
        comfoair_auth_check(header, password_set ? "set" : "", verify_against_device);

    comfoair_http_ctx_t ctx = {
        // NOT "are there credentials in NVS". While an OPEN access point is on the air the device
        // counts as unclaimed whatever the flash holds, because saving credentials otherwise hands
        // POST /api/config to whoever is still on that access point -- the same passer-by who
        // supplied them (docs/architecture/security.md, "Trust model").
        .provisioned   = comfoair_net_is_provisioned(),
        .password_set  = password_set,
        // GRANTED and nothing else. comfoair_auth.h states the rule outright: "a caller bridging
        // this into its `authenticated` field must map only GRANTED to true", because
        // COMFOAIR_AUTH_NO_PASSWORD means there was no question to answer -- told that a stranger
        // on the open access point had authenticated, the policy would hand them the device.
        // DO NOT write COMFOAIR_AUTH_OK here: that is a value of comfoair_auth_parse_t, the OTHER
        // enum in that header, and it compiles because both are zero. It would keep working until
        // somebody adds a case to either enum.
        .authenticated = auth.result == COMFOAIR_AUTH_GRANTED,
    };

    // A wrong password costs a fixed wait. A delay and not a lockout: locking out is a denial
    // of service against the owner, who is the person most likely to mistype.
    if (password_set && header != NULL && auth.result != COMFOAIR_AUTH_GRANTED && auth.delay_ms > 0)
        vTaskDelay(pdMS_TO_TICKS(auth.delay_ms));

    switch (comfoair_http_check(method_of(req), path, ctx)) {
    case COMFOAIR_HTTP_ALLOW:
        return true;
    case COMFOAIR_HTTP_UNAUTHORIZED:
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"comfoair\"");
        httpd_resp_sendstr(req, "{\"error\":\"unauthorized\"}");
        return false;
    case COMFOAIR_HTTP_FORBIDDEN:
    default:
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"forbidden\"}");
        return false;
    }
}
