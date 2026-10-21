// order.ts and its layout groups.ts, pinned against the REAL entity list, not a fixture.
//
// Run: node src/pages/state/tests/order.test.ts
//
// GROUPS hardcodes keys in a hand-placed layout, and groups.ts's own
// header explains why that is allowed even though a second entity list is forbidden: it is safe
// only as long as this suite enforces both drift directions --
//
//   * a GROUPS key the firmware no longer has must fail this suite (see "every GROUPS key exists
//     in ENTITIES" below) -- that is what stops GROUPS from silently going stale, and it is the
//     whole reason an ordering file is allowed to name keys at all;
//   * a firmware key GROUPS does not know must still render (see the "unknown key" tests below),
//     because an entity nobody placed going invisible is worse than a wrong position.
//
// The "placed exactly once" test runs against the live ENTITIES array on purpose: a fixture
// would keep passing after pdo_table.py added or removed an entity, which is exactly the drift
// this suite exists to catch.

import { ENTITIES, type EntityMeta } from "../../../api/entities.ts";
import { GROUPS } from "../groups.ts";
import { groupOf, orderedGroups } from "../order.ts";
import { eq, ok, report } from "../../settings/tests/harness.ts";

// --- GROUPS must not name a key the firmware no longer has -----------------------------------
//
// groups.ts's own header explains this is now caught twice, deliberately: EntityGroup.keys is
// typed as `readonly KnownKey[]` (the live union `(typeof ENTITIES)[number]["key"]`), so a
// rename or removal in pdo_table.py fails `tsc -b` -- inside the firmware build, before anyone
// runs a test at all. The loop below is the belt-and-braces layer for the case that guard can't
// reach: a bare `node order.test.ts` (this file's own run line above) does no typechecking, so
// the runtime check is what still catches the drift there.

// Widened to Set<string> explicitly, unlike EntityGroup.keys in groups.ts: ENTITIES is `as
// const`, so `.key` is a narrow literal union, and this loop's whole point is to compare GROUPS'
// keys against it at RUNTIME, the one place groups.ts's own compile-time guard (EntityGroup.keys
// : readonly KnownKey[]) cannot reach. Typing this Set narrowly would just move the comparison
// back to compile time and make this loop redundant with that guard instead of covering what it
// misses.
const entityKeys = new Set<string>(ENTITIES.map((e) => e.key));
for (const group of GROUPS) {
  for (const key of group.keys) {
    ok(entityKeys.has(key), `GROUPS["${group.id}"] key "${key}" exists in ENTITIES`);
  }
}

// --- Every real entity is placed exactly once -------------------------------------------------

const rendered = orderedGroups(ENTITIES);
const flat = rendered.flatMap((g) => g.entities);

eq(flat.length, ENTITIES.length, "orderedGroups(ENTITIES) places every entity, none twice");

const seen = new Set<string>();
const duplicates: string[] = [];
for (const meta of flat) {
  if (seen.has(meta.key)) duplicates.push(meta.key);
  seen.add(meta.key);
}
eq(duplicates, [], "no key is placed in two groups");

const missing = ENTITIES.filter((e) => !seen.has(e.key)).map((e) => e.key);
eq(missing, [], "no entity from ENTITIES is dropped");

// --- GROUPS itself has no internal duplicate --------------------------------------------------
// (Caught indirectly by the count check above too, but this pins the cause rather than just the
// symptom if it ever regresses.)

const allListedKeys = GROUPS.flatMap((g) => g.keys);
eq(
  allListedKeys.length,
  new Set(allListedKeys).size,
  "no key appears in GROUPS twice, whether in the same group or two different ones",
);

// --- Group display order is the layout's own order of the groups themselves ----------------

eq(
  rendered.map((g) => g.id),
  [
    "ventilation",
    "temperature",
    "humidity",
    "airflow",
    "bypass",
    "energy",
    "filter",
    "timers",
    "settings",
    "diagnostic",
  ],
  "every group renders in the layout's order, all present against the real entity list",
);

eq(
  rendered.map((g) => g.label),
  [
    "Ventilation",
    "Temperatures",
    "Humidity",
    "Airflow and fans",
    "Bypass and season",
    "Energy",
    "Filter",
    "Timers",
    "Settings",
    "Diagnostics",
  ],
  "group labels match the layout's group names",
);

ok(
  rendered.every((g) => !("optional" in g) && !("evidence" in g)),
  "no group is optional: the firmware publishes only the base unit's rows, so every group always shows",
);

ok(
  rendered.every((g) => g.id !== "other"),
  "every real entity is placed by GROUPS or its fallback group; nothing lands in Other",
);

// --- Within-group order is the layout's per-entity order, not PDO order -----------------------

function keysOf(id: string): string[] {
  return rendered.find((g) => g.id === id)!.entities.map((e) => e.key);
}

eq(
  keysOf("temperature"),
  [
    "outdoor_air_temp",
    "preheated_outdoor_air_temp",
    "supply_air_temp",
    "supply_air_temp_post_heater",
    "extract_air_temp",
    "exhaust_air_temp",
    "profile_target_temp",
    "running_mean_outdoor_temp",
  ],
  "Temperatures follows the air's path through the heat exchanger, not PDO order",
);

eq(
  keysOf("airflow"),
  [
    "supply_fan_flow",
    "exhaust_fan_flow",
    "supply_fan_duty",
    "exhaust_fan_duty",
    "supply_fan_speed",
    "exhaust_fan_speed",
    "fan_speed_modulated",
    "airflow_constraints",
  ],
  "Airflow and fans puts airflow_constraints beside the flow numbers it explains, not in Diagnostics",
);

eq(
  keysOf("filter"),
  ["filter_replacement_remaining_days", "changing_filters"],
  "Filter exists as its own group and leads with the one number that asks the owner to act",
);

eq(
  keysOf("timers"),
  [
    "boost_duration",
    "bypass_duration",
    "balance_duration",
    "away_duration",
    "boost_timer_running",
    "away_timer_running",
  ],
  "Timers holds the four settable durations and the two timer flags, not the next_*_change_in_raw countdowns",
);

eq(
  keysOf("diagnostic").slice(0, 2),
  ["temperature_unit", "airflow_unit"],
  "temperature_unit/airflow_unit lead Diagnostics: every other number's unit depends on them",
);

eq(
  keysOf("diagnostic").at(-1),
  "schema_version",
  "schema_version is the last row of Diagnostics: a machine-contract fact, read by nobody",
);

eq(
  keysOf("diagnostic").at(-2),
  "data_freshness",
  "data_freshness sits just above schema_version, not appended after it by the fallback",
);

const energy = keysOf("energy");
eq(
  energy[energy.indexOf("preheater_power") + 1],
  "frost_protection_preheater_power",
  "the frost-protection pre-heater draw sits beside the pre-heater's power: the pre-heater is the Q's own",
);

ok(
  keysOf("bypass").includes("frost_protection_unbalance"),
  "frost_protection_unbalance is the base unit's own figure: shown with the bypass (PDO 219's frost-protection power sits in Energy)",
);

const diag = keysOf("diagnostic");
ok(
  diag.indexOf("next_fan_change_in_raw") > diag.indexOf("exhaust_fan_mode"),
  "the next_*_change_in_raw countdowns join this unit's mode flags in Diagnostics, moved out of Timers",
);

ok(
  diag.indexOf("intake_air_temp_unverified") > diag.indexOf("next_exhaust_change_in_raw"),
  "the (unverified) readings are quarantined below the mode flags and countdowns, away from trusted readings",
);
eq(
  diag.slice(diag.indexOf("intake_air_temp_unverified")),
  ["intake_air_temp_unverified", "sensor_ventilation_mode", "data_freshness", "schema_version"],
  "Diagnostics ends with the two unverified rows, then the two machine-contract rows",
);

// --- groupOf(): the fallback derivation, unit-tested directly ----------------------------------

function fakeMeta(overrides: Partial<EntityMeta>): EntityMeta {
  return {
    key: "fake",
    name: "Fake",
    kind: "numeric",
    pdo: null,
    unit: null,
    deviceClass: null,
    icon: null,
    entityCategory: null,
    options: null,
    writable: false,
    enabledByDefault: true,
    min: null,
    max: null,
    ...overrides,
  };
}

eq(groupOf(fakeMeta({ entityCategory: "diagnostic" })), "diagnostic", "diagnostic category wins first");
eq(groupOf(fakeMeta({ deviceClass: "temperature" })), "temperature", "temperature device class");
eq(groupOf(fakeMeta({ deviceClass: "humidity" })), "humidity", "humidity device class");
eq(groupOf(fakeMeta({ deviceClass: "power" })), "energy", "power device class groups as energy");
eq(groupOf(fakeMeta({ deviceClass: "energy" })), "energy", "energy device class");
eq(groupOf(fakeMeta({ unit: "m³/h" })), "airflow", "m³/h unit groups as airflow");
eq(groupOf(fakeMeta({ unit: "rpm" })), "airflow", "rpm unit groups as airflow");
eq(groupOf(fakeMeta({ deviceClass: "volume_flow_rate" })), "airflow", "volume_flow_rate groups as airflow");
eq(groupOf(fakeMeta({ kind: "fan" })), "ventilation", "fan kind");
eq(groupOf(fakeMeta({ kind: "select" })), "ventilation", "select kind");
eq(groupOf(fakeMeta({ kind: "switch" })), "ventilation", "switch kind");
eq(groupOf(fakeMeta({ kind: "number", unit: "min" })), "other", "nothing else matches falls to other");

// --- groupOf()'s range is exactly GROUPS' ids plus "other" -------------------------------------
// orderedGroups() only ever reads two kinds of bucket back out: one per id in GROUPS, and the
// dedicated "other" bucket (see its final read-out loop). groupOf()'s own doc comment claims
// every branch returns one or the other on purpose -- this pins that claim against the live
// GROUPS list, over the live ENTITIES array, rather than trusting the comment. If a future
// branch in groupOf() ever returned something else, orderedGroups()'s id-folding (the
// `knownGroupIds.has(raw) ? raw : OTHER_GROUP_ID` line) is what stops that entity from landing
// in a bucket nothing reads out -- this test is what would tell you that fallback had started
// being needed.
const knownGroupIds = new Set(GROUPS.map((g) => g.id));
for (const meta of ENTITIES) {
  const id = groupOf(meta);
  ok(
    knownGroupIds.has(id) || id === "other",
    `groupOf("${meta.key}") = "${id}" is one of GROUPS' ids, or "other"`,
  );
}

// --- An unlisted key must still appear, appended after the listed entities of its group --------
// This is the regression this whole file exists to prevent: an entity GROUPS has never heard of
// (the firmware gained a PDO after this file was last edited) must render, not vanish.

const newTempSensor = fakeMeta({
  key: "new_temp_sensor_v2",
  name: "New Temp Sensor",
  deviceClass: "temperature",
  unit: "°C",
});
const withUnknown = orderedGroups([...ENTITIES, newTempSensor]);
const tempGroup = withUnknown.find((g) => g.id === "temperature")!;
eq(
  tempGroup.entities.at(-1)!.key,
  "new_temp_sensor_v2",
  "an unlisted temperature-class key falls back to groupOf() and lands last in Temperatures",
);
// Derived from GROUPS itself, not hardcoded: this pins "exactly the listed entities plus the
// one unlisted key", which stays true no matter how many temperatures GROUPS lists. A literal
// count here would fail the day someone added an eleventh PDO to the Temperatures group in
// GROUPS, for a reason unrelated to what this test exists to check (the at(-1) assertion above
// already covers the actual regression: unlisted entities append, they don't vanish).
const listedTemperatureCount = GROUPS.find((g) => g.id === "temperature")!.keys.length;
eq(
  tempGroup.entities.length,
  listedTemperatureCount + 1,
  "Temperatures gained exactly one row: the listed entities plus the one unlisted key",
);

// An entity groupOf() cannot place anywhere (matches none of its branches) still has to render
// somewhere: the "Other" group, appended after every known group, exactly like the old
// State.tsx derivation's catch-all.
const trulyUnclassifiable = fakeMeta({
  key: "mystery_entity",
  name: "Mystery",
  kind: "numeric",
  unit: "widgets",
});
const withOther = orderedGroups([...ENTITIES, trulyUnclassifiable]);
eq(
  withOther.at(-1)!.id,
  "other",
  "an entity groupOf() cannot classify creates a trailing Other group, after Diagnostics",
);
eq(
  withOther.at(-1)!.entities.map((e) => e.key),
  ["mystery_entity"],
  "Other holds only the entity nothing else could place",
);

// --- Two entities sharing an unlisted key render once, not twice -------------------------------
// The listed path dedups via byKey (a Map, one entry per key); the unlisted/fallback path used
// to push every match with no such check, so two entities sharing a key that GROUPS does not
// list would both render, with duplicate Preact `key` props as the visible symptom. Not
// reachable from the generated ENTITIES array today, but nothing downstream promises unique
// keys, so the fallback path dedups the same way the listed path always has.
const dup1 = fakeMeta({ key: "dup_key", name: "Dup 1", deviceClass: "temperature" });
const dup2 = fakeMeta({ key: "dup_key", name: "Dup 2", deviceClass: "temperature" });
const withDupes = orderedGroups([...ENTITIES, dup1, dup2]);
eq(
  withDupes.flatMap((g) => g.entities).filter((e) => e.key === "dup_key").length,
  1,
  "a duplicated unlisted key renders exactly once, the same as the listed path already guaranteed",
);

// --- A key GROUPS lists that the entity set does not have is skipped, not crashed on -----------
// (The suite above already fails loudly on this via the "GROUPS key exists in ENTITIES" checks;
// this pins that orderedGroups() itself degrades gracefully rather than throwing, since a stale
// GROUPS entry and a fresh build reaching this function at the same time must not crash the page.)

const withoutFan = ENTITIES.filter((e) => e.key !== "fan");
const rendered2 = orderedGroups(withoutFan);
ok(
  rendered2.flatMap((g) => g.entities).every((e) => e.key !== "fan"),
  "a GROUPS key absent from the entity set is silently skipped, not fabricated",
);
eq(
  rendered2.flatMap((g) => g.entities).length,
  withoutFan.length,
  "removing one entity removes exactly one row, with no crash",
);

// --- A group with nothing to show is omitted, not rendered empty -------------------------------

const onlyOneTemp = ENTITIES.filter((e) => e.key === "outdoor_air_temp");
const rendered3 = orderedGroups(onlyOneTemp);
eq(
  rendered3.map((g) => g.id),
  ["temperature"],
  "a group with no matching entities at all does not render as an empty section",
);

report("order");
