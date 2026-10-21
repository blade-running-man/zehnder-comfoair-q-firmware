// comfoair_net: bringing the network up (the stored document, the machine, the netifs, the radio,
// the task) and the device's names. Read comfoair_net.h first; the three arrangements in
// comfoair_net.c's head govern this file. Shape: README.md, Implementation.
#include <stdio.h>

#include "comfoair_config_nvs.h"
#include "comfoair_net_internal.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "net";

// Facts, not work. Sixteen is far more than the radio produces in one tick; s_events_dropped in
// comfoair_net.c is what makes an overflow visible rather than a lost address.
#define EVENT_QUEUE_LEN 16

static esp_netif_t *s_sta_netif;

// THE FOUR CHARACTERS THAT NAME THIS DEVICE TO A HUMAN, and the only place they are made.
//
// ESP_MAC_WIFI_SOFTAP, NOT ESP_MAC_WIFI_STA, AND DO NOT "FIX" THAT. The two differ by exactly +1
// in the last octet -- CONFIG_ESP_MAC_ADDR_UNIVERSE_WIFI_AP=y and esp_hw_support/mac_addr.c:369-386
// take the base MAC and add one for the access point, while :365-367 hands the station the base
// address verbatim -- so the mistake produces a name of the right shape, one hex digit away from
// the right value, and nothing in the host suite can see it (test/test_mdns_name, header note 1).
// What it breaks is the one thing the shared name exists to protect: the access point in
// the phone's Wi-Fi list, the sticker on the case and the address bar all say the same string.
// The device on the bench serves `comfoair-8ae5` and its STA MAC ends 8a:e4 -- one digit.
//
// Sixteen bits, because that is enough for a person to tell two units in one house apart and NOT
// enough to be an identifier: identity stays on the full MAC (device_id() below), and a name
// that is printed on a case has to be transcribable.
static void short_id(char out[5])
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(out, 5, "%02x%02x", mac[4], mac[5]);
}

// The access point's name, and also the DHCP host name and the `.local` name.
//
// BYTE FOR BYTE WHAT IT ALWAYS WAS. This used to inline the esp_read_mac() above and the format
// string is unchanged around it -- "comfoair-" + the same two octets in the same order -- because
// the SSID is already on the air on every flashed device and, in the intended shape of this
// product, printed on a label. A device whose network name moved in a firmware update is a false
// sticker and a stale entry in the phone's saved networks.
static void build_ap_ssid(void)
{
    char id[5];
    short_id(id);
    snprintf(g_net_ap_ssid, sizeof g_net_ap_ssid, "comfoair-%s", id);
}

// Identity is the full MAC, never the device's name. It seeds the topic prefix and the
// broker client id, so renaming a device must not re-key anything -- otherwise Home Assistant
// ends up holding two devices, one of them dead and unremovable.
static void device_id(char out[COMFOAIR_CONFIG_DEVICE_ID_LEN + 1])
{
    uint8_t mac[6] = {0};
    // The base MAC, read out of efuse; it needs no radio and is available before esp_wifi_init().
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, COMFOAIR_CONFIG_DEVICE_ID_LEN + 1, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1],
             mac[2], mac[3], mac[4], mac[5]);
}

// Settings that vanish silently are indistinguishable from a firmware that reset itself, so
// what the flash could not answer for is named out loud.
//
// The field NAME, and nothing else. A name is not a value, and a value is what may never reach
// the log ring that /api/log serves to whoever can hear the access point.
static void report_repairs(comfoair_config_repairs_t repairs)
{
    if (repairs == 0)
        return;
    for (int i = 0; i < COMFOAIR_CONFIG_F_COUNT; i++) {
        if ((repairs & COMFOAIR_CONFIG_REPAIRED(i)) == 0)
            continue;
        const comfoair_config_field_info_t *f = comfoair_config_field((comfoair_config_field_t)i);
        ESP_LOGW(TAG, "stored setting '%s' was not usable and has been reset to its default",
                 f != NULL ? f->name : "?");
    }
}

// --- lifecycle -------------------------------------------------------------------------------

esp_err_t comfoair_net_start(const board_t *board, comfoair_net_cb_t cb, void *ctx)
{
    if (board == NULL)
        return ESP_ERR_INVALID_ARG;
    if (board->net_transport != BOARD_NET_WIFI) {
        // The Olimex board is Ethernet. Refusing loudly beats pretending: v1 ships one
        // board and this is where the second transport plugs in.
        ESP_LOGE(TAG, "board %s wants a transport this build does not have", board->name);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (g_net_task != NULL)
        return ESP_ERR_INVALID_STATE;

    g_net_cb  = cb;
    g_net_ctx = ctx;

    // Recursive, because comfoair_net_apply_action() takes the machine while step() may already
    // hold nothing and apply_sta_config() takes it from inside comfoair_net_apply_mode(). A plain
    // mutex would deadlock the moment those paths meet.
    g_net_machine = xSemaphoreCreateRecursiveMutex();
    g_net_radio   = xSemaphoreCreateRecursiveMutex();
    g_net_events  = xQueueCreate(EVENT_QUEUE_LEN, sizeof(ev_t));
    if (g_net_machine == NULL || g_net_radio == NULL || g_net_events == NULL) {
        ESP_LOGE(TAG, "out of memory bringing the network up");
        return ESP_ERR_NO_MEM;
    }

    // The whole stored document, migrated and repaired. NOT two bare NVS keys: the pair is one
    // blob under one key because NVS gives no atomicity between two of them, and a power cut
    // between an SSID write and a PSK write leaves a new SSID with an old password -- a typo
    // nobody made. comfoair_config_nvs_load() also moves what the previous
    // firmware wrote into "comfoair"/wifi_ssid+wifi_psk, so a device that has been on a network
    // for a year does not meet the setup page after an OTA.
    //
    // The return value is REPORTED, never checked into a panic: a store that will not answer
    // still leaves `g_net_config` full of defaults, and a device on its own access point with
    // default settings is a device the owner can reach.
    char id[COMFOAIR_CONFIG_DEVICE_ID_LEN + 1];
    device_id(id);
    comfoair_config_repairs_t repairs = 0;
    const esp_err_t           load    = comfoair_config_nvs_load(&g_net_config, id, &repairs);
    if (load != ESP_OK)
        ESP_LOGW(TAG, "settings did not read (%s); running on defaults", esp_err_to_name(load));
    report_repairs(repairs);

    // The four facts the machine wants from the last run of this device.
    //
    // has_credentials is comfoair_config_nvs_has_credentials(), which is comfoair_config_wifi_usable()
    // over the same blob comfoair_config_sanitize() cleaned -- ONE predicate, because two of them
    // is a device told it has a network and handed an empty one: no network, no access point, no
    // way back, permanently (comfoair_config_nvs.h:79-91).
    comfoair_prov_boot_t boot = {
        .has_credentials = comfoair_config_wifi_usable(&g_net_config.wifi),
        .has_known_good  = comfoair_config_nvs_has_known_good(),
    };
    comfoair_config_nvs_load_prov_flags(&boot.window_closed_before, &boot.address_never_arrived);
    g_net_stored_window_closed = boot.window_closed_before;
    g_net_stored_no_address    = boot.address_never_arrived;

    // Defaults: a fifteen-minute setup window, a thirty-minute fallback, and a ninety-second
    // ap_timeout for the trial. All of them are comfoair_prov_config_defaults()'s, and the place
    // to change one is there, next to the reasoning for the number.
    comfoair_prov_init(&g_net_prov, NULL, &boot);

    build_ap_ssid();

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "netif init: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop: %s", esp_err_to_name(err));
        return err;
    }

    // BOTH netifs, once, before the radio starts. esp_netif_create_default_wifi_*() may be called
    // only once each, and the mode changes underneath them all through a device's life -- a
    // station that is provisioned, an access point that comes back by itself, both at once for the
    // trial. Creating them on demand would mean creating one twice.
    s_sta_netif = esp_netif_create_default_wifi_sta();
    g_net_ap_netif  = esp_netif_create_default_wifi_ap();

    // THE NAME THE ROUTER SHOWS, and it is the same string as the SSID and the `.local` name --
    // g_net_ap_ssid itself, not a second buffer built the same way, because two producers of one
    // name is how they come to differ.
    //
    // Without this line the device is `espressif` in the router's client list --
    // CONFIG_LWIP_LOCAL_HOSTNAME (sdkconfig.m5stack-nanoc6:2763) -- indistinguishable from every
    // other ESP in the house, and that list is the LAST fallback when mDNS does not work (Android
    // browsers, isolated guest networks). DHCP option 12 already goes out: LWIP_NETIF_HOSTNAME is
    // hard-wired to 1 in the IDF's lwIP port (lwip/port/include/lwipopts.h:714), not taken from
    // sdkconfig, so this changes what is sent and not whether anything is.
    //
    // HERE AND NOT LATER: the name has to be set before the DHCP client sends its DISCOVER, and
    // the radio is started further down this function. Legal this early, read rather than assumed:
    // esp_netif_set_hostname() only needs `lwip_netif`, which esp_netif_new() calloc's inside
    // esp_netif_create_default_wifi_sta() above (esp_netif_lwip.c:820-830) -- the branch that
    // answers ESP_ERR_ESP_NETIF_IF_NOT_READY (:1779) cannot be reached from here.
    //
    // STA only -- option 12 is what a DHCP CLIENT sends, and the access point is a server.
    //
    // Logged and continued (AGENTS.md): an unnamed device still connects.
    const esp_err_t host_err = esp_netif_set_hostname(s_sta_netif, g_net_ap_ssid);
    if (host_err != ESP_OK)
        ESP_LOGW(TAG, "could not set the DHCP host name (%s); the router will show the default",
                 esp_err_to_name(host_err));

    // THE TWO FACTS THIS COMPONENT OWNS, printed together so a bench check costs one glance at a
    // USB log instead of a phone, a laptop and a browser. Hardware scenario HV-PROV-02 does the
    // arithmetic: `id` must end ONE LOWER than the SSID, because the SoftAP MAC is the base MAC
    // plus one in the last octet (esp_hw_support/mac_addr.c:369-386). On the bench unit,
    // `comfoair-8ae5` against an id ending 8ae4. That is the ONLY guard on the swap of
    // ESP_MAC_WIFI_SOFTAP for ESP_MAC_WIFI_STA, which no host test can see -- test/test_mdns_name
    // is handed those four characters as an argument.
    //
    // THE .local NAME IS DELIBERATELY NOT PRINTED HERE. comfoair_mdns logs it from its own buffer
    // ("findable at http://...") and HV-PROV-02 compares the two lines, so the comparison has
    // two independent producers behind it. Printing both from g_net_ap_ssid -- which the first
    // version of this line did -- compares a string with itself and would pass while the format
    // string in build_ap_ssid() drifted away from comfoair_mdns_name.c's PREFIX, breaking the
    // label/address identity with every test still green.
    //
    // Neither value is a secret: the SSID is in every beacon this radio sends and the MAC is in
    // every frame, so this is safe in a ring that GET /api/log serves to whoever can reach it.
    ESP_LOGI(TAG, "names: access point '%s', id %s", g_net_ap_ssid, id);

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err                     = esp_wifi_init(&init);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi init: %s", esp_err_to_name(err));
        return err;
    }
    // Credentials live in NVS under this firmware's own keys and nowhere else. Letting esp_wifi
    // keep its own copy would be a second store with its own idea of what is configured -- and
    // the two disagreeing is the state the invariants forbid.
    comfoair_net_radio_try("storage", esp_wifi_set_storage(WIFI_STORAGE_RAM));

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, comfoair_net_on_wifi_event, NULL,
                                              NULL);
    if (err == ESP_OK)
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, comfoair_net_on_wifi_event,
                                                  NULL, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "event handlers: %s", esp_err_to_name(err));
        return err;
    }

    // 4096: the task writes NVS (nvs_set_blob down through the flash driver) and formats log
    // lines. It is not the system event task and does not have its 2304 bytes -- which is the
    // whole reason the event handler only enqueues.
    if (xTaskCreate(comfoair_net_task, "comfoair_net", 4096, NULL, 5, &g_net_task) != pdPASS) {
        ESP_LOGE(TAG, "provisioning task did not start");
        g_net_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "provisioning: %s, access point '%s'",
             boot.has_credentials ? "a network is stored" : "nothing stored", g_net_ap_ssid);
    return ESP_OK;
}

void comfoair_net_device_id(char out[COMFOAIR_CONFIG_DEVICE_ID_LEN + 1])
{
    if (out != NULL)
        device_id(out);
}

void comfoair_net_short_id(char out[5])
{
    if (out != NULL)
        short_id(out);
}
