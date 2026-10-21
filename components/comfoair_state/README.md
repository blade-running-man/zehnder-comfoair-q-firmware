# comfoair_state — the single state model: one value per entity, fed by PDO frames

> This is documentation, not code — re-read the source before relying on a detail.

`comfoair_state` holds the current value of every entity in the generated registry
(`registry_generated.h`, from `tools/pdo_table.py`) and turns raw PDO payloads into those
values. It is the one model underneath all three transports: REST, MQTT and the WebSocket
push read from here, none of them keeps a copy, and none of them knows anything about CAN. It
also keeps per-consumer change marks so each transport can send deltas instead of full
documents. It is pure apart from the state lock (`comfoair_lock`, a no-op on the host), never
allocates, and is host-tested.

## Responsibility

Owns:
- One `comfoair_value_t` slot per registry entity (`g_values[COMFOAIR_ENTITY_COUNT]`, same index
  as `COMFOAIR_ENTITIES`).
- Applying one PDO frame to every entity that claims it (one-to-many: PDO 337 feeds three).
- The entities derived from PAIRS of PDOs: the fan (PDO 49 operating mode + PDO 65 level),
  `ventilation_mode` (mirrors the fan preset) and `balance_mode` (PDO 54 + PDO 55).
- Cold-start seeding: `schema_version`, the freshness entity, and every entity with a registry
  `default_value` (today the four durations only; no select has one).
- The setters for the writable entities no PDO carries: the four durations (optimistic) and the
  three RMI-read-back selects (fed by `comfoair_probe_task` and by accepted writes).
- Change marks and their generations, one row per consumer (PUSH, MQTT).
- `comfoair_entity_by_key()`, the key → registry-row lookup other components share.

Does NOT do:
- CAN reception, frame filtering (extended/RTR) or PDO id extraction — `src/main.cpp`'s CAN
  callback, over `comfoair_can`/`comfoair_protocol`.
- Raw byte decoding — `comfoair_protocol` (`pdo_decode.h`: `decode_numeric`, `decode_u64`;
  `airflow_constraints.h`).
- JSON rendering — `comfoair_api`; MQTT payloads and discovery — `comfoair_ha` / `comfoair_mqtt`.
- Sending commands, validating command ranges, persisting durations to NVS —
  `comfoair_command` / `comfoair_control`. `comfoair_state_set_number()` does not range-check.
- Waking the transports after a change — the caller does (`comfoair_http_notify_state_changed()`,
  `comfoair_mqtt_notify_state_changed()`).
- Bus-health / staleness verdicts — `comfoair_phase` (`comfoair_bus_health.h`).

## Public API (`include/comfoair_state.h`)

| Symbol | Contract |
| --- | --- |
| `comfoair_state_reset()` | Zeroes values, marks and derived inputs, then seeds defaults. Must run once at boot (`main.cpp` does, after `nvs_init()`); without it every defaulted entity reads null for ever. Does NOT zero generations and does NOT mark the seeded values changed. |
| `comfoair_state_apply_pdo(pdo, data, len)` | Called from the CAN task. Applies the frame to the derived entities and to every registry entity with `e.pdo == pdo`, marking only slots whose VALUE changed. Returns false when nothing claims the PDO (or `data == NULL`) — a fact to log at DEBUG, not an error. |
| `comfoair_state_get(key, out)` | Copies one slot. false = no such key (or NULL arg); true with `type == COMFOAIR_VALUE_NONE` = known entity, no value yet. Callers must keep those apart (missing vs `null` in JSON). |
| `comfoair_state_get_copy(key, out, text, cap)` | `comfoair_state_get()` with a TEXT copied into the caller's `text` inside the same lock hold; `out->text` then points at `text` (labels too). false = no such key, a NULL argument, or `cap < COMFOAIR_STATE_TEXT_MAX + 1` (refused by size, never by the text stored today). For a reader that renders after unlocking -- the MQTT publisher: a double-buffered text survives only ONE later write. REST and the WebSocket render under the lock and use `comfoair_state_get()`. |
| `comfoair_state_get_fan(out)` | Copies the fan struct. Returns true for any non-NULL `out`; `out->known` says whether PDO 65 has been seen. |
| `comfoair_state_set_option(key, label)` | Select write, from an accepted command or from the probe task's RMI read-back. Refuses (false) a non-writable entity, one without options, or a label the registry does not list. Stores the REGISTRY's pointer, never the caller's. Marks changed only if the label differs. Does NOT refuse PDO-backed selects (see Notes). |
| `comfoair_state_set_number(key, value)` | Optimistic number write. Refuses a non-writable key and any entity a PDO carries (`e->pdo >= 0`). Used for the four durations (`main.cpp` from NVS at boot; HTTP/MQTT after a command). |
| `comfoair_state_set_unit_errors(any, summary)` | The probe task's answered GETACTIVEERRORS read, into `COMFOAIR_UNIT_ERROR_ANY` / `COMFOAIR_UNIT_ERROR_TEXT` (generated indices). Stores the flag as handed (the summary decides that informational ids do not raise it). Copies the text into the other half of a double buffer; marks each row only when its value changed (text by content). Refuses NULL and a text over `COMFOAIR_STATE_TEXT_MAX` (255, HA's state limit). Both rows stay NONE until the first call. |
| `comfoair_entity_by_key(key)` | Registry row for a key, or NULL. Pure strcmp walk, no lock. |
| `comfoair_state_changed(who, index)` | Has entity `index` changed (by value) since consumer `who` last cleared it. Out of range → false. Under the lock. |
| `comfoair_state_clear_changes(who)` | Clears ONE consumer's whole row (the PUSH renderer, after rendering a push frame, full or delta; a REST `GET /api/state` render does not clear). Generations untouched. |
| `comfoair_state_clear_change(who, index)` | Unconditional single clear. Safe only when nothing was read for that entity (MQTT uses it for entities with no HA component). |
| `comfoair_state_change_generation(who, index)` | Per-consumer, per-entity counter; rises on every marking, never falls. Out of range → 0. Sample it BEFORE reading the value. |
| `comfoair_state_clear_change_if(who, index, seen)` | Compare-and-clear in ONE lock hold. true = still at `seen`, mark cleared (a repeat call also returns true). false = a newer change arrived; the mark stays and the next pass publishes it. Do NOT retry the publish in place on false — the held value is stale. |

Types and constants:
- `comfoair_value_type_t`: `NONE` (0), `NUMBER`, `BOOL`, `TEXT`. "Not reported yet" and
  "nothing pending" (all-ones sentinel) are both `NONE` on purpose — both mean no value to show.
- `comfoair_value_t.text` points into registry storage or into a buffer this component owns (the
  airflow-constraints text rendered from PDO 230 and the unit's active-errors text, each
  double-buffered, so a held pointer survives ONE later write); never freed, and a caller's buffer
  only when `comfoair_state_get_copy()` filled it.
- `comfoair_fan_state_t { known, on, speed, preset }`: not flattened into `comfoair_value_t`
  because Home Assistant needs on/off, speed and preset, and preset can be absent while the
  other two are known. `speed` stays 1 when off (a fan cannot show speed 0); `preset` is NULL
  when the operating mode maps to no preset.
- `comfoair_state_consumer_t`: `COMFOAIR_STATE_CONSUMER_PUSH` (WebSocket delta),
  `COMFOAIR_STATE_CONSUMER_MQTT`, `_COUNT`. One mark row per consumer, because each consumer
  clears what it reads; a shared row lets whichever renders first steal the other's updates.

## Implementation

- `comfoair_state.cpp` — the core: the slots, `seed_defaults()`, the public
  readers/setters, and the core services the derived file calls (`set_text`, `option_of`,
  `mark_fan_changed`). The ONLY file that walks `COMFOAIR_ENTITIES` (see Notes).
- `comfoair_state_marks.cpp` — the change marks: `g_changed`, `g_generation`, `mark_changed()`,
  `marks_reset()` and the five public mark functions. Never touches `COMFOAIR_ENTITIES`.
- `comfoair_state_decode.cpp` — `apply_one()` / `same_value()`: one payload into one slot, per
  kind. Pure; decodes against the entity row it is handed.
- `comfoair_state_derived.cpp` — the fan, `ventilation_mode`, `balance_mode` and
  `comfoair_state_get_fan()`; owns the four derived inputs and `g_fan`.
- `comfoair_state_internal.h` — the private seam between the three (component root, not
  `include/`); every function in it expects `comfoair_lock()` already held.
- `include/comfoair_state.h` — the public contract.
- Storage: `g_values[N]`, `g_changed[2][N]`, `g_generation[2][N]` (uint32), N =
  `COMFOAIR_ENTITY_COUNT`, and the active-errors text pair `g_error_text[2]`, in the core; the
  four derived inputs `g_operating_mode/g_fan_level/g_supply_mode/g_exhaust_mode` initialised to
  `NOT_SEEN = -1000` (0 and 0xff are both legitimate readings) and `g_fan` in the derived file.
- `apply_pdo` flow: (1) decode the payload unsigned via `decode_numeric`; if OK, `apply_derived()`
  handles PDOs 49/65/54/55 (low byte) and recomputes fan / balance; (2) walk the registry and
  `apply_one()` every entity with that PDO, comparing before/after with `same_value()`. Both
  steps run — PDO 65 feeds the Fan Level sensor AND the fan, so an if/else would drop one.
- `apply_one()` per kind: `NUMERIC` → signed/all-ones-aware decode, divided by `divider`;
  decode failure or the all-ones sentinel (the countdown rows flagged `none_when_all_ones`) → `NONE`. `BINARY`/`SWITCH` →
  `raw == match_value` or, for `match_bit`, bit `match_value` of the raw value is set (`NONE` when
  the frame is too short to carry that bit — a false would be a guess); inverted if `match_negated`. `ENUM` maps a WIRE VALUE via
  `option_values` (`enum_label`); `SELECT` maps an INDEX into `options` (`option_at`) — they are
  different lookups (operating mode values are 1/5/6/11/255; PDO 66's byte is 0/1/2). Unlisted
  value or out-of-range index → `NONE`, never guessed or clamped. `SPECIAL` (PDO 230,
  `airflow_constraints`) → `decode_u64` (the bitset arrives in 7 bytes, which `decode_numeric`
  rejects), rendered into a pair of static buffers that alternate exactly when the text changes
  (see Notes). Other kinds carry no PDO; `active_errors` is `SPECIAL` too but has no PDO, and is
  written by `comfoair_state_set_unit_errors()`.
- `recompute_fan()`: needs only the level; operating mode 0xff→Auto, 11→Away, 6→Boost, 1/5
  (manual) → preset from level 0..3 (Away/Low/Medium/High). Preset labels come from
  `ventilation_mode`'s generated options by index (`option_of`), never literals, so a rename in
  `pdo_table.py` cannot desync. A non-NULL preset is mirrored into `ventilation_mode`; a NULL
  preset leaves that select on its last value. The fan is marked via every `KIND_FAN` entity.
- `recompute_balance()`: waits for BOTH 54 and 55 (one alone flip-flops); 54==1 → Supply Only,
  else 55==1 → Extract Only, else Balanced.
- `seed_defaults()`: `DIAGNOSTIC_CONST` → `COMFOAIR_SCHEMA_VERSION`; `FRESHNESS` → `true` for the
  life of the device (its MQTT arrival is the signal HA's `expire_after` reads; seeded so REST
  and the State page do not show "--" for ever); otherwise a non-NaN `default_value` → a number.
  Today that is the four `*_duration`s, which `src/main.cpp` then overwrites from NVS. There is no
  select branch: `temperature_passive`, `humidity_comfort` and `humidity_protection` have no
  default and read `NONE` until `comfoair_probe_task`'s RMI read-back (`COMFOAIR_RMI_READBACK`)
  answers. A Zehnder factory default used to be seeded for them; the owner's unit held different
  values, so the device published settings it had never read.
- Concurrency: every public reader/writer takes `comfoair_lock()` (recursive on the device, so
  `comfoair_api` can hold it across a whole document and still call `comfoair_state_get()`).
  Writers: the CAN task (`apply_pdo`), HTTP and MQTT tasks (setters). Readers: the WebSocket
  renderer and the MQTT publisher. `comfoair_entity_by_key()` takes no lock — the table is const.

DO NOT:
- Skip `g_generation++` when the mark is already set — that repeat is the only signal a second
  change arrived mid-publish.
- Zero `g_generation` in `reset()` or `clear_changes()` — a rewound counter can match a
  generation a publisher sampled earlier and erase a mark it never sent. Wrap after 2^32 is moot.
- Split `clear_change_if` into `change_generation()` + clear — the PDO lands in the gap.
- Mark a slot on every frame — the unit rebroadcasts constantly; "changed" means the value differs.
- Use the optimistic setters on an entity a PDO confirms (e.g. `away_mode`, PDO 337 bit 11): showing a
  state the unit refused is worse than showing the real one a second later.
- Store a caller's string in `text`, or hard-code option labels here.

## Dependencies

`REQUIRES comfoair_registry comfoair_protocol comfoair_lock` (CMakeLists.txt). Header includes
`registry_generated.h`; the sources include `pdo_decode.h`, `airflow_constraints.h`, `comfoair_lock.h`.

Used by: `src/main.cpp` (reset, apply_pdo, durations from NVS), `comfoair_api` (full/delta
documents, PUSH marks), `comfoair_http` (get, get_fan, setters), `comfoair_mqtt` /
`comfoair_mqtt_publish.c` (MQTT marks and generations, get, get_fan, setters, entity_by_key),
`comfoair_mqtt_inbound` and `comfoair_ha` (types and `comfoair_entity_by_key`).

## Tests

- `test/test_state/test_state.cpp` (599 lines, 38 cases): lookup, numeric/divider/sign, the
  countdown sentinel, binary, enum by value vs select by index, one-PDO-many-entities, unclaimed
  PDO, fan/preset/ventilation_mode mirroring, balance waiting for both halves, the durations'
  defaults, the read-back selects reading `NONE` after reset, schema_version, optimistic setters and their refusals, per-consumer marks, per-entity clear,
  generation compare-and-clear, bulk clear not rewinding generations.
- `test/test_state_bits/test_state_bits.cpp`: the `match_bit` path — PDO 337's boost (bit 6) and
  away (bit 11) rows, alone, together and under other timer bits, and an `eq` row untouched by it;
  and the `away_mode` switch on the same bit — on while timer 11 runs, off for the fan's Away
  preset (PDO 16 == 7 with bit 11 clear), unknown on PDO 16 alone or a frame too short for bit 11,
  and PDO 338's bypass override (bit 1).
  Its own suite because `test_state.cpp` is at the 600-line suite ceiling.
- `test/test_state_copy/test_state_copy.cpp`: `comfoair_state_get_copy()` -- refusals (unknown
  key, NULL, a cap below `COMFOAIR_STATE_TEXT_MAX + 1`), numbers and `NONE` as `get()` reads them,
  and the copied text (active errors, PDO 230, a registry label) pointing at the caller's buffer
  and surviving two later writes; no registry label longer than the copy holds.
- `test/test_state_special/test_state_special.cpp`: PDO 230's text — a second, different bitset
  is marked for both consumers, an unchanged one is not re-marked, a pointer read before a change
  still reads the old text, and an undecodable frame becomes `NONE` and is marked.
- `test/test_state_unit_errors/test_state_unit_errors.cpp`: `comfoair_state_set_unit_errors()` —
  both rows `NONE` after reset, the flag stored as handed (informational vectors arrive with
  `any == false`), marks only on a change (text by content), the refusals, and the 255-byte limit.
- `test/test_api/test_api.cpp` drives `apply_pdo`/`reset` through rendering, including PDO 230
  decoding to text.
- `test_ha`, `test_ha_availability`, `test_ha_freshness`, `test_ha_cleanup`, `test_mqtt_inbound`
  link it only for `comfoair_entity_by_key()` / `comfoair_fan_state_t`.

Not covered: real concurrency (the host lock is a no-op, suites are single-threaded); the boot
call order in `main.cpp`.

## Notes

- **Four cold-start defaults, and the count is load-bearing.** Only the durations carry a
  `default_value`. `comfoair_ha_payload.c` gives the `<prefix>/bus` availability to every entity
  with no default that is neither `DIAGNOSTIC_CONST` nor `FRESHNESS`: 70 bus-backed, 6 exempt (the
  durations, `schema_version`, `data_freshness`). Giving a select a default changes what HA greys
  out when the bus dies — and would seed it as a NUMBER, since `seed_defaults()` has no select
  branch. "No PDO" is not "no feedback": of the five PDO-less selects, `balance_mode` and
  `ventilation_mode` are derived from PDOs and the other three are read back over RMI.
- **`set_option` accepts PDO-backed writable selects** (`bypass_mode`/PDO 66,
  `temperature_profile`/PDO 67) — only `set_number` checks `pdo >= 0`. HTTP calls it after every
  accepted select command; MQTT only when its decision says `optimistic_text`. The next PDO
  corrects the value either way.
- **`set_option` takes a label, not an index**: an index from the wire would mean something
  different per build; a label is resolved against this build's registry.
- **Text change detection is pointer-based, so each owned text is a double buffer.**
  `same_value()` compares `text` by pointer. PDO 230's text moves to the other half of its pair
  exactly when the rendered text differs, so a changed text is a new pointer (marked) and an
  unchanged one keeps its pointer (not marked). One buffer rewritten in place compared equal to
  itself, and every change after the first went unmarked — MQTT and the WebSocket kept the boot's
  first text. The active-errors text keeps a pair of its own and is compared by content in its
  setter (the caller's buffer is not ours). Either way a pointer survives ONE later write; a reader
  that renders after unlocking (the MQTT publisher) uses `comfoair_state_get_copy()`.
- **Pointer identity is per translation unit — inside this component too.** `registry_generated.h`
  defines `COMFOAIR_ENTITIES` and the option arrays `static const`, so each TU has its own copy.
  That is why only `comfoair_state.cpp` walks the table: the decode file uses the row it is
  handed and the derived file asks the core for labels (`option_of`) and writes (`set_text`), so
  every stored label comes from one copy and the pointer compares in `set_text`/`same_value` hold.
  DO NOT add a `COMFOAIR_ENTITIES` lookup to either. Compare labels with `strcmp`
  outside this component, and do not compare `comfoair_entity_by_key()`'s result by address against
  another TU's `&COMFOAIR_ENTITIES[i]`.
- **Reset does not mark seeded values.** After `comfoair_state_reset()` both consumers' rows are
  clear; transports must send their full state on (re)connect rather than rely on marks.
