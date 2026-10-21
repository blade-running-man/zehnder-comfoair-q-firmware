// The PDO census: a heard-set, an open-addressing index and an append-only record array.
#include "comfoair_pdo_census.h"

#include <cstring>

namespace {

constexpr size_t SLOTS = sizeof(static_cast<comfoair_pdo_census_t *>(nullptr)->slot) /
                         sizeof(static_cast<comfoair_pdo_census_t *>(nullptr)->slot[0]);

// Load factor <= 0.5 when every record is in: a probe for a PDO that has no record (one past the
// ceiling, heard again) always reaches an empty slot quickly, and the loop below always ends.
static_assert(SLOTS >= 2 * COMFOAIR_PDO_CENSUS_MAX, "index must stay at most half full");
static_assert((SLOTS & (SLOTS - 1)) == 0, "the hash is a mask, so the slot count is a power of 2");
// A slot stores record index + 1 in a uint16_t, with 0 meaning empty.
static_assert(COMFOAIR_PDO_CENSUS_MAX < 0xffff, "slot encoding needs index + 1 to fit");
static_assert(COMFOAIR_PDO_CENSUS_PDO_LIMIT == 8192, "the 13-bit PDO field of the CAN id");
// The header orders the record's fields widest first; a field added or moved out of that order
// reintroduces padding and costs 2 KB per 8 B over 256 rows.
static_assert(sizeof(comfoair_pdo_census_record_t) == 40, "record must stay padding-free");

constexpr size_t MASK = SLOTS - 1;

void copy_last(comfoair_pdo_census_record_t *r, const uint8_t *data, size_t len)
{
    // The tail is cleared so a shorter frame never leaves the previous frame's bytes behind; the
    // renderer prints only last_len bytes, but a copied record compared in a test (or dumped in a
    // debugger) should not carry stale payload. A fixed 8-byte memset is not the memmove the
    // header forbids: it is constant time and touches only this record.
    std::memset(r->last, 0, sizeof r->last);
    if (len > 0) std::memcpy(r->last, data, len);
    r->last_len = static_cast<uint8_t>(len);
}

}  // namespace

void comfoair_pdo_census_reset(comfoair_pdo_census_t *c)
{
    // One memset of ~12 KB, once at boot, before the receive task can see the census.
    std::memset(c, 0, sizeof *c);
}

void comfoair_pdo_census_record(comfoair_pdo_census_t *c, uint16_t pdo, uint8_t node,
                                const uint8_t *data, size_t len, uint32_t now_ms)
{
    if (pdo >= COMFOAIR_PDO_CENSUS_PDO_LIMIT || node >= 64 || len > 8 ||
        (len > 0 && data == nullptr)) {
        return;
    }
    c->frames++;

    const uint8_t bit = static_cast<uint8_t>(1u << (pdo & 7));
    uint8_t *heard = &c->heard[pdo >> 3];
    comfoair_pdo_census_record_t *r = nullptr;

    if (!(*heard & bit)) {
        // First sight. `distinct` counts it whether or not it gets a row, which is what lets the
        // caller report overflow as distinct - count without a third counter. And because the PDO
        // was never heard, it cannot be in the index: append and insert without a lookup.
        *heard |= bit;
        c->distinct++;
        if (c->count >= COMFOAIR_PDO_CENSUS_MAX) return;

        const uint32_t idx = c->count;
        r = &c->rec[idx];
        r->pdo = pdo;
        r->count = 0;
        r->first_ms = now_ms;
        r->dlc_mask = 0;
        r->node_mask = 0;
        size_t s = pdo & MASK;
        while (c->slot[s] != 0) s = (s + 1) & MASK;
        c->slot[s] = static_cast<uint16_t>(idx + 1);
        // `count` is bumped only once the record and its slot are in place. Under the caller's
        // spinlock the order is invisible, but it keeps the invariant "records [0, count) are
        // complete" true at every step, which is what order() and the renderer lean on.
        c->count = idx + 1;
    } else {
        // Heard before: either it has a record, reachable from its home slot before the first
        // empty one, or it was past the ceiling and the probe ends on an empty slot. Records are
        // never removed, so there are no tombstones and an empty slot is a definite miss.
        size_t s = pdo & MASK;
        for (;;) {
            const uint16_t v = c->slot[s];
            if (v == 0) return;
            if (c->rec[v - 1].pdo == pdo) {
                r = &c->rec[v - 1];
                break;
            }
            s = (s + 1) & MASK;
        }
    }

    r->count++;
    r->last_ms = now_ms;
    r->dlc_mask = static_cast<uint16_t>(r->dlc_mask | (1u << len));
    r->node_mask |= 1ull << node;
    copy_last(r, data, len);
}

size_t comfoair_pdo_census_order(const comfoair_pdo_census_t *c, uint32_t count, uint16_t *out,
                                 size_t cap)
{
    if (count > COMFOAIR_PDO_CENSUS_MAX) count = COMFOAIR_PDO_CENSUS_MAX;

    // An insertion sort bounded by `cap`: each record finds its place among the n kept so far,
    // and a record that would land at or past `cap` is dropped. At most 256 x 256 uint16_t
    // compares-and-shifts, on the HTTP task, outside the spinlock, once per render -- cheaper in
    // code and stack than anything cleverer, and it needs no scratch buffer beyond `out`.
    // DO NOT sort c->rec itself: records never move (the header's THREAD CONTEXT), and the index
    // stores record positions.
    size_t n = 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint16_t pdo = c->rec[i].pdo;
        size_t pos = n;
        while (pos > 0 && c->rec[out[pos - 1]].pdo > pdo) pos--;
        if (pos >= cap) continue;
        size_t end = n < cap ? n : cap - 1;
        for (size_t k = end; k > pos; k--) out[k] = out[k - 1];
        out[pos] = static_cast<uint16_t>(i);
        if (n < cap) n++;
    }
    return n;
}
