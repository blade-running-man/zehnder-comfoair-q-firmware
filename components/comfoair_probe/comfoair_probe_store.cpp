// Where the walk's answers are kept. See comfoair_probe_store.h.
#include "comfoair_probe_store.h"

#include <algorithm>
#include <cstring>

namespace comfoair_probe_store {

comfoair_probe_results_t g_results{};

namespace {

// Before the first comfoair_probe_begin() the report is IDLE, and it must still name the node a
// walk would target: live Q350 R1.11.0: the idle report said "node":0, a node that
// does not exist. Set by a static initialiser after g_results is zeroed (same translation unit,
// so the order is the declaration order). DO NOT initialise g_results itself with the plan: a
// non-zero initialiser moves the whole ~30 KB struct from .bss into .data, i.e. into the image.
// DO NOT fix it in the renderer instead: comfoair_probe_results() is read by others too, and
// "IDLE carries the default plan" is a fact about the results, not about their JSON.
struct DefaultPlan {
    DefaultPlan()
    {
        const comfoair_probe_plan_t plan = COMFOAIR_PROBE_DEFAULT_PLAN();
        g_results.plan = plan;
    }
} g_default_plan;

// Where answers go when the report is full. Writing into a sink instead of branching on "is there
// room" keeps every rule of the walk identical for a stored and a dropped property -- which is why
// the walk's shape (and its request count) does not depend on COMFOAIR_PROBE_MAX_ENTRIES.
comfoair_probe_unit_t g_row_sink;
comfoair_probe_entry_t g_entry_sink;

// One answer into a report field: the prefix that fits, and `len` as received (clamped to the
// field's 255). A timeout keeps no length -- whatever the caller left in `len` was not received.
void keep(comfoair_probe_answer_kind_t *kind, uint8_t *error, uint8_t *len, uint8_t *bytes, size_t cap,
          const comfoair_probe_outcome_t &o)
{
    *kind = o.kind;
    if (o.kind == COMFOAIR_PROBE_ANSWER_TIMEOUT)
        return;
    *error = o.kind == COMFOAIR_PROBE_ANSWER_ERROR ? o.error : 0;
    *len = o.len > 0xFF ? 0xFF : static_cast<uint8_t>(o.len);
    size_t n = std::min(o.len, cap);
    if (o.data && n)
        std::memcpy(bytes, o.data, n);
}

// The next free slot of a report table, or the sink once the table is full.
template <typename T, size_t N> T *take(T (&slots)[N], size_t *used, T *sink)
{
    T *t = sink;
    if (*used < N)
        t = &slots[(*used)++];
    else
        g_results.truncated = true;
    *t = T{};
    return t;
}

}  // namespace

comfoair_probe_unit_t *open_row(uint8_t node, uint8_t unit, comfoair_probe_unit_status_t status)
{
    comfoair_probe_unit_t *row = take(g_results.units, &g_results.unit_count, &g_row_sink);
    row->node = node;
    row->unit = unit;
    row->status = status;
    return row;
}

comfoair_probe_unit_t *row_sink() { return &g_row_sink; }

comfoair_probe_entry_t *store_value(uint8_t node, uint8_t unit, uint8_t subunit, uint8_t property,
                                    const comfoair_probe_outcome_t &o)
{
    comfoair_probe_entry_t *e = take(g_results.entries, &g_results.entry_count, &g_entry_sink);
    e->node = node;
    e->unit = unit;
    e->subunit = subunit;
    e->property = property;
    keep(&e->value.kind, &e->value.error, &e->value.len, e->value.bytes, sizeof e->value.bytes, o);
    return e;
}

comfoair_probe_entry_t *entry_sink() { return &g_entry_sink; }

void keep_aux(comfoair_probe_aux_t *dst, const comfoair_probe_outcome_t &o)
{
    *dst = comfoair_probe_aux_t{};
    keep(&dst->kind, &dst->error, &dst->len, dst->bytes, sizeof dst->bytes, o);
}

void keep_errors(const comfoair_probe_outcome_t &o)
{
    comfoair_probe_results_t &r = g_results;
    keep(&r.errors_kind, &r.errors_code, &r.errors_len, r.errors, sizeof r.errors, o);
}

}  // namespace comfoair_probe_store

extern "C" const comfoair_probe_results_t *comfoair_probe_results(void)
{
    return &comfoair_probe_store::g_results;
}
