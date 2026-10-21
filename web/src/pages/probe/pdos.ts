// The PDO census card's pure half: what the census means beside the entity table, and how its
// numbers are spelled. No DOM, no signals, no t(): every word is chosen by PdosSection.tsx from
// a tag this file returns, so this runs under node in tests/pdos.test.ts.
//
// Imports carry .ts and reach nothing with CSS or the DOM (tests/harness.ts explains both).
// ENTITIES is a VALUE import on purpose: "in the table" is the generated entity list, never a
// hand-written list of PDO numbers here -- that would be the second entity list AGENTS.md forbids.

import { ENTITIES, type EntityMeta } from "../../api/entities.ts";
import type { ProbePdos } from "../../api/probe.ts";

/**
 * Under this uptime the census cannot yet say "never heard". Every table PDO is requested once
 * at boot and again each hour, so until the second request a PDO the unit does send may only have
 * missed its one answer; 65 minutes is one hourly poll plus five minutes for the boot sweep
 * and the bus to settle.
 */
export const CAUTION_MS = 65 * 60_000;

/** The device clock is a u32 of milliseconds; JS numbers are not, so the difference is wrapped
 *  back into u32 range. After the 49.7-day wrap, `last_ms` is ahead of `uptime_ms` and a plain
 *  subtraction would be negative. */
export function ageMs(uptimeMs: number, lastMs: number): number {
  return (uptimeMs - lastMs) >>> 0;
}

export type AgeUnit = "s" | "min" | "h" | "d";

/** One unit, truncated: "how long ago" needs a magnitude, not a stopwatch. Hours run to two
 *  days so "30 h" stays readable as "since yesterday" rather than "1 d". */
export function humanAge(ms: number): { n: number; unit: AgeUnit } {
  const s = Math.floor(Math.max(0, ms) / 1000);
  if (s < 60) return { n: s, unit: "s" };
  if (s < 3600) return { n: Math.floor(s / 60), unit: "min" };
  if (s < 48 * 3600) return { n: Math.floor(s / 3600), unit: "h" };
  return { n: Math.floor(s / 86400), unit: "d" };
}

/**
 * Whether the census is too young to trust its gaps.
 *
 * A row first or last heard AFTER `uptime_ms` cannot be, unless the u32 clock wrapped: the
 * device has then been up at least 49.7 days, which is far past the threshold, however small
 * `uptime_ms` reads. Both stamps are checked: a PDO first heard early may still have been heard
 * last just before the wrap. DO NOT drop that check: without it a long-running unit shows the caution for the
 * first hour after every wrap.
 */
export function tooEarly(p: ProbePdos): boolean {
  if (p.rows.some((r) => r.first_ms > p.uptime_ms || r.last_ms > p.uptime_ms)) return false;
  return p.uptime_ms < CAUTION_MS;
}

/** Sorted, de-duplicated, each value spelled; "—" for an empty list so the cell is never blank.
 *  The firmware already sends these ascending; sorting again costs nothing and survives a
 *  firmware that does not. */
export function listText(values: number[], spell: (n: number) => string = String): string {
  if (values.length === 0) return "—";
  return [...new Set(values)].sort((a, b) => a - b).map(spell).join(", ");
}

/** "0102ff" as "01 02 ff": bytes read as bytes. */
export function spacedHex(hex: string): string {
  return (hex.match(/../g) ?? []).join(" ");
}

/** Rows the firmware's entity table does not know: the reason this card exists. */
export function unlistedCount(p: ProbePdos): number {
  return p.rows.filter((r) => !r.known).length;
}

export interface NeverHeard {
  pdo: number;
  entities: Array<{ key: string; enabledByDefault: boolean }>;
  /** "off": every entity on it is disabled by default -- an accessory's PDO, expected to be
   *  missing on a unit without that accessory. "mixed": some are. */
  defaults: "on" | "off" | "mixed";
}

/**
 * The entity table's PDOs that were never heard, ascending, one item per PDO.
 *
 * Several entities may decode one PDO (one frame, several fields), so the item carries every
 * key. With `overflow > 0` a PDO may have been heard yet not recorded; the card says so beside
 * the list rather than this function guessing.
 */
export function neverHeard(p: ProbePdos, entities: readonly EntityMeta[] = ENTITIES): NeverHeard[] {
  const heard = new Set(p.rows.map((r) => r.pdo));
  const byPdo = new Map<number, NeverHeard["entities"]>();
  for (const e of entities) {
    if (e.pdo === null || heard.has(e.pdo)) continue;
    const list = byPdo.get(e.pdo);
    const item = { key: e.key, enabledByDefault: e.enabledByDefault };
    if (list) list.push(item);
    else byPdo.set(e.pdo, [item]);
  }
  return [...byPdo.keys()].sort((a, b) => a - b).map((pdo) => {
    const list = byPdo.get(pdo)!;
    const on = list.filter((e) => e.enabledByDefault).length;
    const defaults = on === list.length ? "on" : on === 0 ? "off" : "mixed";
    return { pdo, entities: list, defaults };
  });
}
