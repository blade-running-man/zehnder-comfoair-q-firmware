// comfoair_config, the half that touches flash. See comfoair_config_nvs.h for the two rules
// that govern every function here.
//
// THE GUARD BELOW IS NOT DECORATION. This component is an ESP-IDF component AND a PlatformIO
// library at once (platformio.ini, lib_extra_dirs), so the host test build compiles every .c in
// this directory the moment test_config includes comfoair_config.h -- and this one cannot
// compile without nvs.h. Faking NVS to get it to would mean test/test_config was testing the
// fake, which is exactly the split this component was designed around: what can be decided
// without flash is decided in the pure half (comfoair_config.c, _check.c, _password.c and
// _apply.c), where the suite reaches it for real.
#ifdef ESP_PLATFORM

#include "comfoair_config_nvs.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/pkcs5.h"
#include "nvs.h"

static const char *TAG = "cfg";

// What may never reach a log line is a VALUE for which comfoair_secret_key() is true: logging is
// captured whole and served by /api/log to whoever can reach the device
// (docs/architecture/security.md). A key NAME is not a value, and it is exactly what the owner
// needs to know when a store fails -- so names appear below and nothing else does.
static void warn_store(const char *key, esp_err_t err)
{
    if (err != ESP_OK)
        ESP_LOGW(TAG, "could not store %s: %s", key, esp_err_to_name(err));
}

// --- reading -----------------------------------------------------------------------------------

// The default already in `out` stands when the key is absent, unreadable, or longer than the field.
// FALSE says a value was there and this build could not take it, which is not the same fact and
// does not have the same answer: an absent key on a fresh device is nothing to report, while a
// stored broker address that does not fit becomes "no broker at all" and the owner has to be told,
// because settings that vanish silently are indistinguishable from a firmware that reset itself.
// Nothing else notices this one -- what replaces the value is a perfectly valid default, so
// comfoair_config_sanitize() sees nothing wrong.
static bool load_str(nvs_handle_t handle, const char *key, char *out, size_t cap)
{
    if (key == NULL)
        return true;
    // The length first, with a NULL buffer, which is the documented way to ask (nvs.h,
    // nvs_get_str). It is also what turns "stored by a firmware with bigger fields" into a
    // decision instead of an accident, and it saves carrying a second buffer on a task stack.
    size_t          len   = 0;
    const esp_err_t probe = nvs_get_str(handle, key, NULL, &len);
    if (probe == ESP_ERR_NVS_NOT_FOUND)
        return true;  // never written: the default is the answer, and nothing was lost
    if (probe != ESP_OK)
        return false;  // it is in there and the store would not say what it is
    // len counts the terminator. A value that does not fit is DROPPED, never truncated: half a
    // broker password authenticates against nothing and looks exactly like a broker that changed
    // its credentials.
    if (len == 0 || len > cap)
        return false;
    if (nvs_get_str(handle, key, out, &len) != ESP_OK)
        return false;
    // The window in which this struct holds an unterminated string is zero bytes wide. Not
    // instead of comfoair_config_sanitize() -- that is the real backstop and it runs over
    // everything this function fills -- but a terminator costs nothing here.
    out[cap - 1] = '\0';
    return true;
}

// Everything below addresses NVS by FIELD, never by a bare string, so the table in
// comfoair_config.c is the only place a key name is written down. The NULL check is not
// defensive noise: comfoair_config_field() answers NULL for an enumerator that was added without
// a row, and a NULL dereference at BOOT is the one failure this component may never have -- an
// unnamed field must simply not persist. The name pinned by test_config is what stops that
// happening quietly.
static const char *key_of(comfoair_config_field_t field)
{
    const comfoair_config_field_info_t *f = comfoair_config_field(field);
    return f != NULL ? f->key : NULL;
}

// Addressed by FIELD so the repair bit and the NVS key cannot drift apart -- the bitmask uses the
// same enumerator that names the key, which is why it can be told to the owner at all.
static void load_str_field(nvs_handle_t handle, comfoair_config_field_t field, char *out,
                           size_t cap, comfoair_config_repairs_t *lost)
{
    if (!load_str(handle, key_of(field), out, cap))
        *lost |= COMFOAIR_CONFIG_REPAIRED(field);
}

static void load_u16(nvs_handle_t handle, const char *key, uint16_t *out)
{
    if (key == NULL)
        return;
    uint16_t value = 0;
    if (nvs_get_u16(handle, key, &value) == ESP_OK)
        *out = value;
}

static void load_bool(nvs_handle_t handle, const char *key, bool *out)
{
    if (key == NULL)
        return;
    // Read as u8 and normalised, never as a bool. A _Bool holding a bit pattern that is neither
    // 0 nor 1 is not a value a C program may read at all, and flash can hand back 0xff -- so a
    // blob straight into a struct with a bool in it is undefined before anything has validated
    // anything. This is also why the document is written field by field rather than as one blob.
    uint8_t value = 0;
    if (nvs_get_u8(handle, key, &value) == ESP_OK)
        *out = value != 0;
}

// The pair, whole or not at all. A blob of any other size was written by a firmware whose
// comfoair_wifi_t is not this one, and reinterpreting it field by field is how a length byte
// becomes an SSID length. The size IS the version here; there is no room in the record for one
// and no need, because the two lengths are already at fixed offsets and any change to them
// changes the size.
static bool load_wifi(nvs_handle_t handle, const char *key, comfoair_wifi_t *out)
{
    comfoair_wifi_t record;
    size_t          len = sizeof record;
    if (nvs_get_blob(handle, key, &record, &len) != ESP_OK)
        return false;
    if (len != sizeof record)
        return false;
    *out = record;
    return true;
}

// Erasing is by NAMESPACE, with nvs_erase_all(), and never by a list of keys: a list goes stale
// the first time a field is added, and it can never name a key an OLDER firmware wrote and this
// build has not heard of. Both callers below want exactly this and nothing more.
static esp_err_t erase_namespace(const char *name)
{
    // Asked READONLY first, and only to find out whether the namespace exists: nvs_open in
    // NVS_READWRITE CREATES one. Without this probe, a factory reset on a device that has never
    // been configured leaves behind four namespaces it never had, and the legacy migration
    // would create the very namespace it exists to retire.
    nvs_handle_t probe;
    esp_err_t    err = nvs_open(name, NVS_READONLY, &probe);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return ESP_OK;  // never written: nothing to erase, and nothing wrong
    if (err == ESP_OK)
        nvs_close(probe);

    nvs_handle_t handle;
    err = nvs_open(name, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "could not open %s to erase it: %s", name, esp_err_to_name(err));
        return err;
    }
    err = nvs_erase_all(handle);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK)
        ESP_LOGW(TAG, "could not erase %s: %s", name, esp_err_to_name(err));
    return err;
}

// --- the migration ------------------------------------------------------------------------------

// An earlier comfoair_net stored the pair as two strings in its own namespace
// (COMFOAIR_CONFIG_NS_LEGACY_NAME), before this component existed. Moving it is what keeps an OTA from sending an owner back to the setup page with a
// device that has forgotten a network it has been on for a year.
//
// The source is ERASED once the pair is safely under the new key, and that is not tidiness: with
// it left in place, a later "Forget Wi-Fi" -- which clears the new key -- would be undone by this
// very function at the next boot, and the network the owner deleted would come back.
static bool migrate_legacy_wifi(comfoair_wifi_t *out)
{
    nvs_handle_t handle;
    // READONLY. Opening a namespace NVS_READWRITE CREATES it, and this runs at the first boot of
    // every device -- a fresh unit would be given an empty legacy namespace to remember for ever.
    if (nvs_open(COMFOAIR_CONFIG_NS_LEGACY_NAME, NVS_READONLY, &handle) != ESP_OK)
        return false;

    char   ssid[COMFOAIR_CONFIG_SSID_MAX + 1] = {0};
    char   psk[COMFOAIR_CONFIG_PSK_MAX + 1]   = {0};
    size_t ssid_len                           = sizeof ssid;
    size_t psk_len                            = sizeof psk;

    bool have = nvs_get_str(handle, COMFOAIR_CONFIG_LEGACY_KEY_SSID, ssid, &ssid_len) == ESP_OK &&
                ssid[0] != '\0';
    if (have) {
        // ESP_ERR_NVS_NOT_FOUND, and nothing else, is "there is no password": a network with no
        // key is legal, which is the rule comfoair_config_check_psk() applies to every key.
        // But that code only had to ANSWER a question with it, and this one ERASES what it read.
        // Any other error -- ESP_ERR_NVS_INVALID_LENGTH from a value longer than this buffer, a
        // transient failure -- is not the fact "there is no password", and taking it as one
        // writes an OPEN-NETWORK record and then destroys the only copy of the key: the device
        // can never associate with that network again and nothing on it remembers the password.
        const esp_err_t e = nvs_get_str(handle, COMFOAIR_CONFIG_LEGACY_KEY_PSK, psk, &psk_len);
        if (e == ESP_ERR_NVS_NOT_FOUND) {
            psk[0] = '\0';
        } else if (e != ESP_OK) {
            // The key NAME and the error, never the value: GET /api/log serves the ring to anyone.
            ESP_LOGW(TAG, "the old %s did not read (%s); the old network is left where it is",
                     COMFOAIR_CONFIG_LEGACY_KEY_PSK, esp_err_to_name(e));
            have = false;
        }
    }
    nvs_close(handle);

    // VALIDATED before it is written, like everything else on the way in -- a migration is a point
    // of acceptance, and this is the one path that puts bytes this build never checked under a key
    // it will act on. A pair this build cannot use (a 64-character key that is not hex, a
    // passphrase below the WPA2 floor) written under the new key is one
    // comfoair_config_nvs_has_credentials() calls present for the rest of the device's life while
    // comfoair_config_sanitize() clears it from the document at every boot.
    //
    // Refused means the source is LEFT ALONE. Erasing what this build declined to accept would
    // destroy the owner's only copy of a network a later build might understand.
    const bool moved = have && comfoair_config_wifi_from_legacy(out, ssid, psk);
    if (have && !moved)
        ESP_LOGW(TAG, "the old network is not one this build can store; it is left where it is");

    if (moved) {
        // Written under the new key FIRST, and the old namespace erased only after that write
        // reported success. A power cut in between costs one duplicated pair, which the next
        // boot resolves; the other order costs the network.
        if (comfoair_config_nvs_save_wifi(out) != ESP_OK)
            return false;
        // ONLY the legacy namespace. DO NOT reach for comfoair_config_nvs_reset(SOFT) because it
        // is the tier that clears Wi-Fi: it would erase the pair this function has just written
        // AND the owner's UI password, turning an OTA into a factory reset. The tiers are a
        // gesture's vocabulary, not a general-purpose eraser.
        const esp_err_t err = erase_namespace(COMFOAIR_CONFIG_NS_LEGACY_NAME);
        // Failing to clear the source is survivable and worth seeing: the pair is already safe,
        // and the only cost is that a later "Forget Wi-Fi" could be undone at the next boot.
        if (err != ESP_OK)
            ESP_LOGW(TAG, "migrated the network but could not clear the old namespace: %s",
                     esp_err_to_name(err));
    }
    return moved;
}

// --- loading ---------------------------------------------------------------------------------

esp_err_t comfoair_config_nvs_load(comfoair_config_t *cfg, const char *device_id,
                                   comfoair_config_repairs_t *repairs)
{
    if (repairs != NULL)
        *repairs = 0;
    if (cfg == NULL)
        return ESP_ERR_INVALID_ARG;

    // Defaults FIRST, so that every early return below hands back a document the device can boot
    // on. There is no path out of this function that leaves `cfg` half filled.
    comfoair_config_defaults(cfg, device_id);

    esp_err_t    result = ESP_OK;
    nvs_handle_t app;
    esp_err_t    err = nvs_open(COMFOAIR_CONFIG_NS_APP_NAME, NVS_READONLY, &app);
    uint32_t     stored_schema = 0;
    if (err == ESP_OK) {
        if (nvs_get_u32(app, COMFOAIR_CONFIG_KEY_SCHEMA, &stored_schema) != ESP_OK)
            stored_schema = 0;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        // Not "no settings yet" but "the store did not answer". Reported, never fatal.
        ESP_LOGW(TAG, "settings unreadable (%s); starting from defaults", esp_err_to_name(err));
        result = err;
    }

    const comfoair_config_schema_action_t action = comfoair_config_schema_check(stored_schema);
    // An OTA rollback lands on a store written by a build that knew more than this one.
    // Erasing it would destroy a working owner's settings to fix a problem they do not have, and
    // refusing to boot is a brick behind a front panel -- so the document is read with whatever
    // this build understands and every SETTINGS write is refused: comfoair_config_apply() and
    // comfoair_config_nvs_save() both check this flag.
    //
    // DO NOT extend that refusal to comfoair_config_nvs_save_wifi(), _commit_known_good() or
    // _restore_known_good(), which take no document and are deliberately not gated. Those three are
    // the way back. A rolled-back device whose network no longer works raises its access point, and
    // a provisioning POST that stores nothing leaves no network, a useless access point and no way
    // in -- the one state docs/architecture/failure-model.md forbids outright. The price of the
    // exemption is one pair a roll-forward may have to be re-entered, from a device the owner can
    // reach; the fifteen-second gesture is not an answer here, because the button may be behind the
    // front panel of a running ventilation unit.
    cfg->read_only = (action == COMFOAIR_CONFIG_SCHEMA_FUTURE);

    // What was in the flash and could not be taken. It is NOT what comfoair_config_sanitize()
    // finds: a dropped value leaves a valid default behind, so the sanitizer sees a document with
    // nothing wrong in it and reports nothing. The loss is shown to the owner either way.
    comfoair_config_repairs_t lost = 0;

    if (err == ESP_OK) {
        load_str_field(app, COMFOAIR_CONFIG_F_MQTT_HOST, cfg->mqtt_host, sizeof cfg->mqtt_host,
                       &lost);
        load_u16(app, key_of(COMFOAIR_CONFIG_F_MQTT_PORT), &cfg->mqtt_port);
        load_str_field(app, COMFOAIR_CONFIG_F_MQTT_USER, cfg->mqtt_user, sizeof cfg->mqtt_user,
                       &lost);
        // Plaintext, on purpose: MQTT authenticates with it and a hash here is not hardening, it is
        // a broker that never connects. It is never RETURNED -- that is comfoair_config_project()'s
        // job, and it is the same answer for both passwords.
        load_str_field(app, COMFOAIR_CONFIG_F_MQTT_PASSWORD, cfg->mqtt_password,
                       sizeof cfg->mqtt_password, &lost);
        load_str_field(app, COMFOAIR_CONFIG_F_TOPIC_PREFIX, cfg->topic_prefix,
                       sizeof cfg->topic_prefix, &lost);
        load_bool(app, key_of(COMFOAIR_CONFIG_F_HA_DISCOVERY), &cfg->ha_discovery);
        load_str_field(app, COMFOAIR_CONFIG_F_DEVICE_NAME, cfg->device_name,
                       sizeof cfg->device_name, &lost);
        nvs_close(app);
    }

    nvs_handle_t owner;
    if (nvs_open(COMFOAIR_CONFIG_NS_OWNER_NAME, NVS_READONLY, &owner) == ESP_OK) {
        // A record too long for this field is a password the owner still believes in and this
        // build cannot check. Dropping it leaves the device OPEN rather than sealed -- the same
        // trade comfoair_config_sanitize() makes for a record that will not parse -- so it has to
        // be reported, or the owner meets a device that stopped asking for its password.
        load_str_field(owner, COMFOAIR_CONFIG_F_UI_PASSWORD, cfg->ui_pw_hash,
                       sizeof cfg->ui_pw_hash, &lost);
        nvs_close(owner);
    }

    bool         have_wifi = false;
    nvs_handle_t wifi;
    if (nvs_open(COMFOAIR_CONFIG_NS_WIFI_NAME, NVS_READONLY, &wifi) == ESP_OK) {
        have_wifi = load_wifi(wifi, COMFOAIR_CONFIG_KEY_STA, &cfg->wifi);
        nvs_close(wifi);
    }
    // Only when there is nothing under the new key. A device that has been provisioned by THIS
    // firmware must never have its network replaced by whatever the old one left behind.
    if (!have_wifi && action == COMFOAIR_CONFIG_SCHEMA_MIGRATE)
        (void)migrate_legacy_wifi(&cfg->wifi);

    const comfoair_config_repairs_t repaired = comfoair_config_sanitize(cfg, device_id);
    if (repairs != NULL)
        *repairs = repaired | lost;

    // The schema number is written only once the document has been READ and repaired. Writing it
    // earlier would mark a store as migrated that a power cut then left half moved -- and
    // `result` is the other half of "read": it carries an open that failed with something other
    // than "never written", which is a boot that got not one field out of this namespace.
    // Marking that store current retires the migration for good, on a device where it never ran.
    if (action == COMFOAIR_CONFIG_SCHEMA_MIGRATE && !cfg->read_only && result == ESP_OK) {
        nvs_handle_t write;
        if (nvs_open(COMFOAIR_CONFIG_NS_APP_NAME, NVS_READWRITE, &write) == ESP_OK) {
            esp_err_t e = nvs_set_u32(write, COMFOAIR_CONFIG_KEY_SCHEMA,
                                      COMFOAIR_CONFIG_SCHEMA_VERSION);
            if (e == ESP_OK)
                e = nvs_commit(write);
            warn_store(COMFOAIR_CONFIG_KEY_SCHEMA, e);
            nvs_close(write);
        }
    }

    return result;
}

// --- writing ------------------------------------------------------------------------------------

esp_err_t comfoair_config_nvs_save_wifi(const comfoair_wifi_t *wifi)
{
    if (wifi == NULL)
        return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t    err = nvs_open(COMFOAIR_CONFIG_NS_WIFI_NAME, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;

    // ONE key. Not two, not "ssid then psk", and DO NOT split this for symmetry with the API:
    // the whole reason comfoair_wifi_t exists is that no power cut can land between the two
    // halves of a credential (docs/device/config-storage.md).
    err = nvs_set_blob(handle, COMFOAIR_CONFIG_KEY_STA, wifi, sizeof *wifi);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    warn_store(COMFOAIR_CONFIG_KEY_STA, err);
    nvs_close(handle);
    return err;
}

// One field, one warning, and the first failure kept. Written as three helpers rather than a
// chain of `if (e == ESP_OK)` because that chain reports the FIRST field's error against the
// SECOND field's name, and a warning that blames the wrong setting is worse than none.
static void set_str_field(nvs_handle_t handle, comfoair_config_field_t field, const char *value,
                          esp_err_t *first)
{
    const char *key = key_of(field);
    if (key == NULL)
        return;
    const esp_err_t e = nvs_set_str(handle, key, value);
    warn_store(key, e);
    if (*first == ESP_OK)
        *first = e;
}

static void set_u16_field(nvs_handle_t handle, comfoair_config_field_t field, uint16_t value,
                          esp_err_t *first)
{
    const char *key = key_of(field);
    if (key == NULL)
        return;
    const esp_err_t e = nvs_set_u16(handle, key, value);
    warn_store(key, e);
    if (*first == ESP_OK)
        *first = e;
}

static void set_bool_field(nvs_handle_t handle, comfoair_config_field_t field, bool value,
                           esp_err_t *first)
{
    // Stored as u8; see load_bool() for why a bool never goes near flash in either direction.
    const char *key = key_of(field);
    if (key == NULL)
        return;
    const esp_err_t e = nvs_set_u8(handle, key, value ? 1 : 0);
    warn_store(key, e);
    if (*first == ESP_OK)
        *first = e;
}

esp_err_t comfoair_config_nvs_save(const comfoair_config_t *cfg)
{
    if (cfg == NULL)
        return ESP_ERR_INVALID_ARG;
    // The same gate comfoair_config_apply() has, because this function can be reached without
    // going through it. A store written by a newer build is not written into by this one.
    if (cfg->read_only)
        return ESP_ERR_INVALID_STATE;

    esp_err_t first = comfoair_config_nvs_save_wifi(&cfg->wifi);

    nvs_handle_t app;
    esp_err_t    err = nvs_open(COMFOAIR_CONFIG_NS_APP_NAME, NVS_READWRITE, &app);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "could not open %s: %s", COMFOAIR_CONFIG_NS_APP_NAME, esp_err_to_name(err));
        if (first == ESP_OK)
            first = err;
    } else {
        // Every field is attempted even after one fails. Stopping early would leave a document
        // that is neither the old one nor the new one, and nothing would say which fields made it.
        set_str_field(app, COMFOAIR_CONFIG_F_MQTT_HOST, cfg->mqtt_host, &first);
        set_u16_field(app, COMFOAIR_CONFIG_F_MQTT_PORT, cfg->mqtt_port, &first);
        set_str_field(app, COMFOAIR_CONFIG_F_MQTT_USER, cfg->mqtt_user, &first);
        // Plaintext, on purpose: MQTT authenticates with it, so a hash here is not hardening, it is
        // a broker that never connects. It is never RETURNED, which is comfoair_config_project()'s
        // job and the same answer for both passwords.
        set_str_field(app, COMFOAIR_CONFIG_F_MQTT_PASSWORD, cfg->mqtt_password, &first);
        set_str_field(app, COMFOAIR_CONFIG_F_TOPIC_PREFIX, cfg->topic_prefix, &first);
        set_bool_field(app, COMFOAIR_CONFIG_F_HA_DISCOVERY, cfg->ha_discovery, &first);
        set_str_field(app, COMFOAIR_CONFIG_F_DEVICE_NAME, cfg->device_name, &first);

        esp_err_t e = nvs_set_u32(app, COMFOAIR_CONFIG_KEY_SCHEMA, COMFOAIR_CONFIG_SCHEMA_VERSION);
        warn_store(COMFOAIR_CONFIG_KEY_SCHEMA, e);
        if (e == ESP_OK)
            e = nvs_commit(app);
        nvs_close(app);
        if (first == ESP_OK)
            first = e;
    }

    nvs_handle_t owner;
    err = nvs_open(COMFOAIR_CONFIG_NS_OWNER_NAME, NVS_READWRITE, &owner);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "could not open %s: %s", COMFOAIR_CONFIG_NS_OWNER_NAME, esp_err_to_name(err));
        if (first == ESP_OK)
            first = err;
    } else {
        // The record, never the password -- and there is NO companion "a password is set" key to
        // write, because that flag is DERIVED (comfoair_config_password_set). A half-finished
        // write that stored the flag and not the record would leave the device demanding a
        // password nobody has.
        set_str_field(owner, COMFOAIR_CONFIG_F_UI_PASSWORD, cfg->ui_pw_hash, &first);
        const esp_err_t e = nvs_commit(owner);
        if (e != ESP_OK)
            ESP_LOGW(TAG, "could not commit %s: %s", COMFOAIR_CONFIG_NS_OWNER_NAME,
                     esp_err_to_name(e));
        nvs_close(owner);
        if (first == ESP_OK)
            first = e;
    }

    return first;
}

// --- the rollback ---------------------------------------------------------------------------

esp_err_t comfoair_config_nvs_commit_known_good(void)
{
    nvs_handle_t handle;
    esp_err_t    err = nvs_open(COMFOAIR_CONFIG_NS_WIFI_NAME, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;

    comfoair_wifi_t wifi;
    if (!load_wifi(handle, COMFOAIR_CONFIG_KEY_STA, &wifi)) {
        nvs_close(handle);
        // Nothing to promote. Not an error worth propagating as one: the caller asked to record
        // that what is stored works, and what is stored is nothing.
        return ESP_ERR_NVS_NOT_FOUND;
    }
    err = nvs_set_blob(handle, COMFOAIR_CONFIG_KEY_STA_GOOD, &wifi, sizeof wifi);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    warn_store(COMFOAIR_CONFIG_KEY_STA_GOOD, err);
    nvs_close(handle);
    return err;
}

esp_err_t comfoair_config_nvs_restore_known_good(comfoair_wifi_t *out)
{
    nvs_handle_t handle;
    esp_err_t    err = nvs_open(COMFOAIR_CONFIG_NS_WIFI_NAME, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;

    comfoair_wifi_t wifi;
    if (!load_wifi(handle, COMFOAIR_CONFIG_KEY_STA_GOOD, &wifi)) {
        nvs_close(handle);
        // A first provisioning has nothing to roll back to, and that is a state, not a fault:
        // the way back for that device is its own access point, which it still has.
        return ESP_ERR_NVS_NOT_FOUND;
    }
    err = nvs_set_blob(handle, COMFOAIR_CONFIG_KEY_STA, &wifi, sizeof wifi);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    warn_store(COMFOAIR_CONFIG_KEY_STA, err);
    nvs_close(handle);
    if (err == ESP_OK && out != NULL)
        *out = wifi;
    return err;
}

static bool has_pair(const char *key)
{
    nvs_handle_t handle;
    if (nvs_open(COMFOAIR_CONFIG_NS_WIFI_NAME, NVS_READONLY, &handle) != ESP_OK)
        return false;
    comfoair_wifi_t wifi;
    const bool      found = load_wifi(handle, key, &wifi);
    nvs_close(handle);
    // comfoair_config_wifi_usable(), and NOT a length test written out here. This answer decides
    // whether the device raises its own access point; comfoair_config_sanitize() decides what the
    // station is handed, from the same blob. A second, weaker predicate here made those two
    // disagree -- present to the state machine, cleared in the document -- which is a device with
    // no network, no access point and no way back (docs/architecture/failure-model.md). The test
    // that keeps them one is test_config's table over both.
    return found && comfoair_config_wifi_usable(&wifi);
}

bool comfoair_config_nvs_has_credentials(void) { return has_pair(COMFOAIR_CONFIG_KEY_STA); }

bool comfoair_config_nvs_has_known_good(void) { return has_pair(COMFOAIR_CONFIG_KEY_STA_GOOD); }

// --- the two facts a reboot must not erase ------------------------------------------------

void comfoair_config_nvs_load_prov_flags(bool *window_closed, bool *address_never_arrived)
{
    if (window_closed == NULL || address_never_arrived == NULL)
        return;
    // False before anything is read, and false after anything that fails. Both are ways back and
    // the safe answer is the one that offers MORE of a way back: forgetting that a window closed
    // costs the owner a longer window, forgetting a no-address failure costs one more attempt at
    // a network. The opposite default costs an open access point at every boot.
    *window_closed         = false;
    *address_never_arrived = false;

    nvs_handle_t handle;
    // READONLY: opening NVS_READWRITE creates the namespace, and this runs at the first boot of
    // every device -- a fresh unit would be given an empty cfg_wifi to remember for ever.
    if (nvs_open(COMFOAIR_CONFIG_NS_WIFI_NAME, NVS_READONLY, &handle) != ESP_OK)
        return;
    // u8 and normalised, never a bool straight out of flash: a _Bool holding a bit pattern that
    // is neither 0 nor 1 is not a value a C program may read at all, and flash can hand back
    // 0xff. Same rule and same reason as load_bool() above.
    uint8_t value = 0;
    if (nvs_get_u8(handle, COMFOAIR_CONFIG_KEY_WINDOW_CLOSED, &value) == ESP_OK)
        *window_closed = value != 0;
    value = 0;
    if (nvs_get_u8(handle, COMFOAIR_CONFIG_KEY_NO_ADDRESS, &value) == ESP_OK)
        *address_never_arrived = value != 0;
    nvs_close(handle);
}

esp_err_t comfoair_config_nvs_save_prov_flags(bool window_closed, bool address_never_arrived)
{
    nvs_handle_t handle;
    esp_err_t    err = nvs_open(COMFOAIR_CONFIG_NS_WIFI_NAME, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;

    // Both attempted before either is reported, and ONE commit for the pair. They are read back
    // together as one answer about the last run, and a commit between them would let a power cut
    // land on a half-answer.
    esp_err_t first = nvs_set_u8(handle, COMFOAIR_CONFIG_KEY_WINDOW_CLOSED, window_closed ? 1 : 0);
    warn_store(COMFOAIR_CONFIG_KEY_WINDOW_CLOSED, first);
    const esp_err_t second =
        nvs_set_u8(handle, COMFOAIR_CONFIG_KEY_NO_ADDRESS, address_never_arrived ? 1 : 0);
    warn_store(COMFOAIR_CONFIG_KEY_NO_ADDRESS, second);
    if (first == ESP_OK)
        first = second;

    const esp_err_t commit = nvs_commit(handle);
    if (first == ESP_OK)
        first = commit;
    nvs_close(handle);
    return first;
}

// --- the two-tier reset ----------------------------------------------------------------------

esp_err_t comfoair_config_nvs_reset(comfoair_config_reset_t tier)
{
    esp_err_t first = ESP_OK;

    // Over the ENUM, so a namespace added later is erased by a factory reset without anyone
    // remembering to extend anything here. DO NOT replace this with a list of keys: a list cannot
    // name a key an older firmware wrote and this build has never heard of, and nvs_erase_all()
    // takes both.
    for (int i = 0; i < COMFOAIR_CONFIG_NS_COUNT; i++) {
        const comfoair_config_ns_t ns = (comfoair_config_ns_t)i;
        if (!comfoair_config_reset_erases(tier, ns))
            continue;

        // The loop CARRIES ON past a failure. A reset that stops at the first bad namespace
        // leaves the network gone and the password kept, or the reverse, and neither of those is
        // what either gesture means to the person holding the button.
        const esp_err_t err = erase_namespace(comfoair_config_ns_name(ns));
        if (err != ESP_OK && first == ESP_OK)
            first = err;
    }
    return first;
}

// --- the one-way function ---------------------------------------------------------------------

static bool device_kdf(const char *password, const uint8_t *salt, size_t salt_len,
                       uint32_t iterations, uint8_t *out, size_t out_len)
{
    // MBEDTLS_PKCS5_C is defined unconditionally by the IDF's mbedtls port
    // (mbedtls/port/include/mbedtls/esp_config.h:2415) and has no Kconfig switch, so this cannot
    // be configured away by an sdkconfig change. SHA-256 rides the hardware accelerator
    // (CONFIG_MBEDTLS_HARDWARE_SHA=y, sdkconfig.m5stack-nanoc6:3156).
    //
    // _ext, not mbedtls_pkcs5_pbkdf2_hmac: the latter is deprecated in mbedtls 3.x
    // (pkcs5.h:160) and wants a prepared md context this has no reason to own.
    const int rc = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, (const unsigned char *)password,
                                                 strlen(password), salt, salt_len,
                                                 (unsigned int)iterations, (uint32_t)out_len, out);
    return rc == 0;
}

comfoair_config_kdf_t comfoair_config_device_kdf(void) { return device_kdf; }

void comfoair_config_device_hash_ctx(comfoair_config_hash_ctx_t *ctx,
                                     uint8_t salt[COMFOAIR_CONFIG_SALT_LEN])
{
    if (ctx == NULL || salt == NULL)
        return;
    // esp_random() is only a TRUE random source once Wi-Fi or Bluetooth is running
    // (esp_random.h, esp_random). A password can be set before that -- the first one is set on the
    // access point -- so this salt may come from a weaker source, and that is ACCEPTABLE HERE and
    // nowhere else in this firmware: a salt has to be unique, not unpredictable. Its entire job is
    // to stop one recovered flash image from answering for every device whose owner chose the same
    // word, and a counter would do that. DO NOT copy this reasoning to a key or a token.
    esp_fill_random(salt, COMFOAIR_CONFIG_SALT_LEN);
    ctx->kdf        = device_kdf;
    ctx->salt       = salt;
    ctx->iterations = COMFOAIR_CONFIG_KDF_ITERATIONS;
}

#endif  // ESP_PLATFORM
