// comfoair_net: the half of provisioning that owns the radio, the flash and the clock.
//
// Read comfoair_net.h first, and comfoair_provision.h after it. Nothing here decides anything --
// the machine next door answers "what should this device be doing", and this file does it and
// reports back what actually happened. The two facts "we asked for an access point" and "an
// access point is on the air" are deliberately different events, because the setup window's
// clock starts on the second one.
//
// THREE ARRANGEMENTS IN HERE ARE LOAD-BEARING AND WILL LOOK LIKE CEREMONY:
//
//  * THE EVENT HANDLER ONLY ENQUEUES. It runs on the system event task, whose stack is 2304 bytes
//    (CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE), and an NVS write or a JSON parse in there either
//    overflows it or blocks the queue behind it -- and the event that then arrives late, or not at
//    all, is IP_EVENT_STA_GOT_IP, which is the single event the whole design turns on.
//  * ONE LOCK ROUND THE MACHINE, A DIFFERENT ONE ROUND THE RADIO. A request handler asking
//    comfoair_net_is_provisioned() must not wait out an esp_wifi_scan_start(), and the task must
//    not hold the machine while it talks to the radio. The radio lock may wrap the machine lock
//    (comfoair_net_apply_mode() -> apply_sta_config()); the machine lock never wraps the radio one.
//  * NO ESP_ERROR_CHECK ON ANYTHING DOWNSTREAM OF STORED OR RECEIVED DATA. A configuration
//    survives a reboot, so a panic over one is a loop with no way out over the air, on a device
//    behind the front panel of a running ventilation unit. Radio calls are the
//    same shape of hazard for a different reason: nothing may reboot because a peer is absent
//    (AGENTS.md).
#include "comfoair_net.h"

#include <stdio.h>

#include "comfoair_config_nvs.h"
#include "comfoair_net_internal.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "net";

// How often the machine is asked to decide. Every deadline it holds is seconds or minutes long,
// so this is not a resolution requirement -- it is how long a fact can sit in the queue before it
// is acted on, and how long after a window expires the access point actually goes down.
#define TICK_MS 250

// --- state -------------------------------------------------------------------------------------

comfoair_net_cb_t    g_net_cb;
void                *g_net_ctx;
comfoair_net_state_t g_net_state = COMFOAIR_NET_DOWN;

// The whole stored document, loaded once at start.
//
// STATIC, not a local: comfoair_config_t is the better part of a kilobyte. DO NOT hand it to
// ESP_LOG* in any form -- it holds the broker password in plaintext (MQTT needs it) and the UI
// password record beside it, and the log ring is served to anyone who can reach the device.
// Only `wifi` is read in here, and only to fill the radio's own struct.
comfoair_config_t g_net_config;

comfoair_prov_t   g_net_prov;
SemaphoreHandle_t g_net_machine;  // guards g_net_prov and g_net_config.wifi
// Guards every esp_wifi call in this component, except comfoair_net_link().
SemaphoreHandle_t g_net_radio;
QueueHandle_t     g_net_events;
TaskHandle_t      g_net_task;

esp_netif_t *g_net_ap_netif;

// What the radio was last told, so a mode is applied on change rather than every quarter second.
comfoair_prov_mode_t g_net_radio_mode = COMFOAIR_PROV_MODE_OFF;
bool                 g_net_wifi_started;
// The station interface has reported STA_START. esp_wifi_connect() before that is
// ESP_ERR_WIFI_NOT_STARTED, so a CONNECT that arrives first is held here rather than dropped -- a
// dropped nudge is a station that never tries again, which on a device with no access point is
// exactly the stranding docs/architecture/failure-model.md forbids: no network, no way back.
bool g_net_sta_ready;
bool g_net_associated;
bool g_net_have_address;

// What was last written to flash, so the two durable facts cost one flash cycle per transition
// rather than one per tick.
bool g_net_stored_window_closed;
bool g_net_stored_no_address;

char g_net_ap_ssid[33];
char g_net_ip[16];

// --- events ------------------------------------------------------------------------------------

static uint32_t s_events_dropped;

static void post_event(ev_kind_t kind, uint8_t reason)
{
    if (g_net_events == NULL)
        return;
    const ev_t ev = {.kind = kind, .reason = reason};
    // NEVER BLOCKS. Waiting here would stall the system event task, and the event queued behind
    // the wait is the one this whole component exists to receive.
    if (xQueueSend(g_net_events, &ev, 0) != pdTRUE)
        s_events_dropped++;
}

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void set_state(comfoair_net_state_t state)
{
    if (g_net_state == state)
        return;
    g_net_state = state;
    if (g_net_cb != NULL)
        g_net_cb(state, g_net_ctx);
}

// --- the loop ------------------------------------------------------------------------------

static void feed(const ev_t *ev)
{
    const uint32_t t = now_ms();
    lock_machine();
    switch (ev->kind) {
    case EV_STA_READY:
        g_net_sta_ready = true;
        break;
    case EV_STA_STOPPED:
        g_net_sta_ready  = false;
        g_net_associated = false;
        break;
    case EV_AP_STARTED:
        comfoair_prov_on_ap_started(&g_net_prov, t);
        break;
    case EV_AP_STOPPED:
        comfoair_prov_on_ap_stopped(&g_net_prov, t);
        break;
    case EV_ASSOCIATED:
        g_net_associated = true;
        comfoair_prov_on_associated(&g_net_prov, t);
        break;
    case EV_GOT_IP:
        g_net_have_address = true;
        comfoair_prov_on_connected(&g_net_prov, t);
        break;
    case EV_DISCONNECTED:
        g_net_associated   = false;
        g_net_have_address = false;
        g_net_ip[0]        = '\0';
        comfoair_prov_on_disconnected(&g_net_prov, t, ev->reason);
        break;
    case EV_CREDENTIALS_SAVED:
        comfoair_prov_on_credentials_saved(&g_net_prov, t);
        break;
    case EV_CLIENT_SEEN:
        comfoair_prov_on_client_seen(&g_net_prov, t);
        break;
    case EV_SETUP_REQUESTED:
        comfoair_prov_on_setup_requested(&g_net_prov, t);
        break;
    }
    unlock_machine();
}

// One turn of the crank: tick the machine, take what it decided, do it.
//
// The machine is READ AND RELEASED before anything is acted on. Holding it across an
// esp_wifi_connect() or an NVS write would put a request handler asking
// comfoair_net_is_provisioned() behind the flash.
static void step(void)
{
    comfoair_prov_mode_t   mode;
    comfoair_prov_action_t action;
    bool                   window_closed;
    bool                   no_address;
    bool                   ap_on_air;
    bool                   window_open;

    lock_machine();
    comfoair_prov_tick(&g_net_prov, now_ms());
    mode          = comfoair_prov_mode(&g_net_prov);
    action        = comfoair_prov_take_action(&g_net_prov);
    window_closed = comfoair_prov_window_has_closed(&g_net_prov);
    no_address    = comfoair_prov_address_never_arrived(&g_net_prov);
    window_open   = comfoair_prov_window_is_open(&g_net_prov);
    unlock_machine();

    ap_on_air = (comfoair_net_wifi_mode_of(mode) & WIFI_MODE_AP) != 0;

    // ORDER: the action is armed before the mode is applied, so a CONNECT issued in the same turn
    // as the mode that makes the station exist is not held for another quarter second.
    comfoair_net_apply_action(action);
    comfoair_net_apply_mode(mode);
    comfoair_net_captive_dns_follow_ap(ap_on_air);
    comfoair_net_persist_flags(window_closed, no_address);

    // A window closing has to be OBSERVABLE, or a network that silently
    // vanished is indistinguishable from a device that broke.
    static bool s_was_open;
    if (s_was_open && !window_open)
        ESP_LOGW(TAG, "setup window closed");
    s_was_open = window_open;

    set_state(g_net_have_address        ? COMFOAIR_NET_CONNECTED
              : ap_on_air           ? COMFOAIR_NET_ACCESS_POINT
                                    : COMFOAIR_NET_DOWN);

    if (s_events_dropped != 0) {
        ESP_LOGE(TAG, "%u radio events were dropped; provisioning may be a tick behind",
                 (unsigned)s_events_dropped);
        s_events_dropped = 0;
    }
}

void comfoair_net_task(void *arg)
{
    (void)arg;
    for (;;) {
        ev_t ev;
        // Waking on a timeout as well as on an event: every deadline the machine holds is a
        // timeout, and a device whose radio has gone quiet is exactly the one with a window to
        // close and an access point to raise.
        while (xQueueReceive(g_net_events, &ev, pdMS_TO_TICKS(TICK_MS)) == pdTRUE)
            feed(&ev);
        step();
    }
}

// --- the event handler ---------------------------------------------------------------------

// RUNS ON THE SYSTEM EVENT TASK, 2304 bytes of stack. It queues a fact and returns. Everything
// else -- flash, the radio, the state machine -- is the provisioning task's, next door.
void comfoair_net_on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
        snprintf(g_net_ip, sizeof g_net_ip, IPSTR, IP2STR(&event->ip_info.ip));
        post_event(EV_GOT_IP, 0);
        return;
    }
    if (base != WIFI_EVENT)
        return;

    switch (id) {
    case WIFI_EVENT_STA_START:
        post_event(EV_STA_READY, 0);
        break;
    case WIFI_EVENT_STA_STOP:
        post_event(EV_STA_STOPPED, 0);
        break;
    case WIFI_EVENT_STA_CONNECTED:
        post_event(EV_ASSOCIATED, 0);
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        // THE REASON IS READ. The previous version of this function began with `(void)data;` and
        // threw it away, which is how "the router is rebooting" and "you changed the password"
        // became one message saying neither -- the failure classes of
        // docs/device/networking.md exist to keep them apart. A uint8_t on the wire
        // (esp_wifi_types_generic.h:1167), so nothing above 255 needs carrying.
        const wifi_event_sta_disconnected_t *e = (const wifi_event_sta_disconnected_t *)data;
        post_event(EV_DISCONNECTED, e != NULL ? (uint8_t)e->reason : 0);
        break;
    }
    case WIFI_EVENT_AP_START:
        post_event(EV_AP_STARTED, 0);
        break;
    case WIFI_EVENT_AP_STOP:
        post_event(EV_AP_STOPPED, 0);
        break;
    case WIFI_EVENT_AP_STACONNECTED:
        // Somebody joined the setup network. The window is extended for it: the window closing
        // while the owner is fetching their router password is the most annoying failure in the
        // design, and it is not an attack.
        post_event(EV_CLIENT_SEEN, 0);
        break;
    default:
        break;
    }
}

esp_err_t comfoair_net_provision(const comfoair_wifi_t *wifi)
{
    if (wifi == NULL)
        return ESP_ERR_INVALID_ARG;
    if (g_net_task == NULL)
        return ESP_ERR_INVALID_STATE;

    // Stored FIRST, and the machine told afterwards. The other order gives a machine in TRIAL
    // over a pair that is not in the flash: a power cut in between would leave the trial's
    // rollback timer running against the OLD credentials and roll back to them, which is the
    // right answer for the wrong reason -- and if the write then failed, the owner would be
    // watching a device try a network it was never given.
    const esp_err_t err = comfoair_config_nvs_save_wifi(wifi);
    if (err != ESP_OK)
        return err;

    lock_machine();
    g_net_config.wifi = *wifi;
    unlock_machine();

    // A fact on the queue, not a connection. The connection happens on the provisioning task,
    // after this function has returned and after the handler that called it has answered --
    // which is the invariant, not an optimisation.
    post_event(EV_CREDENTIALS_SAVED, 0);
    if (g_net_task != NULL)
        xTaskNotifyGive(g_net_task);
    return ESP_OK;
}

void comfoair_net_note_client(void) { post_event(EV_CLIENT_SEEN, 0); }

// The short press. Queued and returned from, exactly like the line above: the caller is the button
// task, which has just finished timing a gesture and must be back at its pin within 20 ms -- a gap
// wider than the debounce reads as a release to the decider (comfoair_button.h). Blocking here on
// the machine's lock would put that deadline behind whatever the radio is doing.
//
// A dropped event costs the owner a second press, and that is the right trade for a queue that
// never blocks: post_event() counts the drop and the next press is a fresh one. Nothing here can
// erase anything, so a lost one is not a lost reset.
void comfoair_net_open_setup_window(void) { post_event(EV_SETUP_REQUESTED, 0); }
