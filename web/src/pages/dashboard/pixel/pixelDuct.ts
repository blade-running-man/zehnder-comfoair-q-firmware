// The ducts of the unit scene (pixelUnit.ts), painted onto its canvas: grey spiral-duct pipe lit
// from the upper left, ribbed couplings along the straight runs, swept elbows, a silencer and a
// tee — the fittings the real installation is built from. The air shows through the pipe only as
// a glow along its middle and as packets of light gliding along it, in the air's colour, and the
// pipe's outline is tinted the same way, so a duct reads warm or cold even between packets.

import { litOffset, packetsAt, rasterize, type PipePixel, type PipeRun, type PipeSample } from "./pixelPipe";

/** The scene's one drawing primitive: a w×h block of `color` at (x, y), rounded to whole pixels. */
export type Px = (x: number, y: number, color: string, alpha: number, w?: number, h?: number) => void;

const P_OUT = "#0e1016";
// The cylinder's tones from the lit side round to the shaded one; index 0 faces the light.
const PIPE_TONES = ["#c9d0dc", "#aab2c0", "#8c95a5", "#6f7889", "#5a6273", "#474e5d"];
const RING_TONES = ["#e4e9f0", "#c3cad5", "#a3abb9", "#848d9e", "#6a7383", "#3c4250"];
const SILENCER_TONES = ["#d9dee6", "#bfc6d1", "#a5adbb", "#8b94a4", "#737c8c", "#5b6373"];

export const PIPE_HALF = 4;           // a 9px pipe
const RING_HALF = 5;                  // couplings stand a pixel proud of it
const SILENCER_HALF = 7;              // the silencer is a fat drum on the run
const N_PACKETS = 3, TAIL = 6;

/**
 * One cross-section of a cylinder `half` pixels either side of the sample, toned by how lit each
 * point is; the outermost pixel is the outline. Only for STRAIGHT runs — the couplings and the
 * silencer; a pipe's elbows go through rasterize().
 */
function section(px: Px, s: PipeSample, half: number, tones: readonly string[], outline: string, outAlpha: number): void {
  for (let k = -half; k <= half; k += 0.5) {
    const x = s.x + s.nx * k, y = s.y + s.ny * k;
    if (Math.abs(k) > half - 0.75) { px(x, y, outline, outAlpha); continue; }
    px(x, y, tone(tones, litOffset(s, k) / (half - 1)), 1);
  }
}

export interface DuctLook {
  /** Where couplings go: every `pitch` samples counted from the run's start, clear of the elbows. */
  ringPitch: number;
  ringFrom: number;
  /** A silencer drum over samples [from, to), or none. */
  silencer?: [number, number];
}

/** A run's body, rasterized once (rasterize() is far too slow to run every frame). */
export function pipeBody(run: PipeRun): PipePixel[] {
  return rasterize(run, PIPE_HALF);
}

const tone = (tones: readonly string[], lit: number) =>
  tones[Math.max(0, Math.min(tones.length - 1, Math.round(((1 - lit) / 2) * (tones.length - 1))))];

/** The bare pipe with its outline tinted by the air in it. Paint the back runs first. */
export function drawPipe(px: Px, run: PipeRun, body: PipePixel[], air: string, inten: number, look: DuctLook): void {
  for (const p of body) {
    if (p.edge) { px(p.x, p.y, P_OUT, 1); px(p.x, p.y, air, 0.55 * inten); } else px(p.x, p.y, tone(PIPE_TONES, p.lit), 1);
  }
  for (const s of run.samples) px(s.x, s.y, air, 0.35 * inten);
  for (let i = look.ringFrom; i < run.total - 3; i += look.ringPitch) {
    if (run.samples.slice(Math.max(0, i - 6), i + 7).some((s) => s.bend)) continue;
    if (look.silencer && i > look.silencer[0] - 5 && i < look.silencer[1] + 5) continue;
    drawRing(px, run.samples, i);
  }
}

/**
 * A ribbed coupling: three samples of ring a pixel proud of the pipe, with a dark seam either
 * side where the duct sections butt into it.
 */
function drawRing(px: Px, samples: PipeSample[], i: number): void {
  for (const j of [i - 2, i + 2]) {
    const s = samples[j];
    if (s) for (let k = -PIPE_HALF + 1; k <= PIPE_HALF - 1; k++) px(s.x + s.nx * k, s.y + s.ny * k, P_OUT, 0.55);
  }
  for (let j = i - 1; j <= i + 1; j++) if (samples[j]) section(px, samples[j], RING_HALF, RING_TONES, P_OUT, 1);
}

/** The air moving through: short bright packets with a fading tail, the head leading. */
export function drawPackets(px: Px, run: PipeRun, air: string, phase: number, inten: number, count = N_PACKETS): void {
  for (const head of packetsAt(phase, count, run.total)) {
    for (let t = 0; t < TAIL; t++) {
      const s = run.samples[head - t];
      if (!s) break;
      const a = (1 - t / TAIL) * inten;
      for (let k = -1; k <= 1; k++) px(s.x + s.nx * k, s.y + s.ny * k, air, k === 0 ? a : a * 0.6);
    }
  }
}

/** The silencer: a fat drum over part of a straight run, a rolled lip at each end. */
export function drawSilencer(px: Px, run: PipeRun, from: number, to: number): void {
  for (let i = from; i < to; i++) section(px, run.samples[i], SILENCER_HALF, SILENCER_TONES, P_OUT, 1);
  for (const i of [from, from + 1, to - 2, to - 1]) section(px, run.samples[i], SILENCER_HALF, RING_TONES, P_OUT, 1);
}

/** A tee where a branch leaves the run: a square fitting a coupling's width across. */
export function drawTee(px: Px, x: number, y: number): void {
  const w = RING_HALF * 2 + 1, x0 = x - RING_HALF, y0 = y - RING_HALF;
  px(x0, y0, P_OUT, 1, w, w);
  px(x0 + 1, y0 + 1, RING_TONES[3], 1, w - 2, w - 2);
  px(x0 + 1, y0 + 1, RING_TONES[1], 1, w - 2, 2);
  px(x0 + 1, y0 + 1, RING_TONES[1], 1, 2, w - 2);
  px(x0 + 2, y0 + 2, RING_TONES[0], 1, w - 5, 1);
  px(x0 + 1, y0 + w - 2, RING_TONES[5], 1, w - 2, 1);
  px(x0 + w - 2, y0 + 1, RING_TONES[5], 1, 1, w - 2);
}
