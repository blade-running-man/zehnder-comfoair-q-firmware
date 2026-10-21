// comfoair_http: writing an entity and running an operation -- both queue a command and answer.
// Read comfoair_http.h first; the component's shape is in README.md, Implementation.
#include "comfoair_http.h"
#include "comfoair_http_internal.h"

#include <string.h>

#include "comfoair_command.h"
#include "comfoair_control.h"
#include "comfoair_mqtt.h"
#include "comfoair_state.h"
#include "comfoair_wire.h"
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "http";

// --- writing an entity and running an operation ------------------------------------------------

// Both handlers below queue and answer. NOTHING here touches the bus: comfoair_can_transmit()
// waits for the driver, and on an unpowered unit nothing is ever acknowledged -- which would stop
// the single task esp_http_server serves every socket from. comfoair_control owns the waiting.
static esp_err_t send_command_result(httpd_req_t *req, comfoair_command_err_t err)
{
    if (err != COMFOAIR_COMMAND_OK) {
        const char *status = err == COMFOAIR_COMMAND_ERR_UNKNOWN_ENTITY ||
                                     err == COMFOAIR_COMMAND_ERR_UNKNOWN_OP
                                 ? "404 Not Found"
                                 : "422 Unprocessable Entity";
        return comfoair_http_send_error(req, status, comfoair_command_strerror(err));
    }
    return ESP_OK;
}

// POST /api/entities/<key>
esp_err_t comfoair_http_entity_post(httpd_req_t *req)
{
    if (!comfoair_http_allowed(req))
        return ESP_OK;

    char path[128];
    comfoair_http_request_path(req, path, sizeof path);
    static const char prefix[] = "/api/entities/";
    if (strncmp(path, prefix, sizeof prefix - 1) != 0)
        return comfoair_http_send_error(req, "404 Not Found", "no such endpoint");
    const char *key = path + sizeof prefix - 1;

    char body[COMFOAIR_HTTP_BODY_MAX];
    if (!comfoair_http_read_body(req, body, sizeof body))
        return ESP_OK;

    // Long enough for the longest option label in the registry, and refused rather than truncated
    // above that -- a truncated label matches a different option, or none.
    char                     text[64];
    comfoair_command_value_t value = {0};
    const comfoair_wire_result_t decoded =
        comfoair_wire_parse_entity_write(body, &value, text, sizeof text);
    if (decoded.status != COMFOAIR_WIRE_OK)
        return comfoair_http_send_wire_error(req,
                               decoded.status == COMFOAIR_WIRE_BAD_BODY ? "400 Bad Request"
                                                                       : "422 Unprocessable Entity",
                               decoded);

    comfoair_command_durations_t durations;
    comfoair_control_durations(&durations);
    // The level the unit is actually running at, so that turning the fan on resumes it instead of
    // dropping the house to Low. It lives in the state model, not in comfoair_control: it is the
    // unit's own reading, arriving on PDO 65.
    comfoair_fan_state_t fan = {0};
    if (comfoair_state_get_fan(&fan) && fan.known && fan.on)
        durations.fan_resume_level = fan.speed;
    comfoair_command_t command = {0};
    const comfoair_command_err_t built =
        comfoair_command_from_entity(key, &value, &durations, &command);
    if (built != COMFOAIR_COMMAND_OK)
        return send_command_result(req, built);

    // A duration is a LOCAL change and produces no frame at all: the unit is told a duration only
    // as part of a command (pdo_table.py, NumberSpec). Storing it is the whole of the work.
    if (command.stores_duration) {
        const esp_err_t stored =
            comfoair_control_store_duration(command.duration_key, command.duration_minutes);
        if (stored == ESP_ERR_INVALID_STATE)
            // The sender never started, which happens on every boot where the CAN bus did not come
            // up (src/main.cpp starts it inside that branch). NOTHING was stored and nothing will
            // be used. Answering "accepted" here told the owner their duration had taken effect on
            // exactly the device where no command can be sent at all -- and it was the one error
            // code excluded from the warning below, so it was silent as well. The frame path two
            // lines down has always answered 503 for the same condition.
            return comfoair_http_send_error(req, "503 Service Unavailable", "commands are not running");
        if (stored != ESP_OK)
            // The value is already in effect; only its persistence failed. Saying "rejected" would
            // be a lie the owner could not act on.
            ESP_LOGW(TAG, "%s took effect but was not persisted: %s", command.duration_key,
                     esp_err_to_name(stored));
        // No PDO carries a duration, so the state model is the ONLY place it can be seen. Without
        // this the owner sets 30, the device stores and uses 30, and every projection keeps
        // reporting the registry default 60 -- across reboots. Two sources of truth for one value.
        comfoair_state_set_number(command.duration_key, (float)command.duration_minutes);
        comfoair_http_notify_state_changed();
        comfoair_mqtt_notify_state_changed();
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"accepted\":true}");
    }

    const esp_err_t queued = comfoair_control_submit(&command);
    if (queued == ESP_ERR_NO_MEM)
        // Commands are arriving faster than a 50 kbit/s bus can carry them. "Busy" is the honest
        // answer; a bigger queue would apply this one minutes after it was asked for.
        return comfoair_http_send_error(req, "503 Service Unavailable", "the bus is busy");
    if (queued != ESP_OK)
        return comfoair_http_send_error(req, "503 Service Unavailable", "commands are not running");

    // OPTIMISTIC for EVERY select label, as over MQTT: the echo is what shows a write at once. The
    // three RMI-read selects only get the unit's value back at the next settings read (up to 6 h),
    // so without it the page would show the pre-write value until then. It is silently declined
    // for a label the registry does not list under the key; the other four (bypass_mode,
    // temperature_profile and the derived balance_mode, ventilation_mode) are overwritten by
    // their own PDOs within seconds, which is what optimistic means. DO NOT extend this to away_mode: the device requests
    // PDO 337 about 1.5 s after the command and its bit 11 confirms it (comfoair_control), and
    // showing AWAY for a command the unit refused is worse than showing it that much later.
    if (value.kind == COMFOAIR_COMMAND_VALUE_TEXT &&
        comfoair_state_set_option(key, value.text)) {
        comfoair_http_notify_state_changed();
        comfoair_mqtt_notify_state_changed();
    }

    httpd_resp_set_type(req, "application/json");
    // ACCEPTED, not applied. Every write is fire-and-forget -- the command path sends RMI and never
    // listens for a reply -- so a refused frame is indistinguishable from an accepted one, and the only
    // confirmation is the unit's own PDO arriving seconds later on /ws.
    return httpd_resp_sendstr(req, "{\"accepted\":true}");
}

// POST /api/ops/<name>
esp_err_t comfoair_http_operation_post(httpd_req_t *req)
{
    if (!comfoair_http_allowed(req))
        return ESP_OK;

    char path[128];
    comfoair_http_request_path(req, path, sizeof path);
    static const char prefix[] = "/api/ops/";
    if (strncmp(path, prefix, sizeof prefix - 1) != 0)
        return comfoair_http_send_error(req, "404 Not Found", "no such endpoint");
    const char *name = path + sizeof prefix - 1;

    char body[COMFOAIR_HTTP_BODY_MAX];
    if (!comfoair_http_read_body(req, body, sizeof body))
        return ESP_OK;

    comfoair_command_params_t  params  = {0};
    comfoair_wire_op_storage_t storage = {0};
    const comfoair_wire_result_t decoded = comfoair_wire_parse_operation(body, &params, &storage);
    if (decoded.status != COMFOAIR_WIRE_OK)
        return comfoair_http_send_wire_error(req,
                               decoded.status == COMFOAIR_WIRE_BAD_BODY ? "400 Bad Request"
                                                                       : "422 Unprocessable Entity",
                               decoded);

    comfoair_command_durations_t durations;
    comfoair_control_durations(&durations);
    comfoair_command_t command = {0};
    const comfoair_command_err_t built =
        comfoair_command_from_op(name, &params, &durations, &command);
    if (built != COMFOAIR_COMMAND_OK)
        return send_command_result(req, built);

    const esp_err_t queued = comfoair_control_submit(&command);
    if (queued == ESP_ERR_NO_MEM)
        return comfoair_http_send_error(req, "503 Service Unavailable", "the bus is busy");
    if (queued != ESP_OK)
        return comfoair_http_send_error(req, "503 Service Unavailable", "commands are not running");

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"accepted\":true}");
}
