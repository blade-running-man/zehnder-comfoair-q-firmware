// comfoair_http: the read-only documents -- state, entities, one entity, the log and the device's
// status. Read comfoair_http.h first; the component's shape is in README.md, Implementation.
#include "comfoair_http_internal.h"

#include "comfoair_api.h"
#include "comfoair_config.h"
#include "comfoair_log.h"
#include "comfoair_mqtt.h"
#include "comfoair_net.h"
#include "comfoair_phase_monitor.h"
#include "comfoair_state.h"
#include "comfoair_wire.h"
#include "esp_http_server.h"

esp_err_t comfoair_http_state_get(httpd_req_t *req)
{
    if (!comfoair_http_allowed(req))
        return ESP_OK;
    return comfoair_http_send_json(req, comfoair_api_render_state(g_http_scratch, sizeof g_http_scratch));
}

esp_err_t comfoair_http_entities_get(httpd_req_t *req)
{
    if (!comfoair_http_allowed(req))
        return ESP_OK;
    return comfoair_http_send_json(req, comfoair_api_render_entities(g_http_scratch, sizeof g_http_scratch));
}

// GET /api/entities/<key> -- one entity's value. The registry is the same one every other
// projection reads, so a key that exists here exists in MQTT and in discovery too.
esp_err_t comfoair_http_log_get(httpd_req_t *req)
{
    if (!comfoair_http_allowed(req))
        return ESP_OK;
    const size_t len = comfoair_log_render(g_http_scratch, sizeof g_http_scratch);
    if (len == 0) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"log does not fit\"}");
        return ESP_OK;
    }
    return comfoair_http_send_json(req, len);
}

esp_err_t comfoair_http_entity_get(httpd_req_t *req)
{
    if (!comfoair_http_allowed(req))
        return ESP_OK;

    char path[128];
    comfoair_http_request_path(req, path, sizeof path);
    static const char prefix[] = "/api/entities/";
    const char *key = path + sizeof prefix - 1;

    comfoair_value_t value = {0};
    if (!comfoair_state_get(key, &value)) {
        // 404 means "no such entity". A known entity with no reading is 200 with null --
        // the two are different answers and a client must be able to tell a dead bus from
        // a typo.
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"unknown entity\"}");
        return ESP_OK;
    }
    return comfoair_http_send_json(req, comfoair_api_render_value(key, g_http_scratch, sizeof g_http_scratch));
}

esp_err_t comfoair_http_status_get(httpd_req_t *req)
{
    if (!comfoair_http_allowed(req))
        return ESP_OK;

    comfoair_net_status_t st;
    comfoair_net_status(&st);

    // Rendered by comfoair_wire, where the two things this document gets wrong by hand are pinned
    // by tests: the failure is a NAME decided once in comfoair_provision -- four different answers
    // to the owner is an invariant, and it dies the moment REST and MQTT each invent a spelling --
    // and COMFOAIR_PROV_NEVER is rendered as null rather than as 4294967295, because "no time
    // left" and "no end" are opposite answers about a device sealed behind a front panel.

    // The broker connection, mapped here, on a side that already depends on both, because
    // comfoair_wire cannot: it is pure and host-tested, and comfoair_mqtt drags in esp-mqtt and
    // FreeRTOS (comfoair_wire.h, comfoair_wire_mqtt_t). Counters only -- no host, no user, no
    // password, and comfoair_mqtt_stats_t carries none to copy.
    //
    // Until this call existed comfoair_mqtt_get_stats() had no caller anywhere in the tree, so
    // "the broker refused our password" was reachable only over a serial cable. It takes no locks
    // and does not block, so it is safe on the httpd task.
    comfoair_mqtt_stats_t stats;
    comfoair_mqtt_get_stats(&stats);
    const comfoair_wire_mqtt_t mqtt = {
        .configured   = stats.configured,
        .connected    = stats.connected,
        .published    = stats.published,
        .commands     = stats.commands,
        .rejected     = stats.rejected,
        .reconnects   = stats.reconnects,
        .last_connack = stats.last_connack,
        // The ladder's, in the order the block renders them. THE THREE ENUMERATIONS ARE CAST HERE
        // AND NOWHERE ELSE: comfoair_mqtt_stats_t holds them as uint8_t so that its public header
        // stays free of comfoair_mqtt_backoff's, and this is the one place the counters are mapped
        // into comfoair_wire_mqtt_t. A value outside the enumeration is not a hazard -- every name function
        // answers "unknown" rather than NULL -- so the cast cannot put a bare word in the document.
        .link                 = (comfoair_mqtt_link_t)stats.link,
        .failure              = (comfoair_mqtt_fail_t)stats.failure,
        .attempts             = stats.attempts,
        .retry_in_ms          = stats.retry_in_ms,
        .subscriptions        = (comfoair_mqtt_subs_t)stats.subscriptions,
        .disconnects          = stats.disconnects,
        .applied_epoch        = stats.applied_epoch,
        .last_transport_errno = stats.last_transport_errno,
    };

    // The aggregate. One word that says what the whole device is doing, from the only component
    // that reads the network, the broker, the bus and app_main's seven start branches in the same
    // moment -- and the only channel it had before this line was a colour on a pixel in a technical
    // room. It reads one atomic and takes no lock, so it is safe on the httpd task.
    st.phase = comfoair_phase_monitor_get();
    // Read on THIS task and not inside the snapshot: comfoair_net_rssi() enters the Wi-Fi stack,
    // and comfoair_phase_monitor calls comfoair_net_status() once a second without ever looking at
    // the answer. One field closes two support questions -- "the router is in the next room and
    // DHCP is silent" against "we are at the edge of the cell" -- beside the raw failure_reason
    // that already separates 201 from 212.
    st.has_rssi = comfoair_net_rssi(&st.rssi);

    char                         id_buf[COMFOAIR_CONFIG_DEVICE_ID_LEN + 1];
    char                         mac_buf[18];
    const comfoair_wire_device_t device = comfoair_http_this_device(id_buf, mac_buf);

    const size_t used =
        comfoair_wire_render_provision(&st, &mqtt, &device, g_http_scratch, sizeof g_http_scratch);
    if (used == 0)
        return comfoair_http_send_error(req, "500 Internal Server Error", "status does not fit");
    return comfoair_http_send_json(req, used);
}
