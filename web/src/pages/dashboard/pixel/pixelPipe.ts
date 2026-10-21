// The duct geometry behind the unit scene (pixelUnit.ts): a pipe run as a polyline of straight,
// axis-aligned legs whose corners are swept as quarter-circle elbows, sampled once per pixel of
// length. No DOM here, so it can be pinned by a host test without a browser.
//
// Each sample carries the pipe's centre and a unit normal, and the renderer paints the pipe's
// cross-section along that normal. The normal is always the one on the travel's left, so it turns
// smoothly through an elbow; which side is LIT is decided per pixel by litOffset(), not by
// flipping the normal — a normal flipped mid-elbow paints the dark side where the light was.

export interface PipeSample { x: number; y: number; nx: number; ny: number; bend: boolean }
/** A pipe run: one sample per pixel of length, samples[0] at the start, samples[total] at the end. */
export interface PipeRun { samples: PipeSample[]; total: number }

type Leg =
  | { kind: "line"; x0: number; y0: number; dx: number; dy: number; len: number }
  | { kind: "arc"; cx: number; cy: number; r: number; a0: number; sweep: number; len: number };

/**
 * The run through `points`, in the direction the air travels, every interior corner rounded to
 * `radius` (clamped to half of the shorter leg beside it, so two elbows never overlap). A radius
 * of 0 leaves the corner sharp — for a corner a tee fitting covers. Throws on a leg that is not
 * axis-aligned: a diagonal duct is not drawn here, and silently drawing one would leave gaps.
 */
export function pipeRun(points: Array<[number, number]>, radius: number): PipeRun {
  const n = points.length;
  for (let i = 0; i < n - 1; i++) {
    const [x0, y0] = points[i], [x1, y1] = points[i + 1];
    if (x0 !== x1 && y0 !== y1) throw new Error(`pixelPipe: leg ${i} is not axis-aligned`);
    if (x0 === x1 && y0 === y1) throw new Error(`pixelPipe: leg ${i} has no length`);
  }
  const legLen = (i: number) => Math.abs(points[i + 1][0] - points[i][0]) + Math.abs(points[i + 1][1] - points[i][1]);
  const dir = (i: number) => [Math.sign(points[i + 1][0] - points[i][0]), Math.sign(points[i + 1][1] - points[i][1])];
  // The cut at each interior corner: how far the elbow eats into the legs on either side.
  const cut = points.map((_, i) => (i === 0 || i === n - 1 ? 0 : Math.min(radius, legLen(i - 1) / 2, legLen(i) / 2)));

  const legs: Leg[] = [];
  for (let i = 0; i < n - 1; i++) {
    const [dx, dy] = dir(i);
    const len = legLen(i) - cut[i] - cut[i + 1];
    if (len > 0) legs.push({ kind: "line", x0: points[i][0] + dx * cut[i], y0: points[i][1] + dy * cut[i], dx, dy, len });
    const r = cut[i + 1];
    if (r > 0) {
      // The elbow's centre sits r along the incoming leg's turn side; the arc runs from the
      // incoming direction to the outgoing one, a quarter turn either way.
      const [ex, ey] = dir(i + 1);
      const [px, py] = points[i + 1];
      const cx = px - dx * r + ex * r, cy = py - dy * r + ey * r;
      const a0 = Math.atan2(py - dy * r - cy, px - dx * r - cx);
      const sweep = dx * ey - dy * ex > 0 ? Math.PI / 2 : -Math.PI / 2;
      legs.push({ kind: "arc", cx, cy, r, a0, sweep, len: (Math.PI / 2) * r });
    }
  }

  const total = Math.max(1, Math.round(legs.reduce((s, l) => s + l.len, 0)));
  const exact = legs.reduce((s, l) => s + l.len, 0);
  const samples: PipeSample[] = [];
  for (let k = 0; k <= total; k++) samples.push(at(legs, (k / total) * exact));
  return { samples, total };
}

function at(legs: Leg[], dist: number): PipeSample {
  let d = dist;
  for (let i = 0; i < legs.length; i++) {
    const l = legs[i];
    if (d <= l.len + 1e-9 || i === legs.length - 1) {
      const t = Math.min(d, l.len);
      if (l.kind === "line") return { x: l.x0 + l.dx * t, y: l.y0 + l.dy * t, nx: l.dy, ny: -l.dx, bend: false };
      const a = l.a0 + l.sweep * (t / l.len);
      // Travel tangent of the arc, then its left-hand normal — the same rule as the lines'.
      const s = Math.sign(l.sweep);
      const tx = -Math.sin(a) * s, ty = Math.cos(a) * s;
      return { x: l.cx + Math.cos(a) * l.r, y: l.cy + Math.sin(a) * l.r, nx: ty, ny: -tx, bend: true };
    }
    d -= l.len;
  }
  throw new Error("pixelPipe: empty run");
}

/**
 * How far toward the light (the upper left) a point `k` pixels out along a sample's normal sits,
 * in pixels: positive is the lit side. On a horizontal run it is -dy, on a vertical one -dx, and
 * half-way round an elbow, where the light falls across both sides alike, it is 0 — the whole
 * cross-section takes the middle tone, as a real elbow's flank does.
 */
export function litOffset(s: PipeSample, k: number): number {
  return -k * (s.nx + s.ny);
}

/** One pixel of a pipe's body: where it is, how lit (-1 shaded .. +1 lit), and whether it is outline. */
export interface PipePixel { x: number; y: number; lit: number; edge: boolean }

/**
 * Every pixel of a run's body, `half` pixels either side of its centre line — computed ONCE per
 * run, since the geometry never changes. Each pixel takes its shade from the nearest centre-line
 * sample. DO NOT paint the body as cross-sections laid along the run instead: round an elbow the
 * sections fan out on the outer curve and overlap on the inner one, and a section's outline pixel
 * lands inside its neighbour's body — the elbow comes out striped with dark stairs.
 */
export function rasterize(run: PipeRun, half: number): PipePixel[] {
  const xs = run.samples.map((s) => s.x), ys = run.samples.map((s) => s.y);
  const x0 = Math.floor(Math.min(...xs) - half - 1), x1 = Math.ceil(Math.max(...xs) + half + 1);
  const y0 = Math.floor(Math.min(...ys) - half - 1), y1 = Math.ceil(Math.max(...ys) + half + 1);
  const out: PipePixel[] = [];
  for (let y = y0; y <= y1; y++) {
    for (let x = x0; x <= x1; x++) {
      let best = Infinity, bi = 0;
      for (let i = 0; i <= run.total; i++) {
        const s = run.samples[i], d = (x - s.x) ** 2 + (y - s.y) ** 2;
        if (d < best) { best = d; bi = i; }
      }
      const s = run.samples[bi];
      const k = (x - s.x) * s.nx + (y - s.y) * s.ny;            // across the run
      const t = (x - s.x) * -s.ny + (y - s.y) * s.nx;           // along it
      if (Math.abs(k) > half + 0.25) continue;
      if ((bi === 0 || bi === run.total) && Math.abs(t) > 0.5) continue;   // the ends are cut square
      out.push({ x, y, lit: Math.max(-1, Math.min(1, litOffset(s, k) / (half - 1))), edge: Math.abs(k) > half - 0.75 });
    }
  }
  return out;
}

/** Evenly spaced packet positions along a run (sample indices) for a given phase. */
export function packetsAt(phase: number, count: number, total: number): number[] {
  const out: number[] = [];
  for (let i = 0; i < count; i++) out.push(Math.floor((((phase + (i / count) * total) % total) + total) % total));
  return out;
}
