// The ComfoAir Q's cabinet in the unit scene (pixelUnit.ts), drawn after the real thing with its
// front cover off: a light-grey frame seen a little from above and to the left, four black
// spigots on its top in two staggered pairs with the T-shaped cover between them, and inside, on
// the dark EPS insulation, what the unit is built round — the control box with its display at
// the top, the two filters set either side of it like a roof, the fan bar under them, the tall
// coffin-shaped counterflow exchanger with its centre seam, and the two motors — white
// centrifugal fans in their spiral housings — down in the black pockets beside its lower half.
//
// What moves is what is measured: the rotors turn at their fans' airflow, the exchanger's plates
// carry the supply down and the extract up — counterflow, each in its own colour, cold turning
// warm and warm turning cold along the way — and with the bypass open the exchanger stands
// white and still: no air crosses it then. The display's cells are the measured supply flow,
// its lamp the bypass.

import { mixColor } from "./pixelAirFlow";
import { LEVEL_CELLS } from "./pixelFan";
import type { Px } from "./pixelDuct";

// The front face; the depth of the box shows as a 45° top and left side DEPTH pixels deep.
export const FX0 = 52, FX1 = 128, FY0 = 50, FY1 = 124, DEPTH = 6;
/** The spigots' centres and the top of their collars: the back pair sits up and to the left. */
export const SPIGOT = {
  backLeft: [60, FY0 - 9], frontLeft: [74, FY0 - 6], backRight: [104, FY0 - 9], frontRight: [118, FY0 - 6],
} as const;
const SPIGOT_HALF = 6, SPIGOT_H = 5;
const RIM = 3;                                  // the frame round the open front

export const FANS: Array<[number, number]> = [[64, 101], [116, 101]];   // supply (left), extract (right)
const FAN_R = 7, IMPELLER_R = 4, BLADES = 6;
// The exchanger's outline, as half-widths about its centre line at three heights: narrow top,
// widest a third of the way down, tapering to a bottom a little wider than the top.
const HEX_CX = 90, HEX_Y = [75, 91, 119] as const, HEX_HW = [10, 20, 13] as const;
const BOX_X0 = 84, BOX_X1 = 96, BOX_Y0 = 54, BOX_Y1 = 66;  // the control box
const BAR_X0 = 66, BAR_X1 = 114, BAR_Y0 = 67, BAR_Y1 = 72; // the fan bar

const OUT = "#0e1016";
const FRAME = "#c9ced6", FRAME_HI = "#e4e8ee", FRAME_SH = "#a3a9b4", SIDE = "#a9afba", SIDE_SH = "#959ba7";
const TOP = "#e9ecf1", RIDGE = "#b3b9c4", RIDGE_HI = "#c9ced7", SCREW = "#7d838e";
const EPS = "#2a2c31", EPS_DARK = "#202226", EPS_LIGHT = "#34373d", EPS_EDGE = "#3d4048";
const SPIGOT_BODY = "#24262c", SPIGOT_HI = "#4a4e57", SPIGOT_RIB = "#16171b", SPIGOT_RIM = "#5c616b";
const HX = "#d8dce3", HX_SH = "#b9bec8", HX_SEAM = "#4a4e57", HX_CASE = "#8f96a3";
const FILTER = "#eef1f5", FILTER_SH = "#b9bec8", MARK = "#c8322b";
const POCKET = "#07080b", VOLUTE = "#e9ecf1", VOLUTE_SH = "#b9bec8", VOLUTE_SEAM = "#9aa1ad";
const BOX = "#3a3d44", SCREEN = "#07080b", BAR = "#111216", MOTOR = "#2a2c31", WIRE = "#c8562b";
const METER_OFF = "#2a2e37", METER_ON = "#05ffa1", NOTICE = "#fcee0a";

export interface CabinetState {
  cold: string; warm: string; supplyOut: string; exhaustOut: string;
  bypassOpen: boolean; supplyRunning: boolean;
  fanAngle: [number, number];
  /** How far each stream has moved through the exchanger, in pixels. */
  supplyPhase: number; extractPhase: number;
  litCells: number;
}

/** The box: its left side and top, the frame, and everything seen inside it. */
export function drawCabinet(px: Px, st: CabinetState): void {
  // Left side face, a 45° parallelogram behind the front's left edge.
  for (let i = 0; i < DEPTH; i++) {
    const x = FX0 - DEPTH + i, y0 = FY0 - DEPTH + i, y1 = FY1 - DEPTH + i;
    px(x, y0, i === 0 ? OUT : SIDE, 1, 1, y1 - y0);
    px(x, y1, OUT, 1);
  }
  px(FX0 - DEPTH + 1, FY0 + 33, SIDE_SH, 1, DEPTH - 1, 1);
  // Top face, the same parallelogram laid flat, with the T-shaped cover between the spigot pairs.
  for (let j = 0; j < DEPTH; j++) {
    const y = FY0 - DEPTH + j, shift = j - DEPTH;
    px(FX0 + shift, y, j === 0 ? OUT : TOP, 1, FX1 - FX0, 1);
    px(FX0 + shift, y, OUT, 1);
    px(FX0 + shift + 32, y, j === 0 ? OUT : j === 1 ? RIDGE_HI : RIDGE, 1, 14, 1);
    px(FX1 + shift, y, OUT, 1);
  }
  // The frame, lit along the top and left, a screw in each corner.
  px(FX0, FY0, OUT, 1, FX1 - FX0 + 1, FY1 - FY0 + 1);
  px(FX0 + 1, FY0 + 1, FRAME, 1, FX1 - FX0 - 1, FY1 - FY0 - 1);
  px(FX0 + 1, FY0 + 1, FRAME_HI, 1, FX1 - FX0 - 1, 1);
  px(FX0 + 1, FY0 + 1, FRAME_HI, 1, 1, FY1 - FY0 - 1);
  px(FX1 - 1, FY0 + 2, FRAME_SH, 1, 1, FY1 - FY0 - 3);
  px(FX0 + 1, FY1 - 1, FRAME_SH, 1, FX1 - FX0 - 1, 1);
  for (const x of [FX0 + 1, FX1 - 2]) for (const y of [FY0 + 1, FY1 - 2]) px(x, y, SCREW, 1);

  drawInsulation(px);
  drawTop(px, st);
  drawExchanger(px, st);
  drawFan(px, 0, st.cold, st.supplyRunning, st.fanAngle[0]);
  drawFan(px, 1, st.warm, true, st.fanAngle[1]);
}

// The open front: dark speckled EPS, the inner panels' chamfers running down from the top
// corners to the fan bar, and a bracket each side at the exchanger's widest point.
function drawInsulation(px: Px): void {
  const x0 = FX0 + RIM, x1 = FX1 - RIM, y0 = FY0 + RIM, y1 = FY1 - RIM;
  px(x0 - 1, y0 - 1, OUT, 1, x1 - x0 + 2, y1 - y0 + 2);
  px(x0, y0, EPS, 1, x1 - x0, y1 - y0);
  for (let y = y0; y < y1; y++) {
    for (let x = x0; x < x1; x++) {
      const h = (x * 73856093) ^ (y * 19349663);           // a fixed speckle, not noise per frame
      if ((h & 15) === 0) px(x, y, EPS_LIGHT, 1); else if ((h & 15) === 1) px(x, y, EPS_DARK, 1);
    }
  }
  for (let t = 0; t <= BAR_Y0 - y0 - 1; t++) {
    px(x0 + 1 + t, y0 + t, EPS_EDGE, 1);
    px(x1 - 2 - t, y0 + t, EPS_EDGE, 1);
  }
  px(x0, HEX_Y[1], EPS_EDGE, 1, 8, 1);
  px(x1 - 8, HEX_Y[1], EPS_EDGE, 1, 8, 1);
}

// The top of the inside: the control box under the T cover with its display (the airflow cells
// and the bypass lamp), the two filters either side of it, and the fan bar across under them.
function drawTop(px: Px, st: CabinetState): void {
  px(BOX_X0 - 1, FY0 + 1, RIDGE, 1, BOX_X1 - BOX_X0 + 2, 3);           // the T cover's stem
  px(BOX_X0, BOX_Y0, OUT, 1, BOX_X1 - BOX_X0, BOX_Y1 - BOX_Y0);
  px(BOX_X0 + 1, BOX_Y0 + 1, BOX, 1, BOX_X1 - BOX_X0 - 2, BOX_Y1 - BOX_Y0 - 2);
  px(BOX_X0 + 1, BOX_Y0 + 2, SCREEN, 1, BOX_X1 - BOX_X0 - 2, 6);
  for (let i = 0; i < LEVEL_CELLS; i++) {
    px(BOX_X0 + 1 + i * 2, BOX_Y0 + 4, i < st.litCells ? METER_ON : METER_OFF, 1, 1, 2);
  }
  px(BOX_X1 - 2, BOX_Y0 + 3, st.bypassOpen ? NOTICE : METER_OFF, 1, 1, 1);

  // The filters: where they slide in, two white pleated slabs at 45° rising toward the box from
  // either side — the roof over the fan bar — each with its red pull tab at the top.
  const FILTER_LEN = 13;
  for (let t = 0; t < FILTER_LEN; t++) {
    for (const [x, dir] of [[BOX_X0 - 16 + t, 1], [BOX_X1 + 15 - t, -1]] as const) {
      const y = BOX_Y1 - t;
      px(x - (dir > 0 ? 0 : 3), y, t % 2 ? FILTER : FILTER_SH, 1, 4, 1);
      px(dir > 0 ? x + 4 : x - 4, y, FILTER_SH, 1);
      px(dir > 0 ? x - 1 : x + 1, y, OUT, 1);
    }
  }
  px(BOX_X0 - 4, BOX_Y0, MARK, 1, 2, 2);
  px(BOX_X1 + 2, BOX_Y0, MARK, 1, 2, 2);

  // The fan bar, its ends cut at 45°, two motor housings and the red wire between them.
  for (let y = BAR_Y0; y < BAR_Y1; y++) {
    const inset = BAR_Y1 - 1 - y;
    px(BAR_X0 + inset, y, BAR, 1, BAR_X1 - BAR_X0 - 2 * inset, 1);
  }
  px(BAR_X0 + 7, BAR_Y0 + 1, MOTOR, 1, 10, 3);
  px(BAR_X1 - 17, BAR_Y0 + 1, MOTOR, 1, 10, 3);
  px(BAR_X0 + 17, BAR_Y0 + 2, WIRE, 1, BAR_X1 - BAR_X0 - 34, 1);
}

/** The exchanger's half-width at a row, or -1 above or below it. */
function hexHalf(y: number): number {
  if (y < HEX_Y[0] || y > HEX_Y[2]) return -1;
  const [a, b, c] = HEX_Y, [wa, wb, wc] = HEX_HW;
  return y <= b ? wa + ((wb - wa) * (y - a)) / (b - a) : wb + ((wc - wb) * (y - b)) / (c - b);
}

/**
 * The exchanger: a white block in a grey casing with a dark seam down its middle. Over the white,
 * its plates: every other column carries the supply DOWN, cold turning toward the supply's out-
 * going colour, and the columns between carry the extract UP, warm turning toward the exhaust's —
 * each with a wave of light running its way at its fan's pace. With the bypass open the plates
 * carry nothing and the block stays plain white.
 */
function drawExchanger(px: Px, st: CabinetState): void {
  const [top, , bottom] = HEX_Y;
  for (let y = top - 1; y <= bottom + 1; y++) {
    const half = Math.round(hexHalf(Math.max(top, Math.min(bottom, y))));
    if (y < top || y > bottom) { px(HEX_CX - half, y, HX_CASE, 1, 2 * half + 1, 1); continue; }
    px(HEX_CX - half - 1, y, HX_CASE, 1, 2 * half + 3, 1);
    px(HEX_CX - half, y, HX, 1, 2 * half + 1, 1);
    px(HEX_CX + half - 2, y, HX_SH, 1, 2, 1);
    if (st.bypassOpen) continue;
    const v = (y - top) / (bottom - top);
    for (let x = HEX_CX - half + 1; x <= HEX_CX + half - 3; x++) {
      const supplyPlate = (x & 1) === 0;
      const c = supplyPlate ? mixColor(st.cold, st.supplyOut, v) : mixColor(st.warm, st.exhaustOut, 1 - v);
      const phase = supplyPlate ? y - st.supplyPhase : y + st.extractPhase;
      const wave = 0.5 + 0.5 * Math.sin(phase * 0.5);
      px(x, y, c, (supplyPlate && !st.supplyRunning ? 0.15 : 0.3) + 0.4 * wave);
    }
  }
  // The black pockets the motors sit in open along the exchanger's lower flanks.
  for (let y = HEX_Y[1] + 1; y <= bottom; y++) {
    const half = Math.round(hexHalf(y)), w = 1 + Math.round((3 * (y - HEX_Y[1])) / (bottom - HEX_Y[1]));
    px(HEX_CX - half - 1 - w, y, POCKET, 1, w, 1);
    px(HEX_CX + half + 2, y, POCKET, 1, w, 1);
  }
  px(HEX_CX - 1, top, HX_SEAM, 1, 2, bottom - top + 1);
  px(HEX_CX - Math.round(hexHalf(HEX_Y[1])), HEX_Y[1], HX_SH, 1, 2 * HEX_HW[1] + 1, 1);
}

// A motor in its pocket: a white centrifugal fan — the spiral housing, shaded toward the lower
// right with the volute's seam winding round it, its outlet rising toward the fan bar on the
// outer side — and, in the intake eye, the many-bladed impeller in its stream's colour. A
// stopped fan's impeller stays where it stopped, dimmed.
function drawFan(px: Px, i: number, color: string, running: boolean, angle: number): void {
  const [cx, cy] = FANS[i];
  const side = i === 0 ? -1 : 1;                       // the outlet is on the side away from the exchanger
  for (let dy = -FAN_R - 1; dy <= FAN_R + 1; dy++) {
    for (let dx = -FAN_R - 1; dx <= FAN_R + 1; dx++) {
      const d = Math.hypot(dx, dy);
      if (d > FAN_R + 0.5) continue;
      if (d > FAN_R - 0.5) { px(cx + dx, cy + dy, OUT, 1); continue; }
      px(cx + dx, cy + dy, dx + dy > 3 ? VOLUTE_SH : VOLUTE, 1);
    }
  }
  // The outlet, drawn over the housing's rim so the two read as one moulding.
  const ox = side < 0 ? cx - FAN_R : cx + FAN_R - 4, oy = cy - FAN_R - 5;
  px(ox, oy, OUT, 1, 5, 1);
  px(ox, oy, OUT, 1, 1, 8);
  px(ox + 4, oy, OUT, 1, 1, 8);
  px(ox + 1, oy + 1, VOLUTE, 1, 3, 8);
  px(ox + (side < 0 ? 3 : 1), oy + 1, VOLUTE_SH, 1, 1, 7);
  // The volute's seam: a spiral from the eye's edge outward round three quarters of a turn.
  for (let a = 0; a < Math.PI * 1.5; a += 0.12) {
    const r = IMPELLER_R + 0.8 + (a / (Math.PI * 1.5)) * 1.6;
    const t = side < 0 ? Math.PI / 2 + a : Math.PI / 2 - a;
    px(cx + Math.cos(t) * r, cy + Math.sin(t) * r, VOLUTE_SEAM, 1);
  }
  for (let dy = -IMPELLER_R; dy <= IMPELLER_R; dy++) {
    for (let dx = -IMPELLER_R; dx <= IMPELLER_R; dx++) {
      if (Math.hypot(dx, dy) <= IMPELLER_R - 0.3) px(cx + dx, cy + dy, POCKET, 1);
    }
  }
  // The impeller: a ring of short blades round the hub. Too small for swept blades at this
  // scale, so each is one pixel on the ring, and the gaps between them carry the turning.
  const a = running ? 0.95 : 0.35;
  for (let b = 0; b < BLADES; b++) {
    const t = angle + (b * 2 * Math.PI) / BLADES;
    px(cx + Math.cos(t) * (IMPELLER_R - 1), cy + Math.sin(t) * (IMPELLER_R - 1), color, a);
  }
  px(cx, cy, color, a * 0.6);
}

/** A spigot's black ribbed collar, standing on the top face; the duct is drawn into it after. */
export function drawSpigot(px: Px, cx: number, top: number): void {
  const x0 = cx - SPIGOT_HALF, w = SPIGOT_HALF * 2 + 1;
  px(x0, top, OUT, 1, w, SPIGOT_H + 1);
  px(x0 + 1, top + 1, SPIGOT_BODY, 1, w - 2, SPIGOT_H - 1);
  px(x0 + 1, top + 1, SPIGOT_HI, 1, 2, SPIGOT_H - 1);
  px(x0 + 1, top + 3, SPIGOT_RIB, 1, w - 2, 1);
}

/** The collar's rim, drawn over the duct's end so the duct sits inside the spigot. */
export function drawSpigotRim(px: Px, cx: number, top: number): void {
  const x0 = cx - SPIGOT_HALF, w = SPIGOT_HALF * 2 + 1;
  px(x0, top, OUT, 1, w, 1);
  px(x0 + 1, top + 1, SPIGOT_RIM, 1, w - 2, 1);
  px(x0, top + 1, OUT, 1, 1, SPIGOT_H);
  px(x0 + w - 1, top + 1, OUT, 1, 1, SPIGOT_H);
}
