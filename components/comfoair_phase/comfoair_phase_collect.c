// One pass of the phase monitor, as arithmetic. The contract, and the argument for the file
// existing at all, are in comfoair_phase_collect.h.
//
// NOT ONE ESP-IDF HEADER, and that is what the whole design of the pair turns on: every line here
// is swept by test/test_phase, and what is left inside comfoair_phase_monitor.c's #ifdef
// ESP_PLATFORM is five calls, a task and a delay.
#include "comfoair_phase_collect.h"

#include <string.h>

// Has the hold-off run out? An UNSIGNED difference and never `now >= since + span`: the second form
// is wrong across the 32-bit millisecond wrap (~49.7 days), where the sum overflows and a broker
// that has been down for a minute reads as one that has been down for no time at all. The same
// arithmetic, and the same reason, as comfoair_bus_health.c's comparisons.
static bool elapsed(uint32_t now_ms, uint32_t since_ms, uint32_t span_ms)
{
    return (uint32_t)(now_ms - since_ms) >= span_ms;
}

void comfoair_phase_collect_pass(comfoair_phase_collect_t *state,
                                 const comfoair_phase_facts_t *facts,
                                 comfoair_phase_collect_out_t *out)
{
    comfoair_phase_collect_out_t answer;
    memset(&answer, 0, sizeof answer);

    // TOTAL, and the NULLs are answered rather than guarded against. A caller with nothing to say
    // has not started the machine, which is literally BOOT -- the same rule and the same wording as
    // comfoair_phase_derive(NULL) and comfoair_bus_health_pass() with a missing argument. The
    // no-reboot rule (AGENTS.md) in the shape it takes here: the monitor reads five
    // subsystems every second and not one failed read may take the task down.
    if (state == NULL || facts == NULL) {
        answer.phase    = COMFOAIR_PHASE_BOOT;
        answer.previous = COMFOAIR_PHASE_BOOT;
        answer.bus      = COMFOAIR_BUS_NEVER_SEEN;
        if (out != NULL)
            *out = answer;
        return;
    }

    // --- the bus, whose verdict belongs to somebody else ------------------------------------
    //
    // The resolver owns the threshold and this file owns nothing of it (two thresholds answering
    // one question drift apart, and what the owner then gets is one colour on the device
    // beside a different card in Home Assistant).
    const comfoair_bus_health_in_t bus_in = {
        .now_ms    = facts->now_ms,
        .started   = facts->can_started,
        .rx_frames = facts->can_rx_frames,
    };
    comfoair_bus_health_out_t bus_out;
    memset(&bus_out, 0, sizeof bus_out);
    comfoair_bus_health_pass(&state->bus, &bus_in, &bus_out);
    answer.bus   = bus_out.state;
    answer.probe = bus_out.probe;

    // --- the broker hold-off ------------------------------------------------------------------
    //
    // A fact enters at once; an ABSENCE enters after a hold-off derived from the natural period of
    // the thing that is absent. The anchor is cleared by a connection and not spent once, so a
    // broker that flaps twice an hour is granted the hold-off twice an hour -- the defect once
    // found in the retry ladder, where a bit survived a new attempt and the device
    // reported "waiting for a rung" over its own live dial.
    if (facts->broker_connected) {
        state->broker_down = false;
    } else if (!state->broker_down) {
        state->broker_down    = true;
        state->broker_down_ms = facts->now_ms;
    }
    const bool broker_counts_as_connected =
        facts->broker_connected ||
        !elapsed(facts->now_ms, state->broker_down_ms, COMFOAIR_PHASE_NO_BROKER_MS);

    // --- the seventeen fields, and this is the whole of the mapping ---------------------------
    comfoair_phase_inputs_t in;
    memset(&in, 0, sizeof in);

    in.machine_ready   = facts->machine_ready;
    in.ota_in_progress = facts->ota_in_progress;

    // The seven branches of app_main. A CLEAR BIT MEANS THE BRANCH CAME UP: nothing reports success
    // and nothing needs to, because comfoair_phase_monitor_note_fault() is called only where a
    // start returned an error. DO NOT invert this into "note_started" without also deciding what a
    // device reports between two of those calls -- the monitor is started last precisely so that
    // question never arises.
    in.can_started         = !(facts->faults & COMFOAIR_PHASE_FAULT_BIT(COMFOAIR_PHASE_FAULT_CAN));
    in.poll_started        = !(facts->faults & COMFOAIR_PHASE_FAULT_BIT(COMFOAIR_PHASE_FAULT_POLL));
    in.control_started     = !(facts->faults & COMFOAIR_PHASE_FAULT_BIT(COMFOAIR_PHASE_FAULT_CONTROL));
    in.net_started         = !(facts->faults & COMFOAIR_PHASE_FAULT_BIT(COMFOAIR_PHASE_FAULT_NET));
    in.mqtt_ready          = !(facts->faults & COMFOAIR_PHASE_FAULT_BIT(COMFOAIR_PHASE_FAULT_MQTT_QUEUE));
    in.mqtt_client_started = !(facts->faults & COMFOAIR_PHASE_FAULT_BIT(COMFOAIR_PHASE_FAULT_MQTT_CLIENT));
    in.http_started        = !(facts->faults & COMFOAIR_PHASE_FAULT_BIT(COMFOAIR_PHASE_FAULT_HTTP));

    in.prov_state   = facts->prov_state;
    in.prov_failure = facts->prov_failure;
    in.ap_on_air    = facts->ap_on_air;

    // THE DRIVER NOW, not app_main's one-time verdict on starting it -- comfoair_phase.h spends a
    // paragraph on the one situation where the two diverge, and on why the `bus_started &&` term of
    // the cascade is not dead code. Filling both from one read is the mutation it warns about.
    in.bus_started = facts->can_started;
    // comfoair_bus_health_is_silent() AND NOT !comfoair_bus_health_available(). The two differ on
    // exactly one state, NEVER_SEEN, and that state is the whole reason the resolver has four:
    // a device whose loom was never plugged in publishes bus = offline honestly -- there is nothing
    // to report -- and must not be painted BUS_SILENT, whose owner instruction is "check the loom
    // and the unit's power" (see the comment on
    // comfoair_phase_inputs_t.bus_silent). Writing the negation here re-creates the boot grace
    // period the four states exist to avoid.
    in.bus_silent = comfoair_bus_health_is_silent(bus_out.state);

    in.broker_configured = facts->broker_configured;
    in.broker_connected  = broker_counts_as_connected;
    // The latch, decided by the one function that owns this code space for phases. DO NOT shorten
    // to `last_connack != 0`: code 3 is "server unavailable", which is what a Mosquitto add-on
    // answers in the first seconds after a restart, and latching it would put the 300-second
    // retry kept for rejected credentials behind a five-second restart.
    in.broker_rejected = comfoair_phase_connack_is_refusal(facts->last_connack);

    // --- the one decision, which is somebody else's ---------------------------------------------
    answer.inputs   = in;
    answer.phase    = comfoair_phase_derive(&in);
    answer.previous = state->phase;
    // ONE PER TRANSITION AND NOT ONE PER TICK. A zeroed state claims the device is showing BOOT,
    // which is exactly true -- that is the value comfoair_led holds from init until the first
    // transition reaches it -- so the first pass of a healthy device IS a transition and gets its
    // line, while a first pass that derives BOOT is not and does not.
    answer.changed = (answer.phase != state->phase);
    state->phase   = answer.phase;

    if (out != NULL)
        *out = answer;
}
