// Build-time data for the "Ventilation unit" section: every RMI unit and property a real
// ComfoAir Q350 answered, joined with what is known about each one.
//
// WHY THE DATA LIVES IN FILES AND IS NOT HAND-COPIED INTO PAGES. The unit's answers come from one
// probe run (data/q350-probe.json, the firmware's own probe report with the serial number
// redacted) and the meaning of each answer from one annotation file (data/annotations.json). A
// table typed into a page would be a third copy: it would drift from the dump on the next probe
// and from the annotations on the next correction, and nothing would notice. Pages render these
// two files through the components in .vitepress/theme/components/, so a re-probe or a corrected
// annotation reaches every page on the next build.
//
// WHY load() THROWS ON A MISMATCH. The join is only honest if it is total: a dump property with
// no annotation would render as an unexplained row, and an annotation with no dump property would
// describe something the unit never said. Either is a build failure, not a blank cell.
//
// DO NOT import the raw dump at the repository root: it carries the unit's serial number. The
// copy under data/ is the redacted one, and it is the only dump this loader reads.
import { readFileSync } from 'node:fs'
import { basename } from 'node:path'

type Confidence = 'confirmed' | 'inferred' | 'unknown'

/** One value field of the dump as the probe report renders it (value, range or step). */
interface DumpField {
  hex?: string
  len?: number
  number?: number | null
  text?: string | null
  label?: string | null
  min?: number | null
  max?: number | null
  error?: number
  error_name?: string
  timeout?: boolean
  redacted?: boolean
}

interface DumpUnit {
  node: number
  unit: number
  name: string | null
  status: string
  subunits: number
  absent_properties: number
}

interface DumpProperty {
  node: number
  unit: number
  subunit: number
  property: number
  name: string | null
  access: string | null
  format: string | null
  unit_label: string | null
  value: DumpField | null
  range: DumpField | null
  step: DumpField | null
}

interface Dump {
  state: string
  elapsed_ms: number
  progress: { requests: number; timeouts: number }
  nodes_seen: number[]
  units: DumpUnit[]
  properties: DumpProperty[]
  active_errors: { hex: string; ids: number[] }
  truncated: boolean
}

interface UnitAnnotation {
  name: string | null
  purpose: string
  area: string
  confidence: Confidence
  sources: string[]
}

interface PropertyAnnotation {
  name: string | null
  meaning: string
  unit_of_measure: string | null
  encoding: string | null
  writable: boolean | null
  confidence: Confidence
  sources: string[]
  firmware: string | null
  sensitive: boolean
}

interface Annotations {
  units: Record<string, UnitAnnotation>
  properties: Record<string, PropertyAnnotation>
  areas: Record<string, { title: string; summary: string }>
}

export interface ProbeMeta {
  state: string
  elapsed_ms: number
  requests: number
  timeouts: number
  nodes_seen: number[]
  truncated: boolean
  active_errors: number[]
  units_present: number
  units_absent: number
  properties: number
}

export interface AreaInfo {
  slug: string
  title: string
  summary: string
  units: number[]
  property_count: number
  /** Properties of the area's units by annotation confidence. */
  confidence: Record<Confidence, number>
}

export interface UnitInfo {
  unit: number
  hex: string
  /** The dump's name, else the annotation's, else null. */
  name: string | null
  status: string
  subunits: number
  absent_properties: number
  purpose: string
  area: string
  confidence: Confidence
  sources: string[]
  property_count: number
}

export interface PropertyInfo {
  key: string
  unit: number
  subunit: number
  property: number
  unit_name: string | null
  area: string
  format: string | null
  access: string | null
  /** What the page shows for the value: label, text, number, else hex; or why there is none. */
  value: string
  /** The raw bytes as hex, or null when the unit returned none (error, redacted). */
  raw_hex: string | null
  range_min: number | null
  range_max: number | null
  step: number | null
  name: string | null
  meaning: string
  unit_of_measure: string | null
  encoding: string | null
  writable: boolean | null
  confidence: Confidence
  sources: string[]
  firmware: string | null
  sensitive: boolean
  redacted: boolean
}

export interface ProbeData {
  meta: ProbeMeta
  /** Areas in annotations.json order (the order the sidebar lists them). */
  areas: Record<string, AreaInfo>
  units: UnitInfo[]
  properties: PropertyInfo[]
}

const CONFIDENCE: Confidence[] = ['confirmed', 'inferred', 'unknown']

// Sensitive properties whose value is published anyway. The country (1.1.13) is marked sensitive
// because it narrows down where a unit is, but a country is not identifying on its own and the
// pages use it to explain the country-dependent defaults. The serial number (1.1.4) identifies one
// physical unit and is redacted in the dump copy; the maintainer password (32.1.3) is redacted by
// the firmware's probe report itself. Any other property marked sensitive must arrive redacted.
const PUBLISHED_SENSITIVE = new Set(['1.1.13'])

function valueText(v: DumpField | null): string {
  if (v === null || v === undefined) return '—'
  if (v.redacted) return 'redacted'
  if (v.timeout) return 'timeout'
  if (v.error !== undefined) return `error ${v.error}${v.error_name ? ` (${v.error_name})` : ''}`
  if (v.label !== null && v.label !== undefined) return v.label
  if (v.text !== null && v.text !== undefined) return v.text === '' ? '(empty)' : v.text
  if (v.number !== null && v.number !== undefined) return String(v.number)
  return v.hex ? `0x${v.hex}` : '—'
}

const num = (x: number | null | undefined): number | null => (typeof x === 'number' ? x : null)

function fail(msg: string): never {
  throw new Error(`unit/probe.data.ts: ${msg}`)
}

export default {
  watch: ['./data/q350-probe.json', './data/annotations.json'],
  load(files: string[]): ProbeData {
    const dumpFile = files.find((f) => basename(f) === 'q350-probe.json')
    const annFile = files.find((f) => basename(f) === 'annotations.json')
    if (!dumpFile) fail('data/q350-probe.json not found')
    if (!annFile) fail('data/annotations.json not found')
    const dump: Dump = JSON.parse(readFileSync(dumpFile, 'utf8'))
    const ann: Annotations = JSON.parse(readFileSync(annFile, 'utf8'))

    // The join, both directions.
    const unitKeys = new Set(dump.units.map((u) => String(u.unit)))
    const propKey = (p: DumpProperty) => `${p.unit}.${p.subunit}.${p.property}`
    const propKeys = new Set(dump.properties.map(propKey))
    for (const k of unitKeys) if (!ann.units[k]) fail(`dump unit ${k} has no annotation`)
    for (const k of Object.keys(ann.units)) if (!unitKeys.has(k)) fail(`annotated unit ${k} is not in the dump`)
    for (const k of propKeys) if (!ann.properties[k]) fail(`dump property ${k} has no annotation`)
    for (const k of Object.keys(ann.properties)) if (!propKeys.has(k)) fail(`annotated property ${k} is not in the dump`)
    for (const [k, u] of Object.entries(ann.units)) {
      if (!ann.areas[u.area]) fail(`unit ${k} names unknown area '${u.area}'`)
    }
    // A sensitive property must reach the site redacted, unless it is published on purpose.
    for (const p of dump.properties) {
      const a = ann.properties[propKey(p)]
      if (a.sensitive && !PUBLISHED_SENSITIVE.has(propKey(p)) && !p.value?.redacted) {
        fail(`sensitive property ${propKey(p)} is not redacted in the dump copy`)
      }
    }

    const unitName = new Map<number, string | null>()
    const unitArea = new Map<number, string>()
    for (const u of dump.units) {
      unitName.set(u.unit, u.name ?? ann.units[String(u.unit)].name ?? null)
      unitArea.set(u.unit, ann.units[String(u.unit)].area)
    }

    const properties: PropertyInfo[] = dump.properties
      .slice()
      .sort((a, b) => a.unit - b.unit || a.subunit - b.subunit || a.property - b.property)
      .map((p) => {
        const key = propKey(p)
        const a = ann.properties[key]
        const v = p.value
        const r = p.range
        return {
          key,
          unit: p.unit,
          subunit: p.subunit,
          property: p.property,
          unit_name: unitName.get(p.unit) ?? null,
          area: unitArea.get(p.unit)!,
          format: p.format,
          access: p.access,
          value: valueText(v),
          raw_hex: v && !v.redacted && v.error === undefined && v.hex ? v.hex : null,
          range_min: r ? num(r.min) : null,
          range_max: r ? num(r.max) : null,
          step: p.step ? num(p.step.number) : null,
          name: a.name ?? p.name ?? null,
          meaning: a.meaning,
          unit_of_measure: a.unit_of_measure ?? p.unit_label ?? null,
          encoding: a.encoding,
          writable: a.writable,
          confidence: a.confidence,
          sources: a.sources,
          firmware: a.firmware,
          sensitive: a.sensitive,
          redacted: Boolean(v?.redacted),
        }
      })

    const units: UnitInfo[] = dump.units
      .slice()
      .sort((a, b) => a.unit - b.unit)
      .map((u) => {
        const a = ann.units[String(u.unit)]
        return {
          unit: u.unit,
          hex: `0x${u.unit.toString(16).toUpperCase().padStart(2, '0')}`,
          name: unitName.get(u.unit) ?? null,
          status: u.status,
          subunits: u.subunits,
          absent_properties: u.absent_properties,
          purpose: a.purpose,
          area: a.area,
          confidence: a.confidence,
          sources: a.sources,
          property_count: properties.filter((p) => p.unit === u.unit).length,
        }
      })

    const areas: Record<string, AreaInfo> = {}
    for (const [slug, a] of Object.entries(ann.areas)) {
      const inArea = properties.filter((p) => p.area === slug)
      const confidence = Object.fromEntries(
        CONFIDENCE.map((c) => [c, inArea.filter((p) => p.confidence === c).length]),
      ) as Record<Confidence, number>
      areas[slug] = {
        slug,
        title: a.title,
        summary: a.summary,
        units: units.filter((u) => u.area === slug).map((u) => u.unit),
        property_count: inArea.length,
        confidence,
      }
    }

    return {
      meta: {
        state: dump.state,
        elapsed_ms: dump.elapsed_ms,
        requests: dump.progress.requests,
        timeouts: dump.progress.timeouts,
        nodes_seen: dump.nodes_seen,
        truncated: dump.truncated,
        active_errors: dump.active_errors.ids,
        units_present: dump.units.filter((u) => u.status === 'present').length,
        units_absent: dump.units.filter((u) => u.status !== 'present').length,
        properties: properties.length,
      },
      areas,
      units,
      properties,
    }
  },
}
