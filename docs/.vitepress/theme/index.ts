// The site theme: VitePress's default theme in the device web UI's cybercore look
// (cyber-palette.css, cyber.css), plus the global components the "Ventilation unit" section
// renders its tables with: the probe tables, and <UnitEntities> over the entity registry.
// They are global so a page uses them as plain tags (<UnitPropertyTable area="ventilation" />)
// without a <script setup> import of its own.
//
// WHY ASYNC. The components import whole datasets (every probed unit and property with its
// annotation; every registry entity). Registered eagerly, that data would sit in the theme chunk
// every page downloads, including the pages that never show a table. defineAsyncComponent splits
// each into its own chunk, fetched only by a page that uses it; server-side rendering still
// resolves them, so the built HTML carries the full tables.
import { defineAsyncComponent } from 'vue'
import DefaultTheme from 'vitepress/theme'
import type { Theme } from 'vitepress'
import './cyber-palette.css'
import './cyber.css'
import './unit.css'

export default {
  extends: DefaultTheme,
  enhanceApp({ app }) {
    // A component that throws while the site is being built (an unknown area or unit on a page)
    // must fail the build. Vue's production default only logs the error and renders nothing, which
    // would publish an empty table under a heading that promises one. In the browser the default
    // stays: a live page should degrade, not die.
    if (import.meta.env.SSR) app.config.throwUnhandledErrorInProduction = true
    app.component('UnitPropertyTable', defineAsyncComponent(() => import('./components/UnitPropertyTable.vue')))
    app.component('UnitList', defineAsyncComponent(() => import('./components/UnitList.vue')))
    app.component('UnitAreaSummary', defineAsyncComponent(() => import('./components/UnitAreaSummary.vue')))
    app.component('UnitEntities', defineAsyncComponent(() => import('./components/UnitEntities.vue')))
  },
} satisfies Theme
