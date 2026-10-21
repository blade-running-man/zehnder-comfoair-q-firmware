// comfoair_wire: the GET /api/config and GET /api/wifi/scan renderers, and the
// provisioning state, mode and CONNACK words the status document and the MQTT log line print.
// Read comfoair_wire.h first; the component's shape is in README.md, Implementation.
// The two rules in comfoair_wire.c govern this file: no secret is rendered, and no submitted
// value is quoted in an error.
#include "comfoair_wire.h"

#include "comfoair_wire_internal.h"

// --- GET /api/config ------------------------------------------------------------------------

size_t comfoair_wire_render_config(const comfoair_config_public_t *pub, bool known_good,
                                   bool permanent_manual, char *out, size_t cap)
{
    if (pub == NULL)
        return 0;
    writer_t w = {out, cap, 0, false};

    put(&w, "{");
    put_key(&w, "wifi_ssid");
    // Not a secret: it is in every beacon that network sends, and it is what tells the page which
    // network is configured. The KEY is next to it and is the sentinel or "" -- never the value.
    put_string(&w, pub->ssid);
    put(&w, ",");
    put_key(&w, "wifi_psk");
    put_string(&w, pub->psk);
    put(&w, ",");
    put_key(&w, "mqtt_host");
    put_string(&w, pub->mqtt_host);
    put(&w, ",");
    put_key(&w, "mqtt_port");
    put_u32(&w, pub->mqtt_port);
    put(&w, ",");
    put_key(&w, "mqtt_user");
    put_string(&w, pub->mqtt_user);
    put(&w, ",");
    put_key(&w, "mqtt_password");
    put_string(&w, pub->mqtt_password);
    put(&w, ",");
    put_key(&w, "topic_prefix");
    put_string(&w, pub->topic_prefix);
    put(&w, ",");
    put_key(&w, "ha_discovery");
    put_bool(&w, pub->ha_discovery);
    put(&w, ",");
    put_key(&w, "device_name");
    put_string(&w, pub->device_name);
    put(&w, ",");
    put_key(&w, "ui_password");
    put_string(&w, pub->ui_password);
    put(&w, ",");
    // Derived, and read-only. A stored "a password is set" flag that disagreed with the stored
    // record would be a device locked by nobody, for ever (comfoair_config.h:257-260), which is
    // why this is computed from the record's emptiness every time it is asked.
    put_key(&w, "ui_password_set");
    put_bool(&w, pub->ui_password_set);
    put(&w, ",");
    put_key(&w, "read_only");
    put_bool(&w, pub->read_only);
    put(&w, ",");
    // From the provisioning machine, not from the stored document. It is the ONLY condition under
    // which a failed provisioning rolls back, and `wifi_ssid != ""` is not a substitute: an SSID
    // stored beside a mistyped key has never produced an address.
    put_key(&w, "wifi_known_good");
    put_bool(&w, known_good);
    put(&w, ",");
    // Owned by comfoair_control, shown and changed here. Without it the owner's own decision --
    // taken deliberately, with the price named -- was reachable only with a serial cable.
    put_key(&w, "permanent_manual");
    put_bool(&w, permanent_manual);
    put(&w, "}");

    return finish(&w);
}

// --- GET /api/wifi/scan --------------------------------------------------------------------

size_t comfoair_wire_render_scan(const comfoair_wire_network_t *list, size_t count, char *out,
                                 size_t cap)
{
    writer_t w = {out, cap, 0, false};

    put(&w, "[");
    for (size_t i = 0; i < count && list != NULL; i++) {
        if (i > 0)
            put(&w, ",");
        put(&w, "{");
        put_key(&w, "ssid");
        // Through the escaper, and this is the one row where that is not a formality: an SSID is
        // chosen by whoever set up the router next door, so `"},{"ssid":"` is a name somebody can
        // broadcast at this device.
        put_string(&w, list[i].ssid);
        put(&w, ",");
        put_key(&w, "rssi");
        put_i32(&w, list[i].rssi);
        put(&w, ",");
        put_key(&w, "secure");
        put_bool(&w, list[i].secure);
        put(&w, "}");
    }
    put(&w, "]");

    return finish(&w);
}

// --- GET /api/provision --------------------------------------------------------------------

const char *comfoair_wire_state_name(comfoair_prov_state_t state)
{
    switch (state) {
    case COMFOAIR_PROV_UNCONFIGURED:  return "unconfigured";
    case COMFOAIR_PROV_CONNECTING:    return "connecting";
    case COMFOAIR_PROV_CONNECTED:     return "connected";
    case COMFOAIR_PROV_TRIAL:         return "trial";
    case COMFOAIR_PROV_RETRYING:      return "retrying";
    case COMFOAIR_PROV_FALLBACK_AP:   return "fallback-ap";
    case COMFOAIR_PROV_WINDOW_CLOSED: return "window-closed";
    case COMFOAIR_PROV_STATE_COUNT:   break;   // a counter, not a state; falls to "unknown"
    }
    // Never NULL. A renderer that printed (null) here would put a bare word in the document and
    // the page would fail to parse a perfectly healthy device.
    return "unknown";
}

const char *comfoair_wire_mode_name(comfoair_prov_mode_t mode)
{
    switch (mode) {
    case COMFOAIR_PROV_MODE_OFF:          return "off";
    case COMFOAIR_PROV_MODE_ACCESS_POINT: return "access-point";
    case COMFOAIR_PROV_MODE_STATION:      return "station";
    case COMFOAIR_PROV_MODE_AP_STA:       return "ap-station";
    }
    return "unknown";
}

// A uint32_t and not the esp-mqtt enum on purpose: this file builds on the host, where
// mqtt_client.h does not exist, and the caller has the code as a number anyway.
//
// NOT THE ONLY READER OF THIS CODE SPACE: comfoair_phase_connack_is_refusal() decides which of
// these codes latch the BROKER_REJECTED phase, and both own "0..5 plus everything else"
// independently. DO NOT give a code a meaning in one and not the other -- a broker answer that
// changes the colour here and keeps the generic spelling there is a device whose light and whose
// words disagree.
const char *comfoair_wire_connack_name(uint32_t code)
{
    switch (code) {
    // "no refusal", NOT "accepted": the field means only that no refusal was recorded, which is
    // also the state of a device that has never contacted a broker at all. Rendering "accepted"
    // there would tell the owner a broker approved us when nothing ever happened.
    case 0: return "no refusal";
    case 1: return "unacceptable protocol version";
    case 2: return "client identifier rejected";
    case 3: return "server unavailable";
    case 4: return "bad username or password";
    case 5: return "not authorized";
    }
    // Same rule as the two above: never NULL, and never a fabricated meaning. A broker sending a
    // code outside the specification tells us nothing, and saying so is the honest answer.
    return "unknown refusal code";
}
