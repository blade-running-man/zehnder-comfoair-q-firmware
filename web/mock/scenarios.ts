// web/mock/scenarios.ts
// The dev-only scenario surface: GET /__mock/scenario reads the knobs and lists the presets,
// POST /__mock/scenario switches them -- live, no restart. NOT part of the firmware contract: it
// exists so the SPA's edge states (a stopped fan, overdue filters, a silent bus, a locked or
// unprovisioned device) can be reached without a unit on the bench. scenarios.md is the manual.
//
// A body is {"preset": "<name>"} and/or individual knobs. A preset is a RESET: the scenario, the
// unit, the network and the UI password start over from that preset, then any knobs in the same
// body are applied on top. Knobs alone change only themselves.
import {
  SETUP_WINDOW_MS, defaultScenario, initialUnit, logLine,
  type MockResponse, type MockState, type Scenario, type Unit,
} from "./model.ts";
import { settle } from "./evolve.ts";

interface Preset {
  description: string;
  knobs: Partial<Scenario>;
  unit?: (u: Unit) => void;
  password?: string;
}

export const LOCKED_PASSWORD = "comfoair-mock";

export const PRESETS: Record<string, Preset> = {
  heating: { description: "Heating season: 2 C outside, 21.5 C indoors, bypass closed, full recovery", knobs: {} },
  cooling: { description: "Mild/cooling season: 19 C outside, 24.5 C indoors, bypass opens for free cooling", knobs: { season: "cooling" } },
  supply_stopped: {
    description: "Frost protection on a unit without a pre-heater: -12 C outside, supply fan stopped",
    knobs: { supplyStopped: true },
    unit: (u) => { u.outdoorBase = -12; u.outdoorRhBase = 88; u.runningMeanC = -6; },
  },
  filters_soon: { description: "Filters due soon: 9 days left", knobs: { filterDays: 9 } },
  filters_overdue: { description: "Filters overdue: 0 days left", knobs: { filterDays: 0 } },
  waiting: { description: "No readings yet: the bus has delivered no PDO, only seeded values exist", knobs: { readings: "none" } },
  refusing: { description: "The device refuses every command: 503 the bus is busy", knobs: { commands: "busy" } },
  bus_down: { description: "CAN bus never came up: no readings, 503 commands are not running", knobs: { commands: "down", readings: "none" } },
  offline: { description: "The device is unreachable: every connection is dropped", knobs: { offline: true } },
  unprovisioned: { description: "Fresh device on its setup access point: only POST /api/provision is accepted", knobs: { provisioned: false } },
  locked: { description: `A UI password is set (any user name, password "${LOCKED_PASSWORD}")`, knobs: {}, password: LOCKED_PASSWORD },
  read_only: { description: "Settings written by a newer firmware: POST /api/config answers 409", knobs: { configReadOnly: true } },
  scan_unavailable: { description: "The radio cannot scan: GET /api/wifi/scan answers 503", knobs: { scan: "unavailable" } },
  // Both OTA presets carry the locked password: without one the policy answers 403 before the
  // knob is ever reached, which would make the preset show nothing it is named for.
  ota_fail: {
    description: `Firmware image fails verification: POST /api/ota answers 422 (password "${LOCKED_PASSWORD}")`,
    knobs: { ota: "fail" }, password: LOCKED_PASSWORD,
  },
  ota_rollback: {
    description: `Firmware is accepted, but the device comes back running the OLD version (password "${LOCKED_PASSWORD}")`,
    knobs: { ota: "rollback" }, password: LOCKED_PASSWORD,
  },
  unit_unread: { description: "The unit has never answered the identity read: GET /api/system's unit block is unknown and null", knobs: { unitInfo: "unread" } },
  unit_errors: { description: "The unit reports two active errors, one the firmware has no text for", knobs: { unitInfo: "errors" } },
  probe_silent: { description: "The unit ignores RMI reads from this device: the probe fails with no_answer unless another_counter is 1", knobs: { probe: "silent" } },
};

// The allowed values of every enumerated knob; booleans and numbers are checked by type.
const CHOICES: Partial<Record<keyof Scenario, readonly string[]>> = {
  season: ["heating", "cooling"], readings: ["live", "none"], commands: ["ok", "busy", "down"],
  scan: ["ok", "unavailable"], ota: ["ok", "fail", "rollback"], probe: ["ok", "silent"],
  unitInfo: ["ok", "unread", "no_answer", "errors"],
};

function bad(message: string): MockResponse {
  return { status: 400, body: { error: message } };
}

// Validates a knob against the default scenario's type, so a typo in a curl is a 400 and not a
// string where the model expects a boolean.
function checkKnob(key: string, value: unknown): string | null {
  const def = defaultScenario() as unknown as Record<string, unknown>;
  if (!(key in def) || key === "preset") return `unknown knob: ${key}`;
  const choices = CHOICES[key as keyof Scenario];
  if (choices) return typeof value === "string" && choices.includes(value) ? null : `${key} must be one of ${choices.join(", ")}`;
  if (key === "filterDays") return Number.isInteger(value) && (value as number) >= 0 ? null : "filterDays must be a whole number >= 0";
  return typeof value === typeof def[key] ? null : `${key} must be a ${typeof def[key]}`;
}

// The network side of `provisioned`: an unclaimed device is on its access point with nothing
// stored; a claimed one is on the owner's network.
function setProvisioned(s: MockState, provisioned: boolean): void {
  s.scenario.provisioned = provisioned;
  s.net.connectingLeftMs = 0;
  if (provisioned) {
    s.net.state = "connected"; s.net.knownGood = true; s.net.windowLeftMs = 0;
    if (s.config.wifi_ssid === "") { s.config.wifi_ssid = "home-wifi"; s.config.wifi_psk = "correct-horse"; }
  } else {
    s.net.state = "unconfigured"; s.net.knownGood = false; s.net.windowLeftMs = SETUP_WINDOW_MS;
    s.config.wifi_ssid = ""; s.config.wifi_psk = "";
  }
}

// A season knob on its own moves the bases and snaps the readings to them; the commanded state
// (mode, bypass, timers) is left as the owner set it.
function setSeason(s: MockState, season: Scenario["season"]): void {
  const fresh = initialUnit(season);
  const u = s.unit;
  u.outdoorBase = fresh.outdoorBase; u.indoorBase = fresh.indoorBase;
  u.outdoorRhBase = fresh.outdoorRhBase; u.indoorRhBase = fresh.indoorRhBase;
  u.extractC = fresh.extractC; u.runningMeanC = fresh.runningMeanC;
  s.scenario.season = season;
}

export function applyScenario(s: MockState, body: unknown): MockResponse {
  if (typeof body !== "object" || body === null || Array.isArray(body)) return bad("the body must be a JSON object");
  const patch = body as Record<string, unknown>;
  const presetName = patch.preset;
  if (presetName !== undefined && (typeof presetName !== "string" || !(presetName in PRESETS))) {
    return bad(`unknown preset: ${String(presetName)}; one of ${Object.keys(PRESETS).join(", ")}`);
  }
  for (const [k, v] of Object.entries(patch)) {
    if (k === "preset") continue;
    if (k === "password") { if (typeof v !== "string") return bad("password must be a string"); continue; }
    const why = checkKnob(k, v);
    if (why) return bad(why);
  }

  if (typeof presetName === "string") {
    const p = PRESETS[presetName];
    s.scenario = { ...defaultScenario(), ...p.knobs, preset: presetName };
    s.unit = initialUnit(s.scenario.season);
    p.unit?.(s.unit);
    settle(s.unit, s.scenario);
    setProvisioned(s, s.scenario.provisioned);
    s.config.ui_password = p.password ?? "";
  }
  for (const [k, v] of Object.entries(patch)) {
    if (k === "preset") continue;
    if (k === "password") s.config.ui_password = v as string;
    else if (k === "provisioned") setProvisioned(s, v as boolean);
    else if (k === "season") setSeason(s, v as Scenario["season"]);
    else (s.scenario as unknown as Record<string, unknown>)[k] = v;
  }
  // The password is not logged: GET /api/log serves this ring to anybody, as on the device.
  const shown = { ...patch, ...("password" in patch ? { password: "***" } : {}) };
  logLine(s, `mock: scenario ${JSON.stringify(shown)}`);
  return { status: 200, body: readScenario(s).body };
}

export function readScenario(s: MockState): MockResponse {
  const presets: Record<string, string> = {};
  for (const [name, p] of Object.entries(PRESETS)) presets[name] = p.description;
  return {
    status: 200,
    body: { scenario: s.scenario, password_set: s.config.ui_password !== "", presets },
  };
}
