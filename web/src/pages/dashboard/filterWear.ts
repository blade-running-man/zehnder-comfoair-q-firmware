// The filter's wear as numbers — the pure maths behind the Filters card and its pixel scene
// (pixelFilters.ts). No DOM here, so it can be pinned by a host test without a browser.
//
// The unit reports days REMAINING (null when unknown, zero or negative once overdue), never the
// days used, so everything here is derived from that one number and the nominal cycle.

export type Wear = "unknown" | "good" | "soon" | "overdue";

/** Days at or under which the card says "order a replacement". */
export const SOON_DAYS = 30;

export interface WearState {
  known: boolean;
  /** Days remaining, 0 when unknown — a safe number for arithmetic; gate display on `known`. */
  days: number;
  /** Fraction of the cycle used, 0..1; 0 when unknown so an unread filter is drawn clean. */
  used: number;
  wear: Wear;
}

export function filterWear(days: number | null, cycle: number): WearState {
  if (days === null || !Number.isFinite(days)) return { known: false, days: 0, used: 0, wear: "unknown" };
  const used = Math.max(0, Math.min(1, 1 - days / cycle));
  const wear: Wear = days <= 0 ? "overdue" : days <= SOON_DAYS ? "soon" : "good";
  return { known: true, days, used, wear };
}

/** The life bar's segment count: one cell per ten days of the 180-day cycle. */
export const LIFE_CELLS = 18;

/**
 * Cells already spent. A cell stays LIT while any of its ten days is left, so the bar goes dark
 * only when the life is really gone: rounding the other way blanked the whole bar with a day
 * still to run, which contradicted the "1 day left" printed beside it. Overdue spends them all.
 */
export function spentCells(used: number, cells = LIFE_CELLS): number {
  const left = Math.max(0, Math.min(1, 1 - used));
  return cells - Math.ceil(left * cells - 1e-9);
}

/**
 * Deterministic dust specks across a w×h face. A filter loads over its whole face, not from the
 * bottom up, so age shows as more specks, never a rising level. The LCG is seeded, so the SAME
 * reading always draws the SAME filter: a random speckle would reshuffle on every redraw and read
 * as the filter changing when it is not. Count scales with `used`.
 */
export function specks(used: number, seed: number, max: number, w: number, h: number): Array<[number, number]> {
  let s = seed >>> 0;
  const rnd = () => (s = (s * 1103515245 + 12345) % 2147483648) / 2147483648;
  const n = Math.round(Math.max(0, Math.min(1, used)) * max);
  const out: Array<[number, number]> = [];
  for (let i = 0; i < n; i++) out.push([Math.floor(rnd() * w), Math.floor(rnd() * h)]);
  return out;
}
