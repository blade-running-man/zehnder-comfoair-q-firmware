// The duct geometry — the part of the unit scene's pipes that runs without a canvas.
//
// Run: node src/pages/dashboard/tests/pixelPipe.test.ts
//
// Pins that a run starts and ends where it is told (the end is the end, not a wrap), that an
// elbow is swept and stays a quarter turn, that the normal stays on the travel's left through
// the elbow (a flipped normal paints the dark side where the light was), that the lit side is
// the upper left, and that the rasterized body has no holes and its ends are cut square.

import { litOffset, packetsAt, pipeRun, rasterize } from "../pixel/pixelPipe.ts";
import { eq, ok, report } from "../../settings/tests/harness.ts";

const near = (a: number, b: number) => Math.abs(a - b) < 1e-6;

// --- the run --------------------------------------------------------------------------------
const run = pipeRun([[0, 10], [40, 10], [40, 40]], 6);
const first = run.samples[0], last = run.samples[run.total];
eq([first.x, first.y], [0, 10], "sample 0 is the start");
ok(near(last.x, 40) && near(last.y, 40), "sample[total] is the end, not a wrap to the start");
eq(run.samples.length, run.total + 1, "one sample per pixel of length, both ends included");
ok(Math.abs(run.total - (34 + 24 + (Math.PI / 2) * 6)) < 1, "an elbow shortens the run by its swept corner");
ok(run.samples.some((s) => s.bend), "the corner is swept, not cut");
ok(!run.samples.some((s) => s.bend && Math.hypot(s.x - 40, s.y - 10) < 6 * (Math.SQRT2 - 1) - 0.01),
  "the elbow rounds inside the corner, never through it");

// The normal turns smoothly: consecutive samples never point it more than a few degrees apart.
let maxTurn = 0;
for (let i = 1; i <= run.total; i++) {
  const a = run.samples[i - 1], b = run.samples[i];
  maxTurn = Math.max(maxTurn, Math.acos(Math.max(-1, Math.min(1, a.nx * b.nx + a.ny * b.ny))));
}
ok(maxTurn < 0.3, "the normal never flips through the elbow");
eq([first.nx, first.ny], [0, -1], "heading right, the left-hand normal points up");
ok(near(last.nx, 1) && near(last.ny, 0), "heading down, the left-hand normal points right");

const sharp = pipeRun([[0, 0], [10, 0], [10, 10]], 0);
ok(!sharp.samples.some((s) => s.bend), "radius 0 leaves the corner sharp — a tee covers it");
const tight = pipeRun([[0, 0], [4, 0], [4, 4]], 10);
ok(tight.samples.every((s) => s.x >= -1e-9 && s.y >= -1e-9), "an elbow clamps to the legs it joins");

let threw = false;
try { pipeRun([[0, 0], [3, 1]], 4); } catch { threw = true; }
ok(threw, "a leg that is not axis-aligned is refused, not drawn with gaps");

// --- light ----------------------------------------------------------------------------------
ok(litOffset(first, 2) > 0, "on a horizontal run the upper side is lit");
ok(litOffset({ x: 0, y: 0, nx: -1, ny: 0, bend: false }, 2) > 0, "on a vertical run the left side is lit");
ok(near(litOffset({ x: 0, y: 0, nx: -Math.SQRT1_2, ny: Math.SQRT1_2, bend: true }, 3), 0),
  "half-way round an elbow the light falls across both sides alike");

// --- the body -------------------------------------------------------------------------------
const body = rasterize(run, 4);
const at = new Map(body.map((p) => [`${p.x},${p.y}`, p]));
let holes = 0;
for (const s of run.samples) {
  for (let k = -3; k <= 3; k++) if (!at.has(`${Math.round(s.x + s.nx * k)},${Math.round(s.y + s.ny * k)}`)) holes++;
}
eq(holes, 0, "every pixel inside the pipe is painted, round the elbow too");
ok(body.every((p) => p.lit >= -1 && p.lit <= 1), "the light stays in -1..1");
ok(at.get("20,6")?.edge === true && at.get("20,14")?.edge === true, "the outermost row on either side is outline");
ok(at.get("20,10")?.edge === false, "the centre is not");
ok((at.get("20,7")?.lit ?? 0) > (at.get("20,13")?.lit ?? 0), "the upper side is lit, the lower shaded");
ok(!body.some((p) => p.x < 0), "the start is cut square, not rounded past its end");

// --- packets --------------------------------------------------------------------------------
eq(packetsAt(0, 4, 40), [0, 10, 20, 30], "packets are spaced evenly along the run");
ok(packetsAt(45, 4, 40).every((d) => d >= 0 && d < 40), "a phase past the end wraps every packet");

report("pixelPipe");
