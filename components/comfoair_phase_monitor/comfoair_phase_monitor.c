// The 1 Hz task, and nothing else. The contract is in comfoair_phase_monitor.h; the arithmetic it
// performs is in comfoair_phase_collect.c, where the host can reach it.
//
// WRAPPED IN #ifdef ESP_PLATFORM ENTIRELY, a build fact rather than taste: this component is a
// PlatformIO library as well as an IDF component, so a host test that reaches it compiles every .c
// in this directory. The same shape, and the same sentence, as comfoair_led_driver.c and
// comfoair_config_nvs.c.
//
// READ THIS BEFORE ADDING A LINE. Anything below that decides something is a line no host case can
// see -- that is the whole argument of comfoair_phase_collect.h, and the reason this file is a
// hundred lines and not three hundred. A new input belongs in comfoair_phase_facts_t; a new rule
// belongs in the pass. What belongs here is a read and nothing more.
#ifdef ESP_PLATFORM

#include "comfoair_phase_monitor.h"

#include <stdatomic.h>
#include <string.h>

#include "comfoair_can.h"
#include "comfoair_led_driver.h"
#include "comfoair_mqtt.h"
#include "comfoair_net.h"
#include "comfoair_phase_collect.h"
#include "comfoair_poll.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "phase";

// 1 Hz: twice slower than the rate at which the indication can show a change
// (comfoair_led renders at 25 ms) and two orders of magnitude faster than the shortest hold-off
// this task applies (30 s).
#define TICK_MS 1000

// 3072, not the 2048 first planned. The task holds a comfoair_net_status_t on its stack -- 33 bytes
// of SSID, 33 of access-point name, 16 of address and the rest of comfoair_wire_provision_t -- plus
// three more snapshots and one ESP_LOG line, whose vsnprintf is the largest single consumer here.
// It is the same figure comfoair_poll, comfoair_control and the DNS server already use, and 1 KB of
// headroom on a task that runs for the life of the device is cheaper than one stack overflow in a
// technical room. DO NOT trim it back without a high-water mark measured on the board.
#define TASK_STACK 3072
// Below the CAN receive and bus tasks (10 and 9) and below the MQTT publisher (4): this task exists
// to WATCH those, and a watcher that preempts the work it is watching changes what it measures.
#define TASK_PRIO 3

static _Atomic uint32_t s_faults;
static _Atomic bool     s_ota;
// The last verdict, for GET /api/status. FAULT AND NOT BOOT AS THE INITIAL VALUE, and the
// reason is this: if the task never starts, "starting up" and "the thing that reports my
// state is dead" would otherwise be the same answer. comfoair_led still holds BOOT until the first
// transition reaches it, which is the other half of the same decision -- the LED is showing what
// the device is doing, this is what the monitor knows.
static _Atomic int s_phase = COMFOAIR_PHASE_FAULT;
static TaskHandle_t s_task;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// Every read this task performs, and every one of them is safe on a subsystem that never came up:
// comfoair_net_status() answers UNCONFIGURED before its task exists (comfoair_net_query.c:47-55),
// comfoair_can_get_stats() and comfoair_mqtt_get_stats() zero their output and read statics. That
// is the no-reboot rule (AGENTS.md) in the shape it takes here -- the monitor must not
// be the one thing that a missing peer can stop.
static void read_the_device(comfoair_phase_facts_t *f)
{
    memset(f, 0, sizeof *f);
    f->now_ms = now_ms();
    // TRUE UNCONDITIONALLY, and comfoair_phase_collect.h says why at length: this task is created
    // from app_main after comfoair_net_start() has returned, and comfoair_prov_init() runs inside
    // it. DO NOT replace this with a read of comfoair_net_status().state -- that call answers
    // UNCONFIGURED both for a machine that does not exist yet and for a device nobody has
    // configured, and the two are opposite facts.
    f->machine_ready   = true;
    f->ota_in_progress = atomic_load_explicit(&s_ota, memory_order_relaxed);
    f->faults          = atomic_load_explicit(&s_faults, memory_order_relaxed);

    // ONE SNAPSHOT AND NOT THREE READS, taken under the machine's own lock, so state, failure and
    // ap_on_air describe one moment: a state of CONNECTED beside a failure of wrong-password is a
    // phase nobody can act on. The cost is known and accepted -- this call
    // waits behind a Wi-Fi scan, which happens only while the owner has the settings page open,
    // that is, while they are looking at a screen rather than at the light.
    comfoair_net_status_t net;
    comfoair_net_status(&net);
    f->prov_state   = net.state;
    f->prov_failure = net.failure;
    f->ap_on_air    = net.ap_on_air;

    comfoair_can_stats_t can;
    comfoair_can_get_stats(&can);
    f->can_started   = can.started;
    f->can_rx_frames = can.rx_frames;

    comfoair_mqtt_stats_t mqtt;
    memset(&mqtt, 0, sizeof mqtt);
    comfoair_mqtt_get_stats(&mqtt);
    f->broker_configured = mqtt.configured;
    f->broker_connected  = mqtt.connected;
    f->last_connack      = mqtt.last_connack;
}

static void monitor_task(void *arg)
{
    (void)arg;
    // ONE INSTANCE, owned by this task and by nothing else -- which is the whole of the concurrency
    // argument for the collector and the bus resolver both, and why neither has an atomic in it.
    static comfoair_phase_collect_t s_collect;

    for (;;) {
        comfoair_phase_facts_t facts;
        read_the_device(&facts);

        comfoair_phase_collect_out_t out;
        comfoair_phase_collect_pass(&s_collect, &facts, &out);

        atomic_store_explicit(&s_phase, (int)out.phase, memory_order_relaxed);
        // On EVERY tick and not only on a change: the setter is a short critical section over one
        // word and its own header asks to be called this way. It is also the only thing that makes
        // the picture right after an indication that came up late or was reconfigured, where a
        // transition-only feed would leave the pixel on a phase the device left minutes ago.
        comfoair_led_set_phase(out.phase);

        // The bus verdict, on its way to Home Assistant. PROJECTED HERE, by the projection that
        // belongs to the verdict, so that what goes on <prefix>/bus and what colours the
        // BUS_SILENT phase are one fact and not two thresholds (comfoair_bus_health.h
        // spends a paragraph on what two of them look like to an owner).
        // comfoair_mqtt_note_bus() publishes nothing and takes no lock -- it stores one atomic and
        // wakes the publisher task, and only when the answer has changed -- so calling it on every
        // tick is a store per second and no message at all on a healthy bus. It is also safe with
        // no broker configured and with the client destroyed.
        comfoair_mqtt_note_bus(comfoair_bus_health_available(out.bus));

        // The bus resolver asked for a sweep, so here is the sweep. This one line is the entire
        // reason comfoair_bus_health_out_t.probe is a bool rather than a call: comfoair_poll.h
        // reaches FreeRTOS and an include of it inside the pure component would take test_phase and
        // test_bus_health down with it. Hardware scenario HV-CAN-12 exists because THIS line is
        // what no host case can see -- one entry into probing must produce exactly one sweep, and
        // a full sweep of RTRs every minute is what getting it wrong looks like on a 50 kbps
        // bus.
        if (out.probe)
            comfoair_poll_request_now();

        // ONE LINE PER TRANSITION AND NOT ONE PER TICK. At 1 Hz the other reading is 86 400 lines a
        // day, and GET /api/log is a bounded ring -- so it would not merely add noise, it would
        // delete the transition the owner opened the log to find.
        if (out.changed)
            ESP_LOGI(TAG, "%s -> %s (bus %s)", comfoair_phase_name(out.previous),
                     comfoair_phase_name(out.phase), comfoair_bus_health_name(out.bus));

        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

esp_err_t comfoair_phase_monitor_start(void)
{
    if (s_task != NULL)
        return ESP_ERR_INVALID_STATE;
    if (xTaskCreate(monitor_task, "comfoair_phase", TASK_STACK, NULL, TASK_PRIO, &s_task) != pdPASS) {
        s_task = NULL;
        // s_phase is left at FAULT, which is what it was initialised to. The device therefore says
        // "I cannot fix this" rather than "starting up" -- see s_phase above; the caller logs and
        // carries on, because nothing in this firmware reboots over a peer
        // (AGENTS.md).
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

device_phase_t comfoair_phase_monitor_get(void)
{
    return (device_phase_t)atomic_load_explicit(&s_phase, memory_order_relaxed);
}

void comfoair_phase_monitor_note_fault(comfoair_phase_fault_t which)
{
    if (which < 0 || which >= COMFOAIR_PHASE_FAULT_COUNT)
        return;
    atomic_fetch_or_explicit(&s_faults, COMFOAIR_PHASE_FAULT_BIT(which), memory_order_relaxed);
}

void comfoair_phase_monitor_note_ota(bool active)
{
    atomic_store_explicit(&s_ota, active, memory_order_relaxed);
}

#endif  // ESP_PLATFORM
