// The half of the configuration store that has no flash in it, and the first of its four files.
//
// Everything in the pure half is a decision -- what a value has to look like to be storable
// (comfoair_config_check.c), what a document means when it mentions half of itself (_apply.c),
// how the UI password is kept (_password.c), and here what a reset destroys, what a client is
// allowed to see and what to make of a document read back from flash. comfoair_config_nvs.c is
// the other half and it is deliberately dull: it opens a namespace, moves bytes, and asks the
// pure half. No file of the pure half includes an ESP-IDF header, which is what lets
// test/test_config cover every rule that matters.
//
// Read comfoair_config.h first; the reasoning lives there, next to each declaration. What is
// repeated here is only what a reader of the implementation would otherwise be tempted to
// "simplify".
#include "comfoair_config.h"
#include "comfoair_config_internal.h"

#include <stdio.h>
#include <string.h>

// IANA has 1883 for MQTT and 8883 for MQTT over TLS. TLS is deferred past v1 (AGENTS.md),
// so the second number is deliberately absent: a default the transport cannot honour is worse
// than no default at all.
#define DEFAULT_MQTT_PORT 1883

// --- the table -------------------------------------------------------------------------------

// `name` is what the API and the owner see; `key` is what NVS is asked for. They are the same
// string wherever the stored thing IS the named thing, and the two rows where they differ are
// the two rows worth reading: the Wi-Fi pair shares ONE key because it is one record, and the
// UI password's key says `hash` because what is kept is not the password.
static const comfoair_config_field_info_t FIELDS[COMFOAIR_CONFIG_F_COUNT] = {
    [COMFOAIR_CONFIG_F_WIFI_SSID] = {"wifi_ssid", COMFOAIR_CONFIG_KEY_STA, COMFOAIR_CONFIG_NS_WIFI,
                                     false},
    [COMFOAIR_CONFIG_F_WIFI_PSK] = {"wifi_psk", COMFOAIR_CONFIG_KEY_STA, COMFOAIR_CONFIG_NS_WIFI,
                                    true},
    [COMFOAIR_CONFIG_F_MQTT_HOST]     = {"mqtt_host", "mqtt_host", COMFOAIR_CONFIG_NS_APP, false},
    [COMFOAIR_CONFIG_F_MQTT_PORT]     = {"mqtt_port", "mqtt_port", COMFOAIR_CONFIG_NS_APP, false},
    [COMFOAIR_CONFIG_F_MQTT_USER]     = {"mqtt_user", "mqtt_user", COMFOAIR_CONFIG_NS_APP, false},
    [COMFOAIR_CONFIG_F_MQTT_PASSWORD] = {"mqtt_password", "mqtt_password", COMFOAIR_CONFIG_NS_APP,
                                         true},
    [COMFOAIR_CONFIG_F_TOPIC_PREFIX]  = {"topic_prefix", "topic_prefix", COMFOAIR_CONFIG_NS_APP,
                                         false},
    [COMFOAIR_CONFIG_F_HA_DISCOVERY]  = {"ha_discovery", "ha_discovery", COMFOAIR_CONFIG_NS_APP,
                                         false},
    [COMFOAIR_CONFIG_F_DEVICE_NAME]   = {"device_name", "device_name", COMFOAIR_CONFIG_NS_APP,
                                         false},
    // NS_OWNER, alone, because this one record is what says the device is claimed -- and the
    // two-tier reset puts it on the soft gesture's side while the broker stays on the other.
    [COMFOAIR_CONFIG_F_UI_PASSWORD] = {"ui_password", "ui_pw_hash", COMFOAIR_CONFIG_NS_OWNER, true},
};

static const char *const NS_NAMES[COMFOAIR_CONFIG_NS_COUNT] = {
    [COMFOAIR_CONFIG_NS_WIFI]   = COMFOAIR_CONFIG_NS_WIFI_NAME,
    [COMFOAIR_CONFIG_NS_OWNER]  = COMFOAIR_CONFIG_NS_OWNER_NAME,
    [COMFOAIR_CONFIG_NS_APP]    = COMFOAIR_CONFIG_NS_APP_NAME,
    [COMFOAIR_CONFIG_NS_LEGACY] = COMFOAIR_CONFIG_NS_LEGACY_NAME,
};

const comfoair_config_field_info_t *comfoair_config_field(comfoair_config_field_t field)
{
    // Compared as unsigned so that a negative id -- which is what an unparsed number from a URL
    // looks like -- is out of range rather than an index behind the table.
    if ((unsigned)field >= (unsigned)COMFOAIR_CONFIG_F_COUNT)
        return NULL;
    // A row nobody filled in reads back as all zeros, and the first renderer to touch its NULL
    // name crashes the HTTP task. A missing row answers "no such field" instead, which the
    // test notices loudly and a device survives.
    return FIELDS[field].name != NULL ? &FIELDS[field] : NULL;
}

const char *comfoair_config_ns_name(comfoair_config_ns_t ns)
{
    if ((unsigned)ns >= (unsigned)COMFOAIR_CONFIG_NS_COUNT)
        return NULL;
    return NS_NAMES[ns];
}

bool comfoair_config_reset_erases(comfoair_config_reset_t tier, comfoair_config_ns_t ns)
{
    if ((unsigned)ns >= (unsigned)COMFOAIR_CONFIG_NS_COUNT)
        return false;

    // The hard tier answers for the WHOLE enum rather than from a list, and that is the point
    // of asking here at all: a namespace added next year is erased by a factory reset without
    // anyone remembering to extend anything. The failure this shape prevents is a device passed
    // on to someone else with the previous owner's broker password still in its flash.
    if (tier == COMFOAIR_CONFIG_RESET_HARD)
        return true;

    // The two-tier reset (docs/device/button.md): five seconds after boot takes the network and the
    // UI password and leaves the broker, the durations and the name. NS_LEGACY is on this side
    // because it holds the Wi-Fi pair an earlier comfoair_net wrote -- a soft reset that leaves
    // it behind is a "Forget Wi-Fi" that the migration undoes at the next boot.
    return ns == COMFOAIR_CONFIG_NS_WIFI || ns == COMFOAIR_CONFIG_NS_OWNER ||
           ns == COMFOAIR_CONFIG_NS_LEGACY;
}

// --- small things ----------------------------------------------------------------------------

// True when the array holds a terminated string. An array with no NUL anywhere in it was not
// written by this build -- every writer here terminates -- so it came from a short read, a bit
// flip, or a firmware that is not this one.
//
// DO NOT delete this because the tests pass without it. THEY DO: every array here is exactly
// MAX+1 bytes, so an unterminated one always measures longer than its own limit and the length
// check rejects it either way -- the outcome is identical and Unity cannot tell the difference.
// What differs is that strlen() got there first. Measured with AddressSanitizer over a
// heap-allocated comfoair_config_t filled with 0xff: with this guard, clean; without it,
// `READ of size 743, 0 bytes after 840-byte region` inside comfoair_config_check_host(), because
// the last member of the struct is a bool and a true one gives strlen nothing to stop at.
static bool terminated(const char *buf, size_t cap) { return memchr(buf, '\0', cap) != NULL; }

// --- defaults ----------------------------------------------------------------------------------

// Exactly COMFOAIR_CONFIG_DEVICE_ID_LEN hex digits. Case is accepted either way and never
// rewritten: a caller that rendered its MAC with %02X gets its own string back rather than a
// silent second spelling of the same device.
static bool device_id_ok(const char *id)
{
    if (id == NULL)
        return false;
    size_t n = 0;
    for (; id[n] != '\0'; n++) {
        if (n >= COMFOAIR_CONFIG_DEVICE_ID_LEN || !is_hex(id[n]))
            return false;
    }
    return n == COMFOAIR_CONFIG_DEVICE_ID_LEN;
}

static void default_prefix(char *out, size_t cap, const char *device_id)
{
    // The shape is `comfoair/<device>`, and what goes in it is the full MAC, never the name
    // (docs/architecture/security.md, "Identity"). A prefix keyed on the name means renaming
    // re-keys every topic, the old retained discovery messages stay in the broker for ever, and
    // Home Assistant shows two devices of which one is dead and cannot be removed.
    if (device_id_ok(device_id))
        snprintf(out, cap, "comfoair/%s", device_id);
    else
        // A WORSE default -- two units in one house would share a topic tree -- and still much
        // better than refusing to boot over it. comfoair_config_sanitize() reports the field so
        // the owner is not left guessing.
        snprintf(out, cap, "comfoair");
}

static void default_name(char *out, size_t cap, const char *device_id)
{
    // The last four characters of the STA MAC, so the owner can tell one of two identical units
    // apart in Home Assistant. Display only: identity is the full MAC, never the name.
    //
    // THESE ARE NOT THE FOUR DIGITS OF THE ACCESS POINT, and the sentence that used to stand here
    // said they were. comfoair_net_short_id() reads ESP_MAC_WIFI_SOFTAP and this reads the STA MAC
    // that device_id is made of; the two differ by one in the last octet
    // (esp_hw_support/mac_addr.c:369-386), so a unit joined as `comfoair-8ae5` is named
    // `ComfoAir Q 8ae4` here. DO NOT "fix" that by switching this to the access point's MAC: this
    // string is stored in NVS on every device already in the field and reaches Home Assistant's
    // device registry, and moving it would rename every one of them. The access point and the
    // `.local` name take the same decision from the other side (both on the SoftAP MAC): what
    // gets kept in step is the pair the owner TYPES -- the label and the address bar -- and the
    // name they click on in Home Assistant is deliberately not in it.
    if (device_id_ok(device_id))
        snprintf(out, cap, "ComfoAir Q %s", device_id + COMFOAIR_CONFIG_DEVICE_ID_LEN - 4);
    else
        snprintf(out, cap, "ComfoAir Q");
}

void comfoair_config_defaults(comfoair_config_t *cfg, const char *device_id)
{
    if (cfg == NULL)
        return;
    memset(cfg, 0, sizeof *cfg);
    cfg->mqtt_port = DEFAULT_MQTT_PORT;
    // On by default, and it costs nothing until a broker is configured -- there is nowhere to
    // publish discovery to before then. AGENTS.md makes MQTT with HA discovery the
    // control path of v1 and criterion 2 makes "Home Assistant sees the device" an acceptance
    // criterion; an owner who typed a broker address and then had to find a checkbox would be
    // paying for a default that protects nobody.
    cfg->ha_discovery = true;
    default_prefix(cfg->topic_prefix, sizeof cfg->topic_prefix, device_id);
    default_name(cfg->device_name, sizeof cfg->device_name, device_id);
}

// --- the projection ------------------------------------------------------------------------------

bool comfoair_config_password_set(const comfoair_config_t *cfg)
{
    // DERIVED, and never its own key. A flag in a second key can be written while the record is
    // not -- a power cut lands between two NVS writes -- and the device then demands a password
    // that does not exist: locked, by nobody, for ever, behind the front panel of a running
    // ventilation unit. comfoair_config_sanitize() is what guarantees a non-empty record here is
    // also a READABLE one.
    return cfg != NULL && cfg->ui_pw_hash[0] != '\0';
}

void comfoair_config_project(const comfoair_config_t *cfg, comfoair_config_public_t *out)
{
    if (out == NULL)
        return;
    memset(out, 0, sizeof *out);
    // Every string a renderer will strlen() has to point somewhere, including on this path.
    out->psk           = "";
    out->mqtt_host     = "";
    out->mqtt_user     = "";
    out->mqtt_password = "";
    out->topic_prefix  = "";
    out->device_name   = "";
    out->ui_password   = "";
    if (cfg == NULL)
        return;

    // Clamped rather than trusted: this can be handed a document that has not been through
    // comfoair_config_sanitize(), and a length byte from flash can say 200. The alternative is
    // 200 bytes written into a 33-byte buffer on the HTTP task.
    size_t ssid_len = cfg->wifi.ssid_len;
    if (ssid_len > COMFOAIR_CONFIG_SSID_MAX)
        ssid_len = COMFOAIR_CONFIG_SSID_MAX;
    memcpy(out->ssid, cfg->wifi.ssid, ssid_len);
    out->ssid[ssid_len] = '\0';

    // comfoair_secret_redact() answers for a C STRING, and the stored key is not one: no
    // terminator, and 64 legal bytes. Same rule, asked of the length instead -- something stored
    // reads back as the sentinel, nothing stored reads back empty.
    out->psk = cfg->wifi.psk_len > 0 ? COMFOAIR_SECRET_SENTINEL : "";

    // The same terminated() guard comfoair_config_sanitize() runs, for the same reason and on the
    // same document -- the clamp above says this function can be handed one that has not been
    // sanitized, and these four leave here as `const char *` for a renderer to strlen(). Measured,
    // not reasoned about: a 0xff document with read_only set gives
    // `READ of size 743, 0 bytes after 840-byte region` under AddressSanitizer, from
    // strlen(pub.mqtt_host) on the HTTP task -- the same report the DO NOT on terminated() quotes,
    // reached through the one function that faces a client.
    //
    // "" rather than the bytes: a field the flash could not answer for is shown as nothing set,
    // which is true, while a truncated one would be a broker address the owner never typed.
    out->mqtt_host = terminated(cfg->mqtt_host, sizeof cfg->mqtt_host) ? cfg->mqtt_host : "";
    out->mqtt_port = cfg->mqtt_port;
    out->mqtt_user = terminated(cfg->mqtt_user, sizeof cfg->mqtt_user) ? cfg->mqtt_user : "";
    out->topic_prefix =
        terminated(cfg->topic_prefix, sizeof cfg->topic_prefix) ? cfg->topic_prefix : "";
    out->ha_discovery = cfg->ha_discovery;
    out->device_name =
        terminated(cfg->device_name, sizeof cfg->device_name) ? cfg->device_name : "";
    out->read_only = cfg->read_only;

    // Two storage policies, ONE disclosure policy (docs/architecture/security.md, "Write-only
    // secrets"). The broker password is in the struct in plaintext because MQTT authenticates with
    // it; the UI password is a one-way record. Neither leaves here. No terminated() guard on these
    // two, and that is not an omission: comfoair_secret_redact() reads exactly one byte and returns
    // a literal either way (comfoair_secrets.c:33-38), so what leaves is never the stored array.
    out->mqtt_password   = comfoair_secret_redact(cfg->mqtt_password);
    out->ui_password     = comfoair_secret_redact(cfg->ui_pw_hash);
    out->ui_password_set = comfoair_config_password_set(cfg);
}

// --- what came back out of the flash ---------------------------------------------------------

comfoair_config_repairs_t comfoair_config_sanitize(comfoair_config_t *cfg, const char *device_id)
{
    if (cfg == NULL)
        return 0;

    comfoair_config_repairs_t repairs = 0;

    // The Wi-Fi pair is repaired as ONE record because it is one, and an empty record is not a
    // repair -- a device with no network is a device on its own access point, which is a state, not
    // damage. A pair that is half readable is worse than none: it keeps the device looking
    // configured while it is unreachable, and the invariant that outranks everything else here is
    // that no state exists with no network, no access point and no way in
    // (docs/architecture/failure-model.md). The predicate is comfoair_config_wifi_usable() and not
    // two checkers spelled out here, because comfoair_config_nvs_has_credentials() asks the same
    // question about the same blob and the two answers decide different halves of one outcome: what
    // the station is handed, and whether an access point goes on air at all. They were two
    // predicates once. See the DO NOT on comfoair_config_wifi_usable().
    if (cfg->wifi.ssid_len != 0 || cfg->wifi.psk_len != 0) {
        if (!comfoair_config_wifi_usable(&cfg->wifi)) {
            memset(&cfg->wifi, 0, sizeof cfg->wifi);
            repairs |= COMFOAIR_CONFIG_REPAIRED(COMFOAIR_CONFIG_F_WIFI_SSID);
            repairs |= COMFOAIR_CONFIG_REPAIRED(COMFOAIR_CONFIG_F_WIFI_PSK);
        }
    }

    // `terminated()` is asked FIRST in each of these, and the short-circuit is load-bearing: the
    // checker takes a C string, and running one over an array with no NUL in it is the very
    // read past the end this is guarding against.
    if (!terminated(cfg->mqtt_host, sizeof cfg->mqtt_host) ||
        comfoair_config_check_host(cfg->mqtt_host) != COMFOAIR_CONFIG_OK) {
        memset(cfg->mqtt_host, 0, sizeof cfg->mqtt_host);
        repairs |= COMFOAIR_CONFIG_REPAIRED(COMFOAIR_CONFIG_F_MQTT_HOST);
    }
    if (comfoair_config_check_port(cfg->mqtt_port) != COMFOAIR_CONFIG_OK) {
        cfg->mqtt_port = DEFAULT_MQTT_PORT;
        repairs |= COMFOAIR_CONFIG_REPAIRED(COMFOAIR_CONFIG_F_MQTT_PORT);
    }
    if (!terminated(cfg->mqtt_user, sizeof cfg->mqtt_user) ||
        comfoair_config_check_user(cfg->mqtt_user) != COMFOAIR_CONFIG_OK) {
        memset(cfg->mqtt_user, 0, sizeof cfg->mqtt_user);
        repairs |= COMFOAIR_CONFIG_REPAIRED(COMFOAIR_CONFIG_F_MQTT_USER);
    }
    if (!terminated(cfg->mqtt_password, sizeof cfg->mqtt_password) ||
        comfoair_config_check_broker_password(cfg->mqtt_password) != COMFOAIR_CONFIG_OK) {
        memset(cfg->mqtt_password, 0, sizeof cfg->mqtt_password);
        repairs |= COMFOAIR_CONFIG_REPAIRED(COMFOAIR_CONFIG_F_MQTT_PASSWORD);
    }
    if (!terminated(cfg->topic_prefix, sizeof cfg->topic_prefix) ||
        comfoair_config_check_prefix(cfg->topic_prefix) != COMFOAIR_CONFIG_OK) {
        default_prefix(cfg->topic_prefix, sizeof cfg->topic_prefix, device_id);
        repairs |= COMFOAIR_CONFIG_REPAIRED(COMFOAIR_CONFIG_F_TOPIC_PREFIX);
    }
    if (!terminated(cfg->device_name, sizeof cfg->device_name) ||
        comfoair_config_check_name(cfg->device_name) != COMFOAIR_CONFIG_OK) {
        default_name(cfg->device_name, sizeof cfg->device_name, device_id);
        repairs |= COMFOAIR_CONFIG_REPAIRED(COMFOAIR_CONFIG_F_DEVICE_NAME);
    }

    // The uncomfortable one, and it is deliberate. A record that cannot be parsed matches no
    // password that exists, so KEEPING it locks the owner out of a device that is visible on their
    // network and completely unusable -- no way in over the air, and the front panel of a running
    // ventilation unit between them and the button. Clearing it leaves the device open on the LAN,
    // which the owner can SEE and fix. That is the no-way-back invariant
    // (docs/architecture/failure-model.md) applied to the password instead of to the radio. DO NOT
    // "harden" this into keeping the record.
    if (!terminated(cfg->ui_pw_hash, sizeof cfg->ui_pw_hash)) {
        memset(cfg->ui_pw_hash, 0, sizeof cfg->ui_pw_hash);
        repairs |= COMFOAIR_CONFIG_REPAIRED(COMFOAIR_CONFIG_F_UI_PASSWORD);
    } else if (cfg->ui_pw_hash[0] != '\0' && !comfoair_config_parse_record(cfg->ui_pw_hash, NULL)) {
        memset(cfg->ui_pw_hash, 0, sizeof cfg->ui_pw_hash);
        repairs |= COMFOAIR_CONFIG_REPAIRED(COMFOAIR_CONFIG_F_UI_PASSWORD);
    }

    // Nothing here logs. A rejected value may itself be a secret and the log ring is served to
    // whoever can reach the device, so the bitmask is how the fact travels -- and it has to
    // travel, because settings that vanish silently are indistinguishable from a firmware that
    // reset itself.
    return repairs;
}

// --- schema -----------------------------------------------------------------------------------

comfoair_config_schema_action_t comfoair_config_schema_check(uint32_t stored)
{
    if (stored == COMFOAIR_CONFIG_SCHEMA_VERSION)
        return COMFOAIR_CONFIG_SCHEMA_CURRENT;
    // A device with nothing stored reads 0 and takes the migration path with everybody else.
    // Making "fresh" a third answer would be three code paths where two do: migrating from
    // nothing finds nothing, which is exactly right, and migrating the legacy namespace
    // (COMFOAIR_CONFIG_NS_LEGACY_NAME) is then not a special case either.
    if (stored < COMFOAIR_CONFIG_SCHEMA_VERSION)
        return COMFOAIR_CONFIG_SCHEMA_MIGRATE;
    return COMFOAIR_CONFIG_SCHEMA_FUTURE;
}
