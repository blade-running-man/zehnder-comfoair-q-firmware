// comfoair_wire: the two configuration parsers, POST /api/provision and POST /api/config.
// Read comfoair_wire.h first; the component's shape is in README.md, Implementation.
// The two rules in comfoair_wire.c govern this file: no secret is rendered, and no submitted
// value is quoted in an error.
#include "comfoair_wire.h"

#include <string.h>

#include "comfoair_json.h"
#include "comfoair_wire_internal.h"

// --- POST /api/provision ---------------------------------------------------------------------

comfoair_wire_result_t comfoair_wire_parse_provision(const char *body,
                                                     const comfoair_wifi_t *stored,
                                                     comfoair_wifi_t *out)
{
    if (out == NULL)
        return bad_body();
    if (comfoair_json_check(body) != COMFOAIR_JSON_DOC_OK)
        return bad_body();

    char ssid[COMFOAIR_CONFIG_SSID_MAX + 1] = {0};
    if (comfoair_json_string(body, "wifi_ssid", ssid, sizeof ssid) != COMFOAIR_JSON_FOUND)
        return bad_field("wifi_ssid");

    // The three states of wifi_psk, resolved by comfoair_secret_decide() rather than by a rule
    // of this route's own: absent is KEEP, "" is CLEAR, anything else is STORE
    // (web/src/api/client.ts, ProvisionRequest). One vocabulary for both routes means the
    // firmware decodes it in one place instead of two that drift.
    char       psk[COMFOAIR_CONFIG_PSK_MAX + 1] = {0};
    const bool has_stored = stored != NULL && stored->psk_len > 0;

    const comfoair_json_read_t read = comfoair_json_string(body, "wifi_psk", psk, sizeof psk);
    const char                *submitted = NULL;
    if (read == COMFOAIR_JSON_FOUND)
        submitted = psk;
    else if (read != COMFOAIR_JSON_MISSING)
        return bad_field("wifi_psk");

    comfoair_wifi_t record = {0};
    record.ssid_len        = (uint8_t)strlen(ssid);
    memcpy(record.ssid, ssid, record.ssid_len);

    switch (comfoair_secret_decide(has_stored, submitted)) {
    case COMFOAIR_SECRET_KEEP:
        // The owner changed only the SSID, or the page handed back the sentinel for an untouched
        // box. KEEP of nothing is an open-network attempt, which is the correct reading and needs
        // no special case.
        if (has_stored) {
            record.psk_len = stored->psk_len;
            memcpy(record.psk, stored->psk, stored->psk_len);
        }
        break;
    case COMFOAIR_SECRET_CLEAR:
        break;  // an open network: the record keeps its zero length
    case COMFOAIR_SECRET_STORE:
        record.psk_len = (uint8_t)strlen(psk);
        memcpy(record.psk, psk, record.psk_len);
        break;
    case COMFOAIR_SECRET_REJECT:
        // The sentinel submitted as a value on a device with nothing stored. It is thirteen
        // printable bytes and passes comfoair_config_check_psk whole, so nothing downstream would
        // catch it -- and a device holding credentials it cannot associate with raises no access
        // point, which is the disappearance comfoair_provision.c calls the likeliest catastrophe
        // in this project, reached with nobody having made a typo.
        return refused(COMFOAIR_CONFIG_ERR_PSK, "wifi_psk");
    }

    // Validated on the way IN, never at the point of use: a stored value that is only discovered
    // to be impossible at the next boot is one the surface that could have fixed it is no longer
    // up to fix (comfoair_config.h, the first of its three rules).
    const comfoair_config_err_t ssid_err = comfoair_config_check_ssid(record.ssid, record.ssid_len);
    if (ssid_err != COMFOAIR_CONFIG_OK)
        return refused(ssid_err, "wifi_ssid");
    const comfoair_config_err_t psk_err = comfoair_config_check_psk(record.psk, record.psk_len);
    if (psk_err != COMFOAIR_CONFIG_OK)
        return refused(psk_err, "wifi_psk");

    // The SAME predicate comfoair_config_nvs_has_credentials() asks, over the same record. It
    // cannot fail after the two checks above -- it is those two checks -- and it is asked anyway
    // because the pair being usable is what decides whether the device raises an access point,
    // and a second answer to that question is the state the invariants forbid outright.
    if (!comfoair_config_wifi_usable(&record))
        return refused(COMFOAIR_CONFIG_ERR_WIFI_PAIR, NULL);

    // Written only now. A caller that ignores the status must not find half a credential here.
    *out = record;
    return ok();
}

// --- POST /api/config --------------------------------------------------------------------

// One string field of the patch. Returns false and names the field when the submission is not
// something this device could store; MISSING leaves the pointer NULL, which is what "the key was
// absent" means all the way down to comfoair_config_apply().
static bool read_field(const char *body, const char *name, char *store, size_t cap,
                       const char **target, comfoair_wire_result_t *fail)
{
    const comfoair_json_read_t read = comfoair_json_string(body, name, store, cap);
    if (read == COMFOAIR_JSON_MISSING) {
        *target = NULL;
        return true;
    }
    if (read != COMFOAIR_JSON_FOUND) {
        *fail = bad_field(name);
        return false;
    }
    *target = store;
    return true;
}

comfoair_wire_result_t comfoair_wire_parse_config(const char *body, comfoair_config_patch_t *out,
                                                  comfoair_wire_patch_storage_t *storage,
                                                  bool *has_permanent_manual,
                                                  bool *permanent_manual)
{
    if (out == NULL || storage == NULL)
        return bad_body();
    if (has_permanent_manual != NULL)
        *has_permanent_manual = false;
    if (comfoair_json_check(body) != COMFOAIR_JSON_DOC_OK)
        return bad_body();

    // Built locally and copied out at the very end. comfoair_config_apply()'s contract is ALL OR
    // NOTHING and it can only keep it if what it is handed is all or nothing too -- a patch
    // carrying the good half of a refused document would be applied by a handler that checked the
    // status after passing it on.
    comfoair_config_patch_t patch = {0};
    comfoair_wire_result_t  fail  = ok();

    // The network does not travel on this route (web/src/api/client.ts, ConfigPatch). Refused BY
    // NAME rather than ignored: silently dropping a credential somebody typed into a form is
    // worse than telling them where it belongs, because the form then reports success over a
    // network that was never stored.
    char probe[COMFOAIR_CONFIG_PSK_MAX + 1];
    if (comfoair_json_string(body, "wifi_ssid", probe, sizeof probe) != COMFOAIR_JSON_MISSING)
        return wrong_route("wifi_ssid");
    if (comfoair_json_string(body, "wifi_psk", probe, sizeof probe) != COMFOAIR_JSON_MISSING)
        return wrong_route("wifi_psk");

    // The names are comfoair_config's `name` column, which that table defines as what the API and
    // the owner see. A name that disagrees with it is a field that silently never saves.
    if (!read_field(body, "mqtt_host", storage->mqtt_host, sizeof storage->mqtt_host,
                    &patch.mqtt_host, &fail) ||
        !read_field(body, "mqtt_user", storage->mqtt_user, sizeof storage->mqtt_user,
                    &patch.mqtt_user, &fail) ||
        !read_field(body, "mqtt_password", storage->mqtt_password, sizeof storage->mqtt_password,
                    &patch.mqtt_password, &fail) ||
        !read_field(body, "topic_prefix", storage->topic_prefix, sizeof storage->topic_prefix,
                    &patch.topic_prefix, &fail) ||
        !read_field(body, "device_name", storage->device_name, sizeof storage->device_name,
                    &patch.device_name, &fail) ||
        !read_field(body, "ui_password", storage->ui_password, sizeof storage->ui_password,
                    &patch.ui_password, &fail))
        return fail;

    uint32_t                   port = 0;
    const comfoair_json_read_t got  = comfoair_json_u32(body, "mqtt_port", &port);
    if (got == COMFOAIR_JSON_FOUND) {
        patch.has_mqtt_port = true;
        // Carried as a uint32_t, and whether it is a PORT is comfoair_config_check_port()'s
        // answer, not this one. This layer says "a number"; asking the same question twice in two
        // places is how the two come to disagree.
        patch.mqtt_port = port;
    } else if (got != COMFOAIR_JSON_MISSING) {
        return bad_field("mqtt_port");
    }

    bool                       discovery = false;
    const comfoair_json_read_t flag      = comfoair_json_bool(body, "ha_discovery", &discovery);
    if (flag == COMFOAIR_JSON_FOUND) {
        patch.has_ha_discovery = true;
        patch.ha_discovery     = discovery;
    } else if (flag != COMFOAIR_JSON_MISSING) {
        return bad_field("ha_discovery");
    }

    // Not a field of the stored document, so it is handed back separately rather than through the
    // patch. Absent leaves it alone: a page that has never heard of it must not turn it off by
    // saving everything else.
    bool                       manual = false;
    const comfoair_json_read_t manual_read = comfoair_json_bool(body, "permanent_manual", &manual);
    if (manual_read == COMFOAIR_JSON_FOUND) {
        if (has_permanent_manual != NULL)
            *has_permanent_manual = true;
        if (permanent_manual != NULL)
            *permanent_manual = manual;
    } else if (manual_read != COMFOAIR_JSON_MISSING) {
        return bad_field("permanent_manual");
    }

    // Everything else in the body is IGNORED, and that includes ui_password_set, read_only and
    // wifi_known_good. They are derived by the device and appear in the projection only; the page
    // submits the whole document, so echoing them back has to be harmless. Ignoring unknown keys
    // in general is what lets a newer page talk to an older firmware without every save failing.
    *out = patch;
    return ok();
}
