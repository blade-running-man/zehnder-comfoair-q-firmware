// What the translation units of comfoair_http share with each other, and nobody else.
//
// PRIVATE: it lives beside the sources and not in include/, so no other component can reach the
// access check or answer a request through these helpers. A handler outside this component would be
// a second door past comfoair_policy, which is the thing comfoair_http_allowed() exists to prevent.
//
// The names carry the component prefix because dropping `static` made them link-visible: a bare
// `allowed` or `send_error` in the global namespace is one other component away from a duplicate
// symbol, or from silently binding to the wrong function. Variables shared by two files are
// g_http_*, after comfoair_mqtt's g_ rule; the component infix keeps them apart from
// comfoair_mqtt's.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "comfoair_api.h"
#include "comfoair_config.h"
#include "comfoair_ota.h"
#include "comfoair_wire.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Bounded because the body is read into a fixed buffer from an open network. A configuration
// document is a few hundred bytes; anything above this is not one.
#define COMFOAIR_HTTP_BODY_MAX 2048

// The push channel. esp_http_server serves max_open_sockets (7) clients at once; its three internal
// sockets come on top of those, not out of them. This is the browser's channel only: MQTT
// subscribers get their own transport, and handing them a socket each would exhaust the server for
// the sake of a client that has a better option. Sized against max_open_sockets in
// comfoair_http_start(), NOT against how many WebSockets are expected. httpd_get_client_list()
// returns ESP_ERR_INVALID_ARG the moment it finds one more open socket than the caller's array
// holds (httpd_main.c:186-193) -- it does not truncate. With four here and seven sockets
// configured, a browser tab holding a WebSocket plus a few parallel fetches reached five and the
// push channel stopped pushing to anybody, for that round and every round while five stayed open.
// Found by counter-review; it is phase 4 code and it breaks the live readings phase 7 exists to
// make useful.
#define MAX_WS_CLIENTS 7

// The server, in comfoair_http.c. Written by comfoair_http_start()/comfoair_http_stop() only;
// read by the push task and by ws_send_state() on the httpd task. NULL while not running.
extern httpd_handle_t g_http_server;

// The one document buffer, in comfoair_http.c (sizing in the comment there). NO LOCK: every
// request-time user runs on the httpd task -- the handlers, and ws_send_state() through
// httpd_queue_work() -- and esp_http_server serves every socket from that one task, so it has one
// user at a time. The one exception is the boot-time size check in comfoair_http_start(), which
// renders both documents into it on the caller's task after httpd_start() has returned. Sized here
// because `sizeof g_http_scratch` is what every renderer is handed.
extern char g_http_scratch[COMFOAIR_API_DOC_BUFFER_SIZE];

// The push task, in comfoair_http.c. Created by comfoair_http_start(), deleted by
// comfoair_http_stop(); notified from any task by comfoair_http_notify_state_changed(). NULL when
// it is not running.
extern TaskHandle_t g_http_ws_task;

// The access check. Every handler calls it first and returns ESP_OK at once on false: the 401 or
// 403 has already been sent. There is no second door -- a handler that forgets to call it is the
// bug this arrangement exists to make obvious.
bool comfoair_http_allowed(httpd_req_t *req);

// The access check's neighbours, in comfoair_http_gate.c: the request target reduced to its path
// (no authority, query, fragment or trailing slash) in `out`, and this device's identity built into
// the CALLER'S two buffers, which the returned struct borrows.
const char *comfoair_http_request_path(httpd_req_t *req, char *out, size_t cap);
comfoair_wire_device_t comfoair_http_this_device(char id_out[COMFOAIR_CONFIG_DEVICE_ID_LEN + 1],
                                                 char mac_out[18]);

// Reads the whole body into `out` (NUL-terminated), or answers 413/400 itself and returns false.
// An EMPTY body is refused as 413 -- a caller for which no body is a valid request must check
// req->content_len == 0 before calling this.
bool comfoair_http_read_body(httpd_req_t *req, char *out, size_t cap);

// {"error":"<message>"} with `status` ("409 Conflict"). `message` must be a LITERAL: the body can
// reach the log ring that GET /api/log serves, so nothing a client sent is ever formatted into it.
esp_err_t comfoair_http_send_error(httpd_req_t *req, const char *status, const char *message);

// The first `len` bytes of g_http_scratch as application/json, in comfoair_http.c.
esp_err_t comfoair_http_send_json(httpd_req_t *req, size_t len);

// comfoair_http_send_error() plus the refused field, in comfoair_http_config.c.
esp_err_t comfoair_http_send_wire_error(httpd_req_t *req, const char *status,
                                        comfoair_wire_result_t result);

// The read-only documents, in comfoair_http_read.c.
esp_err_t comfoair_http_state_get(httpd_req_t *req);
esp_err_t comfoair_http_entities_get(httpd_req_t *req);
esp_err_t comfoair_http_entity_get(httpd_req_t *req);
esp_err_t comfoair_http_log_get(httpd_req_t *req);
esp_err_t comfoair_http_status_get(httpd_req_t *req);

// Provisioning and settings, in comfoair_http_config.c.
esp_err_t comfoair_http_wifi_scan_get(httpd_req_t *req);
esp_err_t comfoair_http_provision_post(httpd_req_t *req);
esp_err_t comfoair_http_config_get(httpd_req_t *req);
esp_err_t comfoair_http_config_post(httpd_req_t *req);

// Writing an entity and running an operation, in comfoair_http_command.c.
esp_err_t comfoair_http_entity_post(httpd_req_t *req);
esp_err_t comfoair_http_operation_post(httpd_req_t *req);

// The catch-all write gate and the GET fallback, in comfoair_http_assets.c.
esp_err_t comfoair_http_write_gate(httpd_req_t *req);
esp_err_t comfoair_http_captive_probe_get(httpd_req_t *req);

// The /ws route and the push task's body, in comfoair_http_ws.c.
esp_err_t comfoair_http_ws_get(httpd_req_t *req);
void      comfoair_http_ws_task(void *arg);

// The RMI probe's two routes, in comfoair_http_probe.c.
esp_err_t comfoair_http_probe_get(httpd_req_t *req);
esp_err_t comfoair_http_probe_post(httpd_req_t *req);

// GET /api/system, in comfoair_http_system.c.
esp_err_t comfoair_http_system_get(httpd_req_t *req);

// POST /api/ota, in comfoair_http_ota.c.
esp_err_t comfoair_http_ota_post(httpd_req_t *req);

// esp_app_desc_t version[32] + a NUL: a foreign image's field may fill all 32 bytes.
#define COMFOAIR_HTTP_OTA_VERSION_CAP 33

// POST /api/ota's judgement of the first `len` (COMFOAIR_OTA_PREFLIGHT_BYTES) bytes against the
// RUNNING chip and project, in comfoair_http_ota_preflight.c. Logs the accepted image against
// `slot`. `version` (COMFOAIR_HTTP_OTA_VERSION_CAP bytes) gets the image's version string.
comfoair_ota_fault_t comfoair_http_ota_preflight(const uint8_t *head, size_t len, const char *slot,
                                                 char *version);
