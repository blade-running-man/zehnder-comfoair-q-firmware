// The unit's two filters on ONE pixel canvas, in the same arcade style as the unit scene
// (pixelUnit.ts): two pleated cartridges side by side — the outdoor-air filter and the
// extract filter, which the ComfoAir has you change together — in a dark frame with a handle tab.
//
// Wear is drawn the way a filter actually ages: the media greys toward dust colour and collects
// more specks across its WHOLE face (filterWear.specks), never a rising level. The cartridge's
// neon outline carries the state colour (green fine, yellow soon, red overdue — the overdue one
// blinks). Motes of air drift across the scene; some catch on the media and vanish there, the
// rest pass through. That motion is decorative, not a measurement. prefers-reduced-motion draws
// one still frame. Colours are FIXED: the scene sits on the dark panel in both themes.

import { specks, type Wear } from "../filterWear";
import { mixColor } from "./pixelAirFlow";

export const W = 100, H = 70;
const CART_X = [8, 54], CART_W = 38;       // the two cartridges' left edges and width
const FRAME_Y = 6, FRAME_H = 56;           // the moulded frame
const MEDIA_DX = 4, MEDIA_Y = 13, MEDIA_W = 30, MEDIA_H = 42; // the pleated media inside it
const PLEAT = 3;                           // pleat pitch: 2px lit fold, 1px shaded crease
const MAX_SPECKS = 80;

const N_MOTES = 16, MOTE_SPEED = 0.45, STICKY = 0.35, SCENE_FPS = 30;

const FRAME = "#3a3a52", FRAME_HI = "#5a5a7a", FRAME_LINE = "#1a1a26";
const MEDIA = "#c9cde0", MEDIA_SH = "#9aa0bb", DUST = "#6b5a4a", SPECK = "#3b3128";
const AIR = "#00f0ff";
export const TONE: Record<Wear, string> = {
  unknown: "#6a7182", good: "#05ffa1", soon: "#fcee0a", overdue: "#ff4060",
};

export interface FilterInputs { used: number; wear: Wear }

interface Mote { x: number; y: number; sticky: boolean }

function inMedia(x: number, y: number): boolean {
  if (y < MEDIA_Y || y >= MEDIA_Y + MEDIA_H) return false;
  return CART_X.some((cx) => x >= cx + MEDIA_DX && x < cx + MEDIA_DX + MEDIA_W);
}

/** Drives the filter scene on one canvas. setInputs() feeds it the wear. */
export class PixelFilters {
  private ctx: CanvasRenderingContext2D;
  private inp: FilterInputs = { used: 0, wear: "unknown" };
  private motes: Mote[] = [];
  private frames = 0;
  private raf = 0;
  private prev = 0;
  private reduced: boolean;

  constructor(canvas: HTMLCanvasElement) {
    canvas.width = W;
    canvas.height = H;
    const ctx = canvas.getContext("2d");
    if (!ctx) throw new Error("pixelFilters: no 2d context");
    this.ctx = ctx;
    for (let i = 0; i < N_MOTES; i++) this.motes.push(this.spawn((i / N_MOTES) * W));
    this.reduced = typeof matchMedia === "function"
      && matchMedia("(prefers-reduced-motion: reduce)").matches;
  }

  setInputs(inp: FilterInputs): void {
    this.inp = inp;
    if (this.reduced) this.draw();
  }

  start(): void {
    if (this.reduced) { this.draw(); return; }
    const loop = (now: number) => {
      this.raf = requestAnimationFrame(loop);
      if (now - this.prev < 1000 / SCENE_FPS) return;
      this.prev = now;
      this.frames++;
      this.step();
      this.draw();
    };
    this.raf = requestAnimationFrame(loop);
  }

  stop(): void {
    if (this.raf) cancelAnimationFrame(this.raf);
    this.raf = 0;
  }

  private spawn(x: number): Mote {
    return { x, y: MEDIA_Y + 2 + Math.random() * (MEDIA_H - 4), sticky: Math.random() < STICKY };
  }

  private step(): void {
    for (let i = 0; i < this.motes.length; i++) {
      const m = this.motes[i];
      m.x += MOTE_SPEED;
      // A sticky mote is caught a pixel or two into the media and is gone — the filter did its job.
      const caught = m.sticky && inMedia(m.x - 2, m.y) && inMedia(m.x, m.y);
      if (caught || m.x > W + 2) this.motes[i] = this.spawn(-2);
    }
  }

  private px(x: number, y: number, color: string, alpha: number, w = 1, h = 1): void {
    const ctx = this.ctx;
    ctx.globalAlpha = Math.max(0, Math.min(1, alpha));
    ctx.fillStyle = color;
    ctx.fillRect(Math.round(x), Math.round(y), w, h);
  }

  // One cartridge: handle tab, a bevelled frame, pleated media greying with wear, deterministic
  // specks, a saw-tooth apex row (the pleats end-on), and the neon state outline round the media.
  private drawCartridge(cx: number, seed: number): void {
    const { used, wear } = this.inp;
    const tone = TONE[wear];
    const mx = cx + MEDIA_DX;

    this.px(cx + 12, FRAME_Y - 4, FRAME_LINE, 1, 14, 5);
    this.px(cx + 13, FRAME_Y - 3, FRAME, 1, 12, 3);
    this.px(cx + 13, FRAME_Y - 3, FRAME_HI, 0.8, 12, 1);

    this.px(cx, FRAME_Y, FRAME_LINE, 1, CART_W, FRAME_H);
    this.px(cx + 1, FRAME_Y + 1, FRAME, 1, CART_W - 2, FRAME_H - 2);
    this.px(cx + 1, FRAME_Y + 1, FRAME_HI, 0.7, CART_W - 2, 1);
    this.px(cx + 1, FRAME_Y + 1, FRAME_HI, 0.5, 1, FRAME_H - 2);

    const lit = mixColor(MEDIA, DUST, used * 0.75);
    const shade = mixColor(MEDIA_SH, DUST, used * 0.75);
    for (let x = 0; x < MEDIA_W; x++) {
      this.px(mx + x, MEDIA_Y, x % PLEAT === PLEAT - 1 ? shade : lit, 1, 1, MEDIA_H);
    }
    for (let x = 0; x < MEDIA_W; x++) {                 // apex row: a peak over each fold
      if (x % PLEAT === 0) this.px(mx + x, MEDIA_Y - 1, lit, 1);
    }
    for (const [sx, sy] of specks(used, seed, MAX_SPECKS, MEDIA_W, MEDIA_H)) {
      this.px(mx + sx, MEDIA_Y + sy, SPECK, 0.75);
    }

    const blink = wear === "overdue" && !this.reduced && Math.floor(this.frames / 15) % 2 === 1;
    const a = blink ? 0.35 : 0.95;
    this.px(mx - 1, MEDIA_Y - 2, tone, a, MEDIA_W + 2, 1);
    this.px(mx - 1, MEDIA_Y + MEDIA_H, tone, a, MEDIA_W + 2, 1);
    this.px(mx - 1, MEDIA_Y - 2, tone, a, 1, MEDIA_H + 3);
    this.px(mx + MEDIA_W, MEDIA_Y - 2, tone, a, 1, MEDIA_H + 3);
    this.px(mx - 2, MEDIA_Y - 3, tone, 0.18 * a, MEDIA_W + 4, 1);
    this.px(mx - 2, MEDIA_Y + MEDIA_H + 1, tone, 0.18 * a, MEDIA_W + 4, 1);

    this.px(mx, FRAME_Y + FRAME_H - 5, FRAME_LINE, 1, MEDIA_W, 2); // the rating strip under the media
  }

  private draw(): void {
    this.ctx.clearRect(0, 0, W, H);
    for (const m of this.motes) {
      if (!inMedia(m.x, m.y)) this.px(m.x, m.y, AIR, m.sticky ? 0.55 : 0.4);
    }
    this.drawCartridge(CART_X[0], 7);
    this.drawCartridge(CART_X[1], 29);
    for (const m of this.motes) {                       // air passing through the media, faintly
      if (inMedia(m.x, m.y) && !m.sticky) this.px(m.x, m.y, AIR, 0.3);
    }
    this.ctx.globalAlpha = 1;
  }
}
