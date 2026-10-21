// One pass of the phase monitor, as arithmetic -- everything the monitor task does EXCEPT calling
// somebody else's API.
//
// WHY THIS FILE EXISTS AT ALL. comfoair_phase_monitor.c is wrapped in #ifdef ESP_PLATFORM from its
// first line to its last, so on the host it compiles to nothing and no case can reach it. That is
// not a gap to be apologised for, it is a gap to be made SMALL: everything left inside the guard
// has to be verified in front of a board. So the guard keeps only the five calls that need a
// device -- comfoair_net_status, comfoair_can_get_stats, comfoair_mqtt_get_stats,
// comfoair_led_set_phase, comfoair_poll_request_now -- plus esp_timer and vTaskDelay, and
// everything that DECIDES anything is here, where test/test_phase sweeps it.
//
// What "decides" covers, and each of the three has already been a defect in this tree or in the
// document that describes it:
//   1. which of comfoair_phase_inputs_t's seventeen fields each read fills. The comment on
//      comfoair_phase_inputs_t.bus_silent exists because ONE of those
//      seventeen was about to be filled from the wrong bit of the right verdict (mutation M9).
//   2. the two hold-offs -- absence enters a phase late, fact enters it at once.
//   3. whether this pass is a TRANSITION, which is the whole of "one log line per transition and
//      not one per tick".
//
// PURE, TOTAL, NO CLOCK AND NO GLOBALS, the same contract as comfoair_phase.c and
// comfoair_bus_health.c next door: `now_ms` is a NUMBER the caller read from esp_timer. DO NOT put
// an esp_timer call in here to save the caller a line -- the rule is that a hold-off is a
// measurement and a derivation is a decision, and this file is the seam between them precisely so
// that both stay callable twice with the same argument.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "comfoair_bus_health.h"
#include "comfoair_phase.h"

#ifdef __cplusplus
extern "C" {
#endif

// The seven start branches of app_main, comfoair_phase.h's own list,
// in the order the inputs struct names them. They arrive here as a BITMASK and not as seven bools
// because their producer is comfoair_phase_monitor_note_fault(), which is called from app_main
// before the monitor task exists and must therefore write somewhere that survives it.
//
// DO NOT collapse two of these into one value on the grounds that they are logged the same way.
// The pair mqtt_ready / mqtt_client_started is the one a reader will reach for -- the queue and the
// client -- and they are two on purpose: a device whose queue exists and
// whose client did not build would otherwise report NO_BROKER, "check your broker", where the true
// answer is "the firmware could not".
typedef enum {
    COMFOAIR_PHASE_FAULT_CAN = 0,      // src/main.cpp, comfoair_can_start()
    COMFOAIR_PHASE_FAULT_POLL,         // comfoair_poll_start()
    COMFOAIR_PHASE_FAULT_CONTROL,      // comfoair_control_start()
    COMFOAIR_PHASE_FAULT_NET,          // comfoair_net_start() -- the branch that returns
    COMFOAIR_PHASE_FAULT_MQTT_QUEUE,   // comfoair_mqtt_init()  -- the QUEUE
    COMFOAIR_PHASE_FAULT_MQTT_CLIENT,  // comfoair_mqtt_start() -- the CLIENT
    COMFOAIR_PHASE_FAULT_HTTP,         // comfoair_http_start()
    COMFOAIR_PHASE_FAULT_COUNT,
} comfoair_phase_fault_t;

#define COMFOAIR_PHASE_FAULT_BIT(which) (1u << (unsigned)(which))

// 30 s before a configured broker that is not connected becomes NO_BROKER.
//
// THREE FAILED ATTEMPTS AND NOT "ABOUT HALF A MINUTE": cfg.network.reconnect_timeout_ms is never
// set in this tree, so esp-mqtt's default of ~10 s applies, and a broker that flaps faster than
// this is a broker the device is still publishing through -- correctly read as ONLINE. The flap
// itself is not hidden by that: `reconnects` counts it in /api/status and has since phase 6.
//
// MEASURED FROM THE TICK THAT FIRST SAW THE BROKER DOWN, not from the last one that saw it up. At
// the monitor's 1 Hz the two readings differ by one tick, and this one is what keeps the pass a
// function of what it was told this time -- the same anchor comfoair_bus_health takes on the frame
// counter, and for the same reason.
//
// DO NOT give BROKER_REJECTED a hold-off to match. Its input is a CONNACK the broker actually
// sent, latched in the client and cleared only by a successful connection -- a fact, and a fact
// enters at once. Under the 300 s retry for rejected credentials `connected` is false
// for five minutes at a stretch, so a rejected device would spend those five minutes reported as
// NO_BROKER -- "check your broker" for a broker that answered.
#define COMFOAIR_PHASE_NO_BROKER_MS 30000u

// What the monitor read this tick, before anything was decided about it. One field per READ, not
// one per phase input: the mapping from these onto comfoair_phase_inputs_t is the thing under test.
typedef struct {
    // esp_timer_get_time() / 1000, which is uptime. Compared only as an unsigned difference, so
    // the pass is correct across the 32-bit wrap (~49.7 days) and at now_ms == 0.
    uint32_t now_ms;
    // comfoair_prov_init() has run. TRUE FROM THE MONITOR, ALWAYS, and that is not a shortcut:
    // comfoair_phase_monitor_start() is called from app_main strictly after comfoair_net_start()
    // has returned, and the machine is initialised inside it. The field is
    // here rather than hard-coded below so the sweep still covers both values -- BOOT is reachable
    // in this struct, and where it is actually SEEN is comfoair_led's initial value
    // (comfoair_phase.h:44-47).
    bool     machine_ready;
    // comfoair_phase_monitor_note_ota(), set by the POST /api/ota handler
    // (comfoair_http_ota.c, indicate()) from just before esp_ota_begin() until the upload fails --
    // or, on success, until the restart into the new image.
    bool     ota_in_progress;
    // COMFOAIR_PHASE_FAULT_BIT() of every branch that reported failure. Zero is a device where all
    // seven came up.
    uint32_t faults;

    // comfoair_net_status(): state, failure and ap_on_air, copied out of one snapshot so the three
    // describe one moment. ap_on_air is the machine's REPORT that an access point is on the air and
    // not an inference from the mode it asked for -- the distinction comfoair_phase_inputs_t makes.
    comfoair_prov_state_t   prov_state;
    comfoair_prov_failure_t prov_failure;
    bool                    ap_on_air;

    // comfoair_can_get_stats(): the driver NOW, and the frame counter it keeps. NOT a count of
    // applied PDOs -- the unit broadcasts PDOs this firmware does not claim (src/main.cpp:72-76),
    // so by "applied" a live bus would look dead.
    bool     can_started;
    uint32_t can_rx_frames;

    // comfoair_mqtt_get_stats(): `configured` is a stored host and not a built client, `connected`
    // is live, and `last_connack` is the LATCHED refusal code -- 0 when there was none.
    bool     broker_configured;
    bool     broker_connected;
    uint32_t last_connack;
} comfoair_phase_facts_t;

// Everything one pass remembers. ONE INSTANCE, owned by the monitor task and by nothing else,
// which is the whole of the concurrency argument and why there is not an atomic in it.
//
// ZERO-INITIALISED IS THE CORRECT STARTING STATE, and `phase` is the reason to say so out loud:
// COMFOAIR_PHASE_BOOT is 0, and a zeroed struct therefore claims the device is showing BOOT --
// which is exactly true, because that is the value comfoair_led holds from init until the first
// transition reaches it (comfoair_phase.h:44-47). The first pass of a healthy device is therefore
// a transition out of BOOT and gets its log line; a first pass that derives BOOT is not, and does
// not.
typedef struct {
    comfoair_bus_health_state_t bus;
    // The broker has been seen disconnected and `broker_down_ms` is the moment it was first seen
    // so. Cleared by a connection, so a broker that drops once an hour is granted the hold-off
    // once an hour rather than once ever.
    bool           broker_down;
    uint32_t       broker_down_ms;
    device_phase_t phase;
} comfoair_phase_collect_t;

// What the caller does with the pass. Every field is written on every pass.
typedef struct {
    // What was handed to comfoair_phase_derive(), kept so that a test can assert the monitor asked
    // the aggregate rather than answering for it, and so that a future /api/status can show the
    // reasoning rather than only the verdict.
    comfoair_phase_inputs_t inputs;
    device_phase_t          phase;
    // The phase the device was showing before this pass. Equal to `phase` when nothing moved.
    device_phase_t          previous;
    // `phase != previous`. The caller's whole side of it is one log line and one observer call --
    // ONE PER TRANSITION AND NOT ONE PER TICK, because at 1 Hz the second reading fills
    // GET /api/log's ring with 86 400 identical lines a day and buries the transition that matters.
    bool                    changed;
    // The bus verdict, for the <prefix>/bus topic and the status document. NOT the
    // same bit as inputs.bus_silent, and that is the point of publishing both from one pass.
    comfoair_bus_health_t   bus;
    // Send one sweep now: `if (out.probe) comfoair_poll_request_now();`. True on the pass that
    // ENTERS probing and on no other -- comfoair_bus_health.h says why in the loudest terms it has.
    bool                    probe;
} comfoair_phase_collect_out_t;

// One tick of the monitor. TOTAL: any input is answered, NULLs included -- a call with no facts
// leaves the state alone and reports BOOT, because a caller with nothing to say has not started
// the machine.
//
// TASK CONTEXT: the monitor task, and only it. Takes no locks and reads no clock.
void comfoair_phase_collect_pass(comfoair_phase_collect_t *state,
                                 const comfoair_phase_facts_t *facts,
                                 comfoair_phase_collect_out_t *out);

#ifdef __cplusplus
}
#endif
