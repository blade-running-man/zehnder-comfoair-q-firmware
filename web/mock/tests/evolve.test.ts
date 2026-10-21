// web/mock/tests/evolve.test.ts
// The evolve step stays inside physical bounds whatever the owner does to it: every preset is run
// for simulated hours under a stream of random (seeded, so reproducible) writes, and every tick
// is checked. Plus the air helpers' own edges.
//
//   node mock/tests/evolve.test.ts
import { eq, ok, report } from "../../src/pages/settings/tests/harness.ts";
import { ENTITIES } from "../../src/api/entities.ts";
import { approach, efficiency, rhAt, LEVEL_FLOW } from "../air.ts";
import { tick } from "../evolve.ts";
import { PRESETS } from "../scenarios.ts";
import { writeEntity } from "../commands.ts";
import type { MockState } from "../model.ts";
import { stateValues } from "../values.ts";
import { fresh, state } from "./req.ts";

// The projection itself rather than GET /api/state: the "locked" preset answers that with 401,
// and the bounds are a property of the model, not of who may read it.
const values = (s: MockState): Record<string, unknown> => stateValues(s);

// mulberry32: a tiny deterministic PRNG, so a failure here reproduces on every run.
function prng(seed: number): () => number {
  let a = seed;
  return () => {
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

const WRITABLE = ENTITIES.filter((e) => e.writable);

// A plausible write for a random writable entity, valid by the registry.
function randomWrite(s: MockState, rnd: () => number): void {
  const e = WRITABLE[Math.floor(rnd() * WRITABLE.length)];
  let value: unknown;
  if (e.options) value = e.options[Math.floor(rnd() * e.options.length)];
  else if (e.kind === "number") value = Math.round((e.min ?? 0) + rnd() * Math.min(30, (e.max ?? 1) - (e.min ?? 0)));
  else value = rnd() < 0.5;
  const text = JSON.stringify({ value });
  writeEntity(s, e.key, text.length, { value });
}

const num = (v: unknown): number => v as number;
const violations: string[] = [];
function check(s: MockState, where: string): void {
  const v = values(s);
  if (v.supply_air_temp === null) return;  // the waiting presets have nothing to bound
  const lo = Math.min(num(v.outdoor_air_temp), num(v.extract_air_temp), num(v.preheated_outdoor_air_temp)) - 1;
  const hi = Math.max(num(v.outdoor_air_temp), num(v.extract_air_temp), num(v.preheated_outdoor_air_temp)) + 1;
  const bad = (cond: boolean, what: string) => { if (!cond && violations.length < 10) violations.push(`${where}: ${what} ${JSON.stringify(v[what] ?? null)}`); };
  for (const k of ["supply_air_temp", "exhaust_air_temp", "extract_air_temp"]) bad(num(v[k]) >= lo && num(v[k]) <= hi, k);
  for (const k of ["supply_air_humidity", "exhaust_air_humidity", "extract_air_humidity", "outdoor_air_humidity", "preheated_outdoor_air_humidity"]) bad(num(v[k]) >= 0 && num(v[k]) <= 100, k);
  for (const k of ["supply_fan_flow", "exhaust_fan_flow"]) bad(num(v[k]) >= 0 && num(v[k]) <= LEVEL_FLOW[4], k);
  for (const k of ["supply_fan_duty", "exhaust_fan_duty", "bypass_position"]) bad(num(v[k]) >= 0 && num(v[k]) <= 100, k);
  for (const k of ["power", "preheater_power", "avoided_heating", "avoided_cooling", "supply_fan_speed", "exhaust_fan_speed"]) bad(num(v[k]) >= 0, k);
  for (const k of ["next_fan_change_in_raw", "next_bypass_change_in_raw", "next_supply_change_in_raw", "next_exhaust_change_in_raw"]) bad(v[k] === null || num(v[k]) > 0, k);
  bad(num(v.supply_fan_flow) === 0 || num(v.supply_fan_speed) > 0, "supply_fan_speed");
  // The dashboard's own recovery formula (pages/dashboard/ventilation.ts readView) must land in
  // 0..1 without its clamp whenever it would be shown -- a mock that needs the clamp is wrong.
  const out = num(v.outdoor_air_temp), sup = num(v.supply_air_temp), ext = num(v.extract_air_temp);
  if (num(v.supply_fan_flow) > 0 && num(v.bypass_position) <= 50 && Math.abs(ext - out) >= 1.5) {
    const r = (sup - out) / (ext - out);
    bad(r >= -0.05 && r <= 1.05, "supply_air_temp");
  }
}

for (const [i, name] of Object.keys(PRESETS).entries()) {
  if (name === "offline") continue;  // nothing is served; the model under it is heating's
  const rnd = prng(1000 + i);
  let s = fresh(name);
  const energyBefore = values(s).energy_total as number | null;
  for (let t = 0; t < 3600; t++) {
    if (t % 97 === 0) randomWrite(s, rnd);
    s = tick(s, 1000);
    if (t % 13 === 0) check(s, `${name} t=${t}`);
  }
  if (energyBefore !== null) ok((values(s).energy_total as number) >= energyBefore, `${name}: energy counters never run backwards`);
}
eq(violations, [], "an hour of random writes in every preset never leaves physical bounds");

// Timers count down and release their mode.
{
  let s = fresh();
  writeEntity(s, "bypass_duration", 12, { value: 1 });
  writeEntity(s, "bypass_mode", 16, { value: "Open" });
  const seen: number[] = [];
  for (let t = 0; t < 65; t++) { s = tick(s, 1000); const left = state(s).next_bypass_change_in_raw; if (left !== null) seen.push(left as number); }
  ok(seen.every((x, j) => j === 0 || x <= seen[j - 1]), "the bypass countdown never goes up");
  eq([state(s).bypass_mode, state(s).next_bypass_change_in_raw], ["Auto", null], "at zero the override releases to Auto");
}

// tick() is pure: the previous state is not touched.
{
  const s = fresh();
  const before = JSON.stringify(s);
  tick(s, 1000);
  eq(JSON.stringify(s), before, "tick() does not mutate its input");
}

// Air helpers.
ok(Math.abs(approach(0, 10, 1e9, 5) - 10) < 1e-9, "approach converges on the target");
eq(approach(3, 3, 1, 5), 3, "approach at the target stays there");
ok(efficiency(LEVEL_FLOW[1]) > efficiency(LEVEL_FLOW[4]), "recovery falls as the flow rises");
ok(rhAt(85, 2, 20) < 30, "outdoor winter air warmed indoors is dry (absolute humidity conserved)");
eq(rhAt(60, 20, 5), 100, "cooling below the dew point caps at 100 %");
ok(Math.abs(rhAt(50, 15, 15) - 50) < 1e-9, "no temperature change, no humidity change");

report("mock/evolve");
