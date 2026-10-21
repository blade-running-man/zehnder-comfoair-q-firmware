// The unit scene's fans and airflow meter — the part that runs without a canvas.
//
// Run: node src/pages/dashboard/tests/pixelFan.test.ts
//
// Pins that the meter states only what was measured (no cell for a stopped fan or a missing
// reading, at least one for a fan that turns), and that a stopped fan's rotor stands still.

import { FAN_STEP_MAX, LEVEL_CELLS, fanStep, levelCells } from "../pixel/pixelFan.ts";
import { MAX_FLOW } from "../pixel/pixelAirFlow.ts";
import { eq, ok, report } from "../../settings/tests/harness.ts";

// --- the airflow meter ----------------------------------------------------------------------
eq(levelCells(null, false), 0, "no reading lights no cell — the meter never guesses a level");
eq(levelCells(200, true), 0, "a stopped fan lights no cell, whatever its last flow");
eq(levelCells(0, false), 0, "zero flow lights no cell");
eq(levelCells(1, false), 1, "a fan that turns at all shows one cell");
eq(levelCells(MAX_FLOW / LEVEL_CELLS, false), 1, "exactly one fifth is one cell");
eq(levelCells(MAX_FLOW / LEVEL_CELLS + 1, false), 2, "a cell lights once any of its fifth is reached");
eq(levelCells(MAX_FLOW, false), LEVEL_CELLS, "the ceiling lights every cell");
eq(levelCells(MAX_FLOW * 2, false), LEVEL_CELLS, "past the ceiling it clamps");

// --- rotor speed ----------------------------------------------------------------------------
eq(fanStep(300, true), 0, "a stopped fan's rotor stands still");
ok(fanStep(null, false) > 0, "an unknown flow still turns the rotor");
ok(fanStep(MAX_FLOW, false) <= FAN_STEP_MAX, "the step never exceeds its maximum");
ok(fanStep(MAX_FLOW, false) > fanStep(50, false), "more air turns the rotor faster");

report("pixelFan");
