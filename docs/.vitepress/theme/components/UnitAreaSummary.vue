<script setup lang="ts">
// <UnitAreaSummary area="ventilation" />: one area at a glance -- its units and how much of it is
// understood. An unknown area throws so a typo fails the build.
import { data } from '../../../unit/probe.data'
import UnitBadge from './UnitBadge.vue'
import { hex2 } from './format'

const props = defineProps<{ area: string }>()

const info = data.areas[props.area]
if (!info) {
  throw new Error(`<UnitAreaSummary area="${props.area}">: no such area ` +
    `(known: ${Object.keys(data.areas).join(', ')})`)
}
const units = data.units.filter((u) => info.units.includes(u.unit))
const levels = ['confirmed', 'inferred', 'unknown'] as const
</script>

<template>
  <div class="unit-area-summary">
    <div class="unit-area-title">{{ info.title }}</div>
    <p class="unit-area-text">{{ info.summary }}</p>
    <div class="unit-area-units">
      <span class="unit-dim">Units:</span>
      <span v-for="u in units" :key="u.unit" class="unit-area-unit">
        {{ u.unit }} <span class="unit-dim">{{ hex2(u.unit) }}</span> {{ u.name ?? 'unnamed' }}
        <span v-if="u.status !== 'present'" class="unit-dim">(absent)</span>
      </span>
    </div>
    <div class="unit-area-counts">
      <span class="unit-dim">{{ info.property_count }} properties:</span>
      <span v-for="l in levels" :key="l" class="unit-area-count">
        <UnitBadge :level="l" /> {{ info.confidence[l] }}
      </span>
    </div>
  </div>
</template>
