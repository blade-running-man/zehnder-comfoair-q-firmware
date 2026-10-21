# comfoair_registry — the generated entity table every surface of the firmware reads

> This is documentation, not code — re-read the source before relying on a detail.

A header-only ESP-IDF component that carries the firmware's single list of entities as C data:
`include/registry_generated.h`, written by `tools/generate_registry.py` (body in
`tools/render_c.py`) from `tools/pdo_table.py`. The state model, REST, MQTT, Home Assistant
discovery, the command layer and the PDO poller all iterate this one array, so an entity exists in
every transport or in none. The same generator run writes the web UI's `web/src/api/entities.ts`
(body in `tools/render_ts.py`) from the same flattened list, which is why the C and TypeScript projections cannot drift.

## Responsibility

Owns:
- The entity descriptor type `comfoair_entity_t` and the kind enum `comfoair_entity_kind_t`.
- The table `COMFOAIR_ENTITIES[COMFOAIR_ENTITY_COUNT]` and the per-entity option/value arrays.
- The PDO request list `COMFOAIR_REQUEST_PDOS[COMFOAIR_REQUEST_PDO_COUNT]`.
- `COMFOAIR_SCHEMA_VERSION` and the freshness-entity constants (`COMFOAIR_FRESHNESS_*`).
- The two unit-error indices (`COMFOAIR_UNIT_ERROR_ANY`, `COMFOAIR_UNIT_ERROR_TEXT`) and the MQTT
  publication-rate bound (`COMFOAIR_PUBLISH_MAX_HOLD_S`).

Does NOT do:
- Lookup by key — `comfoair_entity_by_key()` lives in `comfoair_state` (`comfoair_state.h`).
- Hold values, availability or change tracking — `comfoair_state`.
- Decode PDO payloads — `comfoair_protocol` (`pdo_decode.h`); the registry declares only
  signedness and divider, never a width (the decoder takes width from the CAN DLC at runtime).
- Send the PDO requests — `comfoair_poll` walks `COMFOAIR_REQUEST_PDOS`.
- Judge commands (bounds, writability decisions) — `comfoair_command`.
- Shape discovery documents or topics — `comfoair_ha`; publishing — `comfoair_mqtt`.
- Carry device configuration (Wi-Fi, broker, auth) — that is not an entity; its web types are
  hand-written in `web/src/api/client.ts`.

## Public API

Everything is in `include/registry_generated.h`. All of it is `static const` data: no functions,
no state, no locking, safe to read from any task; nothing returns an `esp_err_t`.

| Symbol | Contract |
| --- | --- |
| `comfoair_entity_kind_t` | `NUMERIC, BINARY, ENUM, SPECIAL, SELECT, NUMBER, SWITCH, FAN, DIAGNOSTIC_CONST, FRESHNESS`. Consumers switch on it; host builds use `-Werror=switch`, so a new kind fails every host-built switch that lacks its `case`. |
| `comfoair_entity_t` | One row. `key` is the stable id (API path, MQTT topic, discovery object_id); `name` is display only, nothing keys on it. `pdo` is `-1` when no PDO carries the entity. `is_signed` (all-ones on a signed field is a real `-1`), `divider`, `none_when_all_ones` (set on the countdown rows the table flags, where an unsigned all-ones value means nothing is pending). `match_value`/`match_negated`/`match_bit` for binary/switch comparisons (`-1` when unused; negated = table `op == "ne"`; `match_bit` = table `op == "bit"`, and then `match_value` is a bit index — PDO 337's boost and away timer bits, PDO 338's bypass override on bit 1, and the `away_mode` switch on bit 11). `enabled_by_default` is `false` for a row Home Assistant creates disabled (discovery `en:false`, `/api/entities` `enabledByDefault`) — the rows the table marks: the raw, duplicate and unverified rows and the `ventilation_mode` select. HA metadata strings (`unit`, `device_class`, `state_class`, `icon`, `entity_category`) are `NULL` when absent — never `""`. `options`/`option_count` for enum labels, select options and fan presets; `option_values` holds enum wire values and is `NULL` for non-enums. `min_value`/`max_value`/`default_value` are `NAN` when the table gives none. `publish_deadband` (`NAN` = none, in the published unit after the divider) and `publish_min_interval_s` (0 = none) are the MQTT-only publication rate, set on numeric rows alone (`NumericPdo` in `pdo_table.py`); REST and the WebSocket ignore them. |
| `COMFOAIR_ENTITY_COUNT` | Every row of `pdo_table.py` plus exactly one synthetic row (`data_freshness`). Sizes the state arrays in `comfoair_state.cpp`. |
| `COMFOAIR_ENTITIES[]` | The table, in generator order (numeric, binary, enum, `airflow_constraints`, `unit_error`, `active_errors`, selects, numbers, `away_mode`, `fan`, `schema_version`, `data_freshness`). Index `i` is the same entity in every consumer's arrays. |
| `<key>_options[]`, `<key>_option_values[]` | Emitted at file scope, named after the entity key. Order is contract-visible: for the RMI-read selects the option index IS the wire value the unit answers with. |
| `COMFOAIR_REQUEST_PDO_COUNT`, `COMFOAIR_REQUEST_PDOS[]` | The sorted PDO numbers (`pdo_table.ALL_REQUEST_PDOS`): every PDO an entity reads plus the extra fan/balance PDOs and the select feedback PDOs. |
| `COMFOAIR_RMI_READBACK_COUNT`, `COMFOAIR_RMI_READBACK[]` | `3` rows `{entity, unit, subunit, property}`: the selects with no PDO whose value the probe task reads over RMI GET ONE (`temperature_passive` 0x1D/0x01/0x04, `humidity_comfort` …/0x06, `humidity_protection` …/0x07; `pdo_table.py SelectSpec.rmi`). |
| `COMFOAIR_UNIT_ERROR_ANY`, `COMFOAIR_UNIT_ERROR_TEXT` | The indices of `unit_error` (`BINARY`, `problem`) and `active_errors` (`SPECIAL`, diagnostic), the two faces of the unit's active errors (`pdo_table.py UNIT_ERRORS`). No PDO carries them; `comfoair_state_set_unit_errors()` writes them by these indices. The generator refuses a table without exactly one row of each form. |
| `COMFOAIR_PUBLISH_MAX_HOLD_S` | `300`: a rate-limited row's held **difference** goes out at the latest this long after the row's last publication; never a repeat of an unchanged value. Read by `comfoair_publish_rate`. |
| `COMFOAIR_SCHEMA_VERSION` | `2` (the base-unit table); published as the value of the `schema_version` (`DIAGNOSTIC_CONST`) entity. |
| `COMFOAIR_FRESHNESS_KEY` | `"data_freshness"` — the only spelling of that key C code may use. |
| `COMFOAIR_FRESHNESS_PERIOD_S` / `_EXPIRE_S` | `300` / `900`, followed by a `static_assert(EXPIRE == 3 * PERIOD)`. |

Value semantics worth knowing: no `SELECT` has a `default_value` (all `NAN`; a select reads
unknown until a PDO, a derivation or an RMI read gives it a value); for `NUMBER` it is the value
itself (the three durations 60, `away_duration` 0, whose real `min_value` 0 must survive).
13 rows are `writable`: 7 selects, 4 numbers, `away_mode`, `fan`.

## Implementation

- `include/registry_generated.h` — the generated output; banner says DO NOT EDIT.
- `CMakeLists.txt` — `idf_component_register(INCLUDE_DIRS "include")`; no sources, no REQUIRES.
- Generation: `tools/regenerate.py` is a PlatformIO pre-build hook that runs
  `tools/generate_registry.py` on every build and aborts the build if it fails;
  `--check` mode exists for staleness checks. The native env reaches the header through
  `-I components/comfoair_registry/include` in `platformio.ini`.
- The header is C11/C++11-dual: `extern "C"`, `<assert.h>` for `static_assert` (a macro in C11, a
  keyword in C++), `<math.h>` for `NAN`.

Invariants:
- Keys are unique (the generator exits on a duplicate).
- `COMFOAIR_ENTITY_COUNT` equals rows emitted; table rows + 1.
- Write-only secrets (`mqtt_password`, `web_password`, `wifi_password`) never appear here.

DO NOT:
- Hand-edit `registry_generated.h`. It is the second entity list the project forbids, and the
  pre-build hook silently overwrites it on the next build. Fix `tools/pdo_table.py` or the
  renderer.
- Write `#define COMFOAIR_FRESHNESS_EXPIRE_S (3 * COMFOAIR_FRESHNESS_PERIOD_S)`. It reads tighter
  and turns the `static_assert` into a tautology; two literals are what make separating them a
  build failure in every translation unit.
- Spell `"data_freshness"` in any hand-written `.c/.h/.cpp`; use `COMFOAIR_FRESHNESS_KEY`
  (`tests/python/test_registry.py` scans `components/` for the literal).
- Add the freshness row to `pdo_table.py`. That file holds only what the bus reports; the
  synthetic row is declared in `generate_registry.py`.
- Replace `NAN` defaults/bounds with `0`. REST would answer "min 0 max 0" for entities with no
  range while the TypeScript projection says `null`; consumers test "is NaN", not "is zero".
- Rely on a row's address across translation units. The table is `static const`, so every TU
  that includes the header has its own copy; `&COMFOAIR_ENTITIES[i] == e` is only valid when `e`
  came from the same TU (as in `comfoair_state.cpp`). Compare by `key` or by index instead.
- Reuse an identifier like `fan_options` or `device_state_options` in a TU that includes this
  header — the per-key arrays are file-scope names and would collide.
- Add a width field. Width comes from the CAN DLC at decode time.

## Dependencies

Requires nothing (C standard headers only).

Depended on by (`REQUIRES comfoair_registry`): `comfoair_state` (re-exports it via
`comfoair_state.h`), `comfoair_api`, `comfoair_command`, `comfoair_poll`, `comfoair_ha`
(re-exports it via `comfoair_ha.h`), `comfoair_mqtt_inbound`. `comfoair_mqtt` reaches it
transitively through `comfoair_ha` and iterates `COMFOAIR_ENTITIES` for publishing and discovery.

## Tests

No suite of its own; the data is exercised by:
- `tests/python/test_registry.py` (pytest, run in CI) — the generator against `pdo_table.py`:
  count = table rows + 1, freshness row declared only by the generator and not hand-spelled in
  `components/`, period/expiry adjacency and the `static_assert`, request list equals
  `ALL_REQUEST_PDOS`, enum and fan-preset order preserved, frozen `next_*_change_in_raw`
  names (`name == key`), no secrets in C or TS output.
- Host Unity suites that iterate the table: `test/test_state`, `test/test_api`,
  `test/test_command`, `test/test_ha`, `test/test_ha_cleanup`, `test/test_ha_availability`,
  `test/test_ha_freshness` (pins 300/900 and `COMFOAIR_ENTITY_COUNT`), `test/test_ha_enabled`,
  `test/test_state_bits`, `test/test_state_unit_errors`, `test/test_ha_none`,
  `test/test_publish_rate`, `test/test_mqtt_inbound`.
- `tests/python/test_registry_unit_errors.py` (the two rows, their kinds and indices, 76 entities)
  and `tests/python/test_registry_publish_rate.py` (the rate fields reach the header, the per-row
  values, the generator's refusals).

Not covered: `comfoair_poll`'s walk of `COMFOAIR_REQUEST_PDOS` has no host suite; nothing
checks the C header and `entities.ts` against each other at runtime (they agree because one
flattened list feeds both renderers).

## Notes

- **Why a header with `static` data, not a `.c` with `extern` declarations.** The component stays
  header-only and identical on host and device; the cost is a per-TU copy of the table and the
  pointer-identity trap above.
- **Why `NULL` and not `""` for absent metadata.** So a consumer can tell "no unit" from "the
  empty unit" and leave the field out of a REST or discovery document rather than emit `""`.
- **Why the freshness row is synthetic.** It reports that this node is still publishing, which
  no CAN frame carries. It is a `connectivity` binary sensor because that is the HA MQTT
  platform pairing that accepts `expire_after`; its arrival, not its value, is the signal.
  Declaring it in the generator keeps it in the same single output as every other entity, so
  REST, the state model, the web UI and discovery all know it — a hand-written discovery record
  in `comfoair_ha` would be an entity only Home Assistant knows.
- **Why 300 s / 900 s.** The point is noticing a house without power, an event nobody reacts to
  within minutes; 5 minutes is 288 messages a day versus 1440 at one minute. Expiry is three
  periods because HA's `expire_after` timer resets on each state message: two publications may be
  lost to a slow broker before the card goes unavailable.
- **Why five selects have `pdo = -1`.** No single PDO carries them. `balance_mode` and
  `ventilation_mode` are derived in `comfoair_state` from two PDOs each; the three
  comfort/protection selects are read over RMI (`COMFOAIR_RMI_READBACK[]`). None has a default:
  each reads unknown until its derivation or first read gives it a value. The four duration
  `NUMBER`s have no PDO at all — a duration is only told to the unit inside a command.
