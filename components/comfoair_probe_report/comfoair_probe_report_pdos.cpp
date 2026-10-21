// The report's "pdos" section: every PDO number heard on the bus since boot, known or not.
//
// A file of its own beside the walk's renderer because it reports a different thing -- the passive
// census of the bus, not the RMI walk -- and shares only the Emitter. Key order is part of the
// schema for the same reason as the rest of the report: owners diff two downloads by eye.
//
// The rows arrive one at a time through meta->pdo_row, never as an array: the device copies each
// record out of a store it guards with its own lock, and the section of 256 worst-case rows is
// ~70 KB of JSON, so it streams through the Emitter like the rest of the report.
#include "comfoair_probe_report_internal.h"

namespace probe_report {

namespace {

// The set bits of `mask` from bit 0 up to `top`, as an ascending JSON array of bit numbers.
// Bits above `top` are not values the field can hold (a CAN frame carries at most 8 bytes; a node
// id is 6 bits) and are dropped rather than printed as lengths or nodes nobody can send.
void put_bits(Emitter &em, uint64_t mask, unsigned top)
{
    em.put("[");
    bool first = true;
    for (unsigned b = 0; b <= top; b++)
        if (mask & (1ull << b)) {
            if (!first)
                em.put(",");
            em.put_uint(b);
            first = false;
        }
    em.put("]");
}

void put_row(Emitter &em, const comfoair_probe_pdo_row_t &r)
{
    em.put("{\"pdo\":");
    em.put_uint(r.pdo);
    em.put(",\"known\":");
    em.put(r.known ? "true" : "false");
    em.put(",\"count\":");
    em.put_uint(r.count);
    em.put(",\"first_ms\":");
    em.put_uint(r.first_ms);
    em.put(",\"last_ms\":");
    em.put_uint(r.last_ms);
    em.put(",\"dlc\":");
    put_bits(em, r.dlc_mask, 8);
    em.put(",\"nodes\":");
    put_bits(em, r.node_mask, 63);
    em.put(",\"last\":");
    // Clamped like every other count in the report: a corrupt length must not read past `last`.
    em.put_hex(r.last, r.last_len < sizeof r.last ? r.last_len : sizeof r.last);
    em.put("}");
}

}  // namespace

void put_pdos(Emitter &em, const comfoair_probe_meta_t *meta)
{
    // No summary, no key at all -- not an empty object: a device without a census (and every
    // golden document written before the census existed) renders byte-identical to before.
    if (meta == nullptr || meta->pdo_summary == nullptr)
        return;
    const comfoair_probe_pdo_summary_t &s = *meta->pdo_summary;
    em.put(",\"pdos\":{\"uptime_ms\":");
    em.put_uint(s.uptime_ms);
    em.put(",\"frames\":");
    em.put_uint(s.frames);
    em.put(",\"distinct\":");
    em.put_uint(s.distinct);
    em.put(",\"overflow\":");
    em.put_uint(s.overflow);
    em.put(",\"rows\":[");
    const size_t n = meta->pdo_row != nullptr ? meta->pdo_rows : 0;
    bool first = true;
    for (size_t i = 0; i < n && em.ok(); i++) {
        comfoair_probe_pdo_row_t r = {};
        // A row the device could not copy is skipped, not the end of the section. DO NOT stop at
        // the first refusal: one record that cannot be copied must not cost the rest of the
        // census, which is exactly the evidence an owner downloads this report for.
        if (!meta->pdo_row(meta->pdo_ctx, i, &r))
            continue;
        if (!first)
            em.put(",");
        put_row(em, r);
        first = false;
    }
    em.put("]}");
}

}  // namespace probe_report
