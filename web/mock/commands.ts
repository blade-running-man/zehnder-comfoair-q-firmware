// web/mock/commands.ts
// POST /api/entities/<key> and POST /api/ops/<name>: the refusals in the firmware's order, then
// the effect on the simulated unit.
//
// The order is the handlers' (comfoair_http_command.c comfoair_http_entity_post/
// comfoair_http_operation_post): the body is read
// (413), decoded (400/422 from comfoair_wire), turned into a command (404/422 from
// comfoair_command), and only then queued (503 when the bus is busy or commands are not running).
// A mock that validated in a friendlier order would teach the SPA error paths the device never
// takes.
//
// WHAT IS WRITABLE comes from the registry (`writable` in src/api/entities.ts) and nowhere else.
// comfoair_command_knows_entity() keeps a list of its own, which today is exactly the registry's
// writable set; if the two ever diverge the firmware is wrong, not this file.
import { ENTITIES } from "../src/api/entities.ts";
import { logLine, type MockState, type MockResponse, type Unit } from "./model.ts";
import { levelOf } from "./evolve.ts";
import { COMMAND, HTTP, badBody, badField, bodyGate, bytes, error, isI32, isObject, isU32 } from "./wire.ts";

type CommandValue = { kind: "text"; text: string } | { kind: "bool"; value: boolean } | { kind: "number"; value: number };
const ACCEPTED: MockResponse = { status: 200, body: { accepted: true } };
const LEVELS = ["Away", "Low", "Medium", "High"];
const TEXT_CAP = 64;  // comfoair_http_entity_post's `text` buffer: the longest label plus its NUL

// comfoair_wire_parse_entity_write(): a string, a boolean or an unsigned integer, and nothing else.
function decodeValue(body: unknown): CommandValue | MockResponse {
  if (!isObject(body)) return badBody();
  if (!("value" in body)) return badField("value");
  const v = body.value;
  if (typeof v === "string") return bytes(v) > TEXT_CAP - 1 ? badField("value") : { kind: "text", text: v };
  if (typeof v === "boolean") return { kind: "bool", value: v };
  // null, a fraction, a negative: nothing writable takes them, and guessing is how a select
  // gets the wrong option.
  if (isU32(v)) return { kind: "number", value: v };
  return badField("value");
}

// Widened on purpose: the generated tuple types each list as a union of its own labels, and the
// question here is whether an arbitrary submitted string is one of them.
function optionsOf(key: string): readonly string[] {
  return ENTITIES.find((e) => e.key === key)?.options ?? [];
}

/** The queue's answer: null when the command may be applied. */
function queueGate(s: MockState): MockResponse | null {
  if (s.scenario.commands === "down") return error(503, HTTP.commandsDown);
  if (s.scenario.commands === "busy") return error(503, HTTP.busBusy);
  return null;
}

// --- effects on the unit -----------------------------------------------------------------

function setLevel(u: Unit, level: number): void {
  u.mode = LEVELS[level];
  u.boostLeftS = null;  // a level chosen by hand ends a running boost -- what the owner meant
}

function startBoost(u: Unit, seconds: number): void {
  if (seconds <= 0) {
    // 0 CANCELS boost: the one place 0 does not mean "forever" (comfoair_command_build.cpp
    // build_boost).
    if (u.mode === "Boost") u.mode = u.resumeMode;
    u.boostLeftS = null;
    return;
  }
  if (u.mode !== "Boost") u.resumeMode = u.mode;
  u.mode = "Boost";
  u.boostLeftS = seconds;
}

// The "Auto" preset releases boost, away and manual together (build_auto).
function setPreset(u: Unit, label: string): void {
  if (label === "Auto") { u.mode = "Auto"; u.resumeMode = "Auto"; u.boostLeftS = null; u.away = false; u.awayLeftS = null; return; }
  if (label === "Boost") { startBoost(u, u.durations.boost_duration * 60); return; }
  setLevel(u, LEVELS.indexOf(label));
}

/** 0 and below mean "until told otherwise" for bypass, balance and away: no countdown at all. */
const timeout = (seconds: number): number | null => (seconds > 0 ? seconds : null);

function setBypass(u: Unit, mode: string, seconds: number): void {
  u.bypassMode = mode;
  u.bypassLeftS = mode === "Auto" ? null : timeout(seconds);  // auto is a release: no timeout field
}
function setBalance(u: Unit, mode: string, seconds: number): void {
  u.balanceMode = mode;
  u.balanceLeftS = mode === "Balanced" ? null : timeout(seconds);
}
function setAway(u: Unit, on: boolean, seconds: number): void {
  u.away = on;
  u.awayLeftS = on ? timeout(seconds) : null;
}

// --- POST /api/entities/<key> ------------------------------------------------------------

export function writeEntity(s: MockState, key: string, rawLength: number, body: unknown): MockResponse {
  const gate = bodyGate(rawLength);
  if (gate) return gate;
  const value = decodeValue(body);
  if ("status" in value) return value;

  const meta = ENTITIES.find((e) => e.key === key);
  if (!meta || !meta.writable) return error(404, COMMAND.unknownEntity);
  const u = s.unit;
  const bad = error(422, COMMAND.badValue);

  // The four durations produce no frame: storing them is the whole of the work, so a full queue
  // does not refuse them -- but a sender that never started does (503, comfoair_http_entity_post).
  if (meta.kind === "number") {
    if (value.kind !== "number" || meta.min === null || meta.max === null) return bad;
    if (value.value < meta.min || value.value > meta.max) return bad;
    if (s.scenario.commands === "down") return error(503, HTTP.commandsDown);
    u.durations[key as keyof Unit["durations"]] = value.value;
    logLine(s, `control: ${key} = ${value.value} min`);
    return ACCEPTED;
  }

  let apply: () => void;
  if (meta.kind === "switch") {
    if (value.kind !== "bool") return bad;
    const on = value.value;
    apply = () => setAway(u, on, u.durations.away_duration * 60);
  } else if (meta.kind === "fan") {
    // Off is the AWAY LEVEL, not a stop: an HRV never stops ventilating. On resumes the level
    // the unit was last running at, Low if there was none.
    if (value.kind === "bool") {
      const on = value.value;
      apply = () => setLevel(u, on ? Math.min(3, Math.max(1, levelOf(u))) : 0);
    } else if (value.kind === "text") {
      if (!optionsOf(key).includes(value.text)) return bad;
      const label = value.text;
      apply = () => setPreset(u, label);
    } else {
      if (value.value > 3) return bad;
      const level = value.value;
      apply = () => setLevel(u, level);
    }
  } else {
    // Every select is written by its option LABEL, matched exactly -- an index would be a
    // number whose meaning depends on the firmware's build.
    if (value.kind !== "text" || !optionsOf(key).includes(value.text)) return bad;
    apply = selectEffect(u, key, value.text);
  }

  const queued = queueGate(s);
  if (queued) return queued;
  apply();
  logLine(s, `control: ${key} <- ${JSON.stringify(value.kind === "text" ? value.text : value.value)}`);
  return ACCEPTED;
}

function selectEffect(u: Unit, key: string, label: string): () => void {
  switch (key) {
    case "ventilation_mode": return () => setPreset(u, label);
    case "bypass_mode": return () => setBypass(u, label, u.durations.bypass_duration * 60);
    case "balance_mode": return () => setBalance(u, label, u.durations.balance_duration * 60);
    case "temperature_profile": return () => { u.profile = label; };
    case "temperature_passive": return () => { u.tempPassive = label; };
    case "humidity_comfort": return () => { u.humidityComfort = label; };
    case "humidity_protection": return () => { u.humidityProtection = label; };
    // A writable select the registry grew and this mock has not modelled: accepted, no effect --
    // which is also what an optimistic firmware write looks like before its PDO arrives.
    default: return () => {};
  }
}

// --- POST /api/ops/<name> ----------------------------------------------------------------

interface Params { mode?: string; profile?: string; duration?: number; level?: number; enabled?: boolean }

// comfoair_wire_parse_operation(): types only. ABSENT STAYS ABSENT -- none of the six operations
// has a default, and for three of them an invented duration would mean "forever".
function decodeParams(body: unknown): Params | MockResponse {
  if (!isObject(body)) return badBody();
  const p: Params = {};
  for (const k of ["mode", "profile"] as const) {
    if (!(k in body)) continue;
    const v = body[k];
    if (typeof v !== "string" || bytes(v) > 15) return badField(k);  // char mode[16] / profile[16]
    p[k] = v;
  }
  for (const k of ["duration", "level"] as const) {
    if (!(k in body)) continue;
    const v = body[k];
    if (!isI32(v)) return badField(k);
    p[k] = v;
  }
  if ("enabled" in body) {
    if (typeof body.enabled !== "boolean") return badField("enabled");
    p.enabled = body.enabled;
  }
  return p;
}

const BYPASS_OPS: Record<string, string> = { auto: "Auto", open: "Open", closed: "Closed" };
const BALANCE_OPS: Record<string, string> = { balanced: "Balanced", supply_only: "Supply Only", exhaust_only: "Extract Only" };
const PROFILE_OPS: Record<string, string> = { normal: "Normal", cool: "Cool", warm: "Warm" };

// comfoair_command_from_op(): which parameters each operation needs and what they may be.
function buildOp(u: Unit, name: string, p: Params): (() => void) | MockResponse {
  const missing = error(422, COMMAND.missingParam), bad = error(422, COMMAND.badValue);
  switch (name) {
    case "boost":
      if (p.duration === undefined) return missing;
      return () => startBoost(u, p.duration ?? 0);
    case "set_bypass": {
      if (p.mode === undefined || p.duration === undefined) return missing;
      const mode = BYPASS_OPS[p.mode];
      return mode ? () => setBypass(u, mode, p.duration ?? 0) : bad;
    }
    case "set_balance_mode": {
      if (p.mode === undefined || p.duration === undefined) return missing;
      const mode = BALANCE_OPS[p.mode];
      return mode ? () => setBalance(u, mode, p.duration ?? 0) : bad;
    }
    case "set_ventilation_level": {
      if (p.level === undefined) return missing;
      const level = p.level;
      return level >= 0 && level <= 3 ? () => setLevel(u, level) : bad;
    }
    case "set_temperature_profile": {
      if (p.profile === undefined) return missing;
      const profile = PROFILE_OPS[p.profile];  // no "auto": never seen on the wire, not guessed
      return profile ? () => { u.profile = profile; } : bad;
    }
    case "set_away":
      if (p.enabled === undefined || p.duration === undefined) return missing;
      return () => setAway(u, p.enabled ?? false, p.duration ?? 0);
    default:
      return error(404, COMMAND.unknownOp);
  }
}

export function runOperation(s: MockState, name: string, rawLength: number, body: unknown): MockResponse {
  const gate = bodyGate(rawLength);
  if (gate) return gate;
  const params = decodeParams(body);
  if ("status" in params) return params;
  const built = buildOp(s.unit, name, params);
  if (typeof built !== "function") return built;
  const queued = queueGate(s);
  if (queued) return queued;
  built();
  logLine(s, `control: op ${name} ${JSON.stringify(params)}`);
  return ACCEPTED;
}
