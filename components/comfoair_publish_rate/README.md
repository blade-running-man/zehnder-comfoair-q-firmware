# comfoair_publish_rate — when one entity's state may go out over MQTT

> This is documentation, not code — re-read the source before relying on a detail.

## Purpose

The unit broadcasts its fan speeds, flows and avoided-power figures several times a minute, and
most of those frames move a value by one rpm or one m³/h. Publishing every one of them put well over
one message a second on the broker and into every Home Assistant recorder subscribed to it. This
component holds the one rule that decides, per entity, whether a changed value goes out now, waits,
or is already on the broker — as a pure function the host suite can drive with a fake clock.

## Responsibility

Owns:
- The verdict `SEND` / `HOLD` / `SAME` for one value against what this connection last published.
- The order of the six steps below, and the clock-wrap-safe arithmetic behind them.
- The slot record of a publication that went out.

Does NOT do:
- The per-row values. They are declared in `tools/pdo_table.py` (`NumericPdo.publish_deadband`,
  `publish_min_interval_s`), generated into `comfoair_entity_t`, checked by
  `tools/registry_checks.py` (`check_publish_rate`), and argued in `docs/interfaces/mqtt.md`.
  `COMFOAIR_PUBLISH_MAX_HOLD_S` (300) is generated too.
- The clock, the slot array, the change marks and the publish — the MQTT publisher task
  (`components/comfoair_mqtt/comfoair_mqtt_publish.c`).
- REST and the WebSocket, which are never limited (see Notes).

## Public API

`include/comfoair_publish_rate.h`:

- `comfoair_publish_rate_slot_t` — what this connection last put on the broker for one entity;
  zeroed means nothing yet. The owner zeroes every slot on a new connection.
- `comfoair_publish_rate_limited(entity)` — true when the row declares a deadband or a min interval.
- `comfoair_publish_rate_decide(entity, value, slot, now_ms)` — the verdict. Any NULL argument
  answers `SEND`: holding back is the optimisation, publishing is the contract.
- `comfoair_publish_rate_record(slot, value, now_ms)` — call only after the broker accepted the
  message, or the slot answers `SAME` to a value the broker never got.

## Implementation

The rule, per entity, against what *this connection* last published:

1. no policy on the row, or nothing sent yet on this connection → **send**;
2. a reading appearing or going away (number ↔ none) → **send** — a state change, never jitter;
3. the same value as sent → **same** (the caller clears the mark and sends nothing);
4. ≥ `COMFOAIR_PUBLISH_MAX_HOLD_S` since the last publication of this row → **send** — a held
   difference catches up;
5. |Δ| < deadband, or < min interval since the last publication → **hold** (the caller keeps the
   mark and asks again on its next 1 s pass);
6. otherwise → **send**.

The guarantee an owner gets: the broker's value is within one deadband of the truth, or at most one
min interval old, and exactly true within 300 s of the last change.

Step 4 sits above step 5 on purpose: below it, a drift that stays inside one band would be held for
good. Elapsed time is `now_ms - at_ms` in `uint32_t`, so the 49.7-day wrap of the millisecond clock
costs nothing; a `now < deadline` comparison would hold every limited row forever after the wrap.
"Same" is exact float equality, which is correct because both sides come out of the one decode and
the state model's own change test is the same equality.

Only numeric rows carry a policy: the fields exist on `NumericPdo` alone. Enums, binaries, selects,
the switch, the fan and number entities are never held, because their moving *is* the state change.

## Tests

`test/test_publish_rate` (host, `pio test -e native -f test_publish_rate`). Expectations are built
from each row's own generated fields, so retuning a value in `pdo_table.py` does not break the rule's
tests; the values themselves are pinned in `tests/python/test_registry_publish_rate.py`. Covered:
only numeric rows limited, no-policy rows always send, first publication never held, jitter held,
min interval, max hold, no repeat of an unchanged value, number ↔ none immediate, a min-interval-only
row, the clock wrap, the record, and NULL arguments failing open.

## Notes

**MQTT only, deliberately.** REST and the WebSocket serve the model exactly:

- REST is the read-back authority: a client re-reads after every write, and `GET /api/state` must
  return what the unit said, not something within 25 rpm of it.
- The WebSocket is a LAN point-to-point delta that already coalesces per render, with no broker
  fan-out, no retained store and no third-party subscribers.
- Criterion 3 is about *coverage* — the same entities and operations on both transports — and that
  is unchanged: every MQTT state topic still has its REST key, and MQTT's value is a declared,
  documented projection of REST's (the guarantee above).

**DO NOT add a timed republish of an unchanged value.** Step 4 flushes a held *difference*; it never
repeats an identical one. A clock-driven refresh of stable values was rejected:
`comfoair_mqtt_internal.h` already says "DO NOT add a second timed publication beside it" about the
one timed publication, `data_freshness` (300 s, `expire_after` 900); no data entity carries
`expire_after`, so a repeat refreshes nothing in Home Assistant; an identical state is no
`state_changed` there; and every row once per 300 s would spend most of the message budget. The max
hold equals the freshness period, so one number describes both: nothing on this broker is more than
five minutes behind.
