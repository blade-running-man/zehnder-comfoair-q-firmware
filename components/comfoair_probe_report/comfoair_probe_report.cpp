// The probe report as streamed JSON. Schema: comfoair_probe_report.h; key order is part of it --
// the web UI and the downloaded JSON are diffed by eye between two walks, and a stable order is
// what makes that diff readable.
//
// No heap and no report-sized buffer: the report can exceed the HTTP layer's 48 KiB shared buffer
// (docs/protocol/probe.md), so it goes through Emitter (comfoair_probe_decode.cpp) in pieces of at
// most EMIT_PIECE bytes.
#include "comfoair_probe_report_internal.h"

namespace probe_report {

namespace {

size_t stored_of(uint8_t len, size_t cap) { return len < cap ? len : cap; }

// The three answer forms that are not data. Returns false for DATA: the caller owns that form,
// because value, range and step each spell their data differently.
bool put_not_data(Emitter &em, comfoair_probe_answer_kind_t kind, uint8_t error,
                  const uint8_t *bytes, size_t stored)
{
    switch (kind) {
    case COMFOAIR_PROBE_ANSWER_DATA: return false;
    case COMFOAIR_PROBE_ANSWER_ERROR:
        em.put("{\"error\":");
        em.put_uint(error);
        em.put(",\"error_name\":");
        em.put_string(error_name(error));
        em.put(",\"hex\":");
        em.put_hex(bytes, stored);
        em.put("}");
        return true;
    case COMFOAIR_PROBE_ANSWER_TIMEOUT: em.put("{\"timeout\":true}"); return true;
    case COMFOAIR_PROBE_ANSWER_NONE: break;
    }
    em.put("null");  // NONE, and any value the enum does not name
    return true;
}

void put_value(Emitter &em, const comfoair_probe_value_t &v, const Decoded &d, bool secret)
{
    const size_t stored = stored_of(v.len, sizeof v.bytes);
    if (put_not_data(em, v.kind, v.error, v.bytes, stored))
        return;
    if (secret) {
        em.put("{\"redacted\":true}");
        return;
    }
    em.put("{\"hex\":");
    em.put_hex(v.bytes, stored);
    em.put(",\"len\":");
    em.put_uint(v.len);
    em.put(",\"number\":");
    em.put_number(d.number);
    em.put(",\"text\":");
    if (d.text == Text::Bytes) {
        em.put_string((const char *)v.bytes, d.text_len, true);
    } else if (d.text == Text::Version) {
        char tmp[16];
        em.put_string(tmp, version_text((uint32_t)d.number.raw, tmp, sizeof tmp), false);
    } else {
        em.put("null");
    }
    em.put(",\"label\":");
    em.put_string(d.label);
    em.put("}");
}

// A range is min then max at the VALUE's width and scale (comfoair-pdo-rmi.md §3). Any other length
// keeps its hex and gets no bounds: splitting it anywhere would be a guess presented as a fact.
void put_range(Emitter &em, const comfoair_probe_aux_t &a, const Decoded &d, bool secret)
{
    const size_t stored = stored_of(a.len, sizeof a.bytes);
    if (put_not_data(em, a.kind, a.error, a.bytes, stored))
        return;
    if (secret) {  // a unit that echoes the value for any type must not leak it here either
        em.put("{\"redacted\":true}");
        return;
    }
    const size_t width = format_width(d.format);
    Number lo{false, 0, d.number.divisor}, hi = lo;
    if (width != 0 && stored == a.len && a.len == 2 * width) {
        lo.valid = decode_int(d.format, a.bytes, width, &lo.raw);
        hi.valid = decode_int(d.format, a.bytes + width, width, &hi.raw);
    }
    em.put("{\"hex\":");
    em.put_hex(a.bytes, stored);
    em.put(",\"len\":");
    em.put_uint(a.len);
    em.put(",\"min\":");
    em.put_number(lo);
    em.put(",\"max\":");
    em.put_number(hi);
    em.put("}");
}

void put_step(Emitter &em, const comfoair_probe_aux_t &a, const Decoded &d, bool secret)
{
    const size_t stored = stored_of(a.len, sizeof a.bytes);
    if (put_not_data(em, a.kind, a.error, a.bytes, stored))
        return;
    if (secret) {
        em.put("{\"redacted\":true}");
        return;
    }
    Number n{false, 0, d.number.divisor};
    if (stored == a.len)
        n.valid = decode_int(d.format, a.bytes, stored, &n.raw);
    em.put("{\"hex\":");
    em.put_hex(a.bytes, stored);
    em.put(",\"len\":");
    em.put_uint(a.len);
    em.put(",\"number\":");
    em.put_number(n);
    em.put(",\"text\":null,\"label\":null}");
}

void put_entry(Emitter &em, uint8_t target, const comfoair_probe_entry_t &e)
{
    const Property *p = catalogue_find(target, e.node, e.unit, e.subunit, e.property);
    const bool secret = is_redacted(e.unit, e.subunit, e.property);
    Decoded d{};
    d.format = p != nullptr ? p->format : Format::None;
    d.number.divisor = 1;
    if (e.value.kind == COMFOAIR_PROBE_ANSWER_DATA)
        d = decode_value(p, e.value.bytes, stored_of(e.value.len, sizeof e.value.bytes),
                         e.value.len);
    if (e.value.kind == COMFOAIR_PROBE_ANSWER_DATA && e.range.kind == COMFOAIR_PROBE_ANSWER_DATA)
        settle_sign(d, e.value.bytes, stored_of(e.value.len, sizeof e.value.bytes), e.range.bytes,
                    stored_of(e.range.len, sizeof e.range.bytes), e.range.len);

    em.put("{\"node\":");
    em.put_uint(e.node);
    em.put(",\"unit\":");
    em.put_uint(e.unit);
    em.put(",\"subunit\":");
    em.put_uint(e.subunit);
    em.put(",\"property\":");
    em.put_uint(e.property);
    em.put(",\"name\":");
    em.put_string(p != nullptr ? p->name : nullptr);
    em.put(",\"access\":");
    em.put_string(p != nullptr ? p->access : nullptr);
    em.put(",\"format\":");
    em.put_string(format_name(d.format));
    em.put(",\"unit_label\":");
    em.put_string(p != nullptr ? p->unit_label : nullptr);
    em.put(",\"value\":");
    put_value(em, e.value, d, secret);
    em.put(",\"range\":");
    put_range(em, e.range, d, secret);
    em.put(",\"step\":");
    put_step(em, e.step, d, secret);
    em.put("}");
}

const char *state_name(comfoair_probe_state_t s)
{
    switch (s) {
    case COMFOAIR_PROBE_IDLE: return "idle";
    case COMFOAIR_PROBE_RUNNING: return "running";
    case COMFOAIR_PROBE_DONE: return "done";
    case COMFOAIR_PROBE_FAILED: return "failed";
    }
    return "idle";
}

const char *status_name(comfoair_probe_unit_status_t s)
{
    switch (s) {
    case COMFOAIR_PROBE_UNIT_PRESENT: return "present";
    case COMFOAIR_PROBE_UNIT_ABSENT: return "absent";
    case COMFOAIR_PROBE_UNIT_TIMEOUT: return "timeout";
    }
    return "timeout";
}

void put_head(Emitter &em, const comfoair_probe_results_t &r, const comfoair_probe_meta_t *meta)
{
    em.put("{\"state\":");
    em.put_string(state_name(r.state));
    em.put(",\"failure\":");
    em.put_string(r.failure == COMFOAIR_PROBE_FAILURE_NO_ANSWER ? "no_answer" : nullptr);
    em.put(",\"node\":");
    em.put_uint(r.plan.target_node);
    em.put(",\"another_counter\":");
    em.put_uint(meta != nullptr ? meta->another_counter : 0);
    em.put(",\"elapsed_ms\":");
    em.put_uint(meta != nullptr ? meta->elapsed_ms : 0);
    const comfoair_probe_progress_t &g = r.progress;
    em.put(",\"progress\":{\"requests\":");
    em.put_uint(g.requests);
    em.put(",\"timeouts\":");
    em.put_uint(g.timeouts);
    em.put(",\"node\":");
    em.put_uint(g.node);
    em.put(",\"unit\":");
    em.put_uint(g.unit);
    em.put(",\"subunit\":");
    em.put_uint(g.subunit);
    em.put(",\"property\":");
    em.put_uint(g.property);
    em.put("},\"nodes_seen\":[");
    const size_t seen =
        (meta != nullptr && meta->nodes_seen != nullptr) ? meta->nodes_seen_count : 0;
    for (size_t i = 0; i < seen; i++) {
        if (i != 0)
            em.put(",");
        em.put_uint(meta->nodes_seen[i]);
    }
    em.put("]");
}

void put_units(Emitter &em, const comfoair_probe_results_t &r)
{
    // Clamped: the counts and the arrays are separate fields, and a count past its array must
    // not turn into a read past the results.
    const size_t n =
        r.unit_count < COMFOAIR_PROBE_MAX_UNITS ? r.unit_count : COMFOAIR_PROBE_MAX_UNITS;
    em.put(",\"units\":[");
    for (size_t i = 0; i < n && em.ok(); i++) {
        const comfoair_probe_unit_t &u = r.units[i];
        em.put(i == 0 ? "{\"node\":" : ",{\"node\":");
        em.put_uint(u.node);
        em.put(",\"unit\":");
        em.put_uint(u.unit);
        em.put(",\"name\":");
        em.put_string(unit_name(r.plan.target_node, u.node, u.unit));
        em.put(",\"status\":");
        em.put_string(status_name(u.status));
        em.put(",\"subunits\":");
        em.put_uint(u.subunits);
        em.put(",\"absent_properties\":");
        em.put_uint(u.absent_properties);
        em.put("}");
    }
    em.put("]");
}

void put_active_errors(Emitter &em, const comfoair_probe_results_t &r)
{
    const size_t stored = stored_of(r.errors_len, sizeof r.errors);
    em.put(",\"active_errors\":");
    if (put_not_data(em, r.errors_kind, r.errors_code, r.errors, stored))
        return;
    em.put("{\"hex\":");
    em.put_hex(r.errors, stored);
    em.put(",\"ids\":[");
    // Each byte from its least significant bit; the running bit index IS the error id (byte i bit
    // b is id i*8+b). DO NOT walk MSB-first: every id would land on its mirror in the byte.
    bool first = true;
    for (size_t i = 0; i < stored; i++)
        for (unsigned b = 0; b < 8; b++)
            if (r.errors[i] & (1u << b)) {
                if (!first)
                    em.put(",");
                em.put_uint(i * 8 + b);
                first = false;
            }
    em.put("]}");
}

}  // namespace

}  // namespace probe_report

bool comfoair_probe_render(const comfoair_probe_results_t *results,
                           const comfoair_probe_meta_t *meta, comfoair_probe_write_fn write,
                           void *ctx)
{
    using namespace probe_report;
    if (results == nullptr || write == nullptr)
        return false;
    const comfoair_probe_results_t &r = *results;
    Emitter em(write, ctx);
    put_head(em, r, meta);
    put_units(em, r);
    em.put(",\"properties\":[");
    const size_t n =
        r.entry_count < COMFOAIR_PROBE_MAX_ENTRIES ? r.entry_count : COMFOAIR_PROBE_MAX_ENTRIES;
    for (size_t i = 0; i < n && em.ok(); i++) {
        if (i != 0)
            em.put(",");
        put_entry(em, r.plan.target_node, r.entries[i]);
    }
    em.put("]");
    put_active_errors(em, r);
    put_pdos(em, meta);
    em.put(",\"truncated\":");
    em.put(r.truncated ? "true" : "false");
    em.put("}");
    return em.flush();
}
