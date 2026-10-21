// comfoair_net: the read side -- the status document, the claim and credential answers, the
// link, RSSI, MAC and address. Read comfoair_net.h first; the three arrangements in
// comfoair_net.c's head govern this file. Shape: README.md, Implementation.
#include <stdio.h>
#include <string.h>

#include "comfoair_net_internal.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_wifi.h"

comfoair_net_state_t comfoair_net_get_state(void) { return g_net_state; }

bool comfoair_net_has_credentials(void)
{
    if (g_net_machine == NULL)
        return false;
    lock_machine();
    const bool has = comfoair_prov_has_credentials(&g_net_prov);
    unlock_machine();
    return has;
}

bool comfoair_net_is_provisioned(void)
{
    // Before the task exists there is no answer but "no". That is the safe end: an unclaimed
    // device refuses every write but the one that hands it a network.
    if (g_net_machine == NULL)
        return false;
    lock_machine();
    const bool provisioned = comfoair_prov_is_provisioned(&g_net_prov);
    unlock_machine();
    return provisioned;
}

// --- what the owner is told -----------------------------------------------------------------

void comfoair_net_status(comfoair_net_status_t *out)
{
    if (out == NULL)
        return;
    const comfoair_net_status_t empty = {0};
    *out                              = empty;

    snprintf(out->ap_ssid, sizeof out->ap_ssid, "%s", g_net_ap_ssid);

    if (g_net_machine == NULL) {
        // Asked before the task exists. comfoair_prov_state() would answer WINDOW_CLOSED here,
        // which is the right answer for the radio and the wrong one for a status page -- it
        // announces a transition that never happened (comfoair_provision.h, the DO NOT on
        // comfoair_prov_state). UNCONFIGURED is what "starting up" looks like from outside.
        out->state = COMFOAIR_PROV_UNCONFIGURED;
        out->mode  = COMFOAIR_PROV_MODE_OFF;
        return;
    }

    // ONE READ OF ONE CLOCK for all three durations in this document. The two countdowns are
    // measured against the 32-bit millisecond form the provisioning machine takes, and `uptime_ms`
    // is the same reading unnarrowed -- so a page cannot subtract one from the other and get two
    // different boot instants out of a document whose whole contract is that its fields agree.
    const int64_t  up = esp_timer_get_time() / 1000;
    const uint32_t t  = (uint32_t)up;
    out->uptime_ms    = (uint64_t)up;
    lock_machine();
    out->state               = comfoair_prov_state(&g_net_prov);
    out->mode                = comfoair_prov_mode(&g_net_prov);
    out->failure             = comfoair_prov_failure(&g_net_prov);
    out->failure_reason      = comfoair_prov_failure_reason(&g_net_prov);
    out->provisioned         = comfoair_prov_is_provisioned(&g_net_prov);
    out->has_credentials     = comfoair_prov_has_credentials(&g_net_prov);
    out->has_known_good      = comfoair_prov_has_known_good(&g_net_prov);
    out->window_open         = comfoair_prov_window_is_open(&g_net_prov);
    out->window_remaining_ms = comfoair_prov_window_remaining_ms(&g_net_prov, t);
    out->fallback_in_ms      = comfoair_prov_fallback_in_ms(&g_net_prov, t);
    // The SSID is in every beacon this network sends; it is not a secret, and it is the one fact
    // that makes "it will not connect" diagnosable. The key is not in comfoair_prov_t at all (its
    // static_assert says so) and is not copied here.
    const uint8_t n = g_net_config.wifi.ssid_len < sizeof out->ssid - 1 ? g_net_config.wifi.ssid_len
                                                                    : (uint8_t)(sizeof out->ssid - 1);
    memcpy(out->ssid, g_net_config.wifi.ssid, n);
    out->ssid[n] = '\0';
    unlock_machine();

    out->connected = g_net_have_address;
    out->ap_on_air = (comfoair_net_wifi_mode_of(out->mode) & WIFI_MODE_AP) != 0;
    snprintf(out->ip, sizeof out->ip, "%s", g_net_have_address ? g_net_ip : "");
    // `has_rssi`/`rssi` ARE NOT FILLED HERE, and that is a decision rather than an omission. This
    // function has a 1 Hz caller that does not read them -- comfoair_phase_monitor's tick -- and
    // comfoair_net_rssi() enters the Wi-Fi stack and takes ITS lock. That monitor's whole contract
    // is that no absent peer can stop it, and adding a second lock to its tick for a fact it never
    // looks at changes its blocking profile for nothing. The two HTTP handlers that render the
    // document call comfoair_net_rssi() themselves, on httpd's task, only when somebody asks --
    // the same treatment `phase` gets, and for a related reason.
}

// RSSI existed only inside a scan result (comfoair_net_scan) and therefore only while the owner
// had the settings page open on the network list; this is what puts it in GET /api/status, and it
// is the whole of what this function is for.
//
// A THIN WRAPPER OVER comfoair_net_link() AND DELIBERATELY NOT ITS OWN esp_wifi CALL. Both public
// readers answer "is the station associated, and with what" -- two calls to
// esp_wifi_sta_get_ap_info() would be two ideas of what a refusal means, which is exactly what the
// header comment on comfoair_net_rssi() forbids. The cost is ~36 bytes of ssid copied and dropped.
//
// FALSE FOR EVERY REFUSAL AND NOT A FABRICATED NUMBER: reporting 0 dBm for a radio that is on no
// network would say "perfect signal"; the document renders the false as null
// (comfoair_wire_render_status.c).
bool comfoair_net_rssi(int8_t *out)
{
    if (out == NULL)
        return false;
    comfoair_net_link_t link;
    if (!comfoair_net_link(&link))
        return false;
    *out = link.rssi;
    return true;
}

void comfoair_net_mac_string(char out[18])
{
    if (out == NULL)
        return;
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
}

void comfoair_net_ip_string(char out[16])
{
    if (out == NULL)
        return;
    snprintf(out, 16, "%s", g_net_have_address ? g_net_ip : "");
}

// THE ONLY CALLER OF esp_wifi_sta_get_ap_info() IN THE TREE; comfoair_net_rssi() reads through it.
// Here rather than in its callers because comfoair_net.h forbids esp_wifi calls outside this
// component -- the rule is about tasks driving the radio, and the cheapest way to keep it true is
// to have no exception to it.
//
// FALSE FOR EVERY REFUSAL, *out untouched. The call answers ESP_ERR_WIFI_NOT_INIT before
// esp_wifi_init(), ESP_ERR_WIFI_NOT_STARTED before esp_wifi_start(), and ESP_ERR_WIFI_NOT_CONNECT
// while the station is not associated -- which is exactly the state a device with a mistyped key
// spends its life in. A stale or invented reading is worse than none.
//
// TASK CONTEXT: any task. It takes the Wi-Fi API's own lock and does not block on the air --
// wifi_ap_record_t is filled from the station's stored record, not from a fresh probe -- so it is
// safe on httpd's. The record is ~80 bytes on the caller's stack and is not kept.
bool comfoair_net_link(comfoair_net_link_t *out)
{
    if (out == NULL)
        return false;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK)
        return false;
    memcpy(out->ssid, ap.ssid, sizeof out->ssid - 1);
    out->ssid[sizeof out->ssid - 1] = '\0';
    out->rssi                       = ap.rssi;
    out->channel                    = ap.primary;
    return true;
}
