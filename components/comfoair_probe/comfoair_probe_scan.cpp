// The RMI walk: node -> unit -> subunit -> property, one read at a time.
//
// A cursor (phase, node, unit, subunit, property, what is being asked of the property) IS the
// outstanding request: next() renders it, record() moves it. Nothing else decides what goes on
// the wire, so "the walk only reads" is a property of current_request() alone -- it can produce a
// GET ONE of type VALUE/RANGE/STEP or GETACTIVEERRORS, and nothing else (comfoair_probe.h:9-13).
#include "comfoair_probe.h"
#include "comfoair_probe_store.h"

#include <algorithm>

namespace {

namespace store = comfoair_probe_store;

// Another node's identity is NODE 0x01/01 properties 0x01..0x14 -- serial, firmware, model
// (docs/protocol/probe.md). A fixed bound, NOT plan.max_property: that bound is for the target's
// exploration, and a plan that narrows it for a quick look must not blind the census.
constexpr uint8_t IDENTITY_LAST_PROPERTY = 0x14;
// Our own node id. Asking ourselves can only time out, and two silences would push a walk that
// has heard nothing yet toward NO_ANSWER for a reason that has nothing to do with the unit.
constexpr uint8_t OWN_NODE = 0x2A;
// A subunit that stops answering mid-walk would otherwise cost 63 x (1 + retries) x 300 ms ~ 40 s.
// Three in a row is past any single dropped frame.
constexpr uint8_t SILENT_RUN_LIMIT = 3;
// 11 is "unknown command": the unit does not know GET ONE there, and asking it 63 times teaches
// nothing. 8 in a row, not 3: an 11 is an answer, so it costs one request, not (1 + retries).
constexpr uint8_t UNKNOWN_COMMAND_RUN_LIMIT = 8;

enum class Phase { UNITS, ERRORS, EXTRA, END };
enum class Ask { VALUE, RANGE, STEP };

// Bound to the store's object at constant initialisation (an address), so no order-of-init issue.
comfoair_probe_results_t &g_res = store::g_results;
Phase g_phase = Phase::END;
Ask g_ask = Ask::VALUE;
uint8_t g_node = 0, g_unit = 0, g_subunit = 0, g_property = 0;
size_t g_extra = 0;          // index into plan.extra_nodes of the node being walked
bool g_outstanding = false;  // handed out by next(), not yet answered
uint8_t g_retries_left = 0;
uint8_t g_silent_run = 0;     // consecutive final VALUE timeouts in the current subunit
uint8_t g_unknown_run = 0;    // consecutive 11s to VALUE reads in the current subunit
bool g_sub_data = false;      // a VALUE read in the current subunit answered with DATA
bool g_sub_real = false;      // ... answered anything but 14 (an error, a timeout, data)
uint8_t g_silent_finals = 0;  // final timeouts before anything was heard
bool g_heard = false;

comfoair_probe_unit_t *g_row = store::row_sink();
comfoair_probe_entry_t *g_entry = store::entry_sink();  // the property whose range/step is asked

void open_row(comfoair_probe_unit_status_t status) { g_row = store::open_row(g_node, g_unit, status); }

void store_value(const comfoair_probe_outcome_t &o)
{
    g_entry = store::store_value(g_node, g_unit, g_subunit, g_property, o);
}

// The discovery read is subunit 1 property 1 by definition, so a plan bound of 0 means 1: there
// is no walk of a unit that asks nothing.
uint8_t last_subunit() { return g_phase == Phase::EXTRA ? 1 : std::max<uint8_t>(1, g_res.plan.max_subunit); }
uint8_t last_property()
{
    return g_phase == Phase::EXTRA ? IDENTITY_LAST_PROPERTY : std::max<uint8_t>(1, g_res.plan.max_property);
}

void enter_subunit(uint8_t subunit)
{
    g_subunit = subunit;
    g_property = 1;
    g_ask = Ask::VALUE;
    g_silent_run = g_unknown_run = 0;
    g_sub_data = g_sub_real = false;
}

void start_unit(uint8_t node, uint8_t unit)
{
    g_node = node, g_unit = unit;
    enter_subunit(1);
    // No row until the discovery read answers: a row says what the unit IS, and that is unknown.
    g_row = store::row_sink();
    g_entry = store::entry_sink();
}

// 0 is skipped beside the target and ourselves: extra_nodes is a zero-filled array, so a 0 is an
// empty slot, and no CAN node has id 0. A duplicate is skipped because its identity is already in
// the report -- re-reading it is 20+ requests that cannot add anything.
void start_extra()
{
    const comfoair_probe_plan_t &p = g_res.plan;
    for (; g_extra < p.extra_node_count; g_extra++) {
        uint8_t n = p.extra_nodes[g_extra];
        bool dup = std::find(p.extra_nodes, p.extra_nodes + g_extra, n) != p.extra_nodes + g_extra;
        if (n == 0 || n == p.target_node || n == OWN_NODE || dup)
            continue;
        g_phase = Phase::EXTRA;
        start_unit(n, 0x01);
        return;
    }
    g_phase = Phase::END;
    g_res.state = COMFOAIR_PROBE_DONE;
}

void end_unit()
{
    if (g_phase == Phase::UNITS && g_unit < g_res.plan.max_unit) {
        start_unit(g_res.plan.target_node, static_cast<uint8_t>(g_unit + 1));
    } else if (g_phase == Phase::UNITS) {
        g_phase = Phase::ERRORS;
        g_node = g_res.plan.target_node;
        g_unit = g_subunit = g_property = 0;
    } else {
        g_extra++;
        start_extra();
    }
}

// A subunit s > 1 that gave no DATA at all ends the unit's subunits. It closes two walks that
// 13 alone never ends: a unit that answers 14 rather than 13 for a subunit it lacks (every property
// "unknown", so subunits 3..16 would cost 63 requests each), and one that answers 11 to everything
// (each subunit cut to 8 by UNKNOWN_COMMAND_RUN_LIMIT, then this stops the next). Subunit 1 is
// exempt: the unit row already proves the unit exists, and an empty subunit 1 says nothing about
// subunit 2. One that answered ONLY 14 is uncounted -- it evidently is not a subunit; one that
// answered something else (32, 11, silence) did answer, and stays in `subunits`.
// DO NOT drop the rule to "only-14 ends it": 11 is "anything but 14", so that rule would not bound
// the 11-to-everything unit beyond the run limit: 8 requests in each of 16 subunits, 128 per unit
// instead of 16 (test_probe_scan_bounds pins both counts).
void next_subunit()
{
    if (g_subunit > 1 && !g_sub_data) {
        if (!g_sub_real)
            g_row->subunits--;
        return end_unit();
    }
    if (g_subunit >= last_subunit())
        return end_unit();
    enter_subunit(static_cast<uint8_t>(g_subunit + 1));
}

void next_property()
{
    g_ask = Ask::VALUE;
    if (g_property >= last_property())
        return next_subunit();
    g_property++;
}

bool is_error(const comfoair_probe_outcome_t &o, uint8_t code)
{
    return o.kind == COMFOAIR_PROBE_ANSWER_ERROR && o.error == code;
}

// --- what an answer means ----------------------------------------------------------------------

void on_value(const comfoair_probe_outcome_t &o)
{
    const bool timeout = o.kind == COMFOAIR_PROBE_ANSWER_TIMEOUT;
    if (g_subunit == 1 && g_property == 1) {
        // The discovery read: it decides the row, and it is also property 1 of subunit 1, so a
        // DATA / 14 / other error falls through and is handled like any property below.
        // A silent unit gets no entry beside its row: the row already says "it did not answer",
        // and the walk asked nothing else of it that an entry would locate.
        if (timeout || is_error(o, COMFOAIR_PROBE_ERR_UNKNOWN_UNIT)) {
            open_row(timeout ? COMFOAIR_PROBE_UNIT_TIMEOUT : COMFOAIR_PROBE_UNIT_ABSENT);
            return end_unit();
        }
        open_row(COMFOAIR_PROBE_UNIT_PRESENT);
        if (is_error(o, COMFOAIR_PROBE_ERR_UNKNOWN_SUBUNIT))
            return end_unit();
        g_row->subunits = 1;
    } else if (g_property == 1) {
        // A later subunit's first read. 12 here is read as 13: either way there is no such
        // subunit, and it is not a property to report.
        if (is_error(o, COMFOAIR_PROBE_ERR_UNKNOWN_SUBUNIT) || is_error(o, COMFOAIR_PROBE_ERR_UNKNOWN_UNIT))
            return end_unit();
        // Kept, and the unit is left: probing further subunits of a unit that went quiet costs
        // (1 + retries) x 300 ms each and cannot tell "absent" from "still quiet". NOT counted in
        // `subunits` -- the header defines it as how many answered.
        if (timeout) {
            store_value(o);
            return end_unit();
        }
        g_row->subunits++;
    } else if (is_error(o, COMFOAIR_PROBE_ERR_UNKNOWN_SUBUNIT)) {
        return next_subunit();  // a 13 mid-subunit: the unit says this subunit is over
    }

    if (is_error(o, COMFOAIR_PROBE_ERR_UNKNOWN_PROPERTY)) {
        g_row->absent_properties++;  // at most 255 subunits x 255 properties: cannot wrap
        g_silent_run = g_unknown_run = 0;
        return next_property();
    }
    g_sub_real = true;
    g_sub_data |= o.kind == COMFOAIR_PROBE_ANSWER_DATA;
    store_value(o);
    // A 12 after the discovery read said the unit exists contradicts it. Kept (it is exactly the
    // kind of wrong assumption the report exists to show), and the subunit is left: every further
    // property would only repeat it.
    if (is_error(o, COMFOAIR_PROBE_ERR_UNKNOWN_UNIT))
        return next_subunit();
    if (timeout) {
        g_unknown_run = 0;
        return ++g_silent_run >= SILENT_RUN_LIMIT ? next_subunit() : next_property();
    }
    g_silent_run = 0;
    if (is_error(o, COMFOAIR_PROBE_ERR_UNKNOWN_COMMAND))
        return ++g_unknown_run >= UNKNOWN_COMMAND_RUN_LIMIT ? next_subunit() : next_property();
    g_unknown_run = 0;
    // Identity reads are values only: a census asks who a node is, not what it accepts.
    if (o.kind == COMFOAIR_PROBE_ANSWER_DATA && g_phase != Phase::EXTRA)
        g_ask = Ask::RANGE;
    else
        next_property();
}

// Range and step silences never touch g_silent_run: the value just answered, so the subunit is
// demonstrably alive, and a unit that ignores RANGE must not lose its later properties for it.
void on_range(const comfoair_probe_outcome_t &o)
{
    store::keep_aux(&g_entry->range, o);
    if (o.kind == COMFOAIR_PROBE_ANSWER_DATA)
        g_ask = Ask::STEP;
    else
        next_property();
}

void on_step(const comfoair_probe_outcome_t &o)
{
    store::keep_aux(&g_entry->step, o);
    next_property();
}

void on_errors(const comfoair_probe_outcome_t &o)
{
    store::keep_errors(o);
    g_extra = 0;
    start_extra();
}

comfoair_probe_request_t current_request()
{
    comfoair_probe_request_t r{};
    r.node = g_node;
    if (g_phase == Phase::ERRORS) {
        r.kind = COMFOAIR_PROBE_REQ_ACTIVE_ERRORS;
        return r;
    }
    r.kind = COMFOAIR_PROBE_REQ_GET;
    r.unit = g_unit;
    r.subunit = g_subunit;
    r.property = g_property;
    static constexpr uint8_t TYPE_OF[] = {COMFOAIR_PROBE_TYPE_VALUE, COMFOAIR_PROBE_TYPE_RANGE,
                                          COMFOAIR_PROBE_TYPE_STEP};
    r.type = TYPE_OF[static_cast<int>(g_ask)];
    return r;
}

}  // namespace

extern "C" void comfoair_probe_begin(const comfoair_probe_plan_t *plan)
{
    comfoair_probe_plan_t p = COMFOAIR_PROBE_DEFAULT_PLAN();
    if (plan)
        p = *plan;
    p.extra_node_count = std::min<uint8_t>(p.extra_node_count, COMFOAIR_PROBE_MAX_EXTRA_NODES);
    g_res = comfoair_probe_results_t{};
    g_res.plan = p;
    g_res.state = COMFOAIR_PROBE_RUNNING;
    g_outstanding = g_heard = false;
    g_retries_left = p.retries;
    g_silent_finals = 0, g_extra = 0;
    // Positioned on "unit 0", so end_unit() moves to unit 1 -- or, for max_unit 0, straight on to
    // the active errors -- by the same rule every later unit uses.
    g_phase = Phase::UNITS;
    start_unit(p.target_node, 0x00);
    end_unit();
}

// A NULL `out` hands nothing out: a request nobody received must not count as sent.
extern "C" bool comfoair_probe_next(comfoair_probe_request_t *out)
{
    if (!out || g_res.state != COMFOAIR_PROBE_RUNNING || g_phase == Phase::END)
        return false;
    if (!g_outstanding) {
        g_outstanding = true;
        g_res.progress.requests++;
    }
    *out = current_request();
    g_res.progress.node = out->node, g_res.progress.unit = out->unit;
    g_res.progress.subunit = out->subunit, g_res.progress.property = out->property;
    return true;
}

extern "C" void comfoair_probe_record(const comfoair_probe_outcome_t *outcome)
{
    if (!g_outstanding || !outcome)
        return;
    // NONE is "not asked" and anything past TIMEOUT is no answer kind at all: neither may move
    // the walk, and the request stays outstanding so the real answer can still land.
    if (outcome->kind < COMFOAIR_PROBE_ANSWER_DATA || outcome->kind > COMFOAIR_PROBE_ANSWER_TIMEOUT)
        return;
    g_outstanding = false;
    const bool timeout = outcome->kind == COMFOAIR_PROBE_ANSWER_TIMEOUT;
    if (timeout)
        g_res.progress.timeouts++;
    if (timeout && g_retries_left > 0) {
        g_retries_left--;
        return;  // the cursor did not move: next() renders the same request
    }
    g_retries_left = g_res.plan.retries;
    if (!timeout)
        g_heard = true;
    else if (!g_heard && g_silent_finals < UINT8_MAX)
        g_silent_finals++;

    if (g_phase == Phase::ERRORS)
        on_errors(*outcome);
    else if (g_ask == Ask::RANGE)
        on_range(*outcome);
    else if (g_ask == Ask::STEP)
        on_step(*outcome);
    else
        on_value(*outcome);

    // Checked after the answer is applied, so the report shows the rows that were asked. An
    // abort_after_silent of 0 disables the abort. DO NOT reset g_silent_finals on an answer or
    // count silences after one: a unit that answered once does answer RMI from this node, and a
    // later silent stretch (a unit with many absent-by-timeout units) is information, not failure.
    if (!g_heard && g_res.plan.abort_after_silent && g_silent_finals >= g_res.plan.abort_after_silent) {
        g_res.state = COMFOAIR_PROBE_FAILED;
        g_res.failure = COMFOAIR_PROBE_FAILURE_NO_ANSWER;
        g_phase = Phase::END;
    }
}
