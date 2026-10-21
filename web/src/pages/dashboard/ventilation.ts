// The home dashboard's view over the device — read model, formatters, write helpers.
//
// This is NOT a second entity list (AGENTS.md). It names the specific keys the bespoke
// dashboard draws, reading them from the SAME generated registry every other screen uses
// (api/entities.ts) and writing them through the SAME REST path (device.setEntity →
// client.writeEntity). A key the firmware stops publishing simply reads as absent here and
// the affected instrument shows "—"; nothing about what entities EXIST is duplicated.
//
// Writes:
//   · Ventilation preset  → the `ventilation_mode` SELECT. It exists precisely so a web UI
//     can set a fan preset — the `fan` entity's write shape is not its read shape
//     (docs/interfaces/rest-api.md) and cannot be driven from a plain select here.
//   · Manual durations    → the four `*_duration` NUMBERS: stored values that feed the
//     presets / selects / switch. They are explicitly NOT the `duration:` parameter of the six
//     services, so setting one here is not the mode/duration split device.ts warns against —
//     that warning is about the runOperation services, which this UI does not use.
//   · Bypass / Away       → the `bypass_mode` SELECT and the `away_mode` SWITCH.

import { deviceState, setEntity } from "../../api/device";
import { ENTITIES, FAN_PRESET_MODES } from "../../api/entities";
import type { StateValue } from "../../api/ws";
import { toast } from "../../components/Toast";
import { locale, t, tp, type MsgKey } from "../../i18n";
import { translateDetail } from "../../i18n/detail";

/** The nominal filter cycle in days. The unit reports days REMAINING (PDO 192), not the cycle
    length, so the life bar needs a whole to draw the part against. 180 is the ComfoAir Q's
    indicative interval (the display labels it "DAYS LEFT — INDICATIVE"). */
export const FILTER_CYCLE_DAYS = 180;

// ── typed readers over the raw state map ────────────────────────────────────────────────
function raw(key: string): StateValue | undefined {
  return deviceState.value[key];
}
function num(key: string): number | null {
  const v = raw(key);
  return typeof v === "number" ? v : null;
}
function bool(key: string): boolean {
  return raw(key) === true;
}
function str(key: string): string | null {
  const v = raw(key);
  return typeof v === "string" ? v : null;
}
/** The fan reads as {on, speed, preset}; its preset is the fallback when the `ventilation_mode`
    select has not reported a value yet. */
function fanPreset(): string | null {
  const v = raw("fan");
  if (v && typeof v === "object" && "preset" in v) {
    const p = (v as { preset: unknown }).preset;
    return typeof p === "string" ? p : null;
  }
  return null;
}

/** The whole read model, computed once per render from the live state. Every field is a plain
    value or null so a component never touches the raw map or the entity keys. */
export interface View {
  outdoor: number | null;
  supply: number | null;
  extract: number | null;
  exhaust: number | null;
  // Relative humidity (%) at the same four points.
  outdoorRh: number | null;
  supplyRh: number | null;
  extractRh: number | null;
  exhaustRh: number | null;
  flow: number | null;
  exhaustFlow: number | null;
  power: number | null;
  bypassPos: number | null;
  bypassMode: string | null;
  bypassOpen: boolean;
  heating: boolean;
  cooling: boolean;
  filterDays: number | null;
  mode: string | null;
  supplyStopped: boolean;
  /** 0..1, or null when there is nothing meaningful to recover (bypassed, stopped, or in and
      out too close together). */
  recovery: number | null;
  boostDuration: number | null;
  bypassDuration: number | null;
  balanceDuration: number | null;
  awayDuration: number | null;
  awayMode: boolean;
  // Comfort & temperature. Season is read-only (the unit derives it from the running mean);
  // the profile and the three comfort selects are writable, the target temp is derived.
  meanOutdoor: number | null;
  tempProfile: string | null;
  targetTemp: number | null;
  tempPassive: string | null;
  humidityComfort: string | null;
  humidityProtection: string | null;
  nextFan: number | null;
  nextBypass: number | null;
  nextSupply: number | null;
}

export function readView(): View {
  const supply = num("supply_air_temp");
  const outdoor = num("outdoor_air_temp");
  const extract = num("extract_air_temp");
  const flow = num("supply_fan_flow");
  const bypassPos = num("bypass_position");
  const bypassOpen = bypassPos !== null && bypassPos > 50;
  // No dedicated "supply fan stopped" entity: a zero supply flow is the observable form of it.
  const supplyStopped = flow !== null && flow <= 0;

  let recovery: number | null = null;
  if (
    !supplyStopped && !bypassOpen &&
    supply !== null && outdoor !== null && extract !== null &&
    // Enough spread that the ratio is meaningful. 1.5 °C, not 4: in cooling season indoor and
    // outdoor sit close, and a 4 °C floor blanked the recovery figure during normal summer runs.
    Math.abs(extract - outdoor) >= 1.5
  ) {
    recovery = Math.max(0, Math.min(1, (supply - outdoor) / (extract - outdoor)));
  }

  return {
    outdoor, supply, extract, exhaust: num("exhaust_air_temp"),
    outdoorRh: num("outdoor_air_humidity"),
    supplyRh: num("supply_air_humidity"),
    extractRh: num("extract_air_humidity"),
    exhaustRh: num("exhaust_air_humidity"),
    flow, exhaustFlow: num("exhaust_fan_flow"), power: num("power"),
    bypassPos, bypassMode: str("bypass_mode"), bypassOpen,
    heating: bool("heating_season"), cooling: bool("cooling_season"),
    filterDays: num("filter_replacement_remaining_days"),
    mode: str("ventilation_mode") ?? fanPreset(),
    supplyStopped, recovery,
    boostDuration: num("boost_duration"),
    bypassDuration: num("bypass_duration"),
    balanceDuration: num("balance_duration"),
    awayDuration: num("away_duration"),
    awayMode: bool("away_mode"),
    meanOutdoor: num("running_mean_outdoor_temp"),
    tempProfile: str("temperature_profile"),
    targetTemp: num("profile_target_temp"),
    tempPassive: str("temperature_passive"),
    humidityComfort: str("humidity_comfort"),
    humidityProtection: str("humidity_protection"),
    nextFan: num("next_fan_change_in_raw"),
    nextBypass: num("next_bypass_change_in_raw"),
    nextSupply: num("next_supply_change_in_raw"),
  };
}

// ── formatters ──────────────────────────────────────────────────────────────────────────
export const one = (v: number): string => v.toFixed(1);

/** Temperature → one of the four semantic air tokens. Four steps, not a ramp, because a
    four-step scale is still a scale for a colour-blind eye once the hues collapse. */
export function band(t: number): string {
  if (t < 2) return "var(--c-cold)";
  if (t < 12) return "var(--c-cool)";
  if (t < 21) return "var(--c-mild)";
  return "var(--c-warm)";
}

/** A set duration, in the largest unit that stays exact. 90 → "1 h 30 m", 1440 → "1 day". */
export function fmtDur(min: number): string {
  if (min === 0) return t("dashboard.dur.indefinite");
  if (min < 60) return t("dashboard.dur.min", { n: min });
  if (min % 1440 === 0) return tp("dashboard.dur.days", min / 1440);
  if (min % 60 === 0) return t("dashboard.dur.h", { h: min / 60 });
  return t("dashboard.dur.hm", { h: Math.floor(min / 60), m: min % 60 });
}

/** A live countdown. Coarse above an hour, exact below it, seconds in the last minute. */
export function fmtLeft(sec: number): string {
  if (sec >= 86400) {
    const d = Math.floor(sec / 86400), h = Math.floor((sec % 86400) / 3600);
    return t("dashboard.dur.dh", { d, h });
  }
  if (sec >= 3600) {
    const h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60);
    return t("dashboard.dur.hm", { h, m: String(m).padStart(2, "0") });
  }
  if (sec >= 60) return t("dashboard.dur.min", { n: Math.floor(sec / 60) });
  return t("dashboard.dur.s", { n: Math.max(0, Math.round(sec)) });
}

/** Today plus n days, the way a person reads a date rather than an ISO string -- in the UI
    language, not the browser's: a German page with an English month name reads as a bug. */
export function dateIn(days: number): string {
  const d = new Date(Date.now() + days * 86400000);
  return d.toLocaleDateString(locale.value, { day: "numeric", month: "short", year: "numeric" });
}

// ── ventilation mode metadata ───────────────────────────────────────────────────────────
// The preset buttons ARE the generated list, imported rather than re-typed: FAN_PRESET_MODES
// is the contract-visible order (api/entities.ts), so a rename there moves these buttons too
// instead of leaving one that writes an option the firmware rejects.
export const PRESETS = FAN_PRESET_MODES;

/** A select's options, read from the generated registry so a control's choices never drift from
    what the firmware will accept. Empty array if the key is gone — the control then renders no
    buttons rather than offering an option the device would reject. */
function optionsOf(key: string): readonly string[] {
  return ENTITIES.find((e) => e.key === key)?.options ?? [];
}
export const BYPASS_MODES = optionsOf("bypass_mode");

// ── comfort & temperature ───────────────────────────────────────────────────────────────
// The ComfoAir Q is not a thermostat: you cannot set a room temperature. What
// IS writable is the temperature PROFILE and three comfort selects; the target temp and the
// heating/cooling season are derived and read-only. Options come from the generated registry.
export const TEMP_PROFILE_MODES = optionsOf("temperature_profile"); // protocol order (Normal/Cool/Warm)
// Buttons read cool → warm, which a person parses faster than protocol order; each button still
// writes its exact option string, so reordering changes nothing the firmware receives.
export const TEMP_PROFILE_DISPLAY = ["Cool", "Normal", "Warm"].filter((m) => TEMP_PROFILE_MODES.includes(m));

// label/help are catalog keys, resolved by the card at render (a string frozen here at import
// would never follow a language switch).
export interface ComfortSpec {
  key: string;
  label: MsgKey;
  help: MsgKey;
  options: readonly string[];
  value(v: View): string | null;
}
export const COMFORT: ComfortSpec[] = [
  {
    key: "temperature_passive", label: "dashboard.comfort.passive.label", options: optionsOf("temperature_passive"),
    help: "dashboard.comfort.passive.help",
    value: (v) => v.tempPassive,
  },
  {
    key: "humidity_comfort", label: "dashboard.comfort.humidity.label", options: optionsOf("humidity_comfort"),
    help: "dashboard.comfort.humidity.help",
    value: (v) => v.humidityComfort,
  },
  {
    key: "humidity_protection", label: "dashboard.comfort.protection.label", options: optionsOf("humidity_protection"),
    help: "dashboard.comfort.protection.help",
    value: (v) => v.humidityProtection,
  },
];

/** Write any writable select (temperature profile, comfort settings) — plain value over REST. */
export function setSelect(key: string, value: string): Promise<void> {
  return setEntity(key, value);
}

/** A one-line answer to "when would I press this?" — the mode control is the primary act, so
    it names what each choice is FOR, not just its label. Keyed by the raw option string; empty
    for a preset a newer firmware adds that has no hint yet. */
const MODE_HELP: Record<string, MsgKey> = {
  Auto: "dashboard.mode.help.Auto",
  Away: "dashboard.mode.help.Away",
  Low: "dashboard.mode.help.Low",
  Medium: "dashboard.mode.help.Medium",
  High: "dashboard.mode.help.High",
  Boost: "dashboard.mode.help.Boost",
};
export function modeHelp(mode: string): string {
  const key = MODE_HELP[mode];
  return key ? t(key) : "";
}

// ── timer rows ──────────────────────────────────────────────────────────────────────────
// Each row pairs one writable `number` (the stored duration that feeds a preset/select/switch)
// with the read-only countdown sensor that same mode produces. `running(v)` decides which row
// currently shows a live ring, from state the device already publishes.
// The prose fields are catalog keys, resolved at render. `nameEntity` is the entity whose
// localized display name titles the row (i18n/entityName.ts) -- the same name the diagnostics
// screen and Home Assistant show for it.
export interface TimerSpec {
  id: string;
  nameEntity: string;
  name: string;
  durationKey: string;
  countdownKey: string;
  presets: number[];
  min: number;
  max: number;
  runningLabel: MsgKey;
  what: MsgKey;
  idle: MsgKey;
  indefinite?: boolean;
  duration(v: View): number | null;
  countdown(v: View): number | null;
  running(v: View): boolean;
  /** Present when this mode has its own on/off switch on the home screen (Away). Absent when
      the mode is started from its natural home elsewhere (Boost from the hero preset, Bypass
      from the bypass card), so the row only sets the length. */
  isOn?(v: View): boolean;
  toggle?(on: boolean): Promise<void>;
}

export const TIMERS: TimerSpec[] = [
  {
    id: "boost", nameEntity: "boost_duration", name: "Boost Duration", durationKey: "boost_duration", countdownKey: "next_fan_change_in_raw",
    presets: [15, 30, 60, 120, 240], min: 1, max: 1440, runningLabel: "dashboard.timers.boost.running",
    what: "dashboard.timers.boost.what", idle: "dashboard.timers.boost.idle",
    duration: (v) => v.boostDuration, countdown: (v) => v.nextFan,
    running: (v) => v.mode === "Boost",
  },
  {
    id: "bypass", nameEntity: "bypass_duration", name: "Bypass Override Duration", durationKey: "bypass_duration", countdownKey: "next_bypass_change_in_raw",
    presets: [30, 60, 120, 240, 480], min: 1, max: 1440, runningLabel: "dashboard.timers.bypass.running",
    what: "dashboard.timers.bypass.what", idle: "dashboard.timers.bypass.idle",
    duration: (v) => v.bypassDuration, countdown: (v) => v.nextBypass,
    running: (v) => v.bypassMode === "Open" || v.bypassMode === "Closed",
  },
  {
    id: "away", nameEntity: "away_mode", name: "Away Mode", durationKey: "away_duration", countdownKey: "next_fan_change_in_raw",
    presets: [120, 480, 1440, 4320, 10080], min: 0, max: 43200, runningLabel: "dashboard.timers.away.running", indefinite: true,
    what: "dashboard.timers.away.what", idle: "dashboard.timers.away.idle",
    duration: (v) => v.awayDuration, countdown: (v) => (v.awayMode ? v.nextFan : null),
    running: (v) => v.awayMode,
    isOn: (v) => v.awayMode,
    toggle: (on) => setAwayMode(on),
  },
];

// ── writes (all plain writable entities over REST) ─────────────────────────────────────────
export function setMode(mode: string): Promise<void> {
  return setEntity("ventilation_mode", mode);
}
export function setBypassMode(mode: string): Promise<void> {
  return setEntity("bypass_mode", mode);
}
export function setAwayMode(on: boolean): Promise<void> {
  return setEntity("away_mode", on);
}
export function setDuration(key: string, minutes: number): Promise<void> {
  return setEntity(key, minutes);
}

/**
 * Fire a write and speak the outcome. A write answers 501 until the firmware wires the RMI
 * command behind it (client.writeEntity), and a control that silently does nothing is the
 * worse failure — so a rejection becomes a toast rather than a swallowed promise. The optimistic
 * UI needs no success toast: the device's next push moves the control to its new state.
 */
export function commit(p: Promise<void>, ok?: string): void {
  p.then(() => { if (ok) toast(ok); }).catch((e: unknown) => {
    const msg = e instanceof Error ? e.message : String(e);
    toast(t("dashboard.toast.notApplied", { msg: translateDetail(msg) }));
  });
}
