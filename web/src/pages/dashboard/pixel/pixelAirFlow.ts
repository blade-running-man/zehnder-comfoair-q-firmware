// The air streams' dynamics and colours — the pure maths behind the air drawn by pixelUnit.ts
// (the duct geometry itself is pixelPipe.ts). No DOM here, so it can be pinned by a host test
// without a browser.

/** The ComfoAir Q ceiling, used only to scale the drift; never shown as a number. */
export const MAX_FLOW = 350;

/**
 * Stream speed as a fraction, 0..1, from its measured airflow. A stopped fan stands still (that is
 * a real state and must look like one); an unknown flow drifts at a modest pace rather than
 * freezing, so a missing reading is not mistaken for a stopped fan.
 */
export function streamSpeed(flow: number | null, stopped: boolean): number {
  if (stopped) return 0;
  if (flow === null || !Number.isFinite(flow)) return 0.35;
  return 0.15 + 0.85 * Math.max(0, Math.min(1, flow / MAX_FLOW));
}

/** Linear blend of two #rrggbb colours; `t` is clamped to 0..1. */
export function mixColor(a: string, b: string, t: number): string {
  const k = Math.max(0, Math.min(1, t));
  const ch = (c: string, i: number) => parseInt(c.slice(1 + i * 2, 3 + i * 2), 16);
  let out = "#";
  for (let i = 0; i < 3; i++) {
    out += Math.round(ch(a, i) + (ch(b, i) - ch(a, i)) * k).toString(16).padStart(2, "0");
  }
  return out;
}

/**
 * A three-stop blend, a → mid → b. Cold cyan and warm orange mixed straight through RGB pass a
 * muddy green halfway; going through a violet middle keeps the half-exchanged air reading as
 * "between cold and warm", the way the ComfoAir's own diagram draws it.
 */
export function mixColor3(a: string, mid: string, b: string, t: number): string {
  const k = Math.max(0, Math.min(1, t));
  return k < 0.5 ? mixColor(a, mid, k * 2) : mixColor(mid, b, k * 2 - 1);
}
