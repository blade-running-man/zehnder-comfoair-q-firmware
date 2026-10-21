# comfoair_pdo_census — the passive record of every PDO heard on the bus

> This is documentation, not code — re-read the source before relying on a detail.

## Purpose

The entity table (`COMFOAIR_REQUEST_PDOS`, generated from `tools/pdo_table.py`) lists the PDOs
the firmware asks for. The ComfoNet bus carries more than that: PDOs the unit sends unasked, and
PDOs from other nodes (a ComfoConnect gateway, a ComfoSense panel, accessories). This component
records every one that goes past: how often, when first and last, from which nodes, at which data
lengths, and the last payload. That record is how an undocumented PDO is found on a real
installation without a logic analyser. It only listens: nothing here can transmit or poll.

## Responsibility

Owns:

- The census state (`comfoair_pdo_census_t`): the 8192-bit heard set, the open-addressing index,
  the append-only record array and the `frames` / `distinct` / `count` counters.
- Which frames count: a PDO id below 8192, a node below 64, a length of 0..8 (and non-NULL data
  when the length is not 0). Anything else is ignored and counted nowhere.
- The 256-row ceiling: a PDO first heard once the array is full counts in `frames` and `distinct`
  but gets no row; the caller reports `overflow` as `distinct - count`.
- Ascending-PDO ordering of the rows for rendering (`comfoair_pdo_census_order`).

Does NOT do:

- Hold the instance, lock, or timestamp — `comfoair_probe_task`'s bus side owns the one static
  census, feeds it from the CAN receive task under its spinlock and supplies `now_ms`.
- Know which PDOs are in the entity table (`known`), or render JSON — the caller fills
  `comfoair_probe_pdo_row_t` and `comfoair_probe_report` renders the `"pdos"` key. This component
  depends on neither, so the renderer is tested against hand-built rows.
- Decode a PDO's value — that is `comfoair_protocol`'s `pdo_decode`.

## Public API

`include/comfoair_pdo_census.h`, C-compatible:

| Function | Contract |
| --- | --- |
| `comfoair_pdo_census_reset(c)` | Zeroes everything. Once at boot, before the receive task can reach `c`. |
| `comfoair_pdo_census_record(c, pdo, node, data, len, now_ms)` | O(1) amortised, worst case bounded by 256 probes under the caller's lock (clustered input); no allocation, memmove, log or blocking. Runs under the caller's spinlock. Out-of-range frames are ignored. |
| `comfoair_pdo_census_order(c, count, out, cap)` | Writes record indices ascending by PDO, at most `cap` (the `cap` lowest PDOs). Reads only `rec[0, count).pdo`, never `c->count`. Runs outside the lock. |

`COMFOAIR_PDO_CENSUS_MAX` (256) is the row ceiling; `COMFOAIR_PDO_CENSUS_PDO_LIMIT` (8192) the
13-bit PDO field.

## Implementation

`comfoair_pdo_census.cpp`, one file.

- **First sight is a bit test.** `heard[pdo >> 3]` decides whether the PDO is new. A new PDO bumps
  `distinct` and, if there is room, is appended at `rec[count]` and inserted into the index with
  no lookup — a PDO never heard cannot be in it. A PDO heard before is looked up in the index; an
  empty slot means it was past the ceiling, and the frame is counted in `frames` only.
- **The index** is 512 `uint16_t` slots, hashed by `pdo & 511` with linear probing, each slot
  holding record index + 1 (0 = empty). With at most 256 records it is never more than half full,
  so probes are short and a miss always ends on an empty slot. Records are never removed, so there
  are no tombstones. `static_assert`s pin the load factor, the power-of-two mask and the slot
  encoding.
- **Records are append-only.** A record never moves and its `pdo` never changes; `count` is bumped
  only after the record and its slot are in place. That is what lets the reader snapshot `count`
  under the lock and then call `order()` outside it: the `pdo` fields it reads are already final.
  The other fields change on every frame, so the reader copies a record under the lock.
- **`last` is cleared before each copy**, so a shorter frame leaves no stale bytes past `last_len`.
- **`order()` is an insertion sort bounded by `cap`** into the caller's `out`: at most 256 × 256
  compares-and-shifts once per render on the HTTP task, with no scratch buffer.

Memory: 1 KB heard set + 1 KB index + 256 × 40 B records (widest field first, no padding, `static_assert`ed) ≈ 12 KB, static, owned by the caller.

## Dependencies

None beyond the C library (`<cstring>`). `CMakeLists.txt` lists no `REQUIRES` on purpose:
`record()` runs under a spinlock on the receive task, and nothing that can log or block should be
reachable from it.

## Tests

`test/test_pdo_census` (host, `pio test -e native -f test_pdo_census`). Records are found in the
tests by scanning the array, not through the index, so the index is tested by re-recording a PDO
and asserting the existing record was updated rather than a second one appended.

- reset from a garbage-filled struct gives zero records, frames and distinct;
- first frame: count 1, `first_ms == last_ms`, dlc bit, node bit, payload and length;
- repeated frames: count, `last_ms`, payload update, dlc/node bits OR in, `first_ms` kept, no
  stale tail after a shorter frame;
- 300 distinct PDOs: 256 records, `distinct` 300, `frames` 300, overflow 44; an overflowed PDO
  heard again stays rowless, a recorded one is still updated;
- pdo ≥ 8192, node ≥ 64, len 9, NULL data with len 1: nothing changes; pdo 8191 / node 63 / len 8
  are in range;
- a zero-length frame sets dlc bit 0 and `last_len` 0;
- `order()` sorts out-of-order inserts, respects `cap` (the lowest PDOs, not the first rows) and
  an older `count` snapshot;
- PDOs colliding on one slot (≡ 16 mod 512) and on slot 511 wrapping to slot 0 are each found;
- a full census clustered on 32 home slots still finds every PDO, and a new PDO past the ceiling
  ends its probe.

## Notes

- **Why not a sorted array with binary search?** Insertion would memmove under the spinlock and
  would move records the reader is walking. The hash index keeps `record()` constant-time and the
  array still, and sorting is paid once per render, outside the lock.
- **Why a separate heard set instead of only the index?** `distinct` must count PDOs past the
  ceiling, and without a bit per PDO a rowless PDO heard twice would be counted twice. 1 KB buys
  an exact `distinct` and a lookup-free first sight.
- **Timestamps wrap.** `now_ms` is a `u32` milliseconds-since-boot from the caller; it wraps after
  49.7 days and the census does not compensate.
- **Counters wrap.** `frames` and each record's `count` are `u32` and wrap silently (~497 days at
  100 frames/s). `distinct` cannot wrap: it is at most 8192.
