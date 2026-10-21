<script setup>
import { data } from './entities.data'
const g = data.groups
const dash = '—'
const show = (v) => (v === null || v === undefined || v === '' ? dash : v)
const range = (e) => (e.min === null && e.max === null ? dash : `${show(e.min)} – ${show(e.max)}`)
const opts = (e) => (e.options ? e.options.join(' · ') : dash)
</script>

# Entities

Every entity the firmware exposes, as it appears over REST, over MQTT, in Home Assistant discovery
and in the web UI. This build lists **{{ data.total }}** entities, **{{ data.writable }}** of them
writable.

::: info How this table is produced
This page is not written by hand. It is rendered at site build time from
`web/src/api/entities.ts`, which `tools/generate_registry.py` generates from `tools/pdo_table.py`
— the one entity list the whole firmware is built from (see
[Entity registry](/development/entity-registry)). The loader is `docs/reference/entities.data.ts`.
Change the table, regenerate, and the next docs build shows the change.
:::

The **key** is the stable identifier: it names the entity in `/api/entities/{key}`, in the MQTT
topics, and it forms the Home Assistant unique id together with the device id, so renaming an
entity never re-keys it. The **name** is the English display name Home
Assistant shows; the web UI also has German, Dutch and Ukrainian names for every entity.
**HA** is the Home Assistant component the entity is discovered as (`comfoair_ha_component()` in
`components/comfoair_ha/comfoair_ha.c`). **PDO** is the single ComfoNet process-data object the
value is read from (see [PDO and RMI](/protocol/pdo-rmi)). An empty PDO cell means the registry
row names no single PDO: the value is derived from two PDOs, read from the unit over RMI, held on the
device, or declared by the firmware itself — see the sections below. **Category** `diagnostic`
places the entity in Home Assistant's diagnostic section.

## Sensors

Numeric readings from the unit, in PDO order.

<table>
<thead><tr><th>Key</th><th>Name</th><th>Unit</th><th>Device class</th><th>PDO</th><th>Category</th></tr></thead>
<tbody>
<tr v-for="e in g.sensors" :key="e.key"><td><code>{{ e.key }}</code></td><td>{{ e.name }}</td><td>{{ show(e.unit) }}</td><td>{{ show(e.deviceClass) }}</td><td>{{ show(e.pdo) }}</td><td>{{ show(e.entityCategory) }}</td></tr>
</tbody>
</table>

## Binary sensors

On/off states decoded from a PDO — and `unit_error`, which no PDO carries: it is on while the unit
reports an active error that is not merely informational, read over RMI every minute (see
[Errors and alarms](/unit/areas/errors-and-alarms)).

<table>
<thead><tr><th>Key</th><th>Name</th><th>PDO</th><th>Category</th></tr></thead>
<tbody>
<tr v-for="e in g.binary" :key="e.key"><td><code>{{ e.key }}</code></td><td>{{ e.name }}</td><td>{{ show(e.pdo) }}</td><td>{{ show(e.entityCategory) }}</td></tr>
</tbody>
</table>

## Enumerated states

A PDO value rendered as one of a fixed set of labels. A raw value the table does not list is not
guessed: the entity's value becomes null (`test_state` pins this). These are discovered as plain
Home Assistant sensors, not enum sensors with a declared option list. `airflow_constraints` is
the one special decoder — the PDO 230 bitset rendered as text. `active_errors` is text too, with
no PDO: the texts of the unit's active errors in one line, read over RMI every minute. Both error
entities read unknown until the unit has answered and its firmware version is known.

<table>
<thead><tr><th>Key</th><th>Name</th><th>PDO</th><th>Values</th><th>Category</th></tr></thead>
<tbody>
<tr v-for="e in g.enums" :key="e.key"><td><code>{{ e.key }}</code></td><td>{{ e.name }}</td><td>{{ show(e.pdo) }}</td><td>{{ opts(e) }}</td><td>{{ show(e.entityCategory) }}</td></tr>
</tbody>
</table>

## Controls

The writable entities. A control with a PDO reads its state back from the unit. Three have no PDO
cell but are still read back: the fan and Ventilation Mode are derived from PDOs 49 and 65
together, and Balance Mode from PDOs 54 and 55 (`components/comfoair_state/comfoair_state.cpp`).
Temperature Passive, Humidity Comfort and Humidity Protection have no PDO: the device reads them
from the unit over RMI about 10 s after boot and every 6 hours, and they read unknown until the
first answer; a write shows the accepted option at once until the next read. The four durations are held on the device and
handed to the unit as part of a command. See [Commands](/protocol/commands).

<table>
<thead><tr><th>Key</th><th>Name</th><th>HA</th><th>Options / range</th><th>Unit</th><th>PDO</th></tr></thead>
<tbody>
<tr v-for="e in g.controls" :key="e.key"><td><code>{{ e.key }}</code></td><td>{{ e.name }}</td><td>{{ e.component }}</td><td>{{ e.options ? opts(e) : range(e) }}</td><td>{{ show(e.unit) }}</td><td>{{ show(e.pdo) }}</td></tr>
</tbody>
</table>

## Device diagnostics

Entities the firmware itself declares rather than reads from the unit: the schema version of the
entity contract, and the freshness signal Home Assistant uses to mark the device unavailable when
it goes silent.

<table>
<thead><tr><th>Key</th><th>Name</th><th>HA</th><th>Device class</th><th>Category</th></tr></thead>
<tbody>
<tr v-for="e in g.synthetic" :key="e.key"><td><code>{{ e.key }}</code></td><td>{{ e.name }}</td><td>{{ e.component }}</td><td>{{ show(e.deviceClass) }}</td><td>{{ show(e.entityCategory) }}</td></tr>
</tbody>
</table>
