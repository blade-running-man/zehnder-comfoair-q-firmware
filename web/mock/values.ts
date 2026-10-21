// web/mock/values.ts
// The simulated unit projected into entity values -- the body of GET /api/state and of every /ws
// frame -- keyed by the GENERATED registry (src/api/entities.ts), never by a list of its own.
//
// Two rules carried over from the firmware's comfoair_api/comfoair_state, because the SPA is
// built on them:
//   * every registry key is present, and a key with no reading is `null`, never omitted -- "no
//     such entity" and "no reading yet" are different answers (comfoair_api.cpp, render_object);
//   * with no PDO heard yet, only what the device seeds itself has a value: the write-only
//     selects and numbers (pdo === null) and the schema version. That is the "waiting" screen.
import { ENTITIES, SCHEMA_VERSION } from "../src/api/entities.ts";
import type { MockState, Unit } from "./model.ts";
import { fanDuty, fanPowerW, fanRpm, r1, rhAt } from "./air.ts";
import { COOLING_LIMIT_C, HEATING_LIMIT_C, levelOf, preheatedC, profileTarget } from "./evolve.ts";
import { MOCK_ERROR_SUMMARY, unitNeverRead } from "./system.ts";

/** The fan entity's value: comfoair_fan_state_t as comfoair_api renders it. */
export interface FanValue { on: boolean; speed: number; preset: string | null }
export type Value = number | boolean | string | null | FanValue;

const FAN_LEVELS = ["Away", "Low", "Medium", "High"];

// Write-only entities no PDO carries but that are derived FROM PDOs (49/65, 54/55): with no PDO
// heard they have nothing to show, even though their registry row has `pdo: null` like a seeded one.
const DERIVED_FROM_PDOS = new Set(["ventilation_mode", "balance_mode"]);

/** True for an entity that has a value on a device that has heard nothing from the unit. */
export function seeded(key: string): boolean {
  const e = ENTITIES.find((x) => x.key === key);
  if (!e || e.kind === "fan") return false;
  return e.pdo === null && !DERIVED_FROM_PDOS.has(key);
}

function operatingMode(u: Unit, permanentManual: boolean): string {
  if (u.away) return "Away";
  if (u.mode === "Auto") return "Auto";
  if (u.mode === "Boost") return "Boost";
  return permanentManual ? "Manual (permanent)" : "Manual";
}

function constraints(s: MockState, level: number): string {
  if (s.scenario.supplyStopped) return "Below: Frost Protection";
  if (s.unit.mode === "Auto" && level === 2) {
    return s.unit.humidityComfort !== "Off" ? "Above: Humidity Comfort" : "Above: Humidity Protection";
  }
  return "No restrictions";
}

/** Every entity the model drives, by key. Keys here MUST exist in the registry (tests check). */
function driven(s: MockState): Record<string, Value> {
  const u = s.unit;
  const level = levelOf(u);
  const inC = preheatedC(u);
  const supplyOnly = u.balanceMode === "Supply Only", extractOnly = u.balanceMode === "Extract Only";
  const fanTimer = u.mode === "Boost" ? u.boostLeftS : u.away ? u.awayLeftS : null;
  const secs = (v: number | null): number | null => (v === null ? null : Math.ceil(v));
  const kwh = (v: number): number => Math.floor(v);
  return {
    next_fan_change_in_raw: secs(fanTimer),
    next_bypass_change_in_raw: secs(u.bypassLeftS),
    next_supply_change_in_raw: supplyOnly ? secs(u.balanceLeftS) : null,
    next_exhaust_change_in_raw: extractOnly ? secs(u.balanceLeftS) : null,
    exhaust_fan_duty: fanDuty(u.exhaustFlow),
    supply_fan_duty: fanDuty(u.supplyFlow),
    exhaust_fan_flow: Math.round(u.exhaustFlow),
    supply_fan_flow: Math.round(u.supplyFlow),
    exhaust_fan_speed: fanRpm(u.exhaustFlow),
    supply_fan_speed: fanRpm(u.supplyFlow),
    power: Math.round(fanPowerW(u.supplyFlow, u.exhaustFlow)),
    energy_ytd: kwh(u.energyYtd),
    energy_total: kwh(u.energyTotal),
    preheater_energy_ytd: kwh(u.preheatYtd),
    preheater_energy_total: kwh(u.preheatTotal),
    preheater_power: u.preheaterW,
    // The mock's pre-heater runs only to hold the frost floor, so all of its draw is frost duty.
    frost_protection_preheater_power: u.preheaterW,
    filter_replacement_remaining_days: s.scenario.filterDays,
    running_mean_outdoor_temp: r1(u.runningMeanC),
    profile_target_temp: profileTarget(u.profile),
    avoided_heating: u.avoidedHeatW,
    avoided_heating_ytd: kwh(u.avoidedHeatYtd),
    avoided_heating_total: kwh(u.avoidedHeatTotal),
    avoided_cooling: u.avoidedCoolW,
    avoided_cooling_ytd: kwh(u.avoidedCoolYtd),
    avoided_cooling_total: kwh(u.avoidedCoolTotal),
    intake_air_temp_unverified: r1(u.outdoorC + 0.4),
    supply_air_temp_post_heater: r1(u.supplyC),
    bypass_position: u.bypassPos,
    fan_speed_modulated: fanDuty(u.supplyFlow),
    frost_protection_unbalance: s.scenario.supplyStopped ? 100 : 0,
    extract_air_temp: r1(u.extractC),
    exhaust_air_temp: r1(u.exhaustC),
    outdoor_air_temp: r1(u.outdoorC),
    preheated_outdoor_air_temp: r1(inC),
    supply_air_temp: r1(u.supplyC),
    extract_air_humidity: u.extractRh,
    exhaust_air_humidity: Math.round(rhAt(u.extractRh, u.extractC, u.exhaustC)),
    outdoor_air_humidity: u.outdoorRh,
    preheated_outdoor_air_humidity: Math.round(rhAt(u.outdoorRh, u.outdoorC, inC)),
    supply_air_humidity: Math.round(rhAt(u.outdoorRh, u.outdoorC, u.supplyC)),
    manual_mode: !u.away && FAN_LEVELS.includes(u.mode) && s.config.permanent_manual,
    supply_only_active: supplyOnly,
    exhaust_only_active: extractOnly,
    heating_season: u.runningMeanC < HEATING_LIMIT_C,
    cooling_season: u.runningMeanC > COOLING_LIMIT_C,
    // PDO 337 bits 6 and 11; bit 11 is the same bit away_mode reads.
    boost_timer_running: u.mode === "Boost",
    away_timer_running: u.away,
    // PDO 16 = 7 follows the fan's Away preset (level 0), not away mode (Q350 census).
    device_state: level === 0 ? "Away (preset)" : "Normal",
    changing_filters: "Normal",
    operating_mode: operatingMode(u, s.config.permanent_manual),
    supply_fan_mode: supplyOnly ? "Supply Only" : "Balanced",
    exhaust_fan_mode: extractOnly ? "Extract Only" : "Balanced",
    fan_level: FAN_LEVELS[Math.min(level, 3)],
    // Raw: the Q350 census saw 0 at the Away preset, 1 at Low; higher levels are unobserved.
    sensor_ventilation_mode: level === 0 ? 0 : 1,
    temperature_unit: "Celsius",
    airflow_unit: "m³/h",
    bypass_override: u.bypassMode !== "Auto",
    airflow_constraints: constraints(s, level),
    // The unit's active errors: GET /api/system's `errors` knob (system.ts), as the probe task
    // feeds comfoair_state_set_unit_errors() -- null until the unit has been read, by the same
    // predicate as the system document (before the first read, on a silent bus, or never).
    unit_error: unitNeverRead(s) ? null : s.scenario.unitInfo === "errors",
    active_errors: unitNeverRead(s) ? null
      : s.scenario.unitInfo === "errors" ? MOCK_ERROR_SUMMARY : "No errors",
    bypass_mode: u.bypassMode,
    temperature_profile: u.profile,
    balance_mode: u.balanceMode,
    temperature_passive: u.tempPassive,
    humidity_comfort: u.humidityComfort,
    humidity_protection: u.humidityProtection,
    ventilation_mode: u.away ? "Away" : u.mode,
    boost_duration: u.durations.boost_duration,
    bypass_duration: u.durations.bypass_duration,
    balance_duration: u.durations.balance_duration,
    away_duration: u.durations.away_duration,
    away_mode: u.away,
    // recompute_fan(): `on` is level > 0, `speed` never drops below 1, preset follows the mode.
    fan: { on: level > 0, speed: level > 0 ? Math.min(level, 3) : 1, preset: u.away ? "Away" : u.mode },
    schema_version: SCHEMA_VERSION,
    // comfoair_state seeds the freshness entity TRUE at boot and never changes it: its meaning is
    // the arrival of the message (Home Assistant's expire_after), so "false" is never rendered.
    data_freshness: true,
  };
}

/** The whole state object, every registry key in registry order. */
export function stateValues(s: MockState): Record<string, Value> {
  const map = driven(s);
  const live = s.scenario.readings === "live";
  const out: Record<string, Value> = {};
  for (const e of ENTITIES) {
    const v = e.key in map ? map[e.key] : null;
    out[e.key] = live || seeded(e.key) ? v : null;
  }
  return out;
}

/** The keys driven() covers, for the test that pins it to the registry. */
export function drivenKeys(s: MockState): string[] {
  return Object.keys(driven(s));
}
