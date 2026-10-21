<script setup lang="ts">
// <UnitPropertyTable area="ventilation" />  every property of one functional area
// <UnitPropertyTable :unit="30" />          every property of one unit
// <UnitPropertyTable />                     every property the probe saw
//
// Rows are grouped by unit. The filter is client-side and hides rows with v-show, so the
// server-rendered HTML always holds every row (search engines and the no-JS reader see them all).
// An unknown area or unit throws: a typo in a page must fail the build, not render an empty table.
import { computed, ref } from 'vue'
import { withBase } from 'vitepress'
import { data } from '../../../unit/probe.data'
import type { PropertyInfo } from '../../../unit/probe.data'
import UnitBadge from './UnitBadge.vue'
import { DASH, hex2, isUrl, sourceLabel, writableText } from './format'

const props = defineProps<{ area?: string; unit?: number | string }>()

const FILTER_MIN_ROWS = 30

if (props.area !== undefined && !data.areas[props.area]) {
  throw new Error(`<UnitPropertyTable area="${props.area}">: no such area ` +
    `(known: ${Object.keys(data.areas).join(', ')})`)
}
const unitNo = props.unit === undefined ? undefined : Number(props.unit)
if (unitNo !== undefined && !data.units.some((u) => u.unit === unitNo)) {
  throw new Error(`<UnitPropertyTable unit="${props.unit}">: no such unit in the probe`)
}

const rows: PropertyInfo[] = data.properties.filter(
  (p) => (props.area === undefined || p.area === props.area) && (unitNo === undefined || p.unit === unitNo),
)

const groups = data.units
  .filter((u) => rows.some((p) => p.unit === u.unit))
  .map((u) => ({ unit: u, rows: rows.filter((p) => p.unit === u.unit) }))

const showFilter = rows.length > FILTER_MIN_ROWS
const q = ref('')

function haystack(p: PropertyInfo): string {
  return [p.key, p.name, p.meaning, p.value, p.unit_name, p.firmware, p.confidence, p.unit_of_measure]
    .filter(Boolean)
    .join(' ')
    .toLowerCase()
}
const index = new Map(rows.map((p) => [p.key, haystack(p)]))
const match = (p: PropertyInfo): boolean => {
  const needle = q.value.trim().toLowerCase()
  return needle === '' || needle.split(/\s+/).every((w) => index.get(p.key)!.includes(w))
}
const shown = computed(() => rows.filter(match).length)

const range = (p: PropertyInfo) =>
  p.range_min === null && p.range_max === null ? DASH : `${p.range_min ?? DASH} – ${p.range_max ?? DASH}`
const valueTitle = (p: PropertyInfo) =>
  [p.raw_hex ? `raw 0x${p.raw_hex}` : null, p.format ? `format ${p.format}` : null].filter(Boolean).join(' · ')
const anchor = (p: PropertyInfo) => `prop-${p.key.replace(/\./g, '-')}`
</script>

<template>
  <div class="unit-props">
    <div v-if="showFilter" class="unit-filter">
      <input
        v-model="q"
        type="search"
        placeholder="Filter: key, name, meaning, value…"
        aria-label="Filter properties"
      />
      <span class="unit-filter-count">{{ shown }} of {{ rows.length }} properties</span>
    </div>
    <div class="unit-table-wrap">
      <table class="unit-table">
        <thead>
          <tr>
            <th>Key</th>
            <th>Name</th>
            <th>Meaning</th>
            <th>Value</th>
            <th>Range</th>
            <th>Step</th>
            <th>Writable</th>
            <th>Confidence</th>
            <th>Firmware use</th>
          </tr>
        </thead>
        <tbody v-for="g in groups" :key="g.unit.unit" v-show="g.rows.some(match)">
          <tr class="unit-group-row">
            <th colspan="9">
              Unit {{ g.unit.unit }} <span class="unit-dim">{{ hex2(g.unit.unit) }}</span>
              · {{ g.unit.name ?? 'unnamed' }}
              <template v-if="area === undefined">
                · <a :href="withBase(`/unit/areas/${g.unit.area}`)">{{ data.areas[g.unit.area].title }}</a>
              </template>
              <span class="unit-dim"> · {{ g.rows.length }} {{ g.rows.length === 1 ? 'property' : 'properties' }}</span>
            </th>
          </tr>
          <tr v-for="p in g.rows" v-show="match(p)" :key="p.key" :id="anchor(p)" class="unit-prop-row">
            <td class="unit-nowrap"><code>{{ p.key }}</code></td>
            <td>{{ p.name ?? DASH }}</td>
            <td class="unit-meaning">
              {{ p.meaning }}
              <details class="unit-sources">
                <summary>details</summary>
                <div v-if="p.encoding">Encoding: {{ p.encoding }}</div>
                <div v-if="p.access">Access flag: {{ p.access }}</div>
                <div>Sources:</div>
                <ul>
                  <li v-for="s in p.sources" :key="s">
                    <a v-if="isUrl(s)" :href="s" target="_blank" rel="noreferrer">{{ sourceLabel(s) }}</a>
                    <code v-else>{{ s }}</code>
                  </li>
                </ul>
              </details>
            </td>
            <td :title="valueTitle(p)">
              <em v-if="p.redacted" class="unit-dim">redacted</em>
              <template v-else>{{ p.value }}<template v-if="p.unit_of_measure"> {{ p.unit_of_measure }}</template></template>
            </td>
            <td class="unit-nowrap">{{ range(p) }}</td>
            <td>{{ p.step ?? DASH }}</td>
            <td>{{ writableText(p.writable) }}</td>
            <td><UnitBadge :level="p.confidence" :title="`${p.sources.length} source(s)`" /></td>
            <td class="unit-firmware">{{ p.firmware ?? DASH }}</td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
</template>
