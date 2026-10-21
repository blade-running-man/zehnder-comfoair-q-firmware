// GET and POST /api/probe: the RMI probe's report and its start button.
//
// Its own file, like every route group of this component (README.md, Implementation); the access
// check, the body reader and the error shape come from comfoair_http_gate.c and comfoair_http.c
// through comfoair_http_internal.h, so these two handlers answer exactly like every other route.
//
// Neither handler touches the bus. comfoair_probe_task_start() queues the walk and returns at once
// (comfoair_probe_task.h), which matters because esp_http_server serves every socket from ONE task.
#include "comfoair_http_internal.h"

#include <stdint.h>

#include "comfoair_json.h"
#include "comfoair_probe_report.h"
#include "comfoair_probe_task.h"

// --- GET /api/probe ---------------------------------------------------------------------------

typedef struct {
    httpd_req_t *req;
    bool         sent;  // a chunk is on the wire, so the status line can no longer change
} probe_stream_t;

static bool stream_write(void *ctx, const char *text, size_t len)
{
    probe_stream_t *stream = ctx;
    // DO NOT forward an empty piece. httpd_resp_send_chunk() with length 0 IS the terminating
    // chunk (httpd_txrx.c:414 writes "0\r\n" for it, the HTTP/1.1 end-of-body marker), so a
    // renderer that ever emits "" would end the response in the middle of the document and every
    // byte after it would arrive as garbage on a connection the client believes is finished.
    if (len == 0)
        return true;
    if (httpd_resp_send_chunk(stream->req, text, (ssize_t)len) != ESP_OK)
        return false;
    stream->sent = true;
    return true;
}

// STREAMED, chunk by chunk, and DO NOT render it into g_http_scratch first: the report is the whole
// catalogue plus every property the walk read, and it can exceed that 48 KiB buffer -- which would
// then have to be either truncated or refused, and the owner's walk lost either way. Streaming
// needs only the few hundred bytes of the largest single write (comfoair_probe_report.h).
//
// While a walk is running this is the partial report with its progress, rendered under the probe's
// lock. A slow client therefore holds that lock for as long as its socket takes to drain; the walk
// waits rather than racing the renderer over half-written results.
esp_err_t comfoair_http_probe_get(httpd_req_t *req)
{
    if (!comfoair_http_allowed(req))
        return ESP_OK;

    httpd_resp_set_type(req, "application/json");
    probe_stream_t  stream = {.req = req, .sent = false};
    const esp_err_t err    = comfoair_probe_task_render(stream_write, &stream);

    if (err == ESP_OK)
        return httpd_resp_send_chunk(req, NULL, 0);

    if (!stream.sent) {
        // Nothing has gone out, so the status line is still ours to choose. INVALID_STATE is "init
        // never ran" (comfoair_probe_task.h), and init follows a successful comfoair_can_start().
        if (err == ESP_ERR_INVALID_STATE)
            return comfoair_http_send_error(
                req, "503 Service Unavailable",
                "the probe is not running on this device (no CAN bus)");
        return comfoair_http_send_error(req, "500 Internal Server Error", "report failed");
    }

    // Half a document is already out under a 200. DO NOT terminate it with the zero-length chunk:
    // that would hand the client a well-formed chunked response holding broken JSON. ESP_FAIL makes
    // esp_http_server close the socket, which a client sees as a transfer that did not finish.
    return ESP_FAIL;
}

// --- POST /api/probe --------------------------------------------------------------------------

// A probe body is two small numbers. Anything longer is not one, and this buffer is on the single
// httpd task's stack, so it is sized for the document rather than borrowing COMFOAIR_HTTP_BODY_MAX.
#define PROBE_BODY_MAX 128

// One optional byte-sized field. Only "is it a whole number that fits a uint8_t" is asked here, so
// that 257 cannot wrap to node 1. Whether the number is a node the walk may address, or a counter it
// may send, is comfoair_probe_task_start()'s answer (ESP_ERR_INVALID_ARG below) -- asking the same
// question in two places is how the two come to disagree.
static bool read_u8(const char *body, const char *key, uint8_t *out)
{
    uint32_t                   value = 0;
    const comfoair_json_read_t got   = comfoair_json_u32(body, key, &value);
    if (got == COMFOAIR_JSON_MISSING)
        return true;
    if (got != COMFOAIR_JSON_FOUND || value > UINT8_MAX)
        return false;
    *out = (uint8_t)value;
    return true;
}

esp_err_t comfoair_http_probe_post(httpd_req_t *req)
{
    if (!comfoair_http_allowed(req))
        return ESP_OK;

    // Defaults: the ventilation unit itself, and the CAN-id counter bits the command sender uses.
    comfoair_probe_start_t start = {.node = 1, .another_counter = 0};

    // An EMPTY body is a valid request -- "walk the unit with the defaults" -- so it is decided
    // here, before comfoair_http_read_body(), which refuses an empty body as 413.
    if (req->content_len > 0) {
        char body[PROBE_BODY_MAX];
        if (!comfoair_http_read_body(req, body, sizeof body))
            return ESP_OK;
        if (comfoair_json_check(body) != COMFOAIR_JSON_DOC_OK)
            return comfoair_http_send_error(req, "400 Bad Request", "not a JSON object");
        if (!read_u8(body, "node", &start.node))
            return comfoair_http_send_error(req, "422 Unprocessable Entity",
                                            "node must be a whole number");
        if (!read_u8(body, "another_counter", &start.another_counter))
            return comfoair_http_send_error(req, "422 Unprocessable Entity",
                                            "another_counter must be a whole number");
    }

    const esp_err_t err = comfoair_probe_task_start(&start);
    if (err == ESP_OK) {
        httpd_resp_set_status(req, "202 Accepted");
        httpd_resp_set_type(req, "application/json");
        // ACCEPTED, not finished: the walk runs on its own task and GET /api/probe shows its
        // progress.
        return httpd_resp_sendstr(req, "{\"accepted\":true}");
    }
    if (err == ESP_ERR_INVALID_ARG)
        return comfoair_http_send_error(
            req, "422 Unprocessable Entity",
            "node must be 1..63 and not this device; another_counter must be 0..3");
    if (err == ESP_ERR_NOT_FOUND)
        return comfoair_http_send_error(req, "503 Service Unavailable",
                                        "the probe is not running on this device (no CAN bus)");
    if (err == ESP_ERR_INVALID_STATE)
        return comfoair_http_send_error(req, "409 Conflict", "a probe is already running");
    return comfoair_http_send_error(req, "500 Internal Server Error", "the probe did not start");
}
