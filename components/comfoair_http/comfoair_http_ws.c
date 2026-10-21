// comfoair_http: the /ws push channel -- the handler, the push task and the notification that
// wakes it. Read comfoair_http.h first; the component's shape is in README.md, Implementation.
#include "comfoair_http.h"
#include "comfoair_http_internal.h"

#include <string.h>

#include "comfoair_api.h"
#include "comfoair_net.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "http";

// A full sweep of the unit's PDOs arrives as dozens of frames within a second or two.
// Without a floor between pushes, one sweep becomes dozens of full state documents on every
// open socket, on a device with seven client sockets and 512 KB of RAM.
#define WS_MIN_INTERVAL_MS 250
// The client closes a socket that has said nothing for 35 s (web/src/api/ws.ts). On a unit
// whose readings are steady, deltas can legitimately be empty for minutes, so the server
// has to volunteer a keep-alive or a healthy connection is torn down and rebuilt for ever.
#define WS_KEEPALIVE_MS 15000

static TickType_t   s_last_sent;
static volatile bool s_ws_dirty;

// esp_http_server answers the WebSocket handshake ITSELF and says so in as many words:
// "If the request is websocket handshake, then do not call the uri->handler"
// (httpd_uri.c:362). The handler below therefore never sees a handshake, which left /ws as
// the one path the access policy did not cover AND left the socket list permanently empty,
// so nothing was ever pushed. Both halves move here, where the server does call us.
// The handler for /ws.
//
// It is NOT called for the handshake: esp_http_server answers that itself and says so --
// "If the request is websocket handshake, then do not call the uri->handler"
// (httpd_uri.c:362). So the access policy cannot be applied to the upgrade from here, and
// that is a real gap while GET is open to everyone anyway; the moment a password exists,
// phase 5 has to authorise the upgrade -- a ticket fetched over the authenticated REST API
// and presented as a query parameter is the usual answer. Written down rather than left to
// be rediscovered.
//
// What does reach here is a data frame from a client, and every one is read and dropped.
// The channel is push-only by design: a command arriving over it would be an endpoint the
// REST API does not have, which is precisely the privileged path AGENTS.md criterion 4 forbids.
esp_err_t comfoair_http_ws_get(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        // Reached only WITHOUT an Upgrade header, since the server handles the real
        // handshake. Such a caller is not a WebSocket client.
        if (!comfoair_http_allowed(req))
            return ESP_OK;
        httpd_resp_set_status(req, "426 Upgrade Required");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"websocket upgrade required\"}");
        return ESP_OK;
    }

    // The payload must actually be DRAINED. httpd_ws_recv_frame with max_len 0 reads only
    // the header and leaves the body in the socket (httpd_ws.c:359-367), so the next read
    // parses body bytes as a frame header and the stream desynchronises for good.
    uint8_t buf[128];
    httpd_ws_frame_t frame = {0};
    frame.type    = HTTPD_WS_TYPE_TEXT;
    frame.payload = buf;

    const esp_err_t err = httpd_ws_recv_frame(req, &frame, sizeof buf);
    if (err != ESP_OK)
        return err;
    // Bigger than anything a push-only channel has a use for. ESP_FAIL is what closes the
    // socket; ESP_OK would leave it open and out of step with the stream.
    return frame.len > sizeof buf ? ESP_FAIL : ESP_OK;
}

// Runs on the server's own context via httpd_queue_work: esp_http_server sockets may only
// be written from there, not from whichever task noticed the change.
static void ws_send_state(void *arg)
{
    (void)arg;

    // The server is asked which of its sockets are WebSockets, rather than a list being
    // kept here. esp_http_server answers the handshake itself and does not call the URI
    // handler for it (httpd_uri.c:362), so a hand-kept list is never populated and the
    // channel is silently dead -- which is exactly what counter-review found. Asking also
    // means a closed tab disappears on its own instead of holding a slot until the next
    // failed send.
    size_t count = MAX_WS_CLIENTS;
    int    fds[MAX_WS_CLIENTS];
    if (httpd_get_client_list(g_http_server, &count, fds) != ESP_OK)
        return;

    size_t websockets = 0;
    for (size_t i = 0; i < count; i++)
        if (httpd_ws_get_fd_info(g_http_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET)
            websockets++;

    // A live WebSocket is somebody LOOKING at the page, and it is the only evidence of that the
    // device gets. A phone resting on the settings screen sends no HTTP request whatever, so
    // comfoair_http_allowed() -- the other caller of this -- never fires for it, and the
    // five-minute setup window closed under the hands of the person reading it. The list above is
    // already asked for every round, so the fact costs nothing extra to learn.
    //
    // Before the early return below on purpose: "nothing changed to push" is a quiet unit, not an
    // absent client, and the reader of a page showing an unchanging number is exactly the person
    // this window is for.
    //
    // DO NOT hoist this out of the `websockets > 0` test. Called unconditionally it reports a
    // client every round for ever, and the setup window of a device with nobody in the room would
    // then end only at the one-hour ceiling. The machine refuses to reopen a window that has
    // already closed (comfoair_prov_on_client_seen(), comfoair_provision.c), so calling it on
    // every round that this IS true is safe.
    if (websockets > 0)
        comfoair_net_note_client();

    // A client that has just connected needs the whole state -- it cannot ask for what it
    // missed. Since the socket list is asked for rather than kept, "somebody new" is
    // detected by the count growing, and everyone gets the full document that round. The
    // cost is one redundant full send to existing clients when a second tab opens; the
    // alternative is per-socket bookkeeping that the handshake gives us no hook to start.
    static size_t s_last_websockets;
    const bool    someone_new = websockets > s_last_websockets;
    s_last_websockets = websockets;

    size_t len = comfoair_api_render_push(g_http_scratch, sizeof g_http_scratch, someone_new);
    if (len == 0) {
        // Nothing changed. The socket still has to say something now and then, or the
        // client's silence timer closes a connection that is perfectly healthy -- on a
        // quiet unit that would be an endless reconnect loop.
        // FreeRTOS ticks rather than esp_timer: monotonic, already available here, and one
        // fewer component dependency for a keep-alive clock.
        if (xTaskGetTickCount() - s_last_sent < pdMS_TO_TICKS(WS_KEEPALIVE_MS))
            return;
        static const char ping[] = "{\"type\":\"ping\"}";
        memcpy(g_http_scratch, ping, sizeof ping);
        len = sizeof ping - 1;
    }
    s_last_sent = xTaskGetTickCount();
    httpd_ws_frame_t frame = {0};
    frame.type    = HTTPD_WS_TYPE_TEXT;
    frame.payload = (uint8_t *)g_http_scratch;
    frame.len     = len;

    for (size_t i = 0; i < count; i++) {
        // A plain HTTP connection must never be sent a WebSocket frame; it would read the
        // binary framing as an HTTP response.
        if (httpd_ws_get_fd_info(g_http_server, fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET)
            continue;
        if (httpd_ws_send_frame_async(g_http_server, fds[i], &frame) != ESP_OK)
            ESP_LOGD(TAG, "ws client %d gone", fds[i]);
    }
}

void comfoair_http_ws_task(void *arg)
{
    (void)arg;
    for (;;) {
        // Waking on a timeout as well as on a notification: a quiet unit produces no
        // notifications at all, and that is exactly when the keep-alive is needed.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(WS_KEEPALIVE_MS));
        if (g_http_server == NULL)
            continue;
        s_ws_dirty = false;
        httpd_queue_work(g_http_server, ws_send_state, NULL);
        // The floor between pushes. Coalescing here rather than at the caller means a burst
        // of PDO frames costs one document, not one per frame.
        vTaskDelay(pdMS_TO_TICKS(WS_MIN_INTERVAL_MS));
    }
}

void comfoair_http_notify_state_changed(void)
{
    s_ws_dirty = true;
    if (g_http_ws_task != NULL)
        xTaskNotifyGive(g_http_ws_task);
}
