// The PDO census card's pure helpers: list spelling, the never-heard set, the age humaniser,
// the u32 clock and the "too early to judge" caution.
//
// Run: node src/pages/probe/tests/pdos.test.ts
import { eq, report } from "../../settings/tests/harness.ts";
import { ENTITIES } from "../../../api/entities.ts";
import type { EntityMeta } from "../../../api/entities.ts";
import type { ProbePdoRow, ProbePdos } from "../../../api/probe.ts";
import {
  CAUTION_MS, ageMs, humanAge, listText, neverHeard, spacedHex, tooEarly, unlistedCount,
} from "../pdos.ts";

function row(pdo: number, over: Partial<ProbePdoRow> = {}): ProbePdoRow {
  return { pdo, known: true, count: 1, first_ms: 0, last_ms: 0, dlc: [1], nodes: [1], last: "01", ...over };
}

function census(rows: ProbePdoRow[], over: Partial<ProbePdos> = {}): ProbePdos {
  return { uptime_ms: CAUTION_MS, frames: rows.length, distinct: rows.length, overflow: 0, rows, ...over };
}

function entity(key: string, pdo: number | null, enabledByDefault = true): EntityMeta {
  return {
    key, name: key, kind: "numeric", pdo, unit: null, deviceClass: null, icon: null, entityCategory: null,
    options: null, writable: false, enabledByDefault, min: null, max: null,
  };
}

// --- listText: the dlc and nodes lists ------------------------------------------------------
eq(listText([1]), "1", "one value is itself");
eq(listText([8, 2, 2, 4]), "2, 4, 8", "values sorted and de-duplicated, whatever the device sent");
eq(listText([]), "—", "an empty list is a dash, never an empty cell");
eq(listText([0x30, 1], (n) => `0x${n.toString(16)}`), "0x1, 0x30", "a spelling applies to each value, after sorting");

// --- spacedHex: the last payload --------------------------------------------------------------
eq(spacedHex("0102ff"), "01 02 ff", "bytes are pairs, spaced");
eq(spacedHex(""), "", "a zero-length frame shows nothing");
eq(spacedHex("ab"), "ab", "one byte has no space");

// --- neverHeard: the entity table minus the heard set ---------------------------------------
{
  const table = [
    entity("a", 16), entity("b", 16, false), entity("c", 49), entity("d", null),
    entity("e", 1024, false), entity("f", 65),
  ];
  const got = neverHeard(census([row(65), row(999, { known: false })]), table);
  eq(got.map((g) => g.pdo), [16, 49, 1024], "every table PDO not heard, ascending; entities with no PDO are not PDOs");
  eq(got[0].entities, [{ key: "a", enabledByDefault: true }, { key: "b", enabledByDefault: false }],
    "a PDO several entities share is one item carrying every key");
  eq(got.map((g) => g.defaults), ["mixed", "on", "off"], "the item says whether its entities are on by default");
  eq(neverHeard(census([row(16), row(49), row(65), row(1024)]), table), [], "everything heard leaves nothing");
}
{
  const unique = new Set(ENTITIES.flatMap((e) => (e.pdo === null ? [] : [e.pdo])));
  const all = neverHeard(census([]));
  eq(all.length, unique.size, "with nothing heard, the generated table's every unique PDO is listed once");
  eq(all.flatMap((g) => g.entities.map((e) => e.key)).length,
    ENTITIES.filter((e) => e.pdo !== null).length, "and every entity with a PDO is under one of them");
  const firstPdo = all[0].pdo;
  eq(neverHeard(census([row(firstPdo)])).some((g) => g.pdo === firstPdo), false, "a heard PDO leaves the list");
}

// --- unlistedCount ---------------------------------------------------------------------------
eq(unlistedCount(census([row(1), row(2, { known: false }), row(3, { known: false })])), 2,
  "rows the firmware's table does not know");

// --- ageMs: the u32 clock -----------------------------------------------------------------------
eq(ageMs(10_000, 4_000), 6_000, "age is uptime minus last");
eq(ageMs(500, 0xffff_ff00), 756, "across the 49.7-day wrap the age is still small and positive");
eq(ageMs(5, 5), 0, "heard this very millisecond");

// --- humanAge -----------------------------------------------------------------------------
eq(humanAge(0), { n: 0, unit: "s" }, "zero is seconds");
eq(humanAge(59_999), { n: 59, unit: "s" }, "under a minute is whole seconds, truncated");
eq(humanAge(60_000), { n: 1, unit: "min" }, "a minute is minutes");
eq(humanAge(3_599_999), { n: 59, unit: "min" }, "under an hour is minutes");
eq(humanAge(3_600_000), { n: 1, unit: "h" }, "an hour is hours");
eq(humanAge(47 * 3_600_000), { n: 47, unit: "h" }, "under two days is hours");
eq(humanAge(48 * 3_600_000), { n: 2, unit: "d" }, "two days on is days");

// --- tooEarly: one full hourly poll -----------------------------------------------------------
eq(CAUTION_MS, 65 * 60_000, "the caution threshold is 65 minutes: one hourly poll plus margin");
eq(tooEarly(census([], { uptime_ms: CAUTION_MS - 1 })), true, "just under 65 minutes is too early");
eq(tooEarly(census([], { uptime_ms: CAUTION_MS })), false, "65 minutes is enough");
eq(tooEarly(census([row(16, { first_ms: 0xf000_0000 })], { uptime_ms: 1_000 })), false,
  "a row first heard after now means the clock wrapped: the device has been up 49 days, not one second");

eq(tooEarly(census([row(16, { first_ms: 100, last_ms: 0xf000_0000 })], { uptime_ms: 1_000 })), false,
  "a row last heard after now also means the clock wrapped, even when its first frame came early");

report("probe pdos");
