// comfoair_config: applying a submitted document -- every field decided before any is written.
// Read comfoair_config.h first; the component's shape is in README.md, Implementation.
#include "comfoair_config.h"

#include <string.h>

static void copy_str(char *dst, size_t cap, const char *src)
{
    // Everything committed through here was length-checked before anything was written, so this
    // cannot truncate in practice. It is bounded anyway: a field added later without its checker
    // then loses characters instead of smashing the struct behind it.
    const size_t n = strlen(src);
    const size_t k = n < cap ? n : cap - 1;
    memcpy(dst, src, k);
    dst[k] = '\0';
}

// --- applying a submitted document ---------------------------------------------------------------

typedef enum { WIFI_KEEP, WIFI_SET, WIFI_CLEAR } wifi_action_t;

comfoair_config_err_t comfoair_config_apply(comfoair_config_t *cfg,
                                            const comfoair_config_patch_t *patch,
                                            const comfoair_config_hash_ctx_t *hash)
{
    // A body that failed to parse arrives as nothing at all, on the HTTP task. It must not crash
    // and it must not answer OK: OK is what the handler turns into 200 and what
    // comfoair_config_strerror() renders as the word "accepted", so the owner would read Saved
    // over a request that stored nothing. An absent FIELD still means "leave it alone" -- that is
    // the whole of the sentinel scheme -- but an absent DOCUMENT is not a save.
    if (cfg == NULL || patch == NULL)
        return COMFOAIR_CONFIG_ERR_NO_DOCUMENT;

    // One check, before every write path, rather than one per caller. An OTA rollback leaves
    // a store written by a build that knew more than this one; writing into it destroys what the
    // newer build stored and the owner who rolls forward again finds their settings gone.
    if (cfg->read_only)
        return COMFOAIR_CONFIG_ERR_READ_ONLY;

    // ---- everything is decided before anything is written -----------------------------------
    //
    // A document with a good host and a bad port must change NEITHER. Otherwise the owner fixes
    // the port, saves again, and the second attempt lands on a document that is no longer the one
    // they were looking at.

    wifi_action_t wifi     = WIFI_KEEP;
    size_t        ssid_len = 0;
    size_t        psk_len  = 0;

    const bool has_ssid = patch->wifi_ssid != NULL;
    const bool has_psk  = patch->wifi_psk != NULL;
    // Half a pair cannot be applied without inventing the other half, and the invented one is
    // always the stored one -- which is precisely the pairing this component exists to prevent.
    if (has_ssid != has_psk)
        return COMFOAIR_CONFIG_ERR_WIFI_PAIR;

    if (has_ssid) {
        ssid_len = strlen(patch->wifi_ssid);
        if (strcmp(patch->wifi_psk, COMFOAIR_SECRET_SENTINEL) == 0) {
            // The settings page renders a stored key as the sentinel, so a form where only the
            // SSID was edited submits a NEW network carrying the PREVIOUS network's password.
            // Storing that pair is the atomicity failure arriving through the front door instead
            // of through a power cut, and it strands the device just as completely.
            if (ssid_len != cfg->wifi.ssid_len ||
                memcmp(patch->wifi_ssid, cfg->wifi.ssid, ssid_len) != 0)
                return COMFOAIR_CONFIG_ERR_WIFI_PAIR;
            // Same network, untouched key: the document says nothing new about the pair. This
            // branch is what makes saving any OTHER setting possible once a network is stored --
            // the page submits the whole document every time.
            wifi = WIFI_KEEP;
        } else if (ssid_len == 0 && patch->wifi_psk[0] == '\0') {
            // Both halves emptied deliberately: forget this network. The only way the page can
            // say it, and it clears both halves in one act for the same reason they are one key.
            wifi = WIFI_CLEAR;
        } else {
            psk_len = strlen(patch->wifi_psk);
            const comfoair_config_err_t e_ssid =
                comfoair_config_check_ssid((const uint8_t *)patch->wifi_ssid, ssid_len);
            if (e_ssid != COMFOAIR_CONFIG_OK)
                return e_ssid;
            const comfoair_config_err_t e_psk =
                comfoair_config_check_psk((const uint8_t *)patch->wifi_psk, psk_len);
            if (e_psk != COMFOAIR_CONFIG_OK)
                return e_psk;
            wifi = WIFI_SET;
        }
    }

    if (patch->mqtt_host != NULL) {
        const comfoair_config_err_t e = comfoair_config_check_host(patch->mqtt_host);
        if (e != COMFOAIR_CONFIG_OK)
            return e;
    }
    if (patch->has_mqtt_port) {
        const comfoair_config_err_t e = comfoair_config_check_port(patch->mqtt_port);
        if (e != COMFOAIR_CONFIG_OK)
            return e;
    }
    if (patch->mqtt_user != NULL) {
        const comfoair_config_err_t e = comfoair_config_check_user(patch->mqtt_user);
        if (e != COMFOAIR_CONFIG_OK)
            return e;
    }

    comfoair_secret_action_t broker = COMFOAIR_SECRET_KEEP;
    if (patch->mqtt_password != NULL) {
        broker = comfoair_secret_decide(cfg->mqtt_password[0] != '\0', patch->mqtt_password);
        if (broker == COMFOAIR_SECRET_REJECT)
            return COMFOAIR_CONFIG_ERR_BROKER_PASSWORD;
        if (broker == COMFOAIR_SECRET_STORE) {
            const comfoair_config_err_t e =
                comfoair_config_check_broker_password(patch->mqtt_password);
            if (e != COMFOAIR_CONFIG_OK)
                return e;
        }
    }

    if (patch->topic_prefix != NULL) {
        const comfoair_config_err_t e = comfoair_config_check_prefix(patch->topic_prefix);
        if (e != COMFOAIR_CONFIG_OK)
            return e;
    }
    if (patch->device_name != NULL) {
        const comfoair_config_err_t e = comfoair_config_check_name(patch->device_name);
        if (e != COMFOAIR_CONFIG_OK)
            return e;
    }

    comfoair_secret_action_t ui = COMFOAIR_SECRET_KEEP;
    char                     new_record[COMFOAIR_CONFIG_HASH_MAX + 1];
    if (patch->ui_password != NULL) {
        ui = comfoair_secret_decide(comfoair_config_password_set(cfg), patch->ui_password);
        if (ui == COMFOAIR_SECRET_REJECT)
            return COMFOAIR_CONFIG_ERR_UI_PASSWORD;
        if (ui == COMFOAIR_SECRET_STORE) {
            const comfoair_config_err_t e = comfoair_config_check_ui_password(patch->ui_password);
            if (e != COMFOAIR_CONFIG_OK)
                return e;
            // NEVER a fall-through to storing the plaintext. A missing hash context is the
            // owner's device being unable to honour a submission that was perfectly good, which
            // is why it has an error code of its own -- retyping will not help them.
            if (hash == NULL || hash->kdf == NULL || hash->salt == NULL)
                return COMFOAIR_CONFIG_ERR_NO_HASH;
            // Derived into a local before anything is committed. A derivation that fails on the
            // device -- the RNG not started yet, mbedtls out of memory -- must leave the previous
            // password working rather than a half-written record that verifies nothing.
            if (!comfoair_config_hash_password(patch->ui_password, hash, new_record,
                                               sizeof new_record))
                return COMFOAIR_CONFIG_ERR_NO_HASH;
        }
    }

    // ---- from here on nothing can fail --------------------------------------------------------

    if (wifi == WIFI_SET) {
        // One act, both halves, and the whole record zeroed first so no tail of the previous key
        // survives behind the new length -- that tail would be written to flash.
        memset(&cfg->wifi, 0, sizeof cfg->wifi);
        memcpy(cfg->wifi.ssid, patch->wifi_ssid, ssid_len);
        memcpy(cfg->wifi.psk, patch->wifi_psk, psk_len);
        cfg->wifi.ssid_len = (uint8_t)ssid_len;
        cfg->wifi.psk_len  = (uint8_t)psk_len;
    } else if (wifi == WIFI_CLEAR) {
        memset(&cfg->wifi, 0, sizeof cfg->wifi);
    }

    if (patch->mqtt_host != NULL)
        copy_str(cfg->mqtt_host, sizeof cfg->mqtt_host, patch->mqtt_host);
    if (patch->has_mqtt_port)
        cfg->mqtt_port = (uint16_t)patch->mqtt_port;
    if (patch->mqtt_user != NULL)
        copy_str(cfg->mqtt_user, sizeof cfg->mqtt_user, patch->mqtt_user);
    if (patch->topic_prefix != NULL)
        copy_str(cfg->topic_prefix, sizeof cfg->topic_prefix, patch->topic_prefix);
    if (patch->has_ha_discovery)
        cfg->ha_discovery = patch->ha_discovery;
    if (patch->device_name != NULL)
        copy_str(cfg->device_name, sizeof cfg->device_name, patch->device_name);

    // Cleared with memset rather than a terminator: what is behind the terminator goes to flash
    // too, and a "cleared" password still readable in a flash dump is not cleared.
    if (broker == COMFOAIR_SECRET_STORE)
        copy_str(cfg->mqtt_password, sizeof cfg->mqtt_password, patch->mqtt_password);
    else if (broker == COMFOAIR_SECRET_CLEAR)
        memset(cfg->mqtt_password, 0, sizeof cfg->mqtt_password);

    if (ui == COMFOAIR_SECRET_STORE)
        copy_str(cfg->ui_pw_hash, sizeof cfg->ui_pw_hash, new_record);
    else if (ui == COMFOAIR_SECRET_CLEAR)
        // The password is optional and the soft reset erases it; an authenticated owner
        // removing it through the API is the same decision reached from the other side. Refusing
        // it would make a physical gesture the only way to undo a password on a device in a wall.
        memset(cfg->ui_pw_hash, 0, sizeof cfg->ui_pw_hash);

    return COMFOAIR_CONFIG_OK;
}
