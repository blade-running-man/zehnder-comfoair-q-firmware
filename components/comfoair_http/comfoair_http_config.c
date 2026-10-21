// comfoair_http: provisioning and settings -- the Wi-Fi scan, POST /api/provision and GET/POST
// /api/config. Read comfoair_http.h first; the component's shape is in README.md, Implementation.
#include "comfoair_http_internal.h"

#include <stdio.h>

#include "comfoair_command.h"
#include "comfoair_config.h"
#include "comfoair_control.h"
#include "comfoair_mdns.h"
#include "comfoair_mqtt.h"
#include "comfoair_net.h"
#include "comfoair_phase_monitor.h"
#include "comfoair_wire.h"
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "http";

// --- provisioning and settings ----------------------------------------------------------

// comfoair_http_send_error(), plus the field that was refused. A page that is told only "bad
// request" leaves the owner to guess which of eight boxes it meant.
esp_err_t comfoair_http_send_wire_error(httpd_req_t *req, const char *status,
                                        comfoair_wire_result_t result)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    char body[320];
    if (result.field != NULL)
        snprintf(body, sizeof body, "{\"error\":\"%s\",\"field\":\"%s\"}",
                 comfoair_wire_strerror(result), result.field);
    else
        snprintf(body, sizeof body, "{\"error\":\"%s\"}", comfoair_wire_strerror(result));
    return httpd_resp_sendstr(req, body);
}

// One name per validation failure, so the page can point at the field that was refused instead
// of saying "bad request" and leaving the owner to guess which of eight values it meant.
static const char *config_error_name(comfoair_config_err_t err)
{
    switch (err) {
    case COMFOAIR_CONFIG_OK:                    return "ok";
    case COMFOAIR_CONFIG_ERR_SSID:              return "ssid";
    case COMFOAIR_CONFIG_ERR_PSK:               return "psk";
    case COMFOAIR_CONFIG_ERR_WIFI_PAIR:         return "wifi-pair";
    case COMFOAIR_CONFIG_ERR_HOST:              return "mqtt-host";
    case COMFOAIR_CONFIG_ERR_PORT:              return "mqtt-port";
    case COMFOAIR_CONFIG_ERR_USER:              return "mqtt-user";
    case COMFOAIR_CONFIG_ERR_BROKER_PASSWORD:   return "mqtt-password";
    case COMFOAIR_CONFIG_ERR_PREFIX:            return "topic-prefix";
    case COMFOAIR_CONFIG_ERR_NAME:              return "device-name";
    case COMFOAIR_CONFIG_ERR_UI_PASSWORD:       return "ui-password";
    case COMFOAIR_CONFIG_ERR_NO_HASH:           return "no-hash";
    case COMFOAIR_CONFIG_ERR_READ_ONLY:         return "read-only";
    case COMFOAIR_CONFIG_ERR_NO_DOCUMENT:       return "no-document";
    default:                                    return "rejected";
    }
}


esp_err_t comfoair_http_wifi_scan_get(httpd_req_t *req)
{
    if (!comfoair_http_allowed(req))
        return ESP_OK;

    static comfoair_net_scan_entry_t entries[COMFOAIR_NET_SCAN_MAX];
    size_t                           count = 0;
    if (comfoair_net_scan(entries, COMFOAIR_NET_SCAN_MAX, &count) != ESP_OK)
        return comfoair_http_send_error(req, "503 Service Unavailable", "scan unavailable");

    // Rendered by comfoair_wire, which is where the shape of this array is pinned by tests --
    // including the one that matters here: an SSID is chosen by whoever set up the router next
    // door, so a neighbour can broadcast a name containing JSON at this device.
    const size_t used = comfoair_wire_render_scan(entries, count, g_http_scratch, sizeof g_http_scratch);
    if (used == 0)
        return comfoair_http_send_error(req, "500 Internal Server Error", "scan does not fit");
    return comfoair_http_send_json(req, used);
}

esp_err_t comfoair_http_provision_post(httpd_req_t *req)
{
    if (!comfoair_http_allowed(req))
        return ESP_OK;

    char body[COMFOAIR_HTTP_BODY_MAX];
    if (!comfoair_http_read_body(req, body, sizeof body))
        return ESP_OK;

    // Decoded inside comfoair_net, because resolving "keep the key already stored" means reading
    // the stored key -- and a handler that could read it could also log it, into a ring that
    // GET /api/log serves to anybody. What comes back is why, never what.
    comfoair_wire_result_t  refusal = {0};
    const esp_err_t         accepted = comfoair_net_provision_body(body, &refusal);

    if (accepted == ESP_ERR_INVALID_ARG)
        return comfoair_http_send_wire_error(req,
                               refusal.status == COMFOAIR_WIRE_BAD_BODY ? "400 Bad Request"
                                                                       : "422 Unprocessable Entity",
                               refusal);
    if (accepted != ESP_OK)
        return comfoair_http_send_error(req, "503 Service Unavailable", "busy");

    // ANSWER FIRST, and comfoair_net_provision_body() has already returned without connecting so
    // that this line can be reached at all. The station moves to the router's channel and the
    // access point follows it, so the phone that sent this is about to lose us -- if the reply
    // had waited for the connection, everything having worked would look like a failure and the
    // owner would press the button again.
    comfoair_net_status_t st;
    comfoair_net_status(&st);
    // NULL, and deliberately not this device's broker counters: the radio has not moved to the
    // owner's network yet, so nothing here has been in a position to reach a broker. Zeros would
    // read as a configured broker that has gone silent, which is the opposite answer
    // (comfoair_wire.h, comfoair_wire_render_provision).
    // The identity, unlike the counters, IS knowable here: it comes from the MAC and the app
    // descriptor, neither of which waits for a network. A client that has just handed this device
    // its credentials is the client most in need of knowing which device it just configured.
    // The one member comfoair_net_status() cannot fill: the aggregate has an owner of its own and
    // the provisioning machine can see neither the bus nor the broker (comfoair_wire.h). Set here
    // as well as in the status handler, and not left at its zero value, because that value is BOOT
    // -- "starting up" -- which on a device that has just been handed credentials is the one
    // reading that is certainly wrong.
    st.phase = comfoair_phase_monitor_get();
    // The other member comfoair_net_status() leaves alone, and for a different reason: reading it
    // enters the Wi-Fi stack, and that call belongs on the task somebody is waiting on rather than
    // on comfoair_phase_monitor's 1 Hz tick, which never looks at it. Filled HERE as well as in
    // the status handler so that a null in this document means "the station is not associated"
    // everywhere, and never "this handler did not look".
    st.has_rssi = comfoair_net_rssi(&st.rssi);

    char                         id_buf[COMFOAIR_CONFIG_DEVICE_ID_LEN + 1];
    char                         mac_buf[18];
    const comfoair_wire_device_t device = comfoair_http_this_device(id_buf, mac_buf);
    const size_t                 used =
        comfoair_wire_render_provision(&st, NULL, &device, g_http_scratch, sizeof g_http_scratch);
    if (used == 0)
        return comfoair_http_send_error(req, "500 Internal Server Error", "status does not fit");
    return comfoair_http_send_json(req, used);
}

esp_err_t comfoair_http_config_get(httpd_req_t *req)
{
    if (!comfoair_http_allowed(req))
        return ESP_OK;

    // comfoair_config_public_t, never comfoair_config_t: the projection is where each stored
    // secret has already become the sentinel or "", so this renderer cannot leak one by
    // forgetting to. `known_good` comes from the provisioning machine under the same lock --
    // the page cannot describe the rollback truthfully without it.
    comfoair_config_public_t pub;
    bool                     known_good = false;
    comfoair_net_config_snapshot(&pub, &known_good);

    // Owned by comfoair_control, shown here. It is not a field of the stored document -- see the
    // note on comfoair_wire_render_config -- so it is fetched rather than projected.
    comfoair_command_durations_t durations;
    comfoair_control_durations(&durations);

    const size_t used = comfoair_wire_render_config(&pub, known_good, durations.permanent_manual,
                                                    g_http_scratch, sizeof g_http_scratch);
    if (used == 0)
        return comfoair_http_send_error(req, "500 Internal Server Error", "config does not fit");
    return comfoair_http_send_json(req, used);
}

esp_err_t comfoair_http_config_post(httpd_req_t *req)
{
    if (!comfoair_http_allowed(req))
        return ESP_OK;

    char body[COMFOAIR_HTTP_BODY_MAX];
    if (!comfoair_http_read_body(req, body, sizeof body))
        return ESP_OK;

    // STATIC, and it has to be: the strings a patch points at have to outlive the parse, and this
    // is the better part of a kilobyte on a task with a fixed stack. It holds the broker password
    // and the owner's new UI password in the clear until the next parse overwrites them, so it is
    // also a buffer that must never reach ESP_LOG*.
    //
    // Single-buffered because esp_http_server serves every socket from ONE task
    // (max_open_sockets, comfoair_http_start()), so two config posts cannot be in this function
    // at once.
    static comfoair_wire_patch_storage_t storage;
    comfoair_config_patch_t              patch = {0};

    bool has_manual = false;
    bool manual      = false;
    const comfoair_wire_result_t decoded =
        comfoair_wire_parse_config(body, &patch, &storage, &has_manual, &manual);
    if (decoded.status != COMFOAIR_WIRE_OK)
        return comfoair_http_send_wire_error(req,
                               decoded.status == COMFOAIR_WIRE_BAD_BODY ? "400 Bad Request"
                                                                       : "422 Unprocessable Entity",
                               decoded);

    // What a value may BE is comfoair_config_apply()'s answer and not this layer's. Asking the
    // same question in two places is how the two come to disagree -- and the one that would then
    // be wrong is the one without the tests.
    const comfoair_config_err_t err = comfoair_net_config_apply(&patch);
    if (err != COMFOAIR_CONFIG_OK) {
        const comfoair_wire_result_t rejected = {COMFOAIR_WIRE_REFUSED, err,
                                                 config_error_name(err)};
        return comfoair_http_send_wire_error(req,
                               err == COMFOAIR_CONFIG_ERR_READ_ONLY ? "409 Conflict"
                                                                    : "422 Unprocessable Entity",
                               rejected);
    }

    // Applied only after the document was accepted, so a refused save does not change it either.
    // It lives in comfoair_control, which is what builds commands from it -- one writer.
    if (has_manual) {
        const esp_err_t err_manual = comfoair_control_set_permanent_manual(manual);
        if (err_manual != ESP_OK && err_manual != ESP_ERR_INVALID_STATE)
            ESP_LOGW(TAG, "manual mode took effect but was not persisted: %s",
                     esp_err_to_name(err_manual));
    }

    // ANSWERED FIRST, and it stays that way. Until 2026-09-05 this ordering was the only thing
    // between the owner and a hang: comfoair_mqtt_reconfigure() called esp_mqtt_client_stop() on
    // THIS task, that call waits for the client task to finish its current iteration -- seconds on
    // a socket that is timing out -- and esp_http_server serves every socket from ONE task. Since
    // then reconfigure() only posts a request to the publisher task and returns, so the stall is
    // gone at its source and this line is no longer load-bearing. DO NOT take that as licence to
    // reply last: POST /api/provision has the same shape for the same reason, and the next thing
    // added here must not have to rediscover why.
    httpd_resp_set_type(req, "application/json");
    const esp_err_t sent = httpd_resp_sendstr(req, "{\"saved\":true}");

    // A saved broker applies without a reboot -- "everything is configured at runtime" (AGENTS.md)
    // is why the settings page exists. Clearing the host stops the client and is a success.
    const esp_err_t mqtt_err = comfoair_mqtt_reconfigure();
    if (mqtt_err != ESP_OK)
        ESP_LOGW(TAG, "settings saved; the broker connection did not restart: %s",
                 esp_err_to_name(mqtt_err));

    // A renamed device gets a new DNS-SD instance name and KEEPS `comfoair-XXXX.local`: the host
    // name is a key, like the MAC identity, so no rename moves it. That is why this is safe while
    // the owner is looking at the settings page over that very name: the tab that sent this
    // request is not the last thing it ever receives.
    //
    // AFTER the answer, like the broker above, and for the softer of the two reasons -- it cannot
    // fail the save. Read out of the document rather than out of `patch`: the patch may not carry a
    // name at all, and the record has to say what the device is called now, not what changed.
    comfoair_config_public_t named;
    comfoair_net_config_snapshot(&named, NULL);
    comfoair_mdns_rename(named.device_name);
    return sent;
}
