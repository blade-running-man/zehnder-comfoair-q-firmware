<script setup lang="ts">
// The confidence badge: how sure the annotations are about a unit or a property.
defineProps<{ level: 'confirmed' | 'inferred' | 'unknown'; title?: string }>()
</script>

<template>
  <span class="unit-badge" :class="`unit-badge--${level}`" :title="title">{{ level }}</span>
</template>
