// Is the ventilation unit answering on CAN. The owner chose to say "network alive, unit silent"
// with a second availability topic, <prefix>/bus (docs/interfaces/mqtt.md); this is its verdict.
//
// The problem, in the owner's words. He cuts the breaker to change the filters. Wi-Fi, the broker
// and the ESP32 keep working, so Home Assistant shows the device online, the supply temperature at
// 21.5 C and fan speed 2 -- the values from the moment the power went. An hour later the graph is
// still a flat line labelled "current reading". Nothing in this firmware distinguishes "the unit
// says 21.5" from "the unit said 21.5 yesterday", because a silent bus changes no value: the
// model keeps the last reading, and only a frame can turn it into `None`.
//
// ONE VERDICT, TWO READERS, AND THAT IS THE WHOLE POINT OF THE FILE. The <prefix>/bus availability
// topic and the BUS_SILENT phase (comfoair_phase.h:148-151) are the same fact seen twice:
// two thresholds answering one question drift apart sooner or later, and
// what an owner would then get is one colour on the device beside a different card in Home
// Assistant. comfoair_bus_health_available() and comfoair_bus_health_is_silent() are projections
// of the state below and compute nothing of their own.
//
// PURE, TOTAL, NO CLOCK AND NO GLOBALS. Not one ESP-IDF header, which is why the whole machine is
// swept in test/test_bus_health instead of being watched in front of a unit with its breaker off.
// `now_ms` and `rx_frames` are NUMBERS the caller reads from esp_timer and comfoair_can_get_stats.
//
// DO NOT call comfoair_poll_request_now() from in here, however direct that reads. comfoair_poll.h
// reaches FreeRTOS, and PlatformIO's dependency finder works on the component directory, so one
// include would drag the poll task, the TWAI driver and esp-mqtt into the host binary and end the
// suite -- the same argument this component's CMakeLists.txt already makes for comfoair_phase.c.
// The provocation therefore leaves as a bool, exactly as comfoair_mqtt_ladder_out_t.reconnect
// leaves the backoff ladder for the publisher task to act on.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ONE NUMBER, AND THE OTHER TWO ARE MADE OF IT. One question is still open: the period with
// which the unit broadcasts PDOs on its own is not documented anywhere in this tree
// (comfoair_state.h:96 says "rebroadcasts constantly" and gives no figure) and has never been
// measured. So the thresholds are argued from BELOW -- a start sweep costs one frame per polled
// PDO x 100 ms, seconds (comfoair_poll.h:26-27, COMFOAIR_REQUEST_PDO_COUNT) -- and NOT from above.
// If the rarest PDO turns out to arrive less often than 60 s, PROBING would be entered on a
// perfectly healthy bus.
//
// This is built to be insensitive to the number: the verdict is passed on the ANSWER TO A
// PROVOCATION, not on the absence of frames. What is left of the risk is bounded by making the
// re-measurement cost one line. Hardware scenario HV-CAN-05 produces N, the maximum interval
// between two frames on a healthy bus; this becomes N and the thresholds follow as 2N and 3N.
//
// DO NOT spell 60000 and 90000 here as two independent numbers: two thresholds living alone drift.
// test_both_thresholds_come_from_one_define is a compile-time assertion rather than a value check
// because of it.
#define COMFOAIR_BUS_QUIET_MS  30000u

// 60 s: the bus is asked a question. Not a verdict and not published as one -- see
// comfoair_bus_health_available().
#define COMFOAIR_BUS_PROBE_MS  (2u * COMFOAIR_BUS_QUIET_MS)
// 90 s: the verdict. Long enough for the provocation to have produced a sweep (COMFOAIR_REQUEST_PDO_COUNT x 100 ms) and for the
// unit to have answered it.
#define COMFOAIR_BUS_SILENT_MS (3u * COMFOAIR_BUS_QUIET_MS)

// Four states, and the fourth is what keeps a fifth NUMBER out of the design.
typedef enum {
    // No frame has been counted since the driver came up. A device whose loom was never plugged in
    // has not "gone quiet" -- it has never spoken, and telling its owner to check a connection
    // that has never worked is a different sentence from telling him his unit stopped answering.
    //
    // THIS IS WHY THERE IS NO BOOT GRACE PERIOD. The draft carried a 120 s start-up allowance and
    // its own author called the number a placeholder; separating "never heard" from "heard and
    // stopped" does that allowance's job exactly, and does it without a third number and without
    // giving a clockless resolver an `uptime` input.
    COMFOAIR_BUS_NEVER_SEEN = 0,
    // A frame arrived less than COMFOAIR_BUS_PROBE_MS ago.
    COMFOAIR_BUS_FRESH,
    // Silence has passed COMFOAIR_BUS_PROBE_MS and one sweep has been asked for. Still AVAILABLE:
    // the verdict is 90 s, and publishing offline here would quietly make the threshold 60.
    COMFOAIR_BUS_PROBING,
    // Silence has passed COMFOAIR_BUS_SILENT_MS with the provocation unanswered. The verdict.
    COMFOAIR_BUS_SILENT,
    COMFOAIR_BUS_HEALTH_COUNT,
} comfoair_bus_health_t;

// Everything one pass remembers. ONE INSTANCE, owned by the monitor task and by nothing else --
// which is the whole of the concurrency argument, and why there is not an atomic in it.
// Zero-initialised is the correct starting state: nothing heard, nothing anchored, nothing asked.
typedef struct {
    // A frame has been counted in THIS driver session. Sticky, and it has to be: comfoair_can.h:69
    // is a uint32_t, so on a busy bus the counter returns to zero after about two months, and a
    // resolver that read "rx_frames == 0" as NEVER_SEEN would declare a working installation new
    // again on that day.
    bool     seen;
    // The counter's value when it was last seen to MOVE, and when that was. Compared with != and
    // not with >, for the same wrap.
    uint32_t last_frames;
    uint32_t last_move_ms;
    // The single provocation for THIS entry into PROBING has been spent. Cleared by a frame, so a
    // bus that flaps once a day is probed once a day rather than once ever.
    bool     probed;
} comfoair_bus_health_state_t;

// What one pass is allowed to know. Not a handle among them.
typedef struct {
    uint32_t now_ms;
    // comfoair_can_stats_t.started (comfoair_can.h:68) -- the driver NOW, not app_main's one-time
    // verdict on starting it. comfoair_can_stop() zeroes every counter on the next start
    // (comfoair_can.h:65-66), so a pass that ignored this would measure a fresh zero against the
    // previous session's anchor and answer SILENT one second into a healthy bus.
    bool     started;
    // comfoair_can_stats_t.rx_frames, and NOT a count of successfully applied PDOs: the unit
    // broadcasts PDOs that are not in our table (src/main.cpp:70-72), so by "applied" a live bus
    // would look dead.
    uint32_t rx_frames;
} comfoair_bus_health_in_t;

// What the caller does. Both fields are written on every pass.
typedef struct {
    comfoair_bus_health_t state;
    // Send ONE sweep now: the caller's whole side of it is `if (out.probe)
    // comfoair_poll_request_now();`. True on the pass that ENTERS probing and on no other, which
    // is not a nicety -- comfoair_poll.h:4-7 warns by name that a sweep of RTRs
    // back to back on a 50 kbps bus is a burst the unit has no reason to expect, and a probe once
    // a minute would turn the hourly sweep into a per-minute one, a sixtyfold rise in traffic.
    //
    // Several callers of comfoair_poll_request_now() would be harmless -- requests collapse
    // (comfoair_poll_plan_request_sweep() sets one flag) so the extra sweep is exactly one -- but
    // a caller that asks every minute produces a sweep every minute, which no notification
    // collapse can undo.
    bool                  probe;
} comfoair_bus_health_out_t;

// One pass of the monitor task, as arithmetic. TOTAL: any input is answered, NULLs included; a
// call with a missing argument leaves NEVER_SEEN and asks for nothing, because a caller with
// nothing to say has heard nothing.
//
// TASK CONTEXT: the monitor task, and only it -- the state above has one owner. The comparison of
// times is an unsigned difference, so the machine is correct across the 32-bit millisecond wrap
// (~49.7 days) and at now_ms == 0.
void comfoair_bus_health_pass(comfoair_bus_health_state_t *state,
                              const comfoair_bus_health_in_t *in, comfoair_bus_health_out_t *out);

// What <prefix>/bus publishes: true is "online", false is "offline".
//
// FALSE FOR NEVER_SEEN, and honestly so: there are no values, so there is nothing to lie with. The
// asymmetry of the hysteresis lives here too -- offline after a threshold, online on the first
// frame with no debounce, because a frame either arrived or it did not and a debounce would cost
// the owner another 90 s of a grey card after he closed the breaker.
bool comfoair_bus_health_available(comfoair_bus_health_t state);

// What comfoair_phase_inputs_t.bus_silent is filled from.
//
// TRUE FOR SILENT ONLY -- deliberately NOT the negation of comfoair_bus_health_available(), and
// NEVER_SEEN is the row that proves it: offline on the topic, and not a BUS_SILENT phase, because
// "check the loom" is the wrong instruction for a device that has never had one. Writing this as
// !available() re-creates the boot grace period the four states exist to avoid.
bool comfoair_bus_health_is_silent(comfoair_bus_health_t state);

// One spelling per state, for /api/status and the log line. NEVER NULL, including for a number
// outside the enumeration -- these travel through JSON documents, and a renderer handed (null)
// would put a bare word into the document and the page would fail to parse a healthy device. The
// same contract, and the same reasoning, as comfoair_phase_name and comfoair_mqtt_fail_name.
const char *comfoair_bus_health_name(comfoair_bus_health_t state);

#ifdef __cplusplus
}
#endif
