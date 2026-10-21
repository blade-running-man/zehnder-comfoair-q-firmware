// A passive record of every PDO heard on the ComfoNet bus: who sent it, how often, how long, and
// the last payload.
//
// PURE. No bus, no clock, no lock, no allocation: the caller hands in each received PDO frame with
// its own millisecond timestamp, so every rule here -- first sight, the 256-row ceiling, the
// out-of-range frames that are ignored -- is pinned on the host (test/test_pdo_census). The device
// half (comfoair_probe_task's bus side) owns the one static instance, feeds it from the CAN receive
// task, and renders it into `GET /api/probe` through comfoair_probe_report's row callback.
//
// WHY A CENSUS AT ALL: the entity table (COMFOAIR_REQUEST_PDOS) lists the PDOs the firmware asks
// for; the bus carries more than that, sent unasked by the unit and by other nodes. A record of
// what actually goes past is how an undocumented PDO is found without a logic analyser.
//
// OWNERSHIP: the caller owns the comfoair_pdo_census_t (≈12 KB -- keep it static, never on a task
// stack). Nothing here keeps a pointer past the call.
//
// THREAD CONTEXT: none of this locks. The intended split is:
//   - comfoair_pdo_census_record() runs on the CAN receive task under the caller's spinlock;
//   - the reader takes the same spinlock just long enough to snapshot `count` (and to copy one
//     record at a time), then calls comfoair_pdo_census_order() OUTSIDE the lock.
// That split is sound only because records are APPEND-ONLY: a record never moves once appended
// and its `pdo` never changes, so the `pdo` fields of records [0, snapshot) can be read without
// the lock while record() keeps appending past them. The other fields of a record DO change on
// every frame, so a reader copies a record under the lock before formatting it.
//
// FAILURE: there is none to report. A frame that cannot be recorded is either out of range
// (ignored, counted nowhere) or a PDO past the 256-row ceiling (counted in `frames` and
// `distinct`, no row); the caller derives `overflow` as distinct - count.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Rows the census holds. The unit and its usual peers put well under 100 distinct PDOs on the bus;
// 256 leaves room for an unfamiliar installation and still costs only 10 KB of records.
#define COMFOAIR_PDO_CENSUS_MAX 256
// The 13-bit PDO field of the CAN id (pdo_decode.h: PDO_ID_MAX + 1).
#define COMFOAIR_PDO_CENSUS_PDO_LIMIT 8192

// One record; never moves once appended, and its pdo never changes. The widest field leads so
// the record packs into 40 bytes instead of 48 (8 KB less over 256 rows); every use is by name.
typedef struct {
    uint64_t node_mask;  // bit n set: node n (0..63) sent it
    uint32_t count, first_ms, last_ms;  // count wraps silently, see comfoair_pdo_census_t
    uint16_t pdo;
    uint16_t dlc_mask;  // bit n set: a frame of length n (0..8) was seen
    uint8_t last_len;   // 0..8; bytes of `last` past it are zero
    uint8_t last[8];
} comfoair_pdo_census_record_t;

typedef struct {
    uint8_t heard[COMFOAIR_PDO_CENSUS_PDO_LIMIT / 8];  // bit p set: PDO p was heard at least once
    uint16_t slot[512];  // open-addressing index over pdo & 511: record index + 1, 0 = empty
    comfoair_pdo_census_record_t rec[COMFOAIR_PDO_CENSUS_MAX];
    uint32_t count;     // records used
    // Every in-range frame, including those of PDOs that got no record. `frames` and each
    // record's `count` are u32 and wrap silently (~497 days at 100 frames/s); a debugging census
    // does not need more, and a wider counter would cost every record 4 bytes.
    uint32_t frames;
    uint32_t distinct;  // distinct PDOs heard, from `heard`; cannot wrap, it is <= 8192
} comfoair_pdo_census_t;

// Empties the census: no records, no frames, nothing heard. Call once before the first record(),
// before the receive task can reach it.
void comfoair_pdo_census_reset(comfoair_pdo_census_t *c);

// Records one received PDO frame from `node`, `len` payload bytes at `data`, at `now_ms`.
//
// pdo >= 8192, node >= 64, len > 8, or data == NULL with len > 0 are ignored: no counter changes,
// because such a frame did not come off a well-formed ComfoNet PDO id and counting it would put a
// row in the census for something that is not on the bus.
//
// O(1) amortised: one bit test, then at most one index probe sequence over a table that is never
// more than half full. Worst case bounded by 256 probes under the caller's lock: PDOs clustered on
// few home slots form one run of at most 256 occupied slots, which a miss walks to its end.
// DO NOT add allocation, memmove, logging or anything that can block here. This runs on the CAN
// receive task under a spinlock, where all three are either forbidden or stall every other frame
// on the bus; and DO NOT compact, sort or evict records in place, because the reader relies on
// records never moving (see THREAD CONTEXT).
void comfoair_pdo_census_record(comfoair_pdo_census_t *c, uint16_t pdo, uint8_t node,
                                const uint8_t *data, size_t len, uint32_t now_ms);

// Writes record indices in ascending PDO order into `out` and returns how many were written
// (<= cap). With cap < count, the result is the `cap` lowest PDOs, still ascending.
//
// Reads only the immutable `pdo` fields of records [0, count), and NOT c->count: the caller passes
// the `count` it snapshotted under its lock, so this may run outside the lock while record()
// appends. `count` is clamped to COMFOAIR_PDO_CENSUS_MAX.
size_t comfoair_pdo_census_order(const comfoair_pdo_census_t *c, uint32_t count, uint16_t *out,
                                 size_t cap);

#ifdef __cplusplus
}
#endif
