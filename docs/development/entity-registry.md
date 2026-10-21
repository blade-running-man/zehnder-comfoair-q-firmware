<script setup>
import { data } from '../reference/entities.data'
</script>

# Entity registry

Every entity the firmware exposes — over REST, over MQTT, in Home Assistant discovery and in the
web UI — comes from **one** table. There is no second, hand-written entity list anywhere in the
tree, and there must never be one: a second list is how four surfaces end up disagreeing while
each of them still agrees with itself.

## The pipeline

```text
tools/pdo_table.py                    the one table (pure Python data)
tools/entity_names_i18n.py            DE/NL/UK display names, keyed by entity key
        │
        ▼
tools/generate_registry.py            flattens the table into one entity list
  ├─ tools/render_c.py                ─► components/comfoair_registry/include/registry_generated.h
  ├─ tools/render_ts.py               ─► web/src/api/entities.ts
  └─ tools/render_entity_names_ts.py  ─► web/src/i18n/entityNames.generated.ts
```

- **`tools/pdo_table.py`** declares every entity as data: numeric, binary and enum PDOs; the
  airflow-constraints special; the unit's active errors (`UNIT_ERRORS`); the selects, numbers, the away switch and the fan; the schema
  version. It never imports anything from a build system, so the Python tests can load it on a
  bare interpreter. Its keys are what the Home Assistant integration builds on (see below).
- **`tools/generate_registry.py`** turns those table shapes into one flat list of entities.
  Flattening happens there and only there, so the C and TypeScript renderers walk the same list
  and cannot drift by one of them learning a table shape the other does not. It also checks that
  no key appears twice.
- **The one row it declares itself** is `data_freshness`: no PDO reports it, because what it
  reports is this node still talking. It leaves through the same outputs as every other row, so
  there is still one list. Its publication period (300 s) and Home Assistant expiry (900 s) are
  emitted side by side with a `static_assert` binding them.
- **`tools/render_c.py`** writes `registry_generated.h`: the `comfoair_entity_t` array with
  `COMFOAIR_ENTITY_COUNT`, option tables, and `COMFOAIR_REQUEST_PDOS` — the PDOs the poller
  requests, derived from the table. The state model, the REST projection, the command layer, the
  poller, MQTT inbound handling and the Home Assistant discovery payloads all read this header.
- **`web/src/api/entities.ts`** (written by `tools/render_ts.py`) is the same list for the web UI: `ENTITIES` (each row's key,
  name, kind, PDO, unit, device class, icon, entity category, options, writability and bounds),
  the derived `EntityKey` and `EntityKind` types, `FAN_PRESET_MODES` and `SCHEMA_VERSION`. The key
  union is derived from the array, never listed a second time.
- **`web/src/i18n/entityNames.generated.ts`** is the display name of every entity in English,
  German, Dutch and Ukrainian, for the web UI's language switcher. English is the table's name —
  the one Home Assistant sees through discovery. The other three come from
  `tools/entity_names_i18n.py`; nothing of them reaches the firmware, so Home Assistant stays
  English whatever the UI shows.

The [Entities](/reference/entities) reference page is produced from `entities.ts` at site build
time, so it is an output of the same pipeline, not a copy.

### The translation overlay cannot drift

`entity_names_i18n.py` is a second table keyed by entity key — exactly the shape that drifts. It
does not, because `render_entity_names_ts.py` refuses to generate unless the overlay's key set is
exactly the registry's: no missing key, no orphan, no empty name. That refusal fails the firmware
build. The overlay has no English column on purpose: a second English name would be a second,
silently divergent entity list.

## When it runs

Generation is part of every firmware build: `tools/regenerate.py` is a PlatformIO `pre:` hook
(`extra_scripts` in `platformio.ini`) that runs the generator before anything compiles. A stale
registry would not fail to build — it would build and ship entities that disagree with the table —
so it is regenerated every time rather than trusted.

It can also be run by hand:

```sh
python3 tools/generate_registry.py           # write the three outputs
python3 tools/generate_registry.py --check   # exit non-zero if any output is stale
```

The outputs are committed, and each carries a `GENERATED … DO NOT EDIT` banner. Fix the table or
the generator, never the output — a hand edit is reverted by the next build.

## Adding an entity

1. **Add the row** to the matching tuple in `tools/pdo_table.py` (`NUMERIC`, `BINARY`, `ENUM`,
   `SELECTS` or `NUMBERS`), with its PDO, signedness and divider where it has one. Widths are
   deliberately absent: the decoder takes the width from the CAN data-length code at runtime.
   Polled PDOs are derived from the table, so a new PDO-backed row is requested automatically.
   `enabled_by_default=False` creates the entity disabled: discovery carries `en:false` and
   `/api/entities` `enabledByDefault:false`; it still exists and can be enabled. Set it for a row
   that duplicates another or is a raw or unverified diagnostic (`ventilation_mode` is the fan's
   presets a second time). A row the base unit does not send does not belong in the table at all.
   A binary row is `eq`, `ne`, or `bit` (true while bit `value` of the PDO is set); the
   generator refuses any other op, a bit outside 0..63 and a PDO above 8191. The switch takes the
   same `op` (`eq` or `bit`, no `ne`) and goes through the same check: `away_mode` is bit 11 of
   PDO 337, the away timer its command starts and cancels.
2. **Add its three translations** to `tools/entity_names_i18n.py` under the same key. The build
   fails until you do.
3. **Regenerate**: `python3 tools/generate_registry.py`, or just build.
4. **Writable entities need behaviour, not just a row.** What a select, number or switch sends
   to the unit is decided in the command layer (`components/comfoair_command/`), against the RMI
   reference. The registry says that an entity is writable; the command layer says what writing
   it means.
5. **Run the gates**: `python3 -m pytest tests/python -q` and `pio test -e native`.
   `tests/python/test_registry.py` pins the contract counts (58 values, 13 controllable
   entities, 60 polled PDOs) and the frozen names; a deliberate change to the counts is a deliberate change to
   that test.
6. **Mind the integration.** A purely additive entity does not bump `SCHEMA_VERSION` in
   `pdo_table.py`; removing or renaming one is a breaking change (see below).

The table has {{ data.total }} entities today: the 58 values, the 13 controllable entities, the two
unit-error entities below, `airflow_constraints`, `schema_version` and `data_freshness`.

## The unit's active errors

No PDO carries the unit's errors: the probe task reads the ERROR unit's 32-byte bitset over RMI
(`80 03 01`, GETACTIVEERRORS) and `comfoair_sysinfo` names the ids with the table selected by the
unit's firmware. `pdo_table.py` declares their two faces as `UnitErrorSpec` rows in `UNIT_ERRORS`:

| Key | `form` | Generated kind | HA component | What it carries |
| --- | --- | --- | --- | --- |
| `unit_error` | `any` | `binary` | `binary_sensor`, `dev_cla: problem` | on while an error that is not merely informational is active (a manual bypass, service mode or a filter reminder never raises it) |
| `active_errors` | `text` | `special` | `sensor`, diagnostic | every active id's text, informational ones included, in one line of at most 255 bytes (Home Assistant's state limit); `No errors` when none is |

They are generated as **existing kinds** on purpose — a binary and a special text — so every surface
that switches on the kind (discovery, REST, the web UI, the integration's platform map) already
places them, with no new kind to teach each one. The generator refuses a table without exactly one
row of each form (`check_unit_errors()` in `tools/registry_checks.py`), and `render_c.py` emits their
positions as `COMFOAIR_UNIT_ERROR_ANY` and `COMFOAIR_UNIT_ERROR_TEXT`: the probe task writes them
through `comfoair_state_set_unit_errors()` by these indices, never by key lookup. Both are unknown
(`null` / `None`) until an errors read has answered and the unit's firmware word is known — see
[The unit read](/protocol/probe#the-unit-read). They were added without a `SCHEMA_VERSION` bump,
which the rule in step 6 above allows.

## MQTT publication rate

Two optional fields on `NumericPdo` limit how often a jittery row is published **over MQTT only**:

| Field | Meaning |
| --- | --- |
| `publish_deadband` | a change smaller than this, in the published unit (after the divider), is held. `None` (the default): every change goes out. Never 0. |
| `publish_min_interval_s` | the shortest gap, in seconds, between two publications of the row. 0 (the default): no gap. Below the max hold. |

Both are generated into `comfoair_entity_t` (`publish_deadband` is `NAN` when unset), together with
`COMFOAIR_PUBLISH_MAX_HOLD_S` (300 s), after which any held difference goes out. The generator
refuses a deadband ≤ 0 and an interval at or past the max hold (`check_publish_rate()`). Only
numeric rows have the fields: an enum, a binary, a select or a switch moving *is* the state change.
The rule that applies them is `components/comfoair_publish_rate`; the values per row and why are on
[MQTT, Publication rate](/interfaces/mqtt#publication-rate) and pinned in
`tests/python/test_registry_publish_rate.py`.

## Keys the Home Assistant integration depends on

The Home Assistant integration (`zehnder-comfoair-q-ha`) reads the device over the REST API and
`/ws`. An entity's `key` is its stable id — API path, MQTT topic and discovery `object_id` alike —
while its `name` is display text that nothing keys on.

The integration looks these up **by key**, and renaming one breaks it:

| Key | PDO | What the integration does with it |
| --- | --- | --- |
| `next_fan_change_in_raw` | 81 | the time the fan mode next changes |
| `next_bypass_change_in_raw` | 82 | the time the bypass mode next changes |
| `next_supply_change_in_raw` | 86 | the time supply-only next changes |
| `next_exhaust_change_in_raw` | 87 | the time extract-only next changes |
| `filter_replacement_remaining_days` | 192 | the filter replacement date and its alert |
| `schema_version` | — | the version check below |

The four countdowns are `null` while nothing is pending; the firmware suppresses the unit's
all-ones sentinel. PDO 192 is days at the current airflow, not a due date — the unit shortens it at
above-nominal airflow and labels it `DAYS LEFT - INDICATIVE`.

**`schema_version`** is a `diagnostic_const` entity in the state document, the version of the
entity table (`SCHEMA_VERSION` in `pdo_table.py`). Removing or renaming an entity bumps it; adding
one does not, so both sides must tolerate entities and fields they do not recognise. The
integration checks it at setup and tells the owner plainly when the version is outside the range it
supports.

## Retiring keys

Removing an entity bumps `SCHEMA_VERSION` (above). It also leaves retained messages on every
broker the device has used. The firmware publishes, and withdraws, only the keys it still has:
the withdraw sequence walks the current `COMFOAIR_ENTITIES`. So a removed key keeps:

- `<prefix>/<key>/state`;
- if discovery was ever on, `homeassistant/<component>/<device_id>/<key>/config`.

`<component>` is the one the key had: `sensor` for numeric, enum and special rows, and
`binary_sensor` for binary rows (`comfoair_ha_component()`). A key that kept its name but changed
its kind leaves its OLD config topic behind and keeps its state topic. In Home Assistant the new
kind is a new entity with a new entity id, and the entity of the old kind is left orphaned;
automations and dashboards that named it must be pointed at the new one. The CHANGELOG entry of
the release that changes a key's kind names the key and both topics.

The firmware does not clear these, and it carries no generated list of retired keys to clear on
every connect: that would be more code for what is a one-time step per release. The cleanup is
manual, once per broker, after the new firmware is running. If the old firmware is
still connected, it would republish the keys.

1. Inventory: `mosquitto_sub -h <broker> -u <user> -P '<password>' -v -W 5 --retained-only -t '<prefix>/#' -t 'homeassistant/+/<device_id>/#' > retained-before.txt`.
2. Config topics first, so Home Assistant drops the entity before its state goes. For each
   retired key: `mosquitto_pub -h <broker> -u <user> -P '<password>' -r -n -t 'homeassistant/<component>/<device_id>/<key>/config'`.
   An empty retained publish deletes the retained message, and it is harmless on a topic that
   has none.
3. Then the state topics: `mosquitto_pub … -r -n -t '<prefix>/<key>/state'`. Skip a key that
   only changed kind.
4. Inventory again. No retired key may remain: `grep -E '/(<key1>|<key2>|…)/' retained-after.txt` prints nothing.
5. In Home Assistant with the integration, the retired keys' entities read "no longer provided".
   Remove them on the device page until the integration removes orphans itself.

`CHANGELOG.md` lists the retired keys, with their component, under the release that removed them.

## Why the device config types are hand-written

Not everything in `web/src/api/` is generated. The **device configuration** — Wi-Fi, broker,
discovery flag, device name, web password — is not an entity: `pdo_table.py` knows nothing about
it, and it must never be generated from there. Its types (`DeviceConfig`, `ConfigPatch`,
`ProvisionRequest` and their neighbours) are hand-written in `web/src/api/client.ts`, beside the
calls that carry them; status, probe, OTA and system documents likewise have hand-written modules
of their own.

The generator writes exactly two files under `web/src/` and is aimed at nothing else: pointing it
at a hand-written module would overwrite it on the first run. Older documents in this repository
sometimes name a `types.ts` for the config types; that file no longer exists, and `entities.ts`
is the only generated API module.
