# comfoair_command — turns an entity write or a named operation into the RMI frames for the unit

> This is documentation, not code — re-read the source before relying on a detail.

This is the firmware's one write path to the ventilation unit, kept pure. It takes "write this
value to this entity" or "run this operation with these parameters" and returns up to three CAN
payloads, plus a per-frame delay, in a plain struct. It sends nothing. The stakes are high: one wrong
unit byte can turn a harmless write into a command that bricks the unit (any `>= 0x80` command to
unit `0x01`, NODE). The only legitimate `>= 0x80` commands are `0x83/0x84/0x85` on SCHEDULE (`0x15`).
Because the component is pure, a host test can check every byte it can produce without a board.

## Responsibility

Owns:
- The mapping from each of the 13 writable registry entities to its frame(s).
- The mapping from the six HA operations (`boost`, `set_bypass`, `set_balance_mode`,
  `set_ventilation_level`, `set_temperature_profile`, `set_away`) to their frame(s).
- Refusing bad input: an unknown entity or op, a value outside the registry's range, an option label
  that is not in the list, or a missing op parameter.
- The meaning of 0 and negative durations, and the minutes-to-seconds conversion (done once).
- The duration defaults (`comfoair_command_durations_defaults()`).

Does NOT do:
- Byte layout. Every frame comes from `comfoair_protocol`'s `rmi_frame.h` (`comfoair::rmi::*`).
  There are no hex literals here.
- Writability, option lists and ranges. These come from the generated table in
  `comfoair_registry` (`COMFOAIR_ENTITIES`).
- Sending, sequencing, waiting out `delay_ms`, or persisting durations. `comfoair_control` does that
  (`comfoair_control_submit()`, `comfoair_control_store_duration()`,
  `comfoair_control_set_permanent_manual()`, NVS).
- JSON decoding of request bodies. `comfoair_wire` does that
  (`comfoair_wire_parse_entity_write`, the op-body parser).
- Reading the unit's current fan level. The caller fills `fan_resume_level`
  (`comfoair_http_command.c`, `comfoair_mqtt_inbound.c`).

## Public API

All in `include/comfoair_command.h`. It is a plain C ABI and the implementation is C++. There is no
state, lock or allocation, so it is safe from any task. The caller owns every output struct.

| Symbol | Contract |
| --- | --- |
| `comfoair_command_durations_defaults(out)` | Fills boost/bypass/balance = 60 min, away = 0 (indefinite), `permanent_manual = true`, `fan_resume_level = 0`. A NULL `out` is a no-op. |
| `comfoair_command_knows_entity(key)` | True only when the registry marks `key` writable AND this file has a builder for it. NULL or unknown keys return false. |
| `comfoair_command_from_entity(key, value, durations, out)` | Zeroes `*out` first, then builds it. Returns `OK` with frames, or with `stores_duration` set and zero frames. Returns `ERR_ARG` (NULL input), `ERR_UNKNOWN_ENTITY` or `ERR_BAD_VALUE`. |
| `comfoair_command_from_op(name, params, durations, out)` | Zeroes `*out` first, then builds it. Returns `OK`, `ERR_ARG`, `ERR_UNKNOWN_OP`, `ERR_MISSING_PARAM` or `ERR_BAD_VALUE`. |
| `comfoair_command_readback_pdo(command)` | The PDO to request shortly after sending `command`, or `-1`. Today: the `away_mode` switch's PDO (from its registry row) when any frame starts or cancels SCHEDULE subunit 1 timer 11 (`84`/`85 15 01 0B`) — the switch, `set_away`, the `Auto` preset. Decided from the frame bytes, so a new builder that releases away is covered without being listed. NULL → `-1`. |
| `comfoair_command_strerror(err)` | Returns a static English sentence that never contains the value. Safe to log and to send to a client. `comfoair_http_command.c` uses it for the error body. |

Types and constants:
- `COMFOAIR_COMMAND_MAX_BYTES = 13`: ENABLETIMERENTRY is exactly 13 bytes and DISABLETIMERENTRY
  exactly 4. Trailing zero padding is not part of the protocol, and no frame here carries any.
- `COMFOAIR_COMMAND_MAX_FRAMES = 3`: the "Auto" preset needs three frames (release boost, release
  away, release manual). Nothing needs a fourth.
- `comfoair_command_frame_t {bytes, len, delay_ms}`: `delay_ms` is how long to wait *before* this
  frame. Today it is non-zero only on Auto's third frame (1000 ms). It is carried as a number
  because this component has no clock.
- `comfoair_command_t`: `frames[]` and `count`, plus `stores_duration`, `duration_key` and
  `duration_minutes`. `duration_key` points at the registry's static key string, so it never
  dangles. **A caller must check `stores_duration` before treating `count == 0` as a no-op.**
- `comfoair_command_value_t`: `NUMBER`, `BOOL` or `TEXT`. `TEXT` is an option *label* matched
  exactly, never an index. An index on the wire would mean different things in different builds.
- `comfoair_command_durations_t`: durations in minutes, because that is what the registry advertises
  and what the owner types. Also carries `permanent_manual` (pair every level change with
  manual-mode-on so the unit does not fall back to AUTO within two hours or at the next scheduler
  step) and `fan_resume_level` (0 = unknown).
- `comfoair_command_params_t`: each `has_*` flag plus `mode` and `profile` (NULL = absent).
  `duration_seconds` is **signed seconds**, because that is the frozen HA `services.yml` contract.

## Implementation

Files:
- `comfoair_command.cpp`: the public entry points (`from_entity`, `from_op`, `knows_entity`,
  `durations_defaults`, `strerror`) and the two registry lookups `entity()` and `option_index()`.
  It decides WHICH command a request is; the builders never consult the registry.
- `comfoair_command_build.cpp`: the frame builders, which decide WHAT BYTES that command is. A file
  of its own so the dispatch and the bytes can each be read whole, under the 350-line ceiling.
- `comfoair_command_internal.h`: private, C++ only. The ten builders the entry points call, in
  `namespace comfoair_command_detail` (an anonymous namespace cannot span two translation units);
  `comfoair_command.cpp` pulls each in with a `using` declaration.
- `comfoair_command_readback.cpp`: `comfoair_command_readback_pdo()`, which reads a built command
  back and builds nothing.
- `include/comfoair_command.h` is the contract.

Flow of `from_entity`:
1. Check for NULL inputs, zero `*out`, look up the registry entity and require
   `writable && knows_entity`.
2. Duration entities (`boost/bypass/balance/away_duration`) take a NUMBER inside the registry's
   `min_value..max_value`. They set `stores_duration` and produce no frame.
3. `away_mode` takes a BOOL and goes to `build_away` with `away_minutes * 60`.
4. `fan` accepts three shapes:
   - BOOL false: level 0 (Away). An HRV never stops ventilating.
   - BOOL true: `fan_resume_level` if it is 1..3, otherwise Low (1).
   - TEXT: one of the six preset labels, through `build_preset`.
   - NUMBER: 0..3, truncated to an integer.
5. Every other entity is a select, written by its option label. `option_index` does an exact
   match, case included. The index is then dispatched per key:
   - `temperature_profile`: the index *is* the wire byte, in protocol order Normal/Cool/Warm.
   - `temperature_passive` / `humidity_comfort` / `humidity_protection`: one `set_property` on
     TEMPHUMCONTROL subunit `0x01`, with the index (0..2, off/auto/on) as the value.
   - `bypass_mode` and `balance_mode`: the index picks a mode string from a local table, which then
     goes to the same builder as the op.
   - `ventilation_mode`: dispatches on the label (`build_preset`), not on the index.

Builders (`comfoair_command_build.cpp`; `append`, `timeout_or_indefinite` and `build_auto` are
private to that file, the rest are `comfoair_command_detail`):
- `build_boost`: `<= 0` cancels the boost (`boost_off`).
- `build_bypass`: `auto` releases and ignores the duration.
- `build_balance`: always sends two frames, one for each of subunits 06 and 07.
- `build_level`: `manual_on` first (if `permanent_manual`), then `ventilation_level`.
- `build_profile`: indefinite timeout, hardcoded inside `rmi::temperature_profile`.
- `build_away`: off ignores the duration.
- `build_auto`: `boost_off`, `away_off`, then `manual_off` with a 1000 ms delay.
- `build_preset`: Auto / Boost / Away / Low / Medium / High.
- `build_off_auto_on`: the shared builder for the three TEMPHUMCONTROL selects above.
- `timeout_or_indefinite`: maps `<= 0` to `rmi::TIMEOUT_INDEFINITE` (-1).
- `append`: drops any frame that is empty or too long, and any frame past the third, so it can
  never emit a DLC=0 frame that would look like a successful send.

Invariants:
- No byte is written by hand.
- Writability, options and ranges are read from `COMFOAIR_ENTITIES`.
- On every return path, `*out` is either fully built or still zeroed.

DO NOT:
- Write a literal opcode or unit byte here. That would be a second copy of the protocol, outside
  the reach of `test_rmi_frame`.
- Fold `bypass_mode` / `balance_mode` into "index = wire byte". For balance, the index chooses a
  *pair of frames*. Sharing one lookup between index-as-byte and value-as-byte is exactly how two
  selects once shipped dead.
- Bound the local `modes[]` arrays by the registry's `option_count`. They are bounded by their own
  size so that a fourth option added in `pdo_table.py` returns `ERR_BAD_VALUE` instead of reading
  past the array.
- Reorder `temperature_profile` options to match the unit's display (WARM/NORMAL/COOL). The
  registry order is protocol order.
- Drop the second balance frame. The other subunit's timer would keep running, and switching from
  supply-only to exhaust-only would leave both timers set.
- Add `"auto"` to `set_temperature_profile`. The supposed release (`85 15 03 01`) has never been
  seen on the wire. It needs hardware verification first.
- Give any op parameter a default. For bypass, balance and away, an invented 0 would mean "forever".
- Sleep on `delay_ms` here. Waiting belongs to `comfoair_control`.

## Dependencies

- `REQUIRES comfoair_protocol` for `rmi_frame.h` (frame builders, `TIMEOUT_INDEFINITE`,
  `UNIT_TEMPHUMCONTROL`, `PROP_*`). `REQUIRES comfoair_registry` for `registry_generated.h`.
- It deliberately does not depend on `comfoair_can`, `esp_driver_twai` or anything else that can
  transmit. If it could send, "what goes on the wire" would no longer be a value a host test can
  inspect.
- Used by:
  - `comfoair_control`: queues `comfoair_command_t`, owns the durations, calls the defaults.
  - `comfoair_http`: `from_entity`, `from_op` and `strerror`.
  - `comfoair_mqtt_inbound`: `from_entity` and `from_op` from `comfoair_mqtt_decide`.
  - `comfoair_mqtt`: reads the durations.
  - `comfoair_wire`: fills `value_t` and `params_t`.
  - `src/main.cpp`: copies the durations into the state model at boot.

## Tests

- `test/test_command_readback/test_command_readback.cpp`: which commands ask for the away
  switch's PDO (switch on/off, `set_away`, the `Auto` preset via `ventilation_mode` and `fan`) and
  which ask for nothing (levels, boost, bypass, a duration write, timer 11's byte on another subunit
  or under a property write). Its own suite because `test_command.cpp` is over the suite ceiling.
- `test/test_command/test_command.cpp` is the dedicated suite (679 lines, over the 600-line suite
  ceiling). It covers:
  - A sweep of the whole registry: every writable entity has a builder; no non-writable or unknown
    key does.
  - The unit byte of every frame the component can build, pinned by number. No frame is empty or
    padded.
  - Each of the six ops, plus missing-parameter and unknown-op refusals.
  - Select label to byte. Balance chooses frames. The bypass select is the same command as the op.
    The away switch.
  - Durations: no frame, range refusal, minutes to seconds.
  - The Auto preset's order and delay. Fan speed, off = Away, preset labels, resume level.
  - NULL arguments.
- `test/test_wire/test_wire.cpp` uses the value/params types and calls `from_op` for an end-to-end
  op-body check.
- `test/test_mqtt_inbound/test_mqtt_inbound.cpp` exercises it through `comfoair_mqtt_decide`.
- Not covered on the host: actual transmission, the 1000 ms wait, and the unit's reaction (all in
  `comfoair_control`, which needs hardware). Fractional NUMBER values (they are truncated) have no
  explicit test.

## Notes

- **Boost treats 0 differently.** For `boost`, `duration_seconds <= 0` cancels. For bypass,
  balance and away, the same values mean indefinite. This is the only way the op can end a boost
  early. A consumer that uses -1 as an "unset" marker gets "forever" on three of the four ops.
- **The "Away" preset is not away mode.** It is fan level 0. Away *mode* has its own command and
  countdown (`away_mode`, `set_away`).
- **The fan accepts preset labels** because its own metadata advertises them. An entity that
  advertises an option and then refuses it would be a 422 the API brought on itself.
- **`set_bypass` with `mode:"auto"` still requires `has_duration`**, even though the release
  ignores it. Every op parameter is mandatory.
- **`permanent_manual` defaults to true**, so a level set from Home Assistant holds. The trade-off,
  as the unit's own screen says: demand-controlled (sensor) ventilation is ignored while the unit
  is in manual mode.
