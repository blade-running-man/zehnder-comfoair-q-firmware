// comfoair_net: the Wi-Fi scan, the one synchronous radio call a request handler makes. Read
// comfoair_net.h first; the three arrangements in comfoair_net.c's head govern this file.
// Shape: README.md, Implementation.
#include <string.h>

#include "comfoair_net_internal.h"
#include "esp_log.h"
#include "esp_wifi.h"

static const char *TAG = "net";

// --- the scan --------------------------------------------------------------------------------

esp_err_t comfoair_net_scan(comfoair_net_scan_entry_t *out, size_t cap, size_t *count)
{
    if (out == NULL || count == NULL)
        return ESP_ERR_INVALID_ARG;
    *count = 0;
    if (g_net_task == NULL || !g_net_wifi_started)
        return ESP_ERR_INVALID_STATE;

    lock_radio();

    // A sweep needs the station interface. On an unconfigured device the machine asks for an
    // access point alone, so the station is switched on for the duration and the machine's answer
    // is put back afterwards -- the radio lock is what makes that safe, and the next step() would
    // restore the mode anyway if this returned early.
    const bool  had_sta = (comfoair_net_wifi_mode_of(g_net_radio_mode) & WIFI_MODE_STA) != 0;
    wifi_mode_t restore = comfoair_net_wifi_mode_of(g_net_radio_mode);
    if (!had_sta) {
        const wifi_mode_t with_sta = restore == WIFI_MODE_AP ? WIFI_MODE_APSTA : WIFI_MODE_STA;
        comfoair_net_radio_try("scan mode", esp_wifi_set_mode(with_sta));
    }

    // Blocking. Thirteen channels, and on a device serving the setup page from its own access
    // point the radio leaves that channel while it sweeps -- the phone may lose the page
    // mid-request, which the settings page is written to expect.
    const wifi_scan_config_t cfg = {0};
    esp_err_t                err = esp_wifi_scan_start(&cfg, true);
    if (err == ESP_OK) {
        uint16_t found = COMFOAIR_NET_SCAN_MAX;
        static wifi_ap_record_t records[COMFOAIR_NET_SCAN_MAX];
        err = esp_wifi_scan_get_ap_records(&found, records);
        if (err == ESP_OK) {
            const size_t n = found < cap ? found : cap;
            for (size_t i = 0; i < n; i++) {
                // ssid is a 33-byte field in wifi_ap_record_t and IDF terminates it, but the
                // length is bounded here anyway: this string is about to be rendered into JSON
                // and the renderer trusts a terminator.
                memcpy(out[i].ssid, records[i].ssid, sizeof out[i].ssid - 1);
                out[i].ssid[sizeof out[i].ssid - 1] = '\0';
                out[i].rssi                         = records[i].rssi;
                // Anything that is not literally WIFI_AUTH_OPEN needs a key. A firmware that
                // guessed the other way would hide the password box for a secured network and
                // leave the owner unable to join their own.
                out[i].secure = records[i].authmode != WIFI_AUTH_OPEN;
            }
            *count = n;
        }
    }
    // Always freed, even when get_ap_records failed: the records stay in esp_wifi's heap
    // otherwise, and a device that scans on every load of the settings page would leak them.
    esp_wifi_clear_ap_list();

    if (!had_sta)
        comfoair_net_radio_try("scan mode restore", esp_wifi_set_mode(restore));

    unlock_radio();

    if (err != ESP_OK)
        ESP_LOGW(TAG, "scan: %s", esp_err_to_name(err));
    return err;
}
