// web/mock/air.ts
// The bits of air physics the simulated unit needs, kept apart from evolve.ts so they can be
// tested as plain functions. Fidelity is "believable on a dashboard", not "matches a datasheet":
// the numbers are chosen so recovery, humidity and power move the way an owner expects.

/** First-order lag: how far `cur` moves toward `target` in dtS with time constant tauS. */
export function approach(cur: number, target: number, dtS: number, tauS: number): number {
  return cur + (target - cur) * (1 - Math.exp(-dtS / tauS));
}

export function clamp(v: number, lo: number, hi: number): number {
  return Math.min(hi, Math.max(lo, v));
}

/** Rounds to one decimal, which is the resolution of the unit's temperature PDOs. */
export const r1 = (v: number): number => Math.round(v * 10) / 10;

// Saturation vapour pressure (hPa), Magnus formula over water. Good to a fraction of a percent
// across -20..40 C, which is the whole range a ventilation unit ever sees.
function saturation(tC: number): number {
  return 6.112 * Math.exp((17.62 * tC) / (243.12 + tC));
}

/**
 * Relative humidity of the same air after it is heated or cooled from fromC to toC.
 *
 * Absolute humidity is conserved: a sensible-only heat exchanger moves heat and not water, so
 * cold outdoor air at 85 % arrives indoors at 25-30 %. That drop is the most recognisable thing
 * about a heat-recovery unit's humidity readings and a mock that kept RH constant would look
 * wrong to anybody who owns one. Capped at 100: past the dew point the surplus condenses.
 */
export function rhAt(rh: number, fromC: number, toC: number): number {
  const vapour = (rh / 100) * saturation(fromC);
  return clamp((100 * vapour) / saturation(toC), 1, 100);
}

/** Flow (m3/h) the unit drives at each ventilation level: Away, Low, Medium, High, Boost.
    A ComfoAir Q350's factory set-points, roughly. */
export const LEVEL_FLOW = [55, 110, 175, 250, 330] as const;

/**
 * Temperature efficiency of the counter-flow exchanger at a given flow.
 *
 * Falls with flow because the air spends less time in the core; 0.92 at Low, about 0.83 at Boost
 * is the shape of the published curves. DO NOT make it a constant: Boost reading the same
 * recovery as Low is the first thing a sharp-eyed owner would call out.
 */
export function efficiency(flow: number): number {
  return clamp(0.965 - 0.0004 * flow, 0.75, 0.95);
}

/** Heat carried by an air stream, W: rho * cp * flow * dT = 1.2 * 1005 / 3600 * flow * dT. */
export function airPowerW(flowM3h: number, deltaC: number): number {
  return 0.335 * flowM3h * deltaC;
}

/** Electrical power of the two EC fans, W. Quadratic in flow (fan laws, with duct losses). */
export function fanPowerW(supplyFlow: number, exhaustFlow: number): number {
  return 2.5 + 0.00038 * (supplyFlow * supplyFlow + exhaustFlow * exhaustFlow);
}

/** Fan duty (%) and speed (rpm) for a flow, zero when the fan is stopped. */
export function fanDuty(flow: number): number {
  return flow <= 0 ? 0 : clamp(Math.round((flow * 100) / 360), 0, 100);
}
export function fanRpm(flow: number): number {
  return flow <= 0 ? 0 : Math.round(600 + flow * 7.5);
}
