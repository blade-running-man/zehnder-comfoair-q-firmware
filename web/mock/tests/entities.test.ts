// web/mock/tests/entities.test.ts
// The mock's entity set IS the generated registry, and writes are judged against it: options,
// ranges and writability come from src/api/entities.ts, refusals from comfoair_command/_wire.
//
//   node mock/tests/entities.test.ts
import { eq, ok, report } from "../../src/pages/settings/tests/harness.ts";
import { ENTITIES } from "../../src/api/entities.ts";
import { drivenKeys, seeded } from "../values.ts";
import { tick } from "../evolve.ts";
import { IDENTITY_FIRST_MS } from "../system.ts";
import { errorOf, fieldOf, fresh, post, get, state } from "./req.ts";

const BAD_VALUE = "That value is not one this entity accepts.";
const BAD_FIELD = "A field is missing, of the wrong type, or longer than this device can store.";
const NOT_WRITABLE = "No such writable entity.";

// --- the projection is the registry, no more and no less ----------------------------------
{
  // Past the first unit read: unit_error and active_errors come from it, not from a PDO, and are
  // rightly null before it (system.test.ts pins that half).
  let s = fresh();
  for (let t = 0; t <= IDENTITY_FIRST_MS; t += 1000) s = tick(s, 1000);
  const doc = state(s);
  eq(Object.keys(doc), ENTITIES.map((e) => e.key), "GET /api/state carries every registry key, in registry order");
  const known = new Set(ENTITIES.map((e) => e.key as string));
  const stray = drivenKeys(s).filter((k) => !known.has(k));
  eq(stray, [], "the model drives no key the registry does not have (a rename fails here)");
  const countdowns = new Set(["next_fan_change_in_raw", "next_bypass_change_in_raw", "next_supply_change_in_raw", "next_exhaust_change_in_raw"]);
  const blank = ENTITIES.filter((e) => !countdowns.has(e.key) && doc[e.key] === null).map((e) => e.key);
  eq(blank, [], "with live readings every non-countdown entity has a value");
  for (const k of countdowns) eq(doc[k], null, `${k} is null with nothing scheduled`);

  const list = get(s, "/api/entities").body as Record<string, unknown>[];
  eq(list.length, ENTITIES.length, "the entity list has one row per registry entity");
  eq(Object.keys(list[0]), ["key", "name", "kind", "pdo", "min", "max", "unit", "deviceClass", "stateClass", "icon", "entityCategory", "writable", "options"], "rows carry comfoair_api's keys in its order");
  const away = list.find((r) => r.key === "away_duration");
  eq([away?.min, away?.max, away?.writable], [0, 43200, true], "ranges and writability come from the registry");

  // Values have the registry's own types: a select's value is one of its options.
  for (const e of ENTITIES) {
    const v = doc[e.key];
    if (e.kind === "select" || e.kind === "enum") ok((e.options as readonly string[]).includes(v as string), `${e.key} = ${JSON.stringify(v)} is one of its options`);
    if (e.kind === "binary" || e.kind === "switch") ok(typeof v === "boolean", `${e.key} is a boolean`);
    if (e.kind === "number" && e.min !== null && e.max !== null) ok(typeof v === "number" && v >= e.min && v <= e.max, `${e.key} is within its range`);
  }
  const fan = doc.fan as { on: boolean; speed: number; preset: string };
  eq(Object.keys(fan), ["on", "speed", "preset"], "the fan renders as {on, speed, preset}");
}

// --- waiting: nothing heard from the unit, only seeded values exist --------------------------
{
  const s = fresh("waiting");
  const doc = state(s);
  const valued = ENTITIES.filter((e) => doc[e.key] !== null).map((e) => e.key);
  eq(valued, ["temperature_passive", "humidity_comfort", "humidity_protection", "boost_duration", "bypass_duration", "balance_duration", "away_duration", "schema_version", "data_freshness"], "only the device's own seeds have values (data_freshness: seeded true at boot, comfoair_state.cpp)");
  ok(valued.every(seeded), "and each of them is a seeded entity");
  eq(doc.fan, null, "the fan is null until PDO 65 arrives");
  eq(doc.ventilation_mode, null, "ventilation_mode is derived from PDOs, so it is null too");
}

// --- writes: decoded, judged against the registry, applied ----------------------------------
{
  const s = fresh();
  const w = post(s, "/api/entities/bypass_mode", { value: "Open" });
  eq([w.status, w.body], [200, { accepted: true }], "a valid select write is 200 {accepted:true}");
  let after = state(s);
  eq([after.bypass_mode, after.next_bypass_change_in_raw], ["Open", 3600], "it takes effect, with the stored 60-min bypass duration counting down");
  eq(after.bypass_override, true, "and the override PDO's timer bit says so");
  let t = s;
  for (let i = 0; i < 12; i++) t = tick(t, 1000);
  eq(state(t).bypass_position, 100, "the damper travels open within seconds");

  const off = post(s, "/api/entities/bypass_mode", { value: "open" });
  eq([off.status, errorOf(off)], [422, BAD_VALUE], "labels are matched exactly, case included");
  const idx = post(s, "/api/entities/bypass_mode", { value: 1 });
  eq([idx.status, errorOf(idx)], [422, BAD_VALUE], "a select written with an index is refused");
  const ro = post(s, "/api/entities/supply_air_temp", { value: 20 });
  eq([ro.status, errorOf(ro)], [404, NOT_WRITABLE], "a read-only entity is 404 No such writable entity.");
  eq(post(s, "/api/entities/nope", { value: 1 }).status, 404, "an unknown entity is 404 too");
  const nov = post(s, "/api/entities/bypass_mode", { val: "Open" });
  eq([nov.status, errorOf(nov), fieldOf(nov)], [422, BAD_FIELD, "value"], "no `value` is 422 bad field value");
  eq(post(s, "/api/entities/bypass_mode", { value: null }).status, 422, "null is refused, never a default");
  const long = post(s, "/api/entities/bypass_mode", { value: "x".repeat(64) });
  eq([long.status, fieldOf(long)], [422, "value"], "a label longer than any option is refused, not truncated");

  eq(post(s, "/api/entities/boost_duration", { value: 30 }).status, 200, "a duration in range is stored");
  eq(state(s).boost_duration, 30, "and reported back, since no PDO carries it");
  const hi = post(s, "/api/entities/boost_duration", { value: 1441 });
  eq([hi.status, errorOf(hi)], [422, BAD_VALUE], "above the registry max is 422");
  eq(post(s, "/api/entities/boost_duration", { value: 0 }).status, 422, "below the registry min is 422");
  eq(post(s, "/api/entities/away_duration", { value: 0 }).status, 200, "away_duration's min is 0 (indefinite)");
  const frac = post(s, "/api/entities/boost_duration", { value: 1.5 });
  eq([frac.status, errorOf(frac), fieldOf(frac)], [422, BAD_FIELD, "value"], "a fraction is a bad field, not a rounding");
  eq(post(s, "/api/entities/boost_duration", { value: -5 }).status, 422, "a negative is refused");
  eq(post(s, "/api/entities/boost_duration", { value: "30" }).status, 422, "a number written as text is refused");

  eq(post(s, "/api/entities/away_mode", { value: "on" }).status, 422, "the switch takes a boolean only");
  eq(post(s, "/api/entities/away_mode", { value: true }).status, 200, "away on");
  after = state(s);
  eq([after.away_mode, after.device_state, after.fan_level, after.next_fan_change_in_raw], [true, "Away (preset)", "Away", null], "away with duration 0 runs indefinitely at the Away level, which PDO 16 reports as the Away preset");

  const s2 = fresh();
  post(s2, "/api/entities/ventilation_mode", { value: "Boost" });
  after = state(s2);
  eq([after.ventilation_mode, after.operating_mode, after.next_fan_change_in_raw], ["Boost", "Boost", 3600], "Boost runs for boost_duration");
  post(s2, "/api/entities/ventilation_mode", { value: "High" });
  after = state(s2);
  eq([after.ventilation_mode, after.next_fan_change_in_raw, after.operating_mode], ["High", null, "Manual (permanent)"], "a level ends boost; permanent manual is the default");
  post(s2, "/api/entities/fan", { value: false });
  eq(state(s2).fan_level, "Away", "fan off is the Away level, not a stop");
  post(s2, "/api/entities/fan", { value: 3 });
  eq(state(s2).ventilation_mode, "High", "fan speed 3 is High");
  eq(post(s2, "/api/entities/fan", { value: 4 }).status, 422, "fan speed above 3 is refused");
  eq(post(s2, "/api/entities/fan", { value: "Turbo" }).status, 422, "a preset the fan does not list is refused");
  post(s2, "/api/entities/temperature_profile", { value: "Warm" });
  eq(state(s2).profile_target_temp, 23, "the profile moves the derived target temperature");
  post(s2, "/api/entities/humidity_comfort", { value: "On" });
  eq(state(s2).humidity_comfort, "On", "a write-only comfort select reports what was written");
  post(s2, "/api/entities/balance_mode", { value: "Supply Only" });
  t = s2;
  for (let i = 0; i < 30; i++) t = tick(t, 1000);
  after = state(t);
  eq([after.exhaust_fan_flow, after.supply_only_active, after.next_supply_change_in_raw !== null], [0, true, true], "supply only stops the extract fan and counts down");
}

// --- the queue refuses: busy and down ------------------------------------------------
{
  const s = fresh("refusing");
  const r = post(s, "/api/entities/ventilation_mode", { value: "High" });
  eq([r.status, errorOf(r)], [503, "the bus is busy"], "refusing: a command is 503 the bus is busy");
  eq(state(s).ventilation_mode, "Auto", "and changes nothing");
  eq(post(s, "/api/entities/boost_duration", { value: 20 }).status, 200, "a duration needs no frame, so a busy bus still stores it");
  eq(post(s, "/api/entities/bypass_mode", { value: "Nope" }).status, 422, "validation still comes before the queue");

  const d = fresh("bus_down");
  const down = post(d, "/api/entities/ventilation_mode", { value: "High" });
  eq([down.status, errorOf(down)], [503, "commands are not running"], "bus down: 503 commands are not running");
  eq(post(d, "/api/entities/boost_duration", { value: 20 }).status, 503, "nothing is stored when the sender never started");
}

report("mock/entities");
