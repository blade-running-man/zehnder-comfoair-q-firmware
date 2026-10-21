// See app_mdns.h for why this lives beside app_main() rather than inside it.
#include "app_mdns.h"

#include <inttypes.h>

#include "comfoair_config.h"
#include "comfoair_mdns.h"
#include "comfoair_net.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_log.h"

static const char *TAG = "comfoair";

// CALLED AFTER comfoair_net_start() AND AFTER comfoair_http_start(), and both halves of that are
// load-bearing: comfoair_net_start() is
// what calls esp_netif_init() and esp_event_loop_create_default(), without which mdns_init() cannot
// come up at all, and the SRV record must not name a port nobody is listening on -- which is what
// `http_port == 0` says. The ordering is the caller's to keep; it is written down here because this
// is where a reader looking at the mDNS wiring will be.
//
// AND NOTHING CYCLES IT AFTERWARDS. espressif/mdns subscribes to WIFI_EVENT and IP_EVENT itself and
// raises and drops its per-interface responders as addresses come and go. Hanging it off
// on_net_state() would look tidier and would be wrong: that callback fires on STATE changes, and a
// lease that moves the device to a new address without a disconnect produces none
// (comfoair_net.c, set_state()) -- the record would go stale in silence.
void app_mdns_start(const board_t *board, const esp_app_desc_t *app, uint16_t http_port)
{
    // The facts are gathered HERE because comfoair_mdns may not include comfoair_net.h -- the
    // measured reason is in comfoair_mdns/CMakeLists.txt. Every string below is borrowed for the
    // length of the call; the component copies whatever it keeps.
    //
    // device_name comes from the REDACTED projection and NOT from comfoair_net_broker(), which
    // carries the same field beside the broker password in the clear. mDNS is multicast to
    // everybody on the link -- a guest VLAN, and everybody in radio range of the open setup access
    // point -- so that component is kept unable to hold a secret rather than trusted not to send
    // one.
    char id[COMFOAIR_CONFIG_DEVICE_ID_LEN + 1] = {0};
    char short_id[5]                           = {0};
    comfoair_net_device_id(id);
    comfoair_net_short_id(short_id);

    comfoair_config_public_t named;
    comfoair_net_config_snapshot(&named, NULL);

    comfoair_mdns_config_t mdns = {0};
    mdns.short_id               = short_id;
    // On a booted device this is default_name()'s "ComfoAir Q <last four of the STA MAC>" and never
    // empty (comfoair_config.c:308-310) -- so the browser shows `ComfoAir Q 8ae4` beside a host
    // name of `comfoair-8ae5`. Different on purpose: not every name can agree, the pair kept in
    // step is the one the owner TYPES, and the name they click on in Home Assistant is not in it.
    mdns.device_name     = named.device_name;
    mdns.http_port       = http_port;
    mdns.facts.board     = board != NULL ? board->name : NULL;
    mdns.facts.version   = app != NULL ? app->version : NULL;
    mdns.facts.device_id = id;

    // THE HEAP COST OF mDNS, MEASURED RATHER THAN ESTIMATED, and it is why these two lines are
    // here instead of in a hardware-verification step. The RAM cost of espressif/mdns was only ever
    // estimated ("not verified"); esp_get_free_heap_size() taken at one and the same point of
    // app_main, on both sides of this one call, answers the question in a single boot, with no
    // second build to compare against. What it covers is the responder's task stack, its action
    // queue and the service and interface structures -- everything mdns_init() and
    // mdns_service_add() allocate.
    const uint32_t heap_before = esp_get_free_heap_size();

    const esp_err_t err = comfoair_mdns_start(&mdns);
    ESP_LOGI(TAG, "mDNS heap cost: %" PRIu32 " -> %" PRIu32 " B free (%" PRId32 " B)", heap_before,
             esp_get_free_heap_size(), (int32_t)heap_before - (int32_t)esp_get_free_heap_size());
    if (err != ESP_OK)
        // Logged and continued, like every other start in app_main (AGENTS.md). What
        // is LOST is named rather than an error code: without mDNS the owner is back to reading the
        // address out of the router, which is the problem mDNS exists to remove -- not a device
        // that is down.
        ESP_LOGE(TAG, "mDNS did not start: %s -- the device is reachable by address only, and the "
                      "way back is the short button press",
                 esp_err_to_name(err));
}
