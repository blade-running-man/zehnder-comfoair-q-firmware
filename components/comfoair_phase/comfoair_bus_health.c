// The four states of the bus, as arithmetic. The header beside this file carries the argument for
// every decision below; this is the machine. The as-built summary is in docs/device/indication.md
// (Hold-offs) and docs/interfaces/mqtt.md (Availability).
//
// BESIDE comfoair_phase.c AND IN THE SAME COMPONENT deliberately. The two are the same kind of
// thing -- a pure verdict with the clock left to the caller -- and comfoair_phase_inputs_t.bus_silent
// is filled from comfoair_bus_health_is_silent(), so a reader of one lands next to the other. The
// component's CMakeLists.txt argues the rest: nothing here includes ESP-IDF, which is what keeps
// the host suites building.
#include "comfoair_bus_health.h"

#include <stddef.h>

void comfoair_bus_health_pass(comfoair_bus_health_state_t *st, const comfoair_bus_health_in_t *in,
                              comfoair_bus_health_out_t *out)
{
    if (out == NULL) {
        return;
    }
    // Written BEFORE the argument check, not after: a caller with nothing to say has heard
    // nothing, and leaving whatever the previous pass wrote would let one missing argument keep a
    // stale verdict -- or a stale provocation -- alive for ever.
    out->state = COMFOAIR_BUS_NEVER_SEEN;
    out->probe = false;
    if (st == NULL || in == NULL) {
        return;
    }

    if (!in->started) {
        // The driver is down: comfoair_can_stop() has run, or it never came up. Every counter is
        // zeroed by the next comfoair_can_start() (comfoair_can.h:65-66), so the anchor below is
        // about to describe a session that no longer exists -- and a pass that kept it would
        // compare a fresh zero against it and answer SILENT one second into a healthy bus.
        //
        // NEVER_SEEN and not SILENT, for the reason comfoair_phase.h:135-146 spells out at length:
        // "the driver did not come up" is our fault and reads as FAULT, while "check the loom" is
        // the owner's. The phase cascade decides FAULT before it reaches the bus anyway; this is
        // the same distinction kept honest one layer down.
        st->seen        = false;
        st->probed      = false;
        st->last_frames = 0;
        st->last_move_ms = in->now_ms;
        return;
    }

    // DID IT MOVE -- not "did it grow". comfoair_can.h:69 is a uint32_t and nothing resets it
    // except a restart of the driver, so on a busy bus it returns to zero after roughly two
    // months; `>` would read that as silence and take a working installation offline.
    //
    // The `!st->seen && rx_frames != 0` half catches the ordinary case where frames arrived before
    // the monitor's first pass: the anchor is still zero, so the != above already fires, and this
    // term is what makes that also count as HAVING HEARD rather than merely as movement.
    //
    // KNOWN AND ACCEPTED: a driver that stops and restarts entirely BETWEEN two passes is never
    // seen with started == false, and its zeroed counter reads as movement -- one optimistic pass,
    // self-correcting on the next. Closing it needs a session number in comfoair_can_stats_t, and
    // comfoair_can.h:65-66 declines to grow the public struct for a case app_main cannot reach:
    // comfoair_can_stop() has no caller that also restarts it inside one second.
    const bool moved = st->seen ? (in->rx_frames != st->last_frames) : (in->rx_frames != 0u);
    if (moved) {
        st->seen         = true;
        st->last_frames  = in->rx_frames;
        st->last_move_ms = in->now_ms;
        // A new silence gets its own single provocation. Without this line a bus that drops out
        // once a day is probed on the first day and never again -- the mutation is invisible to
        // any case that only watches one outage.
        st->probed = false;
    }

    if (!st->seen) {
        // Not silence: newness. This is the branch that removes the draft's 120 s boot allowance
        // and, with it, the third number and an `uptime` input to a resolver that has no clock.
        return;
    }

    // Unsigned difference, which is what makes the machine correct across the 32-bit millisecond
    // wrap (~49.7 days) and at now_ms == 0. DO NOT rewrite this as `now_ms > last + THRESHOLD`:
    // that form is the one that breaks on the wrap, roughly seven weeks into a run nobody is
    // watching.
    const uint32_t quiet_ms = in->now_ms - st->last_move_ms;

    if (quiet_ms >= COMFOAIR_BUS_SILENT_MS) {
        out->state = COMFOAIR_BUS_SILENT;
    } else if (quiet_ms >= COMFOAIR_BUS_PROBE_MS) {
        out->state = COMFOAIR_BUS_PROBING;
        if (!st->probed) {
            // ONE sweep for this entry into PROBING, and the latch is what makes it one.
            // comfoair_poll.h:4-7 warns by name that a sweep of RTRs on a 50 kbps
            // bus is a burst the unit has no reason to expect; asking on every pass would send one
            // per second, and the version that reads as reasonable -- once a minute -- still turns
            // an hourly sweep into a per-minute one, sixty times the traffic.
            //
            // DO NOT try to fix that by rate-limiting the caller instead. Requests collapse
            // (comfoair_poll_plan_request_sweep() sets one flag), so several CALLERS cost exactly
            // one extra sweep -- but a single caller asking once a minute costs a sweep a minute,
            // and no notification collapse can undo that.
            st->probed = true;
            out->probe = true;
        }
    } else {
        out->state = COMFOAIR_BUS_FRESH;
    }
}

bool comfoair_bus_health_available(comfoair_bus_health_t state)
{
    switch (state) {
    case COMFOAIR_BUS_FRESH:
    case COMFOAIR_BUS_PROBING:
        // PROBING is available: the question has been asked and the verdict is 90 s. Publishing
        // offline here would make the effective threshold 60 without anyone editing a number.
        return true;
    case COMFOAIR_BUS_NEVER_SEEN:
    case COMFOAIR_BUS_SILENT:
    case COMFOAIR_BUS_HEALTH_COUNT:
        return false;
    }
    // A number that is not a state. Offline, because the quietest bug must not look like the
    // healthiest device.
    return false;
}

bool comfoair_bus_health_is_silent(comfoair_bus_health_t state)
{
    switch (state) {
    case COMFOAIR_BUS_SILENT:
        return true;
    case COMFOAIR_BUS_NEVER_SEEN:
    case COMFOAIR_BUS_FRESH:
    case COMFOAIR_BUS_PROBING:
    case COMFOAIR_BUS_HEALTH_COUNT:
        // NEVER_SEEN IS THE ROW THAT MATTERS, and it is why this is a switch of its own rather
        // than !comfoair_bus_health_available(). A device that has never heard a frame publishes
        // bus = offline honestly -- there are no values -- and must NOT be painted BUS_SILENT,
        // whose owner instruction is "check the loom and the unit's power". Fusing
        // the two functions brings back the boot grace period the four states exist to avoid.
        return false;
    }
    return false;
}

const char *comfoair_bus_health_name(comfoair_bus_health_t state)
{
    switch (state) {
    case COMFOAIR_BUS_NEVER_SEEN: return "never-seen";
    case COMFOAIR_BUS_FRESH:      return "fresh";
    case COMFOAIR_BUS_PROBING:    return "probing";
    case COMFOAIR_BUS_SILENT:     return "silent";
    case COMFOAIR_BUS_HEALTH_COUNT: break;
    }
    return "unknown";
}

// The compile-time half of test_both_thresholds_come_from_one_define, standing on the DEVICE build
// too: two thresholds that stop being made of one number drift apart, and that is the failure
// this guards. _Static_assert and not static_assert because this file is C.
_Static_assert(COMFOAIR_BUS_PROBE_MS == 2u * COMFOAIR_BUS_QUIET_MS,
               "the probing threshold stopped being derived from COMFOAIR_BUS_QUIET_MS");
_Static_assert(COMFOAIR_BUS_SILENT_MS == 3u * COMFOAIR_BUS_QUIET_MS,
               "the silence threshold stopped being derived from COMFOAIR_BUS_QUIET_MS");
_Static_assert(COMFOAIR_BUS_PROBE_MS < COMFOAIR_BUS_SILENT_MS,
               "the bus would be asked a question after the verdict had been passed");
