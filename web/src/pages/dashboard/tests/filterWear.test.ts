// The filters' wear — everything the Filters card and its pixel scene derive from the unit's
// one number, the days REMAINING.
//
// Run: node src/pages/dashboard/tests/filterWear.test.ts
//
// Pins the state boundaries (fine / soon / overdue / unknown), that an unknown reading draws a
// clean filter rather than a worn one, that the life bar stays lit while any life is left (the
// bug that once blanked it with a day to run), and that the dust is deterministic.

import { LIFE_CELLS, SOON_DAYS, filterWear, spentCells, specks } from "../filterWear.ts";
import { eq, ok, report } from "../../settings/tests/harness.ts";

const CYCLE = 180;

// --- the wear state -------------------------------------------------------------------------
eq(filterWear(null, CYCLE).wear, "unknown", "no reading is unknown");
eq(filterWear(null, CYCLE).used, 0, "an unknown filter is drawn clean, not worn");
eq(filterWear(Number.NaN, CYCLE).wear, "unknown", "a non-number is unknown too");
eq(filterWear(CYCLE, CYCLE).wear, "good", "a fresh filter is fine");
eq(filterWear(CYCLE, CYCLE).used, 0, "a fresh filter has used nothing");
eq(filterWear(SOON_DAYS + 1, CYCLE).wear, "good", "a day over the warning line is still fine");
eq(filterWear(SOON_DAYS, CYCLE).wear, "soon", "the warning starts at SOON_DAYS");
eq(filterWear(1, CYCLE).wear, "soon", "one day left is soon, not overdue");
eq(filterWear(0, CYCLE).wear, "overdue", "zero days left is overdue");
eq(filterWear(-12, CYCLE).wear, "overdue", "the unit counts past zero; still overdue");
eq(filterWear(-12, CYCLE).used, 1, "used clamps at the whole cycle");
eq(filterWear(-12, CYCLE).days, -12, "the overdue count is kept for the card to print");
eq(filterWear(CYCLE * 2, CYCLE).used, 0, "more days than a cycle clamps used at zero");

// --- the life bar ---------------------------------------------------------------------------
const cells = (days: number) => spentCells(filterWear(days, CYCLE).used);
eq(cells(CYCLE), 0, "a fresh filter has every cell lit");
eq(cells(CYCLE - 5), 0, "a cell stays lit while any of its ten days is left");
eq(cells(90), LIFE_CELLS / 2, "half the cycle is half the cells");
eq(cells(1), LIFE_CELLS - 1, "with a day to run one cell is still lit");
eq(cells(0), LIFE_CELLS, "overdue spends every cell");
eq(spentCells(-1), 0, "a used fraction below zero clamps");
eq(spentCells(2), LIFE_CELLS, "a used fraction above one clamps");

// --- dust -----------------------------------------------------------------------------------
const a = specks(0.5, 7, 80, 30, 42);
eq(a.length, 40, "the speck count scales with used");
eq(specks(0, 7, 80, 30, 42).length, 0, "a clean filter has no specks");
ok(JSON.stringify(specks(0.5, 7, 80, 30, 42)) === JSON.stringify(a),
  "the same reading draws the same dust — no reshuffle on redraw");
ok(JSON.stringify(specks(0.5, 29, 80, 30, 42)) !== JSON.stringify(a), "another seed draws other dust");
ok(specks(1, 7, 80, 30, 42).every(([x, y]) => x >= 0 && x < 30 && y >= 0 && y < 42),
  "every speck lands on the face");

report("filterWear");
