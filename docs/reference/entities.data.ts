// Build-time data for /reference/entities: the entity table, taken from the firmware's own
// generated entity list rather than written out here.
//
// WHY THIS IMPORTS web/src/api/entities.ts. The project allows one entity list, ever:
// tools/pdo_table.py, projected by tools/generate_registry.py into the C registry, the web UI's
// entities.ts and the translation table. A hand-written table on this page would be a second
// list, and it would go on agreeing with itself while the firmware changed. entities.ts is the
// generator's TypeScript output, committed and regenerated on every firmware build, so reading it
// is reading the registry.
//
// WHY A PLAIN IMPORT, and not the alternatives:
//  * VitePress runs a data loader through Vite's loadConfigFromFile, which bundles the loader and
//    its relative imports with esbuild. A TypeScript module outside docs/ is therefore imported
//    as-is, with no extra tooling, and its file becomes a tracked dependency: `docs:dev` reloads
//    the table when the generator rewrites entities.ts. That is also why there is no `watch`
//    glob below -- the import already does that job.
//  * DO NOT parse entities.ts with regular expressions: the generator's line format is not a
//    contract, and a parser would break silently on the first formatting change.
//  * DO NOT run `python3 tools/generate_registry.py` from here: it would make the docs build need
//    the firmware's Python toolchain and would WRITE files into the repository during a docs
//    build. The committed output is the right input.
//
// The build fails loudly (load() throws) on an empty list or an unknown kind, so a change in the
// generator's output shape cannot publish an empty or half-labelled page.
import { ENTITIES } from '../../web/src/api/entities'

export interface EntityRow {
  key: string
  name: string
  kind: string
  /** The Home Assistant component the entity is discovered as. */
  component: string
  pdo: number | null
  unit: string | null
  deviceClass: string | null
  entityCategory: string | null
  options: string[] | null
  writable: boolean
  min: number | null
  max: number | null
}

export interface ComponentCount {
  component: string
  count: number
}

export interface EntityData {
  total: number
  writable: number
  groups: Record<string, EntityRow[]>
  /**
   * How many entities each Home Assistant component receives, in COMPONENT's order. Other pages
   * (the Home Assistant guide, the discovery reference) render their per-platform counts from
   * this rather than writing numbers that would drift from the registry.
   */
  components: ComponentCount[]
}

// Mirrors comfoair_ha_component() in components/comfoair_ha/comfoair_ha.c. It is a mapping of
// kinds, not a list of entities; an unknown kind throws below, so a kind added to the generator
// without a line here fails the docs build rather than rendering a blank cell.
const COMPONENT: Record<string, string> = {
  numeric: 'sensor',
  diagnostic_const: 'sensor',
  binary: 'binary_sensor',
  freshness: 'binary_sensor',
  enum: 'sensor',
  special: 'sensor',
  select: 'select',
  number: 'number',
  switch: 'switch',
  fan: 'fan',
}

// Which section of the page a kind is listed under. Same rule as COMPONENT: every kind the
// generator emits must appear here.
const GROUP: Record<string, string> = {
  numeric: 'sensors',
  binary: 'binary',
  enum: 'enums',
  special: 'enums',
  select: 'controls',
  number: 'controls',
  switch: 'controls',
  fan: 'controls',
  diagnostic_const: 'synthetic',
  freshness: 'synthetic',
}

export default {
  load(): EntityData {
    if (!Array.isArray(ENTITIES) || ENTITIES.length === 0) {
      throw new Error('entities.data.ts: web/src/api/entities.ts exported no ENTITIES')
    }
    const groups: Record<string, EntityRow[]> = {
      sensors: [], binary: [], enums: [], controls: [], synthetic: [],
    }
    for (const e of ENTITIES) {
      const component = COMPONENT[e.kind]
      const group = GROUP[e.kind]
      if (!component || !group || typeof e.key !== 'string' || typeof e.name !== 'string') {
        throw new Error(`entities.data.ts: cannot place entity ${JSON.stringify(e)} ` +
          '(unknown kind or missing key/name) -- update COMPONENT/GROUP to match the generator')
      }
      groups[group].push({
        key: e.key,
        name: e.name,
        kind: e.kind,
        component,
        pdo: e.pdo,
        unit: e.unit,
        deviceClass: e.deviceClass,
        entityCategory: e.entityCategory,
        options: e.options ? [...e.options] : null,
        writable: e.writable,
        min: e.min,
        max: e.max,
      })
    }
    // Sensors read best in PDO order (it groups related readings); everything else keeps the
    // generator's order, which is the table's own and, for options, contract-visible.
    groups.sensors.sort((a, b) => (a.pdo ?? 0) - (b.pdo ?? 0))
    // One entry per distinct component, in the order COMPONENT first names it, so the listing is
    // stable from build to build and a component with no entity still shows as 0.
    const components: ComponentCount[] = []
    for (const c of Object.values(COMPONENT)) {
      if (!components.some((x) => x.component === c)) components.push({ component: c, count: 0 })
    }
    for (const e of ENTITIES) {
      const row = components.find((x) => x.component === COMPONENT[e.kind])
      if (row) row.count++
    }
    return {
      total: ENTITIES.length,
      writable: ENTITIES.filter((e) => e.writable).length,
      groups,
      components,
    }
  },
}
