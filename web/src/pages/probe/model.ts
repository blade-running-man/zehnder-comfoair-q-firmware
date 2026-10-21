// The probe page's pure half: how a report is grouped, filtered and spelled, with no DOM, no
// signals and no t(). Every helper that needs a translated word returns a Cell -- a tagged
// value the page maps to a catalog key at render -- so this file runs under node in the suite
// (tests/model.test.ts) and a language switch still re-renders every word.
//
// Imports are TYPE-only, and carry .ts: tests/harness.ts explains both constraints.

import type {
  ProbeActiveErrors, ProbeAnswer, ProbeProperty, ProbeRange, ProbeReport, ProbeUnit,
} from "../../api/probe.ts";

/** One displayable value. `text` is shown as is; the other kinds are words the page translates. */
export type Cell =
  | { kind: "text"; text: string }
  | { kind: "error"; name: string }
  | { kind: "timeout" }
  | { kind: "redacted" }
  | { kind: "none" };

/** The NODE unit: the identity properties (serial, firmware, model) live there on every node. */
export const NODE_UNIT = 0x01;

/** This device's own node id on the bus (0x2A; the unit is 0x01, docs/protocol/probe.md). The
 *  firmware's census never lists it -- TWAI does not receive its own frames -- so this only labels a node
 *  should a future census include it. */
export const OUR_NODE = 0x2a;

export function hex2(n: number): string {
  return "0x" + n.toString(16).toUpperCase().padStart(2, "0");
}

// Trailing zeros come from a divider, not from precision: 21.5 stays 21.5 and 3 stays 3. Two
// decimals is the finest any RMI format this firmware decodes carries.
function num(n: number): string {
  return Number.isInteger(n) ? String(n) : String(Math.round(n * 100) / 100);
}

function withUnit(n: number, unitLabel: string | null): string {
  return unitLabel ? `${num(n)} ${unitLabel}` : num(n);
}

/**
 * An answer as one cell: label ?? number + unit ?? text ?? hex.
 *
 * The label wins over the number because a label IS the number named ("Normal" for profile 0);
 * the hex is the last resort and never empty for data, so a value the firmware could not decode
 * still shows what the unit sent. One exception: a "version" carries both the raw u32 as
 * `number` and "R1.4.0" as `text`, and the raw u32 means nothing to a reader -- text first.
 */
export function answerCell(
  a: ProbeAnswer | null, unitLabel: string | null = null, format: string | null = null,
): Cell {
  if (a === null) return { kind: "none" };
  if ("timeout" in a) return { kind: "timeout" };
  if ("redacted" in a) return { kind: "redacted" };
  if ("error" in a) return { kind: "error", name: a.error_name };
  if (a.label !== null) return { kind: "text", text: a.label };
  if (format === "version" && a.text !== null) return { kind: "text", text: a.text };
  if (a.number !== null) return { kind: "text", text: withUnit(a.number, unitLabel) };
  if (a.text !== null) return { kind: "text", text: a.text };
  return { kind: "text", text: a.hex };
}

/** "min – max", the unit label once at the end. A range with one bound missing shows its hex:
 *  half a range printed as a range would claim a limit the unit did not state. */
export function rangeCell(r: ProbeRange | null, unitLabel: string | null = null): Cell {
  if (r === null) return { kind: "none" };
  if ("timeout" in r) return { kind: "timeout" };
  if ("redacted" in r) return { kind: "redacted" };
  if ("error" in r) return { kind: "error", name: r.error_name };
  if (r.min !== null && r.max !== null) {
    const tail = unitLabel ? ` ${unitLabel}` : "";
    return { kind: "text", text: `${num(r.min)} – ${num(r.max)}${tail}` };
  }
  return { kind: "text", text: r.hex };
}

/** What the unit literally sent, for the raw column. Empty where nothing was sent. */
export function rawHex(a: ProbeAnswer): string {
  return "hex" in a ? a.hex : "";
}

export interface UnitGroup {
  unit: number;
  /** units[]'s name; null when the firmware's table has none (the page says "Unit 0xNN"). */
  name: string | null;
  rows: ProbeProperty[];
}

function byPosition(a: ProbeProperty, b: ProbeProperty): number {
  return a.unit - b.unit || a.subunit - b.subunit || a.property - b.property;
}

/**
 * The target node's properties, one group per unit, in unit order, rows by subunit then id.
 *
 * Grouped from the PROPERTIES, not from units[]: a unit that answered properties is shown even
 * if its units[] row is missing (a truncated report), so nothing the unit said goes unseen.
 * units[] only supplies the name.
 */
export function groupProperties(report: ProbeReport, unknownOnly = false): UnitGroup[] {
  const names = new Map<number, string | null>();
  for (const u of report.units) if (u.node === report.node) names.set(u.unit, u.name);
  const groups = new Map<number, ProbeProperty[]>();
  for (const p of report.properties) {
    if (p.node !== report.node) continue;
    if (unknownOnly && p.name !== null) continue;
    const list = groups.get(p.unit);
    if (list) list.push(p);
    else groups.set(p.unit, [p]);
  }
  return [...groups.keys()].sort((a, b) => a - b).map((unit) => ({
    unit,
    name: names.get(unit) ?? null,
    rows: groups.get(unit)!.sort(byPosition),
  }));
}

/** The target node's units, in unit order. */
export function targetUnits(report: ProbeReport): ProbeUnit[] {
  return report.units.filter((u) => u.node === report.node).sort((a, b) => a.unit - b.unit);
}

/** The NODE unit's properties on `node`: the identity block (serial, firmware, model, name). */
export function identityRows(report: ProbeReport, node: number): ProbeProperty[] {
  return report.properties
    .filter((p) => p.node === node && p.unit === NODE_UNIT)
    .sort(byPosition);
}

/** Every node seen except the target, with the identity rows the walk read from it. */
export function otherNodes(report: ProbeReport): Array<{ node: number; rows: ProbeProperty[] }> {
  return [...report.nodes_seen]
    .filter((n) => n !== report.node)
    .sort((a, b) => a - b)
    .map((node) => ({ node, rows: identityRows(report, node) }));
}

/** Active errors as a cell: the ids, an empty list as "none" (the page's word), or the answer. */
export function activeErrorsCell(e: ProbeActiveErrors | null): Cell | { kind: "ids"; ids: number[] } {
  if (e === null) return { kind: "none" };
  if ("timeout" in e) return { kind: "timeout" };
  if ("error" in e) return { kind: "error", name: e.error_name };
  return { kind: "ids", ids: [...e.ids].sort((a, b) => a - b) };
}

/** m:ss, or h:mm:ss past an hour. A walk is minutes long; milliseconds would be noise. */
export function formatElapsed(ms: number): string {
  const total = Math.max(0, Math.floor(ms / 1000));
  const h = Math.floor(total / 3600);
  const m = Math.floor((total % 3600) / 60);
  const s = String(total % 60).padStart(2, "0");
  return h > 0 ? `${h}:${String(m).padStart(2, "0")}:${s}` : `${m}:${s}`;
}

/** comfoair-probe-2026-10-17T14-03-09.json, in local time: a name that sorts, and that every
 *  file system accepts (no colons). */
export function downloadName(d: Date): string {
  const p = (n: number): string => String(n).padStart(2, "0");
  const stamp = `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}` +
    `T${p(d.getHours())}-${p(d.getMinutes())}-${p(d.getSeconds())}`;
  return `comfoair-probe-${stamp}.json`;
}

/** Whether there is anything to show beyond the controls: a report that has started. */
export function hasContent(report: ProbeReport): boolean {
  return report.state !== "idle" || report.units.length > 0 || report.properties.length > 0;
}
