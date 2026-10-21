<script setup lang="ts">
// <UnitEntities keys="supply_air_temp,fan_speed" />: the firmware entities a page names, as the
// generated registry describes them -- key, name, Home Assistant kind, unit of measure, whether
// it is writable, and the PDO its value is decoded from.
//
// WHY THE ROWS COME FROM entities.data.ts. That loader reads web/src/api/entities.ts, the
// generator's output from tools/pdo_table.py (the one entity list). A page names keys only; every
// other cell is the registry's, so a renamed unit, a moved PDO or a changed writability reaches
// every area page on the next build without anyone editing it.
//
// WHY AN UNKNOWN KEY THROWS. A key the registry does not have is a typo or an entity that was
// removed; either way the page would promise a row that does not exist. Throwing during the build
// fails it (index.ts sets throwUnhandledErrorInProduction under SSR), the same rule the unit
// components follow for an unknown area or unit.
//
// An empty PDO cell is the registry's own answer: the row names no single PDO (a value derived
// from two PDOs, a setting read over RMI, a duration held on the device, or one the firmware
// declares itself). DO NOT fill it from a hand-kept list -- that would be a second entity list.
import { data } from '../../../reference/entities.data'
import type { EntityRow } from '../../../reference/entities.data'
import { DASH } from './format'

const props = defineProps<{ keys: string }>()

const byKey = new Map<string, EntityRow>()
for (const rows of Object.values(data.groups)) for (const e of rows) byKey.set(e.key, e)

const wanted = (props.keys ?? '')
  .split(',')
  .map((k) => k.trim())
  .filter((k) => k !== '')
if (wanted.length === 0) throw new Error('<UnitEntities>: the keys attribute names no entity')
const unknown = wanted.filter((k) => !byKey.has(k))
if (unknown.length > 0) {
  throw new Error(`<UnitEntities keys="${props.keys}">: no such entit${unknown.length === 1 ? 'y' : 'ies'} ` +
    `in the generated registry: ${unknown.join(', ')}`)
}
const rows: EntityRow[] = wanted.map((k) => byKey.get(k)!)

const NO_PDO = 'No single PDO carries this entity in the generated registry'
</script>

<template>
  <div class="unit-table-wrap">
    <table class="unit-table unit-entities">
      <thead>
        <tr>
          <th>Key</th>
          <th>Name</th>
          <th>Kind</th>
          <th>Unit</th>
          <th>Writable</th>
          <th>PDO</th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="e in rows" :key="e.key">
          <td class="unit-nowrap"><code>{{ e.key }}</code></td>
          <td>{{ e.name }}</td>
          <td class="unit-nowrap" :title="`registry kind: ${e.kind}`">{{ e.component }}</td>
          <td class="unit-nowrap">{{ e.unit ?? DASH }}</td>
          <td>{{ e.writable ? 'yes' : 'no' }}</td>
          <td class="unit-nowrap">
            <template v-if="e.pdo !== null">{{ e.pdo }}</template>
            <span v-else class="unit-dim" :title="NO_PDO">{{ DASH }}</span>
          </td>
        </tr>
      </tbody>
    </table>
  </div>
</template>
