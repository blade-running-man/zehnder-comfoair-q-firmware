// The air streams' maths — the part of the unit scene that runs without a canvas.
//
// Run: node src/pages/dashboard/tests/pixelAirFlow.test.ts
//
// Pins that a stopped fan stands still while an unknown flow still drifts, and the colour blends.

import { MAX_FLOW, mixColor, mixColor3, streamSpeed } from "../pixel/pixelAirFlow.ts";
import { eq, ok, report } from "../../settings/tests/harness.ts";

// --- speed: a stopped fan stands still, an unknown flow does not ----------------------------
eq(streamSpeed(200, true), 0, "a stopped fan does not move its stream");
ok(streamSpeed(null, false) > 0, "an unknown flow still drifts — not mistaken for a stopped fan");
ok(streamSpeed(MAX_FLOW, false) > streamSpeed(MAX_FLOW / 4, false), "more air moves faster");
eq(streamSpeed(MAX_FLOW * 3, false), streamSpeed(MAX_FLOW, false), "speed clamps at the unit's ceiling");
ok(streamSpeed(0, false) > 0, "a zero reading on a running fan still drifts a little");

// --- colour blends --------------------------------------------------------------------------
eq(mixColor("#000000", "#ffffff", 0), "#000000", "t = 0 is the first colour");
eq(mixColor("#000000", "#ffffff", 1), "#ffffff", "t = 1 is the second colour");
eq(mixColor("#000000", "#ffffff", 2), "#ffffff", "t clamps above 1");
eq(mixColor("#000000", "#ff8000", 0.5), "#804000", "each channel blends on its own");
eq(mixColor3("#00f0ff", "#b26bff", "#ff8a3d", 0.5), "#b26bff",
  "the three-stop blend passes through its middle at 0.5, not a muddy RGB mix");
eq(mixColor3("#00f0ff", "#b26bff", "#ff8a3d", 1), "#ff8a3d", "and ends on the last stop");

report("pixelAirFlow");
