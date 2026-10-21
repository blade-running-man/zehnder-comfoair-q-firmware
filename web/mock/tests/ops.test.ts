// web/mock/tests/ops.test.ts
// POST /api/ops/<name>: the six operations, their required parameters, and the order of refusals
// (body, then decoding, then the command, then the queue).
//
//   node mock/tests/ops.test.ts
import { eq, report } from "../../src/pages/settings/tests/harness.ts";
import { tick } from "../evolve.ts";
import type { MockState } from "../model.ts";
import { errorOf, fieldOf, fresh, post, state } from "./req.ts";

const MISSING = "This operation has no optional parameters; one is missing.";
const BAD_VALUE = "That value is not one this entity accepts.";

function run(s: MockState, seconds: number): MockState {
  let t = s;
  for (let i = 0; i < seconds; i++) t = tick(t, 1000);
  return t;
}

{
  const s = fresh();
  eq(post(s, "/api/ops/boost", { duration: 5 }).status, 200, "boost with a duration is accepted");
  eq(state(s).next_fan_change_in_raw, 5, "the countdown starts at the duration");
  const t = run(s, 6);
  eq([state(t).ventilation_mode, state(t).next_fan_change_in_raw], ["Auto", null], "boost expires and returns to the previous mode");
  post(s, "/api/ops/boost", { duration: 0 });
  eq(state(s).ventilation_mode, "Auto", "duration 0 CANCELS boost");

  const m = post(s, "/api/ops/boost", {});
  eq([m.status, errorOf(m)], [422, MISSING], "no parameter has a default");
  eq(post(s, "/api/ops/set_bypass", { mode: "open" }).status, 422, "set_bypass needs a duration too");
  eq(post(s, "/api/ops/set_bypass", { mode: "open", duration: 120 }).status, 200, "set_bypass open for 120 s");
  eq([state(s).bypass_mode, state(s).next_bypass_change_in_raw], ["Open", 120], "bypass open, counting down");
  post(s, "/api/ops/set_bypass", { mode: "closed", duration: 0 });
  eq([state(s).bypass_mode, state(s).next_bypass_change_in_raw], ["Closed", null], "0 means until told otherwise");
  const bm = post(s, "/api/ops/set_bypass", { mode: "Open", duration: 10 });
  eq([bm.status, errorOf(bm)], [422, BAD_VALUE], "op modes are lower-case names, not select labels");

  eq(post(s, "/api/ops/set_balance_mode", { mode: "exhaust_only", duration: 60 }).status, 200, "extract only");
  eq(state(s).balance_mode, "Extract Only", "balance follows");
  eq(post(s, "/api/ops/set_ventilation_level", { level: 3 }).status, 200, "level 3");
  eq(state(s).ventilation_mode, "High", "level 3 is High");
  eq(post(s, "/api/ops/set_ventilation_level", { level: 4 }).status, 422, "level 4 does not exist");
  eq(post(s, "/api/ops/set_temperature_profile", { profile: "warm" }).status, 200, "profile warm");
  eq(state(s).temperature_profile, "Warm", "profile follows");
  eq(post(s, "/api/ops/set_temperature_profile", { profile: "auto" }).status, 422, "auto is not a profile the firmware sends");
  eq(post(s, "/api/ops/set_away", { enabled: true }).status, 422, "set_away needs its duration");
  eq(post(s, "/api/ops/set_away", { enabled: true, duration: 3600 }).status, 200, "away for an hour");
  eq([state(s).away_mode, state(s).next_fan_change_in_raw], [true, 3600], "away counts down on PDO 81");

  const frac = post(s, "/api/ops/boost", { duration: 18.5 });
  eq([frac.status, fieldOf(frac)], [422, "duration"], "a fractional duration is a bad field, never 'forever'");
  eq([post(s, "/api/ops/set_away", { enabled: "yes", duration: 1 }).status, fieldOf(post(s, "/api/ops/set_away", { enabled: "yes", duration: 1 }))], [422, "enabled"], "enabled must be a boolean");
  eq(fieldOf(post(s, "/api/ops/set_bypass", { mode: "x".repeat(16), duration: 1 })), "mode", "a mode longer than its buffer is a bad field");
  const unknown = post(s, "/api/ops/self_destruct", {});
  eq([unknown.status, errorOf(unknown)], [404, "No such operation."], "an unknown operation is 404");
  eq(post(s, "/api/ops/self_destruct", [1]).status, 400, "but the body is decoded first: a bad body is 400 even there");
  eq(post(s, "/api/ops/boost").status, 413, "and read before that: no body is 413");
}

{
  const s = fresh("refusing");
  eq(post(s, "/api/ops/boost", {}).status, 422, "a missing parameter is refused before the queue is asked");
  eq([post(s, "/api/ops/boost", { duration: 60 }).status, errorOf(post(s, "/api/ops/boost", { duration: 60 }))], [503, "the bus is busy"], "a valid op on a busy bus is 503");
}

report("mock/ops");
