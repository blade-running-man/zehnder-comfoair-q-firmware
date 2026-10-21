// The ComfoAir Q itself on ONE pixel canvas — the "Where the air goes" scene (Bypass.tsx), drawn
// after the real unit and the way it is really installed: the cabinet with its front cover off
// (pixelCabinet.ts), four spigots on top, and grey spiral ducts rising from them with
// swept elbows, ribbed couplings, a silencer and a tee (pixelDuct.ts) — outdoors on the LEFT,
// your home on the RIGHT.
//
//   left:  outdoor air IN (cold, upper duct)       · exhaust OUT (cooled, lower duct)
//   right: supply OUT to the rooms (warmed, upper  · room air IN (warm, lower duct)
//          duct, through the silencer, teed off
//          to a second run into the ceiling)
//
// The four ducts are routed so none crosses another: on each side the spigot nearer the middle
// takes the upper run, so its horizontal clears the other's elbow. The out-going colours carry
// the measured recovery (none with the bypass open), the supply side moves at the supply fan's
// flow and the extract side at the exhaust fan's. A stopped supply fan stands still and dims.
// prefers-reduced-motion draws one still frame. Colours are FIXED: the scene sits on the dark
// panel in both themes.

import { mixColor3, streamSpeed } from "./pixelAirFlow";
import { fanStep, levelCells } from "./pixelFan";
import { pipeRun, type PipePixel, type PipeRun } from "./pixelPipe";
import { drawPackets, drawPipe, drawSilencer, drawTee, pipeBody, type DuctLook, type Px } from "./pixelDuct";
import { drawCabinet, drawSpigot, drawSpigotRim, SPIGOT } from "./pixelCabinet";

export const W = 180, H = 129;
const UPPER = 10, LOWER = 26, ELBOW_R = 7;   // the two run heights and the elbows' radius
const PPF_MAX = 1.4, SCENE_FPS = 30;

const COLD = "#00f0ff", WARM = "#ff8a3d", MIDAIR = "#b26bff";

export interface UnitInputs {
  supplyFlow: number | null;
  exhaustFlow: number | null;
  supplyStopped: boolean;
  bypassOpen: boolean;
  recovery: number | null;
}

interface Duct { run: PipeRun; body: PipePixel[]; supplySide: boolean; phase: number; color: () => string; look: DuctLook; packets?: number }

// The duct runs, in the direction the air travels; each ends a pixel inside its spigot's collar.
// The edges are overshot by a few pixels so a duct leaves the scene rather than stopping at it.
const [blX, blTop] = SPIGOT.backLeft, [flX, flTop] = SPIGOT.frontLeft;
const [brX, brTop] = SPIGOT.backRight, [frX, frTop] = SPIGOT.frontRight;
const RUN_OUTDOOR = pipeRun([[-3, UPPER], [flX, UPPER], [flX, flTop + 1]], ELBOW_R);
const RUN_EXHAUST = pipeRun([[blX, blTop + 1], [blX, LOWER], [-3, LOWER]], ELBOW_R);
// The supply rises through the silencer into a tee: the main run turns right to the rooms, the
// branch carries on up into the ceiling. The tee covers the corner, so the corner is left sharp.
const RUN_SUPPLY = pipeRun([[brX, brTop + 1], [brX, UPPER], [W + 3, UPPER]], 0);
const RUN_BRANCH = pipeRun([[brX, UPPER], [brX, -3]], 0);
const RUN_EXTRACT = pipeRun([[W + 3, LOWER], [frX, LOWER], [frX, frTop + 1]], ELBOW_R);
const SILENCER: [number, number] = [8, 24];

/** Drives the unit scene on one canvas. setInputs() feeds it live state. */
export class PixelUnit {
  private ctx: CanvasRenderingContext2D;
  private inp: UnitInputs = {
    supplyFlow: null, exhaustFlow: null, supplyStopped: false, bypassOpen: false, recovery: null,
  };
  private outdoor: Duct; private exhaust: Duct; private supply: Duct; private branch: Duct; private extract: Duct;
  private fanAngle: [number, number] = [0, 0.6];
  private raf = 0;
  private prev = 0;
  private reduced: boolean;
  private readonly px: Px;

  constructor(canvas: HTMLCanvasElement) {
    canvas.width = W;
    canvas.height = H;
    const ctx = canvas.getContext("2d");
    if (!ctx) throw new Error("pixelUnit: no 2d context");
    this.ctx = ctx;
    this.px = (x, y, color, alpha, w = 1, h = 1) => {
      ctx.globalAlpha = Math.max(0, Math.min(1, alpha));
      ctx.fillStyle = color;
      ctx.fillRect(Math.round(x), Math.round(y), w, h);
    };
    const ex = () => (this.inp.bypassOpen ? 0 : this.inp.recovery ?? 0.75);
    const duct = (run: PipeRun, supplySide: boolean, color: () => string, look: DuctLook, packets?: number): Duct =>
      ({ run, body: pipeBody(run), supplySide, phase: 0, color, look, packets });
    this.outdoor = duct(RUN_OUTDOOR, true, () => COLD, { ringPitch: 22, ringFrom: 12 });
    this.exhaust = duct(RUN_EXHAUST, false, () => mixColor3(WARM, MIDAIR, COLD, ex()), { ringPitch: 22, ringFrom: 30 });
    this.supply = duct(RUN_SUPPLY, true, () => mixColor3(COLD, MIDAIR, WARM, ex()),
      { ringPitch: 22, ringFrom: 52, silencer: SILENCER });
    this.branch = duct(RUN_BRANCH, true, this.supply.color, { ringPitch: 99, ringFrom: 99 }, 1);
    this.extract = duct(RUN_EXTRACT, false, () => WARM, { ringPitch: 22, ringFrom: 18 });
    this.reduced = typeof matchMedia === "function"
      && matchMedia("(prefers-reduced-motion: reduce)").matches;
  }

  setInputs(inp: UnitInputs): void {
    this.inp = inp;
    if (this.reduced) this.draw();
  }

  start(): void {
    if (this.reduced) { this.draw(); return; }
    const loop = (now: number) => {
      this.raf = requestAnimationFrame(loop);
      if (now - this.prev < 1000 / SCENE_FPS) return;
      this.prev = now;
      const { supplyFlow, exhaustFlow, supplyStopped } = this.inp;
      const sup = PPF_MAX * streamSpeed(supplyFlow, supplyStopped), ext = PPF_MAX * streamSpeed(exhaustFlow, false);
      for (const d of [this.outdoor, this.supply, this.branch]) d.phase += sup;
      for (const d of [this.exhaust, this.extract]) d.phase += ext;
      this.fanAngle[0] += fanStep(supplyFlow, supplyStopped);
      this.fanAngle[1] -= fanStep(exhaustFlow, false); // the two rotors turn opposite ways
      this.draw();
    };
    this.raf = requestAnimationFrame(loop);
  }

  stop(): void {
    if (this.raf) cancelAnimationFrame(this.raf);
    this.raf = 0;
  }

  private drawDuct(d: Duct, inten: number): void {
    const c = d.color();
    drawPipe(this.px, d.run, d.body, c, inten, d.look);
    drawPackets(this.px, d.run, c, d.phase, inten, d.packets);
  }

  // Back to front: the cabinet, then the back pair of spigots and their ducts, then the front
  // pair — so a front duct is never hidden behind a back one's collar.
  private draw(): void {
    const px = this.px, inp = this.inp;
    this.ctx.clearRect(0, 0, W, H);
    const supplyInten = inp.supplyStopped ? 0.35 : 1;
    drawCabinet(px, {
      cold: COLD, warm: WARM, supplyOut: this.supply.color(), exhaustOut: this.exhaust.color(),
      bypassOpen: inp.bypassOpen, supplyRunning: !inp.supplyStopped, fanAngle: this.fanAngle,
      supplyPhase: this.supply.phase, extractPhase: this.extract.phase,
      litCells: levelCells(inp.supplyFlow, inp.supplyStopped),
    });

    drawSpigot(px, blX, blTop);
    drawSpigot(px, brX, brTop);
    this.drawDuct(this.exhaust, 1);
    this.drawDuct(this.branch, supplyInten);
    this.drawDuct(this.supply, supplyInten);
    drawSilencer(px, this.supply.run, SILENCER[0], SILENCER[1]);
    drawTee(px, brX, UPPER);
    drawSpigotRim(px, blX, blTop);
    drawSpigotRim(px, brX, brTop);

    drawSpigot(px, flX, flTop);
    drawSpigot(px, frX, frTop);
    this.drawDuct(this.outdoor, supplyInten);
    this.drawDuct(this.extract, 1);
    drawSpigotRim(px, flX, flTop);
    drawSpigotRim(px, frX, frTop);
    this.ctx.globalAlpha = 1;
  }
}
