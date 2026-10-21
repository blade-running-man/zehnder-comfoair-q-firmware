# comfoair_ha — the MQTT topic tree and the Home Assistant discovery/state payloads, as pure functions

> This is documentation, not code — re-read the source before relying on a detail.

`comfoair_ha` answers three questions for the MQTT transport without ever touching a socket: what
a topic of this device is called, what goes into it (a discovery document or a bare state scalar),
and what an arriving topic addresses. It also enumerates exactly which retained topics this device
must empty when its identity changes or it leaves the broker. It is pure so that every discovery
document can be inspected by a host test instead of by flashing a board and watching a Home
Assistant instance; every discovery key was checked against Home Assistant's own validating schema,
because an invalid key does not produce an error, it produces an entity that silently never appears.

## Responsibility

Owns:

- The topic tree (flat, keyed by the registry `key`): `<prefix>/status`, `<prefix>/bus`,
  `<prefix>/<key>/state`, `<prefix>/<key>/command`, `<prefix>/fan/{state,speed/state,preset/state}`,
  `<prefix>/fan/{speed,preset}/command`, `<prefix>/ops/<name>/set`, and
  `homeassistant/<component>/<device_id>/<key>/config`.
- The registry-kind to HA-component mapping (`comfoair_ha_component()`).
- The retained discovery document per entity, and the state payloads (plain entities and the fan's three).
- The parser for an arriving topic (`comfoair_ha_parse_topic()`).
- The cleanup sequences (`REKEY`, `WITHDRAW`): which topics to empty, and in what order.

Does NOT do:

- Publishing, subscribing, the will, QoS/retain flags, retry or budgets. That is `comfoair_mqtt`
  (`comfoair_mqtt_publish.c`, `comfoair_mqtt_discovery.c`, `comfoair_mqtt_retract.c`,
  `comfoair_mqtt_client.c`), with the retraction budget in `comfoair_mqtt_backoff`.
- Deciding what an arriving command means or executing it. That is `comfoair_mqtt_inbound`, which
  only borrows the parser and `comfoair_ha_ctx_t` from here.
- Owning the entity list. That is the generated registry (`registry_generated.h` from
  `tools/pdo_table.py`); this component walks it and never keeps a list of its own.
- Holding or deriving values. `comfoair_state` supplies `comfoair_value_t` / `comfoair_fan_state_t`.
- Discovering the six parameterised operations. They are deliberately absent from discovery: an
  operation is a mode plus a duration in one atomic call, which HA's `select` (no parameter) and
  `button` (no body) cannot express. HA reaches them through `mqtt.publish` to `ops/<name>/set`.

## Public API

All functions are pure and reentrant, hold no state and allocate nothing; they may be called from
any task. Every `size_t` return is the number of bytes written (excluding the NUL), and **0 means
"nothing usable"**: NULL/partial context, an unknown enum value or key, or a result that does not
fit `cap`. A truncated topic is never returned, because a truncated topic is a different topic
that publishes successfully to nobody. Callers must treat 0 as "skip and log", not as an empty string.

| Symbol | Contract |
| --- | --- |
| `comfoair_ha_ctx_t` | Borrowed, not copied; every pointer must outlive the call. `device_id` = 12 lower-case hex of the full MAC (identity, never the name); `device_name` display only; `topic_prefix` e.g. `comfoair/aabbccddeeff`; `sw_version`; `mac` colon-separated for `cns` (empty omits); `url` must carry a scheme (`http://…/`) or HA rejects the whole device block (empty omits). A context missing `topic_prefix` or `device_id` makes every builder return 0. |
| `comfoair_ha_meta_topic(ctx, which, out, cap)` | Name of a device-level topic. The **only** source of `<prefix>/status` and `<prefix>/bus`; there is intentionally no `status_topic()` wrapper. |
| `comfoair_ha_state_topic(ctx, key, …)` | `<prefix>/<key>/state`, and only for a key `comfoair_entity_by_key()` knows; 0 otherwise. |
| `comfoair_ha_command_topic(ctx, key, …)` | `<prefix>/<key>/command`, deliberately **not** registry-checked: `comfoair_mqtt_discovery.c` passes `"+"` to build its subscription wildcard. |
| `comfoair_ha_fan_topic(ctx, leaf, …)` | The fan's own leaves by `comfoair_ha_fan_leaf_t`; the fan's on/off command is `<prefix>/fan/command` via `command_topic()`. |
| `comfoair_ha_op_topic(ctx, name, …)` | `<prefix>/ops/<name>/set`; `name` is the same one `POST /api/ops/<name>` uses. Also built with `"+"` for the subscription. |
| `comfoair_ha_discovery_topic(ctx, entity, …)` | `homeassistant/<component>/<device_id>/<key>/config`. Carries neither the prefix nor the name. |
| `comfoair_ha_cleanup_count(what)` / `comfoair_ha_cleanup_topic(ctx, what, index, …)` | The `index`-th topic to empty. Iterate `0 .. count-1`; a 0 return for one index (does not fit) must be logged and skipped. **Do not loop until 0.** Topics only: the caller publishes a zero-length retained payload. |
| `comfoair_ha_component(entity)` | HA component name, or NULL for an unpublished entity (in practice only NULL or an out-of-range kind). |
| `comfoair_ha_parse_topic(ctx, topic, topic_len, name, cap)` | Length-bounded parse (esp-mqtt's topic is not NUL-terminated). Returns a `comfoair_ha_target_t`; `name` gets the key or op name (untouched for fan targets). A name that does not fit `cap`, or contains `/`, yields `TARGET_NONE`. |
| `comfoair_ha_discovery_payload(ctx, entity, …)` | The retained abbreviated-key JSON document; 0 if it does not fit (a defect, not a short document). |
| `comfoair_ha_state_payload(entity, value, &skip, …)` | Bare scalar (`123`, `21.5`, `ON`, `Auto`). `COMFOAIR_VALUE_NONE` (and an empty text) → `None`, HA's "unknown", never nothing: a retained reading that went away must not stay on the broker as if current. 0 with `skip = true` only for a NULL argument or a buffer it does not fit. |
| `comfoair_ha_fan_{state,speed,preset}_payload(fan, …)` | `ON`/`OFF` (OFF = the AWAY level, not a stopped fan), `1`..`3`, preset name or `None`. All three are `None` while `fan->known` is false. |

Constants and enums:

- `COMFOAIR_HA_DISCOVERY_PREFIX "homeassistant"`: HA's default subscribe prefix, distinct from the device's own tree.
- `COMFOAIR_HA_PAYLOAD_ONLINE "online"` / `_OFFLINE "offline"`: HA's defaults for `payload_available`/`payload_not_available`, so discovery never has to name them.
- `COMFOAIR_HA_PAYLOAD_NONE "None"`: HA's own "no value" literal (`PAYLOAD_NONE`, and `DEFAULT_PAYLOAD_RESET` for the number and the fan's resets) -- what every state topic carries when REST says `null`, and how the fan's preset is cleared; deliberately not one of the six preset names (HA rejects a preset list that contains it).
- `comfoair_ha_meta_t`: `STATUS` (broker link, also the will topic), `BUS` (the unit answering on CAN, payload from `comfoair_bus_health_available()`), `COUNT`.
- `comfoair_ha_fan_leaf_t`: `FAN_STATE`, `FAN_SPEED_STATE`, `FAN_PRESET_STATE` (retained, published by this device, **must stay first**), then `FAN_SPEED_COMMAND`, `FAN_PRESET_COMMAND` (written by HA, never erased), `FAN_ANY_COMMAND` (`fan/+/command`, a subscription pattern, never published), `LEAF_COUNT`.
- `COMFOAIR_HA_FAN_RETAINED_COUNT = (int)COMFOAIR_HA_FAN_SPEED_COMMAND` (3): derived from enum order, never typed; publisher, cleanup and suite all walk `0 .. RETAINED_COUNT`.
- `comfoair_ha_cleanup_t`: `REKEY` (prefix changed: state side only), `WITHDRAW` (leaving the broker: discovery side first, then state side).
- `comfoair_ha_target_t`: `NONE`, `ENTITY`, `FAN_ON`, `FAN_SPEED`, `FAN_PRESET`, `OP`.

## Implementation

Files:

- `include/comfoair_ha.h`: the public contract, the topic tree and the three "doors" (meta enum, registry, fan-leaf enum).
- `comfoair_ha.c`: what a topic is called. The six builders, the component mapping, the parser.
- `comfoair_ha_payload.c`: what goes in a config topic. The discovery document, `bus_backed()`, `put_device()`.
- `comfoair_ha_state_payload.c`: what goes in a state topic. The entity scalar and the fan's three leaves.
- `comfoair_ha_cleanup.c`: the cleanup enumerator. No formatting of its own; it only orders and walks the builders.
- `comfoair_ha_internal.h`: internal, `static inline`: the bounded `writer_t` (`put`, `put_num` with `%g`, `put_string` via `comfoair_json_escape`, `put_opt`, `finish`) and `usable(ctx)`. Not in `include/`; nothing outside this directory may include it.

The three doors. A retained topic of this device can only come into existence through
`comfoair_ha_meta_t`, a registry key (enforced by `state_topic()`'s `comfoair_entity_by_key()`
check), or `comfoair_ha_fan_leaf_t`. The cleanup walks exactly those three, so adding a value to an
enum automatically adds it to the cleanup. The switches in `meta_topic()` and `fan_topic()` are
total; `[env:native]` builds with `-Werror=switch`, so a new enumerator with no name fails the host
build. The device build (`pio run -e m5stack-nanoc6`) only warns, so an unnamed value can still ship
if the host suites are never built.

Discovery document (`comfoair_ha_discovery_payload`): `name`, `uniq_id = <device_id>_<key>`,
`avty` list + `avty_mode: all`, `stat_t`/`cmd_t` (not for the fan), `icon`, `ent_cat`,
`en:false` for any row whose registry `enabled_by_default` is false, then
per-kind keys: numeric → `dev_cla`/`stat_cla`/`unit_of_meas`; binary → `dev_cla`; freshness →
`dev_cla` + `exp_aft = COMFOAIR_FRESHNESS_EXPIRE_S` (900 s = 3 × the 300 s publication period,
static-asserted in the generated header); enum/special/diagnostic_const → nothing; select → `ops`; number → `min`/`max` when not NaN, `unit_of_meas`; fan →
`stat_t`, `cmd_t`, `pct_stat_t`, `pct_cmd_t`, `spd_rng_min:1`, `spd_rng_max:3`, `pr_mode_stat_t`,
`pr_mode_cmd_t`, `pr_modes`. Then the `dev` block (`ids`, optional `cns`, `name`, `mf`, `mdl`, `sw`, `cu`).

Availability: every entity lists `<prefix>/status`; entities where `bus_backed()` is true also list
`<prefix>/bus`. `bus_backed()` = `isnan(default_value)` and kind is neither `DIAGNOSTIC_CONST` nor
`FRESHNESS`. Every entity is bus-backed except six: the 4 durations (node-local,
with a cold-start default), `schema_version` (a constant about the firmware itself), and the
freshness heartbeat (it reports on this node, so gating it on CAN is wrong). The three RMI-read
selects (`temperature_passive`, `humidity_comfort`, `humidity_protection`) have no default and
carry `<prefix>/bus` like every other value read from the unit.

Cleanup order: the state side yields the meta topics first (an empty payload on an availability
topic matches neither `online` nor `offline`, so HA keeps its previous availability and nothing
flaps), then one `<key>/state` per published entity, with the fan contributing its 3 retained
leaves instead. `WITHDRAW` = all discovery topics, then the state side. Index lookup is an O(n) walk
per call (about two entries per entity); that is intended, because a lookup table would be a second list.

DO NOT:

- Format `<prefix>/…` or `homeassistant/…` anywhere else. A hand-built topic is one the cleanup cannot see, and it stays retained on the owner's broker for ever.
- Add a registry check to `comfoair_ha_command_topic()` "for symmetry". The `"+"` wildcard would return 0 and the device would silently subscribe to nothing.
- Drop the registry check from `comfoair_ha_state_topic()` "for speed". It is what makes the registry a door rather than a convention.
- Put a fan leaf between the three retained ones and the command ones, or erase a command leaf (it is HA's retained message, not ours).
- Emit `avty_t` beside `avty`. They are `vol.Exclusive` in one group, so the config is rejected and the entity silently never appears. Keep the list form even for single-topic entities.
- Change `avty_mode` to `any` or `latest`. With `any`, a retained `bus = online` left after a power cut keeps a dead device available, because only `status` has the will. With `latest`, the answer depends on the broker's redelivery order.
- Add `options`/`device_class: enum` to enum sensors as a side effect of something else. An undocumented value decodes to no value and publishes `None`, which HA accepts before the options check (`mqtt/sensor.py:317`), so options would now be safe — but adding them changes the entity's class for every existing MQTT user, which is a decision of its own.
- Write `bus_backed()` as `entity->pdo < 0`. `balance_mode`, `ventilation_mode` and the fan have no PDO of their own but are derived from bus PDOs, and they must grey out when the bus dies.
- Swap `WITHDRAW`'s order. Emptying state first leaves live entities pointing at cleared topics.
- Edit the parser's fan strings (`fan/speed/command`, …) without running `test_ha`. They are the reader's copy of what `fan_topic()` writes.
- Put `exp_aft` on a data entity. With delta publication, that would mean republishing every slow reading on a clock.

## Dependencies

- `REQUIRES comfoair_state comfoair_json comfoair_registry` (`CMakeLists.txt`): `comfoair_value_t`,
  `comfoair_fan_state_t` and `comfoair_entity_by_key()`; `comfoair_json_escape()`; the generated
  `COMFOAIR_ENTITIES[]` / `COMFOAIR_ENTITY_COUNT` / `COMFOAIR_FRESHNESS_EXPIRE_S`.
- Deliberately NOT `mqtt`/esp-mqtt/`comfoair_mqtt`: once it can publish, its output stops being a value a test can inspect.
- Depended on by `comfoair_mqtt` (publish, discovery, retract, client/will) and `comfoair_mqtt_inbound`
  (`comfoair_ha_parse_topic`). `comfoair_mqtt_backoff` refers to it only in comments
  (`comfoair_retract_budget` takes `comfoair_ha_cleanup_count()`'s result as its total).

## Tests

Host suites that include `comfoair_ha.h` and compile these sources (`pio test -e native -f <suite>`):

- `test/test_ha`: topic shapes, discovery-topic character set, truncation → 0, kind→component mapping, per-kind payload keys, absent metadata omitted, select/number/fan documents, MAC identity, escaping of the device name, the worst-case payload fits the 1536-byte publish buffer, state/fan payloads, the parser (length-bounded, out-of-tree refusal, oversize key), and `test_every_command_leaf_the_builder_names_is_understood_by_the_parser`.
- `test/test_ha_availability`: the bus vs node-local `avty` split, `all` mode, `avty`/`avty_t` exclusivity.
- `test/test_ha_cleanup`: both sequences, ordering, meta and fan-leaf coverage, command topics never erased, registry-only state topics, the old identity never naming the new prefix, no truncation at `COMFOAIR_CONFIG_PREFIX_MAX`.
- `test/test_ha_enabled`: `en:false` appears on exactly the rows whose `enabled_by_default` is false, over the whole registry.
- `test/test_ha_freshness`: freshness is a `binary_sensor` with `exp_aft`, no other entity carries it, and the period/expiry live in one define.
- `test/test_ha_none`: every entity with no value renders `None`, over the whole registry, and the fan's three leaves do before the unit has described the fan.
- `test/test_mqtt_inbound` exercises `comfoair_ha_parse_topic()` transitively through `comfoair_mqtt_inbound`.
- `tests/python/test_registry.py` forbids hand-written entity keys (e.g. the freshness key) anywhere under `components/`.

Not covered here: the actual publish/subscribe glue, the will, QoS/retain flags and the order in
which the publisher calls these builders live in `comfoair_mqtt` (esp-mqtt-bound), which none of the
suites above compile.
Acceptance by a real Home Assistant is a hardware scenario, not a host test. The `-Wswitch` guard on
the enums holds only in `[env:native]`.

## Notes

- **Identity is the MAC, never the name.** `uniq_id`, the discovery node and `dev.ids` use `device_id`. Renaming must re-key nothing, or HA ends up with a duplicate device that the retained old documents keep alive.
- **REKEY leaves discovery alone.** The config topic contains neither the prefix nor the name, so the new client simply rewrites each document with the new `stat_t` and HA moves the entities across. Emptying them would delete every entity together with their entity_ids, history and automation references.
- **An absent reading publishes `None`.** It is HA's own "no value" literal, which every component this device publishes reads as unknown, so a retained reading that went away (a countdown that ended, an unlisted enum value, a short frame) is replaced instead of staying on the broker as if current. An empty payload is never sent: retained, it deletes the broker's copy. `"unknown"` would turn a numeric reading into a string. No option label may be spelled `None` (`tools/registry_checks.py`).
- **Bare scalars, not JSON state.** A plain MQTT sensor reads them without a `value_template`; a template per entity would be a second renderer to keep in step.
- **Abbreviated keys** (`stat_t`, `uniq_id`, …): the device block repeats in every document, and HA expands abbreviations itself.
- **`cns` (MAC connection) is published** so the entities join the device card HA already has for this unit's MAC. Trade-off: while another publisher for the same MAC is still running, the card shows two sets of entities, with `_2` suffixes.
- **`en:false` comes from the registry, never from a key test.** `enabled_by_default` is a table field (`tools/pdo_table.py`), the same for every kind; the rows the table marks set it false — the duplicate, raw and unverified rows (`supply_fan_mode` and `exhaust_fan_mode` on PDOs 54/55, `frost_protection_preheater_power` on 219, `intake_air_temp_unverified` on 220, `sensor_ventilation_mode` on 225: a copy of another entity or a number nobody has pinned is noise on the card), and the `ventilation_mode` select (it and the fan's preset are the same control, and two visible entities that move each other look like a defect). A `strcmp` on a key here would be the second entity list AGENTS.md forbids.
- **Number ranges use `isnan`, never `!= 0`.** `away_duration`'s real minimum is 0. The `unit_of_meas` on numbers keeps a consumer from confusing minutes with seconds.
- **`put_string` escapes into a 512-byte stack buffer**, and `comfoair_json_escape` drops (does not flag) output past that, so `finish()` would not report it. All inputs (registry strings, config-bounded name/prefix/url) are far shorter. Keep it that way, or widen the buffer.
- **The writer helpers are `static inline` in an internal header**, not a `.c` file: they are stateless, and the two translation units each get their own copy at no cost.
