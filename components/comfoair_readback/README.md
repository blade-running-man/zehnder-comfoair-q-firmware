# comfoair_readback — what an RMI read-back reply means, and whether it may be applied

> This is documentation, not code — re-read the source before relying on a detail.

## Purpose

Three writable selects — `temperature_passive`, `humidity_comfort`, `humidity_protection`
(TEMPHUMCONTROL, RMI unit `0x1D`, subunit `0x01`, properties `0x04`/`0x06`/`0x07`, U8
`0 = Off`, `1 = Auto only`, `2 = On`) — are reported by no PDO. The firmware reads them back from
the unit over RMI GET ONE (`01 1D 01 10 <prop>`) during the probe task's unit read. This component
is the pure half of that: the decode of one reply, and the guard that decides whether a reply may
overwrite the state model. It is host-tested; the bus round trip, the lock and the apply live in
`comfoair_probe_task` (`comfoair_probe_task_settings.cpp`).

## Responsibility

Owns:
- `comfoair_readback_decode()`: one reply into an index into the entity's own options.
- `comfoair_readback_may_apply()`: the command-path guard.

Does NOT do:
- Hold a list of rows. The rows are `COMFOAIR_RMI_READBACK` in `registry_generated.h`, generated
  from `tools/pdo_table.py` (`SelectSpec.rmi`).
- Send anything, lock anything, read the command counters, or touch `comfoair_state` — the probe
  task does all four. `REQUIRES comfoair_registry comfoair_protocol` only.

## Public API

`include/comfoair_readback.h` (C linkage, safe from any task, no state):

| Symbol | Contract |
| --- | --- |
| `bool comfoair_readback_decode(const comfoair_rmi_readback_t *entry, const uint8_t *data, size_t len, uint8_t *index_out)` | True only for exactly one byte below the entity's `option_count`. False for any NULL argument, an `entity` outside `COMFOAIR_ENTITIES` or without options, `len != 1`, or a value out of range; `*index_out` untouched on false. |
| `bool comfoair_readback_may_apply(uint32_t queued_before, uint32_t done_before, uint32_t queued_now)` | `queued_before`/`done_before` (`sent + failed`) from `comfoair_control_get_stats()` before the reads; `queued_now` sampled under the same `comfoair_lock()` hold as the apply. True only when `queued_before == done_before` and `queued_now == queued_before`. |

## Implementation

`comfoair_readback.cpp`, two functions, no globals. The decode bounds the value by the entity's
`option_count`, not by a literal 3, so the wire value and the labels the registry publishes cannot
disagree without the decode refusing. A reply longer than one byte is refused rather than
truncated: it means the property is not the one we think it is.

## Tests

`test/test_readback` (`pio test -e native -f test_readback`):
- the decode: values 0/1/2, a value of 3 and 0xFF, lengths 0 and 2, NULL arguments, an entity
  index outside the registry;
- the guard: idle, a command in flight `(5,4,5)`, a command submitted during the read `(5,5,6)`,
  wraparound by equality only;
- the generated table against the WRITE builders in `rmi_frame.h`: each row's unit, subunit and
  property equal the bytes `temperature_passive()` / `humidity_comfort()` / `humidity_protection()`
  write, matched by entity key; each entity is a writable three-option select with no PDO and no
  default (`NaN`).

The glue that uses these is not host-built; see `comfoair_probe_task`'s README.

## Notes

- **Why not a value compare-and-set.** "Apply only if the state still holds what it held before the
  read" looks like the obvious guard and guards nothing. HTTP (`comfoair_http_command.c`, the submit
  and then `comfoair_state_set_option()`) and MQTT (`comfoair_mqtt_command.c`, the same order) queue
  a command BEFORE their optimistic state write, and the sender task sleeps between frames with up
  to eight commands queued. So a GET can reach the wire ahead of a queued SET, come back with the
  OLD value while the state still shows the old value too — the compare passes, and the read-back
  would overwrite the owner's write for the 6 h until the next read. Only the command counters see a
  write that has not reached the bus yet.
- **Why equality and not `<`.** The counters are `uint32_t` and wrap; a dropped submit decrements
  `queued` again (`comfoair_control_submit()`). "Unchanged" is the one question that stays right.
- **Why not NVS.** Persisting the last written value would only know what THIS firmware wrote. It
  is wrong after a change on the unit's own display, from ComfoConnect, after a unit reset, or once
  the ESP is moved to another unit. The unit is the holder of the setting, so the unit is asked.
