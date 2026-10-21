// comfoair_net_internal.h -- PRIVATE to components/comfoair_net/. Not in include/, so nothing
// outside this directory can reach it; the public contract is include/comfoair_net.h.
//
// What the six sources share: the state, the event type, the two locks and the functions one file
// defines and another calls. The rule that governs the locks lives here with them now: ONE LOCK
// ROUND THE MACHINE, A DIFFERENT ONE ROUND THE RADIO. The radio lock may wrap the machine lock
// (comfoair_net_apply_mode() -> apply_sta_config() in _radio.c); the reverse never happens.
//
// NAMING: anything this header makes link-visible carries the component prefix (`g_net_*` for a
// variable, `comfoair_net_*` for a function). comfoair_can shares a callback and a context of its
// own (`g_can_cb`, `g_can_ctx`), and comfoair_mqtt exports a bare `g_task`, so a bare name here is
// a duplicate symbol or, worse, a silent bind to another component's object.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "comfoair_config.h"
#include "comfoair_net.h"
#include "comfoair_provision.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// --- events (posted in comfoair_net.c, consumed by the net task; the queue is made in _start.c) --

typedef enum {
    EV_STA_READY,
    EV_STA_STOPPED,
    EV_AP_STARTED,
    EV_AP_STOPPED,
    EV_ASSOCIATED,
    EV_GOT_IP,
    EV_DISCONNECTED,
    EV_CREDENTIALS_SAVED,
    EV_CLIENT_SEEN,
    // The button's short press, and the only event in this list a PERSON causes directly. It goes
    // through the queue like all the others so
    // that the machine keeps its single-writer rule: the button task must not touch g_net_prov, and
    // through here it does not have to.
    EV_SETUP_REQUESTED,
} ev_kind_t;

typedef struct {
    ev_kind_t kind;
    uint8_t   reason;  // EV_DISCONNECTED only
} ev_t;

// --- state: defined, and explained, in comfoair_net.c --------------------------------------------

extern comfoair_net_cb_t    g_net_cb;
extern void                *g_net_ctx;
extern comfoair_net_state_t g_net_state;
extern comfoair_config_t    g_net_config;
extern comfoair_prov_t      g_net_prov;
extern SemaphoreHandle_t    g_net_machine;
extern SemaphoreHandle_t    g_net_radio;
extern QueueHandle_t        g_net_events;
extern TaskHandle_t         g_net_task;
extern esp_netif_t         *g_net_ap_netif;
extern comfoair_prov_mode_t g_net_radio_mode;
extern bool                 g_net_wifi_started;
extern bool                 g_net_sta_ready;
extern bool                 g_net_associated;
extern bool                 g_net_have_address;
extern bool                 g_net_stored_window_closed;
extern bool                 g_net_stored_no_address;
extern char                 g_net_ap_ssid[33];
extern char                 g_net_ip[16];

// --- the two locks (recursive; created in comfoair_net_start()) ----------------------------------

static inline void lock_machine(void) { xSemaphoreTakeRecursive(g_net_machine, portMAX_DELAY); }
static inline void unlock_machine(void) { xSemaphoreGiveRecursive(g_net_machine); }
static inline void lock_radio(void) { xSemaphoreTakeRecursive(g_net_radio, portMAX_DELAY); }
static inline void unlock_radio(void) { xSemaphoreGiveRecursive(g_net_radio); }

// --- shared functions ----------------------------------------------------------------------------

// comfoair_net_radio.c. Logs a failed radio call and continues; any task.
void comfoair_net_radio_try(const char *what, esp_err_t err);
// comfoair_net_radio.c. Pure; any task.
wifi_mode_t comfoair_net_wifi_mode_of(comfoair_prov_mode_t mode);
// comfoair_net_radio.c. Net task.
void comfoair_net_captive_dns_follow_ap(bool up);
// comfoair_net_radio.c. Net task; takes the radio lock.
void comfoair_net_apply_mode(comfoair_prov_mode_t mode);
// comfoair_net_radio.c. Net task; writes NVS, so never with the machine lock held.
void comfoair_net_apply_action(comfoair_prov_action_t action);
// comfoair_net_radio.c. Net task; writes NVS on a transition only.
void comfoair_net_persist_flags(bool window_closed, bool no_address);

// comfoair_net.c. The net task's body, handed to xTaskCreate() by comfoair_net_start().
void comfoair_net_task(void *arg);
// comfoair_net.c. SYSTEM EVENT TASK (2304-byte stack): only enqueues. Registered by
// comfoair_net_start().
void comfoair_net_on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data);
