# comfoair_mqtt_inbound — the pure decision of what an arriving MQTT command means

> This is documentation, not code — re-read the source before relying on a detail.

A message on the broker is a topic, a payload and a retain flag. Turning that into "submit this
`comfoair_command_t`", "store this duration", "refuse, and here is why" or "not ours, say nothing"
is branching where a regression is silent: a command quietly stops being applied and nothing logs.
This component holds that branching as one pure function, `comfoair_mqtt_decide()`, that returns a
value and performs no effect, so `test/test_mqtt_inbound` can exercise it on the host. The acting
half (submit, flash, state model, counters) stays in `comfoair_mqtt`.

## Responsibility

Owns:
- The order of the early exits: foreign topic → IGNORE, then retained → REJECT.
- Payload shape per target: `ON`/`OFF` for the fan and switches, a strict number for fan speed and
  number entities, an option label for selects and the fan preset, a JSON body for operations.
- The fan-preset key rewrite (`<prefix>/fan/preset/command` addresses the `ventilation_mode` select).
- The resume-level rule: turning the fan on resumes the running speed when the fan is `known && on`.
- Which outcome a valid command is: SUBMIT (with or without an optimistic label) or STORE.
- The strict number parser `comfoair_mqtt_parse_number()`.

Does NOT do:
- Topic parsing / tree membership — `comfoair_ha_parse_topic()` (`comfoair_ha`).
- Writability, bounds, option-list matching, frame building — `comfoair_command_from_entity()` /
  `comfoair_command_from_op()` (`comfoair_command`). There is no second writability table here.
- Operation JSON parsing — `comfoair_wire_parse_operation()` (`comfoair_wire`).
- Submitting, storing durations, writing the state model, logging, counters, waking the browser —
  `comfoair_mqtt_handle_inbound()` in `components/comfoair_mqtt/comfoair_mqtt_command.c`.
- Copying the message out of esp-mqtt and refusing fragmented/oversized ones —
  `MQTT_EVENT_DATA` in `components/comfoair_mqtt/comfoair_mqtt_event.c`.
- Fetching durations or fan state — the caller snapshots them and passes them in.

## Public API

Header: `include/comfoair_mqtt_inbound.h`.

| Symbol | Contract |
| --- | --- |
| `comfoair_mqtt_inbound_t` | One message: `topic[192]` + `topic_len` (topic is NOT NUL-terminated — esp-mqtt hands pointer+length), `payload[512]` (IS terminated: the event handler copies into a zeroed struct and refuses anything that would fill it), `retained`. Owned by the caller; used as the FreeRTOS inbox element type in `comfoair_mqtt`. |
| `comfoair_mqtt_decision_kind_t` | `IGNORE` (not our tree — silent, never counted), `REJECT` (ours, refused, `reason` set), `SUBMIT` (submit `command`, then apply `text` if `optimistic_text`), `STORE` (store `command.duration_key`/`duration_minutes`, check the result, then write the state model). |
| `comfoair_mqtt_decision_t` | `kind`; `reason` (REJECT only, always a string literal, never payload bytes — it ends up in the log ring served by `GET /api/log`); `command`; `optimistic_text`; `key[64]` (copy, set on SUBMIT/STORE); `text[32]` (copy of the option label when `optimistic_text`). Holds no pointer into `in`, so it may outlive the inbound buffer. |
| `comfoair_mqtt_decide(ctx, in, durations, fan)` | Pure; any task; no locks taken, nothing retained, nothing written outside the return value. NULL `ctx`, `in` or `durations` → IGNORE. NULL `fan` means "nothing reported". Returns by value (no `esp_err_t`); the caller switches on `kind` exhaustively. |
| `comfoair_mqtt_parse_number(text, out)` | `true` and `*out` only for a non-empty string made solely of `0-9 + - . e E` that `strtod` consumes fully to a finite value. Exposed so the test pins it directly. |

Constants and why:
- `COMFOAIR_MQTT_PAYLOAD_MAX 512` — operation JSON bodies; entity payloads are scalars.
- `COMFOAIR_MQTT_TOPIC_MAX 192` — the inbound topic buffer.
- `COMFOAIR_MQTT_KEY_MAX 64` — longest registry key is 33 bytes, longest op name 21. It is also
  the cap passed to `comfoair_ha_parse_topic`, which REFUSES a longer name rather than truncating,
  so this number decides which topics exist.
- `COMFOAIR_MQTT_LABEL_MAX 32` — derived: the longest option label in `registry_generated.h` is
  18 bytes ("Manual (permanent)"). A test sweeps every registry label against it. Do not raise it
  back to the payload size: the decision is returned by value onto a 4 KB task stack.

## Implementation

- `comfoair_mqtt_inbound.c` — the whole component: `blank()`, `refuse()`, `set_key()`,
  `comfoair_mqtt_parse_number()`, `comfoair_mqtt_decide()`.
- `include/comfoair_mqtt_inbound.h` — types, constants, contract.
- `CMakeLists.txt` — dependency rationale (see Dependencies).

Flow of `comfoair_mqtt_decide()`:
1. `comfoair_ha_parse_topic()` into a zeroed `name[64]` (it leaves `name` untouched for the three
   fan targets). `TARGET_NONE` → IGNORE.
2. `in->retained` → REJECT.
3. Build `effective` durations: copy `*durations`; if `fan && fan->known && fan->on`, override
   `fan_resume_level` with `fan->speed`.
4. `TARGET_OP`: `comfoair_wire_parse_operation()` (with stack `params` + `storage` that only live
   for the build) then `comfoair_command_from_op(name, …)` → SUBMIT, never optimistic.
5. Otherwise build a `comfoair_command_value_t`: `FAN_ON` (key `fan`, ON/OFF), `FAN_SPEED` (key
   `fan`, number), `FAN_PRESET` (key `ventilation_mode`, text), default/`TARGET_ENTITY` (key from
   the topic; `comfoair_entity_by_key()` → SWITCH ON/OFF, NUMBER strict number, else text).
6. `comfoair_command_from_entity()`; non-OK → REJECT (this is where read-only and unknown entities,
   out-of-range numbers and unlisted labels end).
7. `command.stores_duration` → STORE (no frame on the bus).
8. Text value → copy the (now registry-validated) label into `text`, set `optimistic_text`;
   a label that would not fit is refused rather than truncated (unreachable today). → SUBMIT.

Invariants:
- Every REJECT is built from a zeroed struct (`refuse()`), so it never carries a half-filled key/label.
- The only copies are `key` and `text`; the 512-byte payload is read in place, never copied.
- Optimistic text is set for every text-valued entity SUBMIT (selects and the fan preset), never for
  switches, numbers or operations.

Concurrency: none. No globals, no locks; reentrant. In the firmware it runs on `comfoair_mqtt`'s
publisher task. The decision struct is ~168 bytes and the function frame ~352 bytes on riscv32
(measured per the header) on a 4 KB stack that also holds a 1536-byte publish buffer.

DO NOT:
- Swap the IGNORE and retained checks: a shared broker is mostly other devices' retained messages,
  and refusing them inflates this device's rejection counter.
- Drop either half of `parse_number`: the character sweep alone misses `1e400` (overflow to inf),
  `isfinite` alone misses `0x10`, ` 2`, `nan`/`inf`. `strtod("hello")` is 0 = the AWAY level; NaN
  passes every range check and casts to 65535 minutes on riscv32.
- Turn `text`/`key` back into pointers into `in`: the inbound struct is a reused queue slot.
- Add `ESP_LOG*`, `comfoair_control_*`, `comfoair_state_set_*` or counters here; the decision would
  stop being inspectable by a host test.
- Add a writability check here; `comfoair_command` is the one place that answers it.
- Move this file back into `comfoair_mqtt` with a per-source filter (see Notes).

## Dependencies

- `REQUIRES comfoair_ha` (topic parser, `comfoair_ha_ctx_t`), `comfoair_command`
  (`comfoair_command_t`, `comfoair_command_durations_t`, builders), `comfoair_state`
  (`comfoair_fan_state_t`, `comfoair_entity_by_key()` — a lock-free strcmp over the generated
  table), `comfoair_registry` (entity kinds).
- `PRIV_REQUIRES comfoair_wire` — only the `.c` includes `comfoair_wire.h`; consumers do not inherit it.
- Deliberately not required: `mqtt`, `comfoair_mqtt`, `comfoair_control`, `esp_log`.
- Depended on by `comfoair_mqtt` (`comfoair_mqtt_command.c` `comfoair_mqtt_handle_inbound()`,
  `comfoair_mqtt_event.c`
  for the inbound struct and `COMFOAIR_MQTT_PAYLOAD_MAX`).

## Tests

`test/test_mqtt_inbound/test_mqtt_inbound.cpp` (host, `pio test -e native -f test_mqtt_inbound`)
covers: retained refusal vs. live delivery; foreign tree ignored (also when retained); switch and
fan ON/OFF; fan speed numeric; `parse_number` rejects/accepts tables; non-finite payloads on fan
speed and durations; STORE for durations (no frame, minutes, registry range refusal); select SUBMIT
with optimistic label and its frame; label is a copy, not an alias; fan preset → `ventilation_mode`
with the three-frame "Auto" sequence; every registry label fits `LABEL_MAX`; 511-byte payload
refused on every path; unknown and read-only entities refused; unreadable operation bodies refused;
the resume-level rule (running / off / unreported / NULL fan); NULL arguments → IGNORE.

Not covered here: the acting half (`comfoair_mqtt_handle_inbound()`'s submit-then-optimistic and
store-check-then-set ordering) and the esp-mqtt event copy in `comfoair_mqtt_event.c` — impure glue
with no host suite.

## Notes

- **Its own component, not a filtered source in `comfoair_mqtt`.** `[env:native]` builds via
  PlatformIO's dependency finder, which works at component granularity; a half-host-buildable
  component would need a per-source filter nothing points at.
- **Retained commands are refused because the broker replays them** on every reconnect and boot:
  one `mosquitto_pub -r` would re-apply forever. A retained publish that arrives while subscribed is
  delivered with the retain flag cleared (MQTT 3.1.1 §3.3.1.3) and is applied once — only the
  replay is blocked.
- **Why `known && on` for the resume level:** an off fan still reports speed 1 (it cannot show 0);
  using that would downgrade a house that was on High when the fan is switched on.
- **Why selects are written optimistically:** the three RMI-read selects (`temperature_passive`,
  `humidity_comfort`, `humidity_protection`) get the unit's value back only at the next settings
  read (up to 6 h), so without the echo HA would show the pre-write value until then; selects with
  a PDO are overwritten by it within seconds. The caller applies the label only after the submit
  succeeds, and `comfoair_state_set_option()` silently declines a label the registry does not list.
- **The fan preset rewrite matters:** `comfoair_command_from_entity("fan", …)` also accepts the
  preset labels but builds a different command, so losing the rewrite would fail silently.
- **Durations produce no frame:** the unit is only told a duration as part of a command, so a duration
  write is a local store; submitting it would send nothing and report success.
