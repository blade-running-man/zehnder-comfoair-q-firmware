// web/mock/tests/scenarios.test.ts
// Every preset applies, shows the edge state it is named for, and can be switched away from
// without a restart.
//
//   node mock/tests/scenarios.test.ts
import { eq, ok, report } from "../../src/pages/settings/tests/harness.ts";
import { PRESETS } from "../scenarios.ts";
import { tick } from "../evolve.ts";
import { initialState } from "../model.ts";
import { fresh, get, post, state } from "./req.ts";

for (const name of Object.keys(PRESETS)) {
  const s = initialState();
  const r = post(s, "/__mock/scenario", { preset: name });
  eq([r.status, s.scenario.preset], [200, name], `preset ${name} applies`);
}

{
  const v = state(fresh("heating"));
  eq([v.heating_season, v.cooling_season, v.bypass_position], [true, false, 0], "heating: heating season, bypass closed");
  ok((v.supply_air_temp as number) > (v.outdoor_air_temp as number) + 10, "heating: the core warms the supply air well above outdoor");
  const c = state(fresh("cooling"));
  eq([c.cooling_season, c.bypass_position], [true, 100], "cooling: cooling season, the bypass opened by Auto");
  ok(Math.abs((c.supply_air_temp as number) - (c.outdoor_air_temp as number)) < 1, "cooling: bypassed supply air is outdoor air");
  const f = state(fresh("supply_stopped"));
  eq([f.supply_fan_flow, f.supply_fan_speed, f.airflow_constraints], [0, 0, "Below: Frost Protection"], "supply stopped: zero flow, frost protection named");
  ok((f.exhaust_fan_flow as number) > 0, "supply stopped: the extract fan still runs");
  eq(state(fresh("filters_soon")).filter_replacement_remaining_days, 9, "filters soon: 9 days");
  eq(state(fresh("filters_overdue")).filter_replacement_remaining_days, 0, "filters overdue: 0 days");
  eq(state(fresh("waiting")).supply_air_temp, null, "waiting: no readings");
  eq(post(fresh("refusing"), "/api/entities/bypass_mode", { value: "Open" }).status, 503, "refusing: writes are 503");
  eq(get(fresh("offline"), "/api/state").drop, true, "offline: dropped");
  eq(get(fresh("locked"), "/api/state").status, 401, "locked: 401");
  eq(post(fresh("unprovisioned"), "/api/config", { device_name: "x" }).status, 403, "unprovisioned: settings refused");
}

// Switching back resets what the preset set: no lock, no offline, no stale knobs.
{
  const s = fresh("locked");
  post(s, "/__mock/scenario", { preset: "offline" });
  eq(get(s, "/api/state").drop, true, "locked -> offline");
  post(s, "/__mock/scenario", { preset: "heating" });
  eq(get(s, "/api/state").status, 200, "-> heating: open and reachable again");
  eq(s.config.ui_password, "", "the preset reset the UI password");
}

// Knobs on their own, and on top of a preset.
{
  const s = fresh();
  post(s, "/__mock/scenario", { filterDays: 3 });
  eq(state(s).filter_replacement_remaining_days, 3, "a single knob changes only itself");
  post(s, "/__mock/scenario", { preset: "cooling", filterDays: 25 });
  const v = state(s);
  eq([v.cooling_season, v.filter_replacement_remaining_days], [true, 25], "knobs apply on top of a preset");
  post(s, "/__mock/scenario", { season: "heating" });
  let t = s;
  for (let i = 0; i < 15; i++) t = tick(t, 1000);
  eq(state(t).bypass_position, 0, "a season knob alone moves the weather and the bypass closes");
  eq(post(s, "/__mock/scenario", { commands: "down" }).status, 200, "commands knob");
  eq(post(s, "/api/entities/bypass_mode", { value: "Open" }).status, 503, "takes effect at once");

  const bad = (body: unknown) => post(s, "/__mock/scenario", body).status;
  eq(bad({ preset: "nope" }), 400, "an unknown preset is 400");
  eq(bad({ colour: "red" }), 400, "an unknown knob is 400");
  eq(bad({ offline: "yes" }), 400, "a knob of the wrong type is 400");
  eq(bad({ commands: "sometimes" }), 400, "an enumerated knob outside its choices is 400");
  eq(bad({ filterDays: -1 }), 400, "filterDays below zero is 400");
  eq(bad([1]), 400, "a non-object body is 400");

  const read = get(s, "/__mock/scenario").body as { scenario: Record<string, unknown>; presets: Record<string, string> };
  eq(Object.keys(read.presets), Object.keys(PRESETS), "GET lists every preset with its description");
  eq(read.scenario.commands, "down", "and the current knobs");
  post(s, "/__mock/scenario", { password: "do-not-log-me" });
  ok(!JSON.stringify(get(s, "/api/log", { authorization: "Basic " + btoa(":do-not-log-me") }).body).includes("do-not-log-me"), "a password set through the mock is not written to the public log");
}

report("mock/scenarios");
