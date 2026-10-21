// comfoair_net: the radio -- the machine's mode and actions turned into esp_wifi calls and NVS
// writes, and the captive DNS responder's lifetime. Read comfoair_net.h first; the three
// arrangements in comfoair_net.c's head govern this file. Shape: README.md, Implementation.
#include <string.h>

#include "comfoair_captive_dns_server.h"
#include "comfoair_config_nvs.h"
#include "comfoair_net_internal.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static const char *TAG = "net";

static bool s_connect_wanted;
static bool s_dns_running;

// The pair, out of the document and into the radio's own struct.
//
// memcpy with a measured length, NOT strncpy(..., sizeof - 1). Both fields are fixed-size byte
// arrays that esp_wifi does not require to be NUL-terminated (esp_wifi_types_generic.h:559-560),
// so reserving a byte for a terminator silently truncates a legal 32-character SSID to 31 and a
// 64-character hex PSK to 63 -- and the device then fails to associate for ever, with a reason
// nobody can deduce from the outside. comfoair_wifi_t carries the two lengths for this reason and
// this one alone.
static void wifi_config_from(const comfoair_wifi_t *wifi, wifi_config_t *out)
{
    const wifi_config_t empty = {0};
    *out = empty;
    memcpy(out->sta.ssid, wifi->ssid, wifi->ssid_len);
    memcpy(out->sta.password, wifi->psk, wifi->psk_len);
    // An open network is a legal answer and comfoair_config_check_psk() says so. The threshold
    // has to come down with it, or esp_wifi refuses to associate with a network it can see.
    out->sta.threshold.authmode = wifi->psk_len == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
}

// --- the radio -----------------------------------------------------------------------------

// Every call in here is logged and returned on, never ESP_ERROR_CHECK'd. A radio that will not
// come up must leave a device that still reads the ventilation unit and still says why over USB;
// aborting turns that into a reboot loop, which is the one failure nobody can reach the device to
// fix (AGENTS.md).
void comfoair_net_radio_try(const char *what, esp_err_t err)
{
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT)
        ESP_LOGW(TAG, "%s: %s", what, esp_err_to_name(err));
}

static void apply_ap_config(void)
{
    wifi_config_t ap = {0};
    const size_t  n  = strlen(g_net_ap_ssid);
    memcpy(ap.ap.ssid, g_net_ap_ssid, n);
    ap.ap.ssid_len       = (uint8_t)n;
    ap.ap.max_connection = 4;
    // OPEN, and it is a decision rather than an oversight: a WPA2 key nobody has been told
    // is a brick, and the obvious workaround -- generate one and publish it through a sensor --
    // puts the key exactly where it cannot be read, because reading a sensor needs the network
    // the key is for. What bounds the exposure is the access policy and the setup window's
    // clock, not the radio.
    ap.ap.authmode       = WIFI_AUTH_OPEN;
    comfoair_net_radio_try("ap config", esp_wifi_set_config(WIFI_IF_AP, &ap));
}

static void apply_sta_config(void)
{
    wifi_config_t sta;
    lock_machine();
    wifi_config_from(&g_net_config.wifi, &sta);
    unlock_machine();
    comfoair_net_radio_try("sta config", esp_wifi_set_config(WIFI_IF_STA, &sta));
}

wifi_mode_t comfoair_net_wifi_mode_of(comfoair_prov_mode_t mode)
{
    switch (mode) {
    case COMFOAIR_PROV_MODE_ACCESS_POINT: return WIFI_MODE_AP;
    case COMFOAIR_PROV_MODE_STATION:      return WIFI_MODE_STA;
    case COMFOAIR_PROV_MODE_AP_STA:       return WIFI_MODE_APSTA;
    case COMFOAIR_PROV_MODE_OFF:          break;
    }
    return WIFI_MODE_NULL;
}

// The DNS responder lives and dies with the access point, and with nothing else. An answer for
// every name in existence is correct only on a network that has nothing else in it -- the socket
// binds to the access point's own address for the same reason (comfoair_captive_dns_server.h).
void comfoair_net_captive_dns_follow_ap(bool up)
{
    if (up == s_dns_running)
        return;
    if (!up) {
        comfoair_captive_dns_stop();
        s_dns_running = false;
        return;
    }
    esp_netif_ip_info_t info = {0};
    if (g_net_ap_netif == NULL || esp_netif_get_ip_info(g_net_ap_netif, &info) != ESP_OK) {
        ESP_LOGW(TAG, "no address on the access point; names will not resolve on it");
        return;
    }
    uint8_t octets[4];
    // lwip already stores ip4_addr_t.addr in network order, so this is the whole bridge.
    memcpy(octets, &info.ip.addr, sizeof octets);
    const esp_err_t err = comfoair_captive_dns_start(octets);
    if (err != ESP_OK) {
        // Not fatal and deliberately not retried into a loop: a captive portal that will not
        // start is a device the owner reaches by typing an address, and a device that reboots is
        // a device the owner does not reach at all.
        ESP_LOGW(TAG, "captive DNS did not start (%s); the setup page is still at " IPSTR,
                 esp_err_to_name(err), IP2STR(&info.ip));
        return;
    }
    s_dns_running = true;
    ESP_LOGI(TAG, "captive portal up: every name resolves to " IPSTR, IP2STR(&info.ip));
}

void comfoair_net_apply_mode(comfoair_prov_mode_t mode)
{
    lock_radio();

    if (mode != g_net_radio_mode) {
        const wifi_mode_t want = comfoair_net_wifi_mode_of(mode);
        comfoair_net_radio_try("set mode", esp_wifi_set_mode(want));
        if (want & WIFI_MODE_AP)
            apply_ap_config();
        if (want & WIFI_MODE_STA)
            apply_sta_config();
        g_net_radio_mode = mode;

        if (want == WIFI_MODE_NULL) {
            // The window-closed state on a device with nothing to connect to. The radio has nothing
            // left to do, and the way back is the short window the next boot opens -- which is
            // why this is survivable at all.
            if (g_net_wifi_started) {
                comfoair_net_radio_try("stop", esp_wifi_stop());
                g_net_wifi_started = false;
                g_net_sta_ready    = false;
            }
            ESP_LOGW(TAG, "setup window closed; the access point comes back at the next boot");
        } else if (!g_net_wifi_started) {
            comfoair_net_radio_try("start", esp_wifi_start());
            g_net_wifi_started = true;
        }
    }

    // Held until the station interface says it is up, then issued once.
    if (s_connect_wanted && g_net_sta_ready && (comfoair_net_wifi_mode_of(g_net_radio_mode) & WIFI_MODE_STA)) {
        // The machine's contract for COMFOAIR_PROV_ACTION_CONNECT: disconnect first if the
        // station is associated. That comes straight back as an event with reason 8, and
        // comfoair_prov_classify() knows it is ours -- which is why it is not a verdict on
        // anything and does not start the fallback clock.
        if (g_net_associated)
            comfoair_net_radio_try("disconnect", esp_wifi_disconnect());
        apply_sta_config();
        comfoair_net_radio_try("connect", esp_wifi_connect());
        s_connect_wanted = false;
    }

    unlock_radio();
}

// --- actions -------------------------------------------------------------------------------

void comfoair_net_apply_action(comfoair_prov_action_t action)
{
    switch (action) {
    case COMFOAIR_PROV_ACTION_NONE:
        return;

    case COMFOAIR_PROV_ACTION_CONNECT:
        s_connect_wanted = true;
        return;

    case COMFOAIR_PROV_ACTION_COMMIT_CREDENTIALS: {
        // The pair in NVS just produced an address. One write, on the transition only -- a
        // flapping router must not spend a flash cycle every few minutes rewriting two strings.
        const esp_err_t err = comfoair_config_nvs_commit_known_good();
        if (err != ESP_OK)
            ESP_LOGW(TAG, "could not record the network as known-good: %s", esp_err_to_name(err));
        return;
    }

    case COMFOAIR_PROV_ACTION_RESTORE_CREDENTIALS: {
        // The likeliest catastrophe in this project is not an attack, it is a typo: the owner
        // changes networks, mistypes one character, the old pair is already overwritten, no
        // access point comes up because credentials exist -- and the device disappears
        // for good. This is the way back.
        comfoair_wifi_t restored = {0};
        const esp_err_t err      = comfoair_config_nvs_restore_known_good(&restored);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "could not put the last working network back: %s", esp_err_to_name(err));
            return;
        }
        lock_machine();
        g_net_config.wifi = restored;
        unlock_machine();
        ESP_LOGW(TAG, "the network just saved did not work; '%.*s' put back",
                 (int)restored.ssid_len, (const char *)restored.ssid);
        s_connect_wanted = true;
        return;
    }
    }
}

// The two facts comfoair_prov_boot_t wants back at the next boot. Written on transition only.
void comfoair_net_persist_flags(bool window_closed, bool no_address)
{
    if (window_closed == g_net_stored_window_closed && no_address == g_net_stored_no_address)
        return;
    const esp_err_t err = comfoair_config_nvs_save_prov_flags(window_closed, no_address);
    if (err != ESP_OK) {
        // Survivable and worth seeing: the cost is a device that offers the first-run window
        // again at the next boot, or one that does not offer the no-address escape. Neither is
        // worth a panic on a device nobody can reach.
        ESP_LOGW(TAG, "could not record the provisioning flags: %s", esp_err_to_name(err));
        return;
    }
    g_net_stored_window_closed = window_closed;
    g_net_stored_no_address    = no_address;
}
