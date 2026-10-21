// POST /api/ota: a raw application/octet-stream firmware image, streamed into the free OTA slot.
//
// Its own file, like every route group of this component (README.md, Implementation). WHAT a wrong
// file is and what it is told are comfoair_ota's (pure, host-tested in test_ota); this file only
// asks those questions, maps IDF's answers onto comfoair_ota_fault_t, and moves bytes. The order
// matters and is the point of the handler: every check that can refuse without touching flash runs
// before esp_ota_begin(), so a wrong file never erases a sector of the spare slot.
//
// While an upload runs, every other HTTP request waits: esp_http_server serves all sockets from ONE
// task (httpd_thread, httpd_main.c:330). The CAN reader and the poller are their own tasks and keep running.
#include "comfoair_http_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "comfoair_api.h"
#include "comfoair_led.h"
#include "comfoair_led_driver.h"
#include "comfoair_ota.h"
#include "comfoair_ota_runtime.h"
#include "comfoair_phase_monitor.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"

static const char *TAG = "comfoair_ota";

// One flash sector. esp_ota_write() with OTA_WITH_SEQUENTIAL_WRITES erases each sector as the write
// first reaches it (esp_ota_ops.c:314-326), so writing whole, aligned sectors erases exactly one per
// call. HEAP, not stack: the httpd stack is 8192 (comfoair_http.c) and already budgeted.
#define OTA_CHUNK 4096
// Retries of a recv that timed out. Each try waits recv_wait_timeout (the default 5 s, set as
// SO_RCVTIMEO in httpd_main.c:85), so 1 + 3 tries tolerate 20 s of silence from a client on weak
// Wi-Fi before the upload is called truncated. DO NOT raise recv_wait_timeout instead: it is per
// socket for every route, and a longer one makes every stalled request hold the one httpd task.
#define OTA_RECV_RETRIES 3
// The retries above bound one SILENCE, not the request: the counter starts again after every
// recv that returns a byte, so a client sending 1 byte every 4.9 s would never be cut off, and
// for as long as it holds the one httpd task the web UI and /api/status are not answered. So the
// upload as a whole also has a wall-clock deadline: max(OTA_DEADLINE_MIN_US, content_len at
// OTA_FLOOR_BYTES_PER_S) -- a 1.3 MB image gets ~170 s. 8 KiB/s is far below what any working
// Wi-Fi link moves, even a weak one retrying at the lowest rate, so only a client that is
// stalling meets it; the 120 s minimum leaves a small image room for retransmission bursts.
// DO NOT re-arm the deadline per recv: that is exactly the per-recv counter this replaces.
#define OTA_FLOOR_BYTES_PER_S (8 * 1024)
#define OTA_DEADLINE_MIN_US   (120 * 1000000LL)
// A refusal is read to its end only so its sentence arrives (refuse()): a courtesy, not worth the
// server. 30 s, fixed -- DO NOT scale it with content_len like the upload: the length is the
// client's claim, OTA_DRAIN_MAX bounds the bytes, and this bounds the time.
#define OTA_DRAIN_DEADLINE_US (30 * 1000000LL)
// recv_some()'s answer once the deadline has passed; below every HTTPD_SOCK_ERR_* (-1..-3).
#define OTA_RECV_LATE (-100)
// The largest refused body still read to its end so the client gets the answer (see refuse()).
// 16 MiB covers this firmware's .elf (12.7 MB), the likeliest wrong file that is also too large.
#define OTA_DRAIN_MAX (16u * 1024u * 1024u)
// The 200 must reach the browser before the restart pulls the network away.
#define OTA_RESTART_DELAY_MS 1000

// The pre-flight is read into the start of the first chunk and must be wholly promised by any
// body check_length() lets through, so a short body can never stall the pre-flight read.
_Static_assert(COMFOAIR_OTA_PREFLIGHT_BYTES <= OTA_CHUNK, "the pre-flight fits the first chunk");
_Static_assert(COMFOAIR_OTA_PREFLIGHT_BYTES <= COMFOAIR_OTA_MIN_IMAGE_BYTES, "pre-flight <= minimum");

// NOT atomic: one httpd task runs every handler, so this is never read and written at once. It is
// not for two uploads in parallel (those cannot happen) but for the one AFTER a success: the slot
// just written is still the update slot until the restart, and a second upload would erase the
// image the device is about to boot. So it stays set once an image is installed.
static bool s_busy;

static esp_err_t send_fault(httpd_req_t *req, comfoair_ota_fault_t fault)
{
    ESP_LOGW(TAG, "upload refused: %s", comfoair_ota_message(fault));
    // Escaped, not formatted raw as comfoair_http_send_error() does: these sentences live in
    // another component, and one quote character there would otherwise break this JSON.
    char escaped[256];
    char body[288];
    comfoair_api_escape_json(comfoair_ota_message(fault), escaped, sizeof escaped);
    snprintf(body, sizeof body, "{\"error\":\"%s\"}", escaped);
    httpd_resp_set_status(req, comfoair_ota_status_line(fault));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}

// httpd_req_recv() that survives a slow link: a timeout is retried, anything else is final.
// <= 0 back means the client is gone (0 is an orderly close mid-body, negative a socket error),
// OTA_RECV_LATE that `deadline_us` (esp_timer time) has passed. Checked before every try, so the
// deadline is overshot by at most one recv_wait_timeout (5 s).
static int recv_some(httpd_req_t *req, char *buf, size_t len, int64_t deadline_us)
{
    int n = HTTPD_SOCK_ERR_TIMEOUT;
    for (int attempt = 0; attempt <= OTA_RECV_RETRIES && n == HTTPD_SOCK_ERR_TIMEOUT; attempt++) {
        if (esp_timer_get_time() >= deadline_us)
            return OTA_RECV_LATE;
        n = httpd_req_recv(req, buf, len);
    }
    return n;
}

// A refusal while the client is still sending `left` bytes.
//
// The body has to be READ before the answer can be trusted to arrive. A handler that returns
// ESP_FAIL gets its socket closed at once (httpd_uri.c:367-370, httpd_main.c:266-267), and lwIP
// closes a socket with unread data by sending RST (tcp.c:352-361) -- the browser then reports a
// connection reset, not the sentence. Returning ESP_OK with bytes left makes httpd_req_delete()
// drain them itself (httpd_parse.c:843-851), but CONFIG_HTTPD_PURGE_BUF_LEN = 32 bytes per call and
// one esp_event post per call (httpd_txrx.c:640-646): ~60 000 posts for a 1.9 MB image. So it is
// drained here, OTA_CHUNK at a time, and only then answered.
//
// Above OTA_DRAIN_MAX the answer is sent with Connection: close and the socket dropped: draining a
// body of any size the client claims would let one request hold the server's only task for ever.
// A drain still running at OTA_DRAIN_DEADLINE_US is abandoned and the socket closed unanswered.
// `buf` NULL (no buffer yet): answered at once and left to httpd_req_delete()'s slow drain.
static esp_err_t refuse(httpd_req_t *req, char *buf, size_t left, comfoair_ota_fault_t fault)
{
    if (left > OTA_DRAIN_MAX) {
        httpd_resp_set_hdr(req, "Connection", "close");
        send_fault(req, fault);
        return ESP_FAIL;
    }
    const size_t  owed     = left;
    const int64_t deadline = esp_timer_get_time() + OTA_DRAIN_DEADLINE_US;
    while (buf != NULL && left > 0) {
        const int n = recv_some(req, buf, left < OTA_CHUNK ? left : OTA_CHUNK, deadline);
        if (n == OTA_RECV_LATE)
            ESP_LOGW(TAG, "refusal (%s) not drained in time: %u of %u bytes received; closing",
                     comfoair_ota_message(fault), (unsigned)(owed - left), (unsigned)owed);
        if (n <= 0)
            return ESP_FAIL;  // gone or too slow; either way nobody gets to read an answer
        left -= (size_t)n;
    }
    send_fault(req, fault);
    return ESP_OK;
}

// The client stopped sending, or is past the upload's deadline (`why` == OTA_RECV_LATE). The
// socket is dead or stalled, so nothing is drained; the answer is attempted for a client that is
// merely slow, then the socket is closed.
static esp_err_t truncated(httpd_req_t *req, esp_ota_handle_t handle, size_t got, size_t total,
                           int why)
{
    if (handle != 0)
        esp_ota_abort(handle);
    if (why == OTA_RECV_LATE)
        ESP_LOGW(TAG, "upload cut off by its deadline at %u of %u bytes", (unsigned)got,
                 (unsigned)total);
    else
        ESP_LOGW(TAG, "upload stopped at %u of %u bytes", (unsigned)got, (unsigned)total);
    httpd_resp_set_hdr(req, "Connection", "close");
    send_fault(req, COMFOAIR_OTA_TRUNCATED);
    return ESP_FAIL;
}

// "A firmware partition is being written -- do not cut the power": the OTA phase (for
// /api/status and the 1 Hz phase task) and the OVR_OTA overlay (for the pixel) together. BOTH, and
// the overlay is not redundant: the OTA phase's own look is PROGRESS at 0 % (no overlay arg), which
// draws dark (comfoair_led_pattern.c, PROGRESS), and the phase reaches the LED only on the
// monitor's next tick, up to a second late. The overlay is drawn on the next 25 ms render.
//
// On from just before esp_ota_begin() -- the first call that erases anything -- and off on every
// exit that did not install an image (comfoair_http_ota_post). On SUCCESS IT STAYS ON until the
// restart: the device is about to reboot into the new image, and "do not cut the power" is still
// the right instruction for that second. DO NOT clear it after the 200 "for tidiness".
static void indicate(bool active, unsigned percent)
{
    comfoair_phase_monitor_note_ota(active);
    if (active)
        comfoair_led_override_push(COMFOAIR_LED_OVR_OTA, percent);
    else
        comfoair_led_override_clear(COMFOAIR_LED_OVR_OTA);
}

// Logged and drawn at the same 25 % points: the overlay's duty cycle IS the percentage, and four
// steps are as many as an eye reads off a blinking pixel. Finer pushes would buy nothing visible.
static void report_progress(size_t done, size_t total, unsigned *quarter)
{
    while (*quarter < 4 && (uint64_t)done * 4u >= (uint64_t)total * (*quarter)) {
        if (*quarter > 0) {
            ESP_LOGI(TAG, "%u%% (%u of %u bytes)", *quarter * 25u, (unsigned)done, (unsigned)total);
            indicate(true, *quarter * 25u);
        }
        (*quarter)++;
    }
}

// Everything after the access check and the busy flag. `buf` is OTA_CHUNK bytes. Sets *installed
// only once the boot partition points at the new image.
static esp_err_t upload(httpd_req_t *req, char *buf, bool *installed)
{
    const size_t total = req->content_len;

    // Before the slot lookup: during probation esp_ota_begin() refuses anyway
    // (esp_ota_ops.c:164-170), and saying why beats a flash error after the file has been read.
    if (comfoair_ota_runtime_unconfirmed())
        return refuse(req, buf, total, COMFOAIR_OTA_UNCONFIRMED);

    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    if (update == NULL)
        return refuse(req, buf, total, COMFOAIR_OTA_NO_SLOT);

    comfoair_ota_fault_t fault = comfoair_ota_check_length(total > 0, total, update->size);
    if (fault != COMFOAIR_OTA_OK)
        return refuse(req, buf, total, fault);

    ESP_LOGI(TAG, "upload of %u bytes into %s (%u bytes)", (unsigned)total, update->label,
             (unsigned)update->size);
    const int64_t started  = esp_timer_get_time();
    // total is at most the slot size here (check_length), so the product fits easily.
    const int64_t budget   = (int64_t)total * 1000000 / OTA_FLOOR_BYTES_PER_S;
    const int64_t deadline = started + (budget > OTA_DEADLINE_MIN_US ? budget : OTA_DEADLINE_MIN_US);

    // The image header AND the app descriptor (0x70 bytes) go into the start of the first chunk,
    // so the chunk is still written whole and a foreign image is refused before anything is
    // erased. Never more than the body promises: unreachable after check_length() (static
    // asserts above), but a shorter read just leaves check_app_desc() answering WRONG_PROJECT
    // instead of waiting out the deadline for bytes that will never come.
    const size_t head = total < COMFOAIR_OTA_PREFLIGHT_BYTES ? total : COMFOAIR_OTA_PREFLIGHT_BYTES;
    size_t       filled = 0;
    while (filled < head) {
        const int n = recv_some(req, buf + filled, head - filled, deadline);
        if (n <= 0)
            return truncated(req, 0, filled, total, n);
        filled += (size_t)n;
    }
    char version[COMFOAIR_HTTP_OTA_VERSION_CAP];
    fault = comfoair_http_ota_preflight((const uint8_t *)buf, filled, update->label, version);
    if (fault != COMFOAIR_OTA_OK)
        return refuse(req, buf, total - filled, fault);

    // The first call that touches flash. SEQUENTIAL_WRITES: erase as the write advances, because
    // erasing the whole 1984 KiB slot up front blocks for seconds with the cache disabled.
    // Indicated BEFORE it, so no erase ever happens under a light that says something else; every
    // refusal above left flash untouched and so is never indicated at all.
    indicate(true, 0);
    esp_ota_handle_t handle = 0;
    esp_err_t        err    = esp_ota_begin(update, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
        return refuse(req, buf, total - filled,
                      err == ESP_ERR_OTA_ROLLBACK_INVALID_STATE ? COMFOAIR_OTA_UNCONFIRMED
                                                                : COMFOAIR_OTA_FLASH_ERROR);
    }

    size_t   done    = 0;  // bytes handed to esp_ota_write()
    unsigned quarter = 0;
    while (done < total) {
        const size_t want = total - done < OTA_CHUNK ? total - done : OTA_CHUNK;
        while (filled < want) {
            const int n = recv_some(req, buf + filled, want - filled, deadline);
            if (n <= 0)
                return truncated(req, handle, done + filled, total, n);
            filled += (size_t)n;
        }
        err = esp_ota_write(handle, buf, filled);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write at %u: %s", (unsigned)done, esp_err_to_name(err));
            esp_ota_abort(handle);
            return refuse(req, buf, total - done - filled,
                          err == ESP_ERR_OTA_VALIDATE_FAILED ? COMFOAIR_OTA_VERIFY_FAILED
                                                             : COMFOAIR_OTA_FLASH_ERROR);
        }
        done += filled;
        filled = 0;
        report_progress(done, total, &quarter);
    }

    // Reached only with done == total: every byte Content-Length promised has been written, so a
    // short upload can never get this far. esp_ota_end() is the decisive check -- segments,
    // checksum and the appended SHA-256 (esp_image_verify) -- and it frees the handle either way.
    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(err));
        return send_fault(req, err == ESP_ERR_OTA_VALIDATE_FAILED ? COMFOAIR_OTA_VERIFY_FAILED
                                                                  : COMFOAIR_OTA_FLASH_ERROR);
    }
    err = esp_ota_set_boot_partition(update);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(err));
        return send_fault(req, COMFOAIR_OTA_VERIFY_FAILED);
    }
    *installed = true;

    const unsigned ms = (unsigned)((esp_timer_get_time() - started) / 1000);
    ESP_LOGI(TAG, "installed %u bytes into %s in %u ms", (unsigned)total, update->label, ms);

    // Rendered into the chunk buffer, idle now, not onto the stack: the escaped version can be 6x
    // its 32 chars (\u00XX), and the httpd stack is budgeted (OTA_CHUNK above). Escaped because
    // the version is the uploaded file's bytes, whatever they are.
    char *escaped = buf;
    char *body    = buf + 256;
    comfoair_api_escape_json(version, escaped, 256);
    snprintf(body, OTA_CHUNK - 256,
             "{\"ok\":true,\"rebooting\":true,\"partition\":\"%s\",\"bytes\":%u,\"version\":\"%s\"}",
             update->label, (unsigned)total, escaped);
    httpd_resp_set_type(req, "application/json");
    const esp_err_t sent = httpd_resp_sendstr(req, body);

    // Armed even if the answer did not go out: the boot partition already points at the new
    // image, and a device left running the old one would boot the new one at some random later
    // restart instead of now, while the owner is watching.
    // No restart coming, so the light must stop saying one is: the boot partition is already set,
    // and cutting the power now is exactly as safe as at any other moment.
    if (comfoair_ota_runtime_restart_later(OTA_RESTART_DELAY_MS) != ESP_OK) {
        ESP_LOGE(TAG, "restart not armed; the new image boots at the next restart");
        indicate(false, 0);
    }
    return sent;
}

esp_err_t comfoair_http_ota_post(httpd_req_t *req)
{
    if (!comfoair_http_allowed(req))
        return ESP_OK;

    // No buffer yet, so these two leave the body to httpd_req_delete()'s slow drain (refuse()) --
    // acceptable for answers that are never the owner's normal path. BUSY is only ever seen in
    // the second before the restart.
    if (s_busy)
        return refuse(req, NULL, req->content_len, COMFOAIR_OTA_BUSY);
    char *buf = malloc(OTA_CHUNK);
    if (buf == NULL) {
        ESP_LOGE(TAG, "no %u bytes for the upload buffer", (unsigned)OTA_CHUNK);
        return refuse(req, NULL, req->content_len, COMFOAIR_OTA_FLASH_ERROR);
    }

    s_busy               = true;
    bool            done = false;
    const esp_err_t ret  = upload(req, buf, &done);
    free(buf);
    // Released on every failure; kept on success until the restart (see s_busy).
    s_busy = done;
    // HERE, ONCE, AND NOT AT EACH return INSIDE upload(): abort, truncation, a failed verify and
    // the deadline all leave through this line, and so will an exit added later. Harmless when
    // nothing was indicated (a refusal before esp_ota_begin): both calls are idempotent stores.
    // DO NOT add the same call to the BUSY refusal above: that refusal arrives while a SUCCESSFUL
    // image waits for its restart, and clearing then would take "do not cut the power" off early.
    if (!done)
        indicate(false, 0);
    return ret;
}
