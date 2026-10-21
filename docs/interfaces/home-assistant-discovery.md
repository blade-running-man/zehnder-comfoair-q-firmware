<script setup>
import { data } from '../reference/entities.data'
// Entity counts on this page come from the generated registry (see /reference/entities), never
// from hand-written numbers that would drift from tools/pdo_table.py.
</script>

# Home Assistant discovery

The device makes itself appear in Home Assistant through
[MQTT discovery](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery): it publishes one
retained config document per entity, and Home Assistant builds a device with its entities from
them. No custom integration is needed. Discovery is built in `components/comfoair_ha`
(`comfoair_ha_payload.c` for the documents, `comfoair_ha_cleanup.c` for the cleanup sequence) and
published by `components/comfoair_mqtt` (`comfoair_mqtt_discovery.c`, `comfoair_mqtt_retract.c`).

For the owner's walkthrough — adding the broker and finding the device in Home Assistant — see
[Home Assistant](/guide/home-assistant). The topic tree the documents point at is on
[MQTT](/interfaces/mqtt).

## Discovery topics

```text
homeassistant/<component>/<device_id>/<key>/config
```

- The discovery prefix is fixed at `homeassistant` (`COMFOAIR_HA_DISCOVERY_PREFIX`), Home
  Assistant's default. It is separate from the device's own `topic_prefix`.
- `<component>` comes from the entity's kind, see below.
- `<device_id>` is the twelve lower-case hex digits of the MAC — never the name, and never the
  colon-separated MAC, which Home Assistant's topic matcher does not accept.
- `<key>` is the registry key.

Every document is published retained at QoS 1. The registry has {{ data.total }} entities, every one of
which maps to a component, so a full tree is {{ data.total }} documents.

## Entity kinds to components

| Registry kind | HA component | Extra keys in the document |
| --- | --- | --- |
| `numeric` | `sensor` | `dev_cla`, `stat_cla`, `unit_of_meas` from the registry |
| `enum` | `sensor` | none — deliberately a plain sensor, no `options` |
| `special` (`airflow_constraints`, `active_errors`) | `sensor` | none; the value is a sentence |
| `diagnostic_const` (`schema_version`) | `sensor` | none |
| `binary` | `binary_sensor` | `dev_cla` |
| `freshness` (`data_freshness`) | `binary_sensor` | `dev_cla` (`connectivity`), `exp_aft: 900` |
| `select` | `select` | `ops` (the option labels) |
| `number` | `number` | `min`, `max`, `unit_of_meas` (`min` — minutes) |
| `switch` | `switch` | none; `ON`/`OFF` are HA's defaults |
| `fan` | `fan` | three topic pairs, speed range and presets, see below |

Why `enum` is a plain sensor: Home Assistant validates `options` and `device_class: enum` together
(`mqtt/sensor.py:109-125`) and rejects any state outside the list. An undocumented unit value
decodes to no value and publishes `None`, which Home Assistant accepts before the options check
(`sensor.py:317`), so options would now be safe. They are still not added, because that changes the
entity's class for every existing MQTT user — a decision of its own, not a side effect of this one
(`comfoair_ha_component()` in `comfoair_ha.h`).

The unit's active errors are two entities read over RMI, not from a PDO (see
[Errors and alarms](/unit/areas/errors-and-alarms)): `unit_error` is a `binary_sensor` with
`dev_cla: problem` — Home Assistant shows OK / Problem — and `active_errors` is a diagnostic
`sensor` carrying the error texts in one line of at most 255 characters.

`ventilation_mode` (a select) and `fan` are the same control. The select is published disabled by
default (`en: false`) so the device card does not show two controls that move each other; it
stays available for automations.

## A config document

Keys are abbreviated (`stat_t`, `cmd_t`, `uniq_id`, …); Home Assistant expands them. A select:

```json
{
  "name": "Bypass Mode",
  "uniq_id": "aabbccddeeff_bypass_mode",
  "avty": [{"t": "comfoair/aabbccddeeff/status"}, {"t": "comfoair/aabbccddeeff/bus"}],
  "avty_mode": "all",
  "stat_t": "comfoair/aabbccddeeff/bypass_mode/state",
  "cmd_t": "comfoair/aabbccddeeff/bypass_mode/command",
  "icon": "mdi:valve",
  "ops": ["Auto", "Open", "Closed"],
  "dev": {
    "ids": ["aabbccddeeff"],
    "cns": [["mac", "aa:bb:cc:dd:ee:ff"]],
    "name": "ComfoAir Q 8ae4",
    "mf": "Zehnder", "mdl": "ComfoAir Q",
    "sw": "1.0.0",
    "cu": "http://192.168.1.42/"
  }
}
```

| Key | Content |
| --- | --- |
| `name` | the registry's English display name |
| `uniq_id` | `<device_id>_<key>` — the MAC plus the key, so renaming the device re-keys nothing |
| `avty`, `avty_mode` | always a list, always `all` — see [Availability](#availability) |
| `stat_t` | `<prefix>/<key>/state` (every kind except `fan`) |
| `cmd_t` | `<prefix>/<key>/command`, writable entities only |
| `icon`, `ent_cat` | from the registry, when set |
| `en` | `false` when the registry row's `enabled_by_default` is false (HA creates it disabled); absent otherwise |

The full set of entities, with their kinds, units and options, is generated on
[Entities](/reference/entities).

### What makes Home Assistant reject a document

Home Assistant validates every config document against its schema, and a document that fails is
not a degraded entity — the entity does not appear at all. The payload builder
(`comfoair_ha_payload.c`) is shaped around the rules that would otherwise kill entities:

- **`options` constrains the other keys.** Home Assistant validates `options`, `device_class: enum`,
  `unit_of_measurement` and `state_class` against one another, and once `options` is declared it
  rejects any state outside the list. That is why `enum` entities are plain sensors with none of
  these keys (see above).
- **`preset_modes` needs its command topic.** On a fan, `preset_modes` and
  `preset_mode_command_topic` are an inclusive pair: the list alone fails validation, and the topic
  alone yields a fan with no presets and no error. Every fan feature is switched on by its command
  topic, so all three command topics are always sent.
- **An empty value is omitted, never sent as `""`.** `configuration_url` without a scheme is
  rejected, and a rejected device block takes the whole entity with it — so `cu` is left out until
  the device has an address. The same rule applies to every optional key (`put_opt()` in
  `comfoair_ha_internal.h`): a unit of `""` would be a different entity from one with no unit.

### The fan

The fan reads and writes on three topic pairs, and each Home Assistant feature is switched on by
its command topic:

```json
{
  "stat_t": "comfoair/aabbccddeeff/fan/state",
  "cmd_t": "comfoair/aabbccddeeff/fan/command",
  "pct_stat_t": "comfoair/aabbccddeeff/fan/speed/state",
  "pct_cmd_t": "comfoair/aabbccddeeff/fan/speed/command",
  "spd_rng_min": 1, "spd_rng_max": 3,
  "pr_mode_stat_t": "comfoair/aabbccddeeff/fan/preset/state",
  "pr_mode_cmd_t": "comfoair/aabbccddeeff/fan/preset/command",
  "pr_modes": ["Auto", "Away", "Low", "Medium", "High", "Boost"]
}
```

The speed range 1..3 maps Home Assistant's percentage onto the three levels. Turning the fan off
selects the Away level. When the unit's operating mode maps to no preset, the preset state is
`None`, Home Assistant's reset value.

### The device block

`dev` is repeated in every document:

- `ids` — `[device_id]`, the identity.
- `cns` — `[["mac", "aa:bb:cc:..."]]`. Home Assistant merges devices by connection, so this
  device lands on the same card as a device another firmware registered with the same MAC; while
  both are present, the card carries two sets of entities.
- `name` — the configured `device_name`, display only.
- `mf`, `mdl` — `Zehnder`, `ComfoAir Q`.
- `sw` — the running firmware version.
- `cu` — `http://<ip>/`, the link to the device's own web UI. Omitted until the device has an IP
  address; when the address changes the whole tree is republished so the link follows it.

## Availability

Each document lists its availability topics with `avty_mode: all` — every listed topic must say
`online` for the entity to be available:

| Entity | Topics |
| --- | --- |
| Readings that come from the unit — every entity not in the next row | `<prefix>/status` **and** `<prefix>/bus` |
| Values held on the device — the four durations (restored from NVS), `schema_version` and `data_freshness` | `<prefix>/status` only |

The rule is `bus_backed()` in `comfoair_ha_payload.c`: an entity is bus-backed unless the registry
gives it a cold-start default (the four durations), or it is the diagnostic constant or the
freshness entity. That is 70 bus-backed entities and 6 exempt ones; the three sensor-ventilation
selects are bus-backed, because their value comes from the unit.

So when the broker connection drops, every entity becomes unavailable (the will sets `status` to
`offline`). When only the unit stops answering — its breaker is off for a filter change — the unit
readings become unavailable instead of showing the last values as current, while the
device-held values stay usable.

`data_freshness` is a `connectivity` binary sensor published `ON` every five minutes, not retained,
with `expire_after: 900`. If the device stops publishing for 15 minutes, Home Assistant marks it
unavailable on its own — the one signal that works even if the will never fires.

## When discovery is published

Discovery is published only when `ha_discovery` is true in the configuration (the default) and a
broker is configured.

It is **not** republished on every reconnect. The tree is {{ data.total }} retained documents, and
Home Assistant reads it from the broker's retained store. One flag (`comfoair_discovery_gate()`,
`components/comfoair_mqtt_backoff`) decides when it is owed again:

1. it has never been published fully in this boot;
2. the client was rebuilt from saved settings — every accepted `POST /api/config`, including a
   changed name or prefix, and including a save that changed nothing;
3. the broker link dropped while this device still had an IP address — the broker may have
   restarted and lost its retained store. A drop caused by the device's own Wi-Fi does not count.

A sweep that is cut short (link lost, a publish refused) is not counted as done and runs again. The
subscriptions are renewed on every connection regardless, because the session is clean. The
`configuration_url` changing also republishes the tree: while the link is down, each pass of the
publisher task compares the device's address with the one in `cu` (`comfoair_mqtt_note_address()`).

The third rule is an **edge**, not a level: the gate remembers whether the previous publisher pass
saw the link up, and clears the flag only on the pass where the link is first seen down while the
device still holds an address (`comfoair_discovery_gate()` in
`components/comfoair_mqtt_backoff/comfoair_discovery_gate.c`). A level test — "disconnected, with
an address" — would also be true on every pass between a rebooted router handing the address back
and the broker becoming reachable, and would republish the tree to a broker whose retained store
never went anywhere.

::: details Why one flag and not a fingerprint of the configuration
The tempting design is to hash the fields discovery is built from and republish when the hash
moves. It would compute nothing new: a config document is built from the topic context and a
registry row only, and the context is filled in exactly one place, when the client is rebuilt —
so "the client was rebuilt" already means "the content may have changed". And it would be wrong:
the broker's address is not one of those fields, so moving to a different broker with the same
prefix would produce a matching hash and an empty new broker — every state published, not one
config document, nothing in Home Assistant. The flag is cleared by the rebuild itself, and
changing the host is a rebuild.
:::

## Cleanup and retraction

When the device stops being what it was, it empties its retained topics on the outgoing broker by
publishing a zero-length retained payload to each — which both deletes the retained message and
tells Home Assistant to remove the entity. The sequence is walked from the same topic builders the
publisher uses (`comfoair_ha_cleanup_topic()`), never from a hand-written list.

| Change saved in `POST /api/config` | Operation | What is emptied |
| --- | --- | --- |
| Broker host or port changed, or the host cleared | **withdraw** | every discovery document first (whether or not `ha_discovery` is on), then `status`, `bus` and every state topic, the fan's three leaves included |
| Only `topic_prefix` changed | **rekey** | `status`, `bus` and the old state topics only; discovery documents are rewritten in place by the new client, so entities keep their history and entity ids |
| Only the name changed, or nothing | nothing | the discovery documents are rewritten anyway |

The decision is `comfoair_mqtt_pending_cleanup()` in `comfoair_mqtt_client.c`, and it is made by
comparing identities — the host, port and prefix the live client was built from against the ones
just saved — never by retracting blindly: a blind retraction on a rename would empty the retained
state of every entity that has no reading at that moment, and nothing would publish it back.
Command topics are never emptied — they belong to whoever published to them.

One retained message stays on the old tree on purpose: after the sweep, the outgoing client
publishes `offline` to the old `<prefix>/status` before it disconnects
(`comfoair_mqtt_stop_client()`), because a broker does not fire the will for a clean `DISCONNECT`.
Anything still reading the old prefix sees the device go offline rather than an empty topic.

The web UI warns before the irreversible case. When a save would change the broker host or port
while the device is connected to the current broker — or when the page cannot tell — Save asks for
confirmation and says that Home Assistant on the old broker will delete the device and its
entities, with their entity ids (`web/src/pages/settings/broker.ts`). When the current broker is
known to be unreachable there is nothing to retract, so the page explains what will be left behind
and does not block the save. The retraction runs on the publisher
task only if the outgoing client is still connected at that moment, and has a 2 s budget
(`RETRACT_BUDGET_MS`): if the old broker is unreachable or stops acknowledging, the device gives up
and moves on rather than wait. What could not be cleaned stays on that broker.

::: warning Turning discovery off does not remove the device from Home Assistant
Setting `ha_discovery` to false stops the device publishing discovery documents, but it does
**not** retract the ones already retained on the broker — the cleanup above is triggered only by a
change of broker or prefix. To remove the device from Home Assistant, delete it there and clear the
retained `homeassistant/+/<device_id>/+/config` topics on the broker (for example with
`mosquitto_pub -r -n -t <topic>` per topic or a broker UI). A factory reset does not retract either.
:::

## What is not discovered

The six parameterised operations (`boost`, `set_bypass`, `set_balance_mode`,
`set_ventilation_level`, `set_temperature_profile`, `set_away`) are not discovery entities. Each one
takes all its parameters in one call (for example a bypass mode and its duration), and Home Assistant's `select` has no parameter and its
`button` has no body; splitting an operation into a select plus a number would destroy the
atomicity the operation exists for. They are invoked with the `mqtt.publish` action:

```yaml
action: mqtt.publish
data:
  topic: comfoair/aabbccddeeff/ops/set_bypass/set
  payload: '{"mode":"open","duration":3600}'
```

Everyday control — fan, presets, bypass mode, away, durations — is covered by the {{ data.writable }} writable
discovered entities. Entity names reach Home Assistant in English; the web UI's translations do not
touch discovery.
