// GET /api/system: who the unit is, who this device is, and how the links between them are doing.
//
// Its own file, like every route group of this component (README.md, Implementation). This file
// only GATHERS: every number comes from the component that owns it, and the document's shape and
// every null rule are comfoair_sysinfo's, host-tested in test_sysinfo. Nothing here touches the bus
// -- the unit's identity is what the probe task last read on its own schedule
// (comfoair_probe_task.h).
//
// No secret is in reach of this handler: nothing below reads a Wi-Fi password, a broker credential
// or the unit's service password, so none can leak into the document.
#include "comfoair_http_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "comfoair_can.h"
#include "comfoair_config.h"
#include "comfoair_control.h"
#include "comfoair_mqtt.h"
#include "comfoair_net.h"
#include "comfoair_probe_task.h"
#include "comfoair_sysinfo.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

// 17 KiB: test_sysinfo's worst case (all 256 error bits set, every string at full width and
// escaping to six bytes, 64 nodes) measured 16567 bytes, and that test fails the moment the
// document can outgrow 17 * 1024 -- so a 0 from the renderer here is a defect, not a big unit.
//
// STATIC, in .bss, and DO NOT move it to the stack (the httpd stack is 8192, see comfoair_http.c)
// or render into g_http_scratch (that one holds the state and entity documents, and sharing it
// would tie this document's size to the entity list's). NO LOCK, deliberately: esp_http_server
// serves every socket from ONE task (httpd_thread, httpd_main.c:330), so two handlers never run at
// once and this buffer has one user at a time -- the same guarantee g_http_scratch relies on. DO
// NOT call comfoair_http_system_get() from any other task without adding a mutex here.
#define SYSTEM_DOC_MAX (17 * 1024)
static char s_doc[SYSTEM_DOC_MAX];

// reset_reason's fixed words (docs/device/ota-and-system.md). A switch with a default rather than
// a table, so a reason a later IDF adds lands on "unknown" instead of indexing past the end.
static const char *reset_word(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_SW: return "software";
    // A CPU lock-up is a double exception: the same crash, one level deeper.
    case ESP_RST_PANIC:
    case ESP_RST_CPU_LOCKUP: return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog";
    // A detected glitch is the same advice to the owner as a brownout: look at the supply.
    case ESP_RST_BROWNOUT:
    case ESP_RST_PWR_GLITCH: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_EXT: return "external";
    case ESP_RST_USB: return "usb";
    case ESP_RST_JTAG: return "jtag";
    default: return "unknown";
    }
}

static const char *bus_word(const comfoair_can_stats_t *can)
{
    // `started` first: the driver reports a stopped node as bus-off too (comfoair_can.h), and
    // "off" would send the owner looking for a wiring fault on a bus that was never brought up.
    if (!can->started)
        return "stopped";
    switch (can->state) {
    case COMFOAIR_CAN_BUS_ACTIVE: return "active";
    case COMFOAIR_CAN_BUS_WARNING: return "warning";
    case COMFOAIR_CAN_BUS_PASSIVE: return "passive";
    case COMFOAIR_CAN_BUS_OFF:
    default: return "off";
    }
}

// Everything the device-and-links half needs to outlive the fill until the render. One instance,
// static for the same single-task reason as s_doc; it keeps ~200 bytes of strings off the stack.
typedef struct {
    char             built[48];  // esp_app_desc_t date[16] + " " + time[16]
    char             device_id[COMFOAIR_CONFIG_DEVICE_ID_LEN + 1];
    char             mac[18];
    char             ip[16];
    char             ssid[33];
    uint8_t          nodes[COMFOAIR_SYSINFO_MAX_NODES];
    comfoair_sysinfo_t info;
} system_scratch_t;
static system_scratch_t s_sys;

static void fill_device(system_scratch_t *s)
{
    comfoair_sysinfo_device_t *d   = &s->info.device;
    const esp_app_desc_t      *app = esp_app_get_description();
    d->firmware                    = app->version;
    d->idf                         = app->idf_ver;
    // Bounded by the field sizes (16 + 1 + 16 + NUL < 48); snprintf truncates rather than overrun.
    snprintf(s->built, sizeof s->built, "%.16s %.16s", app->date, app->time);
    d->built = s->built;
    d->board = board_get()->name;

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    d->chip          = CONFIG_IDF_TARGET;
    d->chip_revision = chip.revision;  // MXX already: major * 100 + minor (esp_chip_info.h)

    uint32_t flash = 0;
    d->flash_bytes = esp_flash_get_size(NULL, &flash) == ESP_OK ? flash : 0;
    const esp_partition_t *running = esp_ota_get_running_partition();
    d->partition                   = running != NULL ? running->label : "";
    d->uptime_ms                   = (uint64_t)esp_timer_get_time() / 1000u;
    d->reset_reason                = reset_word(esp_reset_reason());
    // MALLOC_CAP_8BIT: the pool every malloc() and every task stack in this firmware comes from.
    d->heap_free          = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
    d->heap_min_free      = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    d->heap_largest_block = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    comfoair_net_device_id(s->device_id);
    comfoair_net_mac_string(s->mac);
    d->device_id = s->device_id;
    d->mac       = s->mac;
}

static void fill_network(system_scratch_t *s)
{
    comfoair_sysinfo_network_t *n = &s->info.network;
    // comfoair_net's own word for "connected", never a second idea of it (comfoair_net.h).
    n->connected = comfoair_net_get_state() == COMFOAIR_NET_CONNECTED;
    comfoair_net_ip_string(s->ip);  // "" without an address, which the renderer prints as null
    n->ip = s->ip;

    comfoair_net_link_t link;
    if (n->connected && comfoair_net_link(&link)) {
        memcpy(s->ssid, link.ssid, sizeof s->ssid - 1);
        s->ssid[sizeof s->ssid - 1] = '\0';
        n->ssid                     = s->ssid;
        n->has_rssi                 = true;
        n->rssi                     = link.rssi;
        n->channel                  = link.channel;
    }
}

static void fill_links(system_scratch_t *s)
{
    comfoair_can_stats_t can;
    comfoair_can_get_stats(&can);
    comfoair_sysinfo_bus_t *b = &s->info.bus;
    b->started                = can.started;
    b->state                  = bus_word(&can);
    b->rx_frames              = can.rx_frames;
    b->rx_dropped             = can.rx_dropped;
    b->bus_errors             = can.bus_errors;
    b->bus_recoveries         = can.bus_recoveries;
    b->tx_error_count         = can.tx_error_count;
    b->rx_error_count         = can.rx_error_count;
    // The probe task's census, which needs no init (comfoair_probe_task.h): every node heard.
    _Static_assert(COMFOAIR_SYSINFO_MAX_NODES == 64, "the census fills exactly 64");
    b->nodes_seen       = s->nodes;
    b->nodes_seen_count = comfoair_probe_task_nodes(s->nodes);

    comfoair_control_stats_t cmd;
    comfoair_control_get_stats(&cmd);
    s->info.commands = (comfoair_sysinfo_commands_t){cmd.queued, cmd.sent, cmd.failed, cmd.dropped};

    // Counters and two flags only -- comfoair_mqtt_stats_t carries no host, user or password.
    comfoair_mqtt_stats_t mq;
    comfoair_mqtt_get_stats(&mq);
    s->info.mqtt = (comfoair_sysinfo_mqtt_t){
        .configured = mq.configured,
        .connected  = mq.connected,
        .published  = mq.published,
        .commands   = mq.commands,
        .rejected   = mq.rejected,
        .reconnects = mq.reconnects,
    };
}

esp_err_t comfoair_http_system_get(httpd_req_t *req)
{
    if (!comfoair_http_allowed(req))
        return ESP_OK;

    memset(&s_sys, 0, sizeof s_sys);
    // INVALID_STATE (no CAN bus, the probe task never started) leaves the unit zeroed, which IS
    // the document's "unknown": the rest of the page is still worth serving on such a device.
    (void)comfoair_probe_task_unit(&s_sys.info.unit);
    fill_device(&s_sys);
    fill_network(&s_sys);
    fill_links(&s_sys);

    const size_t used = comfoair_sysinfo_render(&s_sys.info, s_doc, sizeof s_doc);
    if (used == 0)
        return comfoair_http_send_error(req, "500 Internal Server Error", "system does not fit");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, s_doc, (ssize_t)used);
}
