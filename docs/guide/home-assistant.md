<script setup>
import { data } from '../reference/entities.data'
// Counts, presets and ranges on this page come from the generated registry (see
// /reference/entities), never from hand-written numbers that would drift from tools/pdo_table.py.
const all = Object.values(data.groups).flat()
const byKey = Object.fromEntries(all.map((e) => [e.key, e]))
const presets = (byKey.fan?.options ?? []).join(', ')
const durations = all.filter((e) => e.kind === 'number')
const what = {
  sensor: 'Readings and state words, e.g. Supply Air Temperature, Ventilation Power',
  binary_sensor: 'On/off states, e.g. Manual Mode, and the data-freshness indicator',
  select: 'Unit modes, e.g. Bypass Mode, Ventilation Mode',
  number: 'Operation durations, in minutes',
  switch: 'Away Mode',
  fan: 'Ventilation, with its presets',
}
</script>

# Home Assistant

The device appears in Home Assistant through **MQTT discovery**. Nothing is installed in Home
Assistant beyond its built-in MQTT integration — no custom integration, no YAML.

## What you need

1. An MQTT broker that Home Assistant uses, for example the Mosquitto add-on.
2. Home Assistant's **MQTT** integration connected to that broker, with discovery on (its default,
   listening under the `homeassistant` prefix).
3. On the device, in **Settings → MQTT broker**: the broker's host, port, user name and password,
   and **Publish Home Assistant discovery** switched on (the default). See
   [Configuration](/guide/configuration#mqtt-broker).

As soon as the device connects, it publishes one discovery document per entity, retained, under
`homeassistant/<component>/<device id>/<key>/config`. Home Assistant creates the device and its
entities from them; nothing has to be confirmed.

## The device card

| Field | Value |
| --- | --- |
| Name | The device name from Settings (default `ComfoAir Q xxxx`) |
| Manufacturer / model | Zehnder / ComfoAir Q |
| Firmware | The firmware version running on the device |
| Visit link | `http://<device IP address>/`, the device's own web interface |
| Connections | The board's station MAC address |

The device is identified by its MAC address (`<device id>` is the full station MAC as 12 hex
digits), and every entity's unique id is `<device id>_<key>`
(`components/comfoair_ha/comfoair_ha_payload.c`). Renaming the device changes only what Home
Assistant displays; it never creates a second device.

::: tip A board that ran another firmware before
The device card lists the board's MAC address as a connection, and Home Assistant merges device
entries that share a connection. If this board previously ran a different firmware and that device
is still in Home Assistant, expect the new entities to appear on the old device card next to the
old ones.
:::

## The entities

All **{{ data.total }}** entities are published. They map onto Home Assistant platforms like this
(`components/comfoair_ha/comfoair_ha.c`, `comfoair_ha_component`):

<table>
<thead><tr><th>Platform</th><th>Count</th><th>What</th></tr></thead>
<tbody>
<tr v-for="c in data.components" :key="c.component"><td><code>{{ c.component }}</code></td><td>{{ c.count }}</td><td>{{ what[c.component] ?? '—' }}</td></tr>
</tbody>
</table>

Several of the sensors are in the **diagnostic** category, so Home Assistant shows them under
*Diagnostic* on the device card. The complete list with keys, units and options is in
[Entities](/reference/entities).

### Controlling the unit

The {{ data.writable }} writable entities — the fan, the selects, the away switch and the durations —
are everyday control:

- **Ventilation** (`fan`): turning it *off* sets the **Away** level, not a stopped fan. Its preset
  modes are **{{ presets }}**, and its speed slider has three steps — Low,
  Medium and High (`spd_rng_min` 1, `spd_rng_max` 3).
- **Durations** (`number`) are stored on the device and used the next time the matching command is
  sent: <span v-for="(e, i) in durations" :key="e.key">{{ i ? '; ' : '' }}{{ e.name }}
  ({{ e.min }}–{{ e.max }} {{ e.unit }})</span>.
- Whether a level change sticks or reverts to AUTO depends on **Keep the ventilation level** in the
  device's settings ([Configuration](/guide/configuration#device)).

The six parameterised operations — `boost`, `set_bypass`, `set_balance_mode`,
`set_ventilation_level`, `set_temperature_profile`, `set_away` — take all their parameters in one
call (for example a bypass mode and how long it holds), so they are **not** discovered. Call them
from an automation or script with `mqtt.publish` to `<prefix>/ops/<name>/set`; the payloads are in
[MQTT](/interfaces/mqtt).

## Availability

Two retained topics decide whether an entity is *available*:

| Topic | Payload | Meaning |
| --- | --- | --- |
| `<prefix>/status` | `online` / `offline` | The device's connection to the broker. `offline` is the broker's last will, so it turns up even after a power cut. |
| `<prefix>/bus` | `online` / `offline` | Whether the ventilation unit is answering on the CAN bus. |

Every entity requires `status` to be `online`. Entities that carry a value from the unit also
require `bus` to be `online` (`avty_mode: all`), including the three sensor ventilation selects,
which are read from the unit. The durations (they live on this device), the schema version and data
freshness depend on `status` alone.

So when you switch the unit off at the breaker — to change filters, say — the unit's readings turn
**unavailable** in Home Assistant instead of freezing at their last values, while the device itself
stays online. The bus is declared silent after 90 seconds without traffic
(`components/comfoair_phase/include/comfoair_bus_health.h`). A device that has never heard the
unit publishes `bus` as `offline` too.

## Changing things later

What happens on the broker depends on what you change, and every clean-up needs a **live broker
connection** at the moment you save — the device does not wait or retry a clean-up for a broker it
cannot reach.

| You change | What the device does |
| --- | --- |
| Device name | Republishes discovery under the same ids; Home Assistant renames the device |
| Topic prefix | Rewrites the discovery documents in place with the new state topics and empties the old state topics. The entities keep their ids and history. |
| Broker host or port | Withdraws its discovery documents from the old broker (Home Assistant removes the device), then connects to the new one |
| Discovery off | Stops publishing discovery, **but removes nothing** — the device and entities stay and keep updating; clearing them means deleting the retained messages at the broker |

### Removing the device

The [15-second button reset](/guide/troubleshooting#the-button) erases the device's settings but does
**not** tidy up the broker, so Home Assistant keeps the device and its entities. To remove them
cleanly, first point the device at a different broker (or an unused host) while it can still reach
the current one — that withdraws its discovery documents — and only then reset it.

## Further detail

- [MQTT](/interfaces/mqtt) — the full topic tree, state payloads and command payloads.
- [Home Assistant discovery](/interfaces/home-assistant-discovery) — the discovery documents,
  availability and the clean-up rules in detail.
