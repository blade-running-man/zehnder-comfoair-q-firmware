// comfoair_net: the stored configuration document -- snapshot, apply and persist, the UI
// password check, the broker settings, the provisioning body. Read comfoair_net.h first; the
// three arrangements in comfoair_net.c's head govern this file. Shape: README.md, Implementation.
#include <stdio.h>

#include "comfoair_config_nvs.h"
#include "comfoair_net_internal.h"
#include "esp_log.h"

static const char *TAG = "net";

// --- the configuration document ------------------------------------------------------------

void comfoair_net_config_snapshot(comfoair_config_public_t *out, bool *known_good)
{
    if (out == NULL)
        return;
    lock_machine();
    comfoair_config_project(&g_net_config, out);
    // Under the SAME lock as the document. It comes from the machine and not from the store --
    // "a stored pair has actually produced an address" is not a thing the flash knows -- and the
    // two have to describe one moment or the page shows a network whose rollback promise belongs
    // to a different one.
    if (known_good != NULL)
        *known_good = comfoair_prov_has_known_good(&g_net_prov);
    unlock_machine();
}

comfoair_config_err_t comfoair_net_config_apply(const comfoair_config_patch_t *patch)
{
    if (patch == NULL)
        return COMFOAIR_CONFIG_ERR_SSID;

    // The salt lives HERE and not inside the context, because the context only borrows it and
    // has to outlive the apply() call. Fresh per stored password -- never per device, never per
    // build -- or one recovered flash image answers for every device whose owner chose the same
    // word (comfoair_config_nvs.h:138-141).
    uint8_t                    salt[COMFOAIR_CONFIG_SALT_LEN];
    comfoair_config_hash_ctx_t ctx;

    lock_machine();
    comfoair_config_device_hash_ctx(&ctx, salt);

    const comfoair_config_err_t err = comfoair_config_apply(&g_net_config, patch, &ctx);
    if (err != COMFOAIR_CONFIG_OK) {
        // Nothing was written -- comfoair_config_apply is all-or-nothing -- so there is nothing
        // to roll back and the owner retries against the document they were looking at.
        unlock_machine();
        return err;
    }

    // Persisting can fail on a full or damaged NVS. The in-memory document is already the new
    // one, and that is the honest outcome to report: the change took effect and will not survive
    // a reboot. Saying "rejected" would be a lie the owner could not act on.
    const esp_err_t saved = comfoair_config_nvs_save(&g_net_config);
    unlock_machine();

    if (saved != ESP_OK)
        ESP_LOGE(TAG, "settings applied but not persisted: %s", esp_err_to_name(saved));
    return COMFOAIR_CONFIG_OK;
}

bool comfoair_net_check_password(const char *candidate)
{
    if (candidate == NULL)
        return false;
    lock_machine();
    const bool ok = comfoair_config_check_ui_password_against(
        g_net_config.ui_pw_hash, candidate, comfoair_config_device_kdf());
    unlock_machine();
    return ok;
}

void comfoair_net_broker(comfoair_net_broker_t *out)
{
    if (out == NULL)
        return;
    const comfoair_net_broker_t empty = {0};
    *out                              = empty;
    if (g_net_machine == NULL)
        return;
    lock_machine();
    // Copied, not projected: the projection is what redacts, and MQTT needs the plaintext to
    // authenticate. See the header -- this struct must never reach a log line.
    snprintf(out->host, sizeof out->host, "%s", g_net_config.mqtt_host);
    out->port = g_net_config.mqtt_port;
    snprintf(out->user, sizeof out->user, "%s", g_net_config.mqtt_user);
    snprintf(out->password, sizeof out->password, "%s", g_net_config.mqtt_password);
    snprintf(out->topic_prefix, sizeof out->topic_prefix, "%s", g_net_config.topic_prefix);
    snprintf(out->device_name, sizeof out->device_name, "%s", g_net_config.device_name);
    out->ha_discovery = g_net_config.ha_discovery;
    unlock_machine();
}

bool comfoair_net_password_set(void)
{
    lock_machine();
    const bool set = comfoair_config_password_set(&g_net_config);
    unlock_machine();
    return set;
}

esp_err_t comfoair_net_provision_body(const char *body, comfoair_wire_result_t *result)
{
    comfoair_wifi_t wifi;

    // Decoded UNDER THE LOCK, because the third state of `wifi_psk` is "keep the key already
    // stored" and resolving it means reading that key. comfoair_wire_parse_provision() is where
    // the three states are decided -- through comfoair_secret_decide(), the same function the
    // settings document uses, so there is one place that knows what the sentinel means and one
    // set of host tests over it.
    lock_machine();
    const comfoair_wire_result_t decoded = comfoair_wire_parse_provision(body, &g_net_config.wifi, &wifi);
    unlock_machine();

    if (result != NULL)
        *result = decoded;
    if (decoded.status != COMFOAIR_WIRE_OK)
        return ESP_ERR_INVALID_ARG;

    return comfoair_net_provision(&wifi);
}
