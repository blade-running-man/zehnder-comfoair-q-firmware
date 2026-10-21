// The unit scene's fan and level-meter maths — the pure part of pixelUnit.ts. No DOM here, so it
// can be pinned by a host test without a browser.

import { MAX_FLOW, streamSpeed } from "./pixelAirFlow.ts";

/** The airflow meter on the unit's front: five cells, one per fifth of the unit's ceiling. */
export const LEVEL_CELLS = 5;

/**
 * Lit cells for a measured airflow. A cell lights once any of its fifth is reached, so a fan that
 * is turning at all shows at least one cell; a stopped fan or an unknown flow shows none — the
 * meter states what was measured and never guesses a level.
 */
export function levelCells(flow: number | null, stopped: boolean): number {
  if (stopped || flow === null || !Number.isFinite(flow) || flow <= 0) return 0;
  return Math.max(1, Math.min(LEVEL_CELLS, Math.ceil((flow / MAX_FLOW) * LEVEL_CELLS - 1e-9)));
}

/** Radians a fan turns per frame at full speed; scaled by the stream speed of its own airflow. */
export const FAN_STEP_MAX = 0.32;

export function fanStep(flow: number | null, stopped: boolean): number {
  return FAN_STEP_MAX * streamSpeed(flow, stopped);
}
