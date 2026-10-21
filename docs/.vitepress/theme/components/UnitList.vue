<script setup lang="ts">
// <UnitList />: every RMI unit the probe asked about, present or absent, in unit order.
import { withBase } from 'vitepress'
import { data } from '../../../unit/probe.data'
import UnitBadge from './UnitBadge.vue'
import { DASH, hex2 } from './format'
</script>

<template>
  <div class="unit-table-wrap">
    <table class="unit-table">
      <thead>
        <tr>
          <th>Unit</th>
          <th>Hex</th>
          <th>Name</th>
          <th>Status</th>
          <th>Subunits</th>
          <th>Properties</th>
          <th>Area</th>
          <th>Confidence</th>
        </tr>
      </thead>
      <tbody>
        <tr
          v-for="u in data.units"
          :key="u.unit"
          class="unit-row"
          :class="{ 'unit-row--absent': u.status !== 'present' }"
          :title="u.purpose"
        >
          <td>{{ u.unit }}</td>
          <td><code>{{ hex2(u.unit) }}</code></td>
          <td>{{ u.name ?? DASH }}</td>
          <td>{{ u.status }}</td>
          <td>{{ u.subunits }}</td>
          <td>{{ u.property_count }}</td>
          <td><a :href="withBase(`/unit/areas/${u.area}`)">{{ data.areas[u.area].title }}</a></td>
          <td><UnitBadge :level="u.confidence" :title="`${u.sources.length} source(s)`" /></td>
        </tr>
      </tbody>
    </table>
  </div>
</template>
