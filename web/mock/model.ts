// web/mock/model.ts
// The simulated ComfoAir Q and its device-side state, as one plain, structured-cloneable object.
//
// Everything the mock serves is a projection of one MockState: the unit (air, fans, bypass,
// timers), the stored configuration, the network/provisioning state and the dev-only scenario
// knobs. Plain data on purpose -- evolve.ts clones and advances it, the router mutates it, and a
// test can build one, poke a field and assert, with no server and no clock.
//
// NOT a firmware contract. The contract is what the projections render (values.ts, config.ts,
// network.ts); the field names here are the mock's own and may change freely.

import type { ProbeRun } from "./probe.ts";

export type Season = "heating" | "cooling";
/** "none" = the CAN bus has delivered no PDO yet: every PDO-carried entity is null. */
export type Readings = "live" | "none";
/** "busy" = the command queue is full (503 "the bus is busy"); "down" = the sender never started
    (503 "commands are not running", as on a boot where the CAN bus did not come up). */
export type Commands = "ok" | "busy" | "down";
/** POST /api/ota: "ok" installs and comes back running the new version; "fail" refuses the image
    after upload (422, verification failed); "rollback" installs, but the device comes back running
    the OLD version -- the bootloader's rollback after a new image that never confirmed itself. */
export type Ota = "ok" | "fail" | "rollback";

/** Dev-only knobs (scenarios.ts). None of them is a firmware field. */
export interface Scenario {
  preset: string;
  provisioned: boolean;
  season: Season;
  readings: Readings;
  commands: Commands;
  /** true = the device cannot be reached at all: the plugin drops every connection. */
  offline: boolean;
  /** The supply fan is held at zero (frost protection on a unit without a pre-heater). */
  supplyStopped: boolean;
  filterDays: number;
  scan: "ok" | "unavailable";
  ota: Ota;
  /** The stored settings were written by a newer firmware: POST /api/config answers 409. */
  configReadOnly: boolean;
  /** "silent" = the unit ignores this device's RMI reads at another_counter 0 (failed/no_answer). */
  probe: "ok" | "silent";
  /** GET /api/system's unit block: "unread" = never answered, "no_answer" = the last read went
      silent (older values kept), "errors" = two active errors, one with no text. */
  unitInfo: "ok" | "unread" | "no_answer" | "errors";
}

/** The ventilation unit itself. Temperatures in degrees C, flows in m3/h, timers in seconds. */
export interface Unit {
  outdoorBase: number; indoorBase: number; outdoorRhBase: number; indoorRhBase: number;
  outdoorC: number; outdoorRh: number;
  extractC: number; extractRh: number;
  supplyC: number; exhaustC: number;
  runningMeanC: number;
  supplyFlow: number; exhaustFlow: number;
  bypassPos: number;          // 0 closed .. 100 open
  preheaterW: number;
  // commanded state -- option labels exactly as the registry spells them
  mode: string;               // ventilation_mode: Auto | Away | Low | Medium | High | Boost
  resumeMode: string;         // what Boost returns to when its timer runs out
  boostLeftS: number | null;
  bypassMode: string; bypassLeftS: number | null;
  balanceMode: string; balanceLeftS: number | null;
  away: boolean; awayLeftS: number | null;   // null while away = indefinite
  profile: string;
  tempPassive: string; humidityComfort: string; humidityProtection: string;
  durations: { boost_duration: number; bypass_duration: number; balance_duration: number; away_duration: number };
  // counters, kWh, integrated from the instantaneous powers
  energyYtd: number; energyTotal: number; preheatYtd: number; preheatTotal: number;
  avoidedHeatYtd: number; avoidedHeatTotal: number; avoidedCoolYtd: number; avoidedCoolTotal: number;
  avoidedHeatW: number; avoidedCoolW: number;
  simS: number;               // simulated seconds, drives the slow sinusoids
}

/** The stored document behind GET/POST /api/config. Secrets are held in the clear HERE ONLY:
    no projection returns them (config.ts redacts every one). */
export interface MockConfig {
  wifi_ssid: string; wifi_psk: string;
  mqtt_host: string; mqtt_port: number; mqtt_user: string; mqtt_password: string;
  topic_prefix: string; ha_discovery: boolean; device_name: string; ui_password: string;
  permanent_manual: boolean;
}

/** comfoair_wire_provision_t, reduced to what the mock moves. */
export interface Net {
  state: "unconfigured" | "connecting" | "connected" | "trial" | "retrying" | "fallback-ap" | "window-closed";
  knownGood: boolean;
  connectingLeftMs: number;   // >0 while a submitted network is being joined
  windowLeftMs: number;       // the setup access point's window
}

export interface MockState {
  scenario: Scenario;
  unit: Unit;
  config: MockConfig;
  net: Net;
  uptimeMs: number;
  mqttPublished: number;
  log: string[];
  /** The last RMI probe started (probe.ts); null until one is. RAM-only on the device: a reboot
      forgets it. */
  probe: ProbeRun | null;
  /** esp_app_desc_t's version, as GET /api/status's sw_version and /api/system's firmware. */
  swVersion: string;
  /** The running app slot. */
  partition: "ota_0" | "ota_1";
  /** > 0 while the device "reboots" after an accepted image: every connection is dropped. */
  rebootLeftMs: number;
  /** What it boots into when rebootLeftMs reaches 0 (ota.ts decides; evolve.ts applies). */
  bootInto: { swVersion: string; partition: "ota_0" | "ota_1"; probation: boolean } | null;
  /** > 0 while a freshly installed image is on probation (not yet confirmed): another upload
      answers 409, as comfoair_ota's UNCONFIRMED does. */
  probationLeftMs: number;
}

export interface MockRequest {
  method: string;                   // upper-case
  path: string;                     // pathname only, no query
  headers: Record<string, string>;  // lower-cased keys
  /** Parsed JSON, UNPARSABLE when the bytes were not JSON, undefined when there were none. */
  body: unknown;
  rawLength: number;                // bytes received; the firmware's 413 rule reads this
}
export interface MockResponse {
  status: number;
  headers?: Record<string, string>;
  body?: unknown;
  /** Transport instruction: drop the connection without answering (the "offline" scenario). */
  drop?: boolean;
  /** Transport instruction: the device "reboots" -- every push socket is closed. */
  restart?: boolean;
}

/** A body that arrived and was not JSON. A unique object rather than null, because `null` IS
    valid JSON and the firmware tells "not a JSON object" apart from "absent" (413 vs 400). */
export const UNPARSABLE: unique symbol = Symbol("unparsable");

export const LOG_LINES = 80;        // COMFOAIR_LOG_LINES
export const SETUP_WINDOW_MS = 15 * 60 * 1000;

export function defaultScenario(): Scenario {
  return {
    preset: "heating", provisioned: true, season: "heating", readings: "live", commands: "ok",
    offline: false, supplyStopped: false, filterDays: 112, scan: "ok", ota: "ok", configReadOnly: false,
    probe: "ok", unitInfo: "ok",
  };
}

/** A unit at rest for `season`, before evolve.ts has settled it. */
export function initialUnit(season: Season): Unit {
  const winter = season === "heating";
  const outdoorBase = winter ? 2 : 19, indoorBase = winter ? 21.5 : 24.5;
  return {
    outdoorBase, indoorBase, outdoorRhBase: winter ? 82 : 60, indoorRhBase: winter ? 42 : 55,
    outdoorC: outdoorBase, outdoorRh: winter ? 82 : 60, extractC: indoorBase, extractRh: winter ? 42 : 55,
    supplyC: indoorBase - 2, exhaustC: outdoorBase + 2, runningMeanC: winter ? 4 : 18,
    supplyFlow: 110, exhaustFlow: 110, bypassPos: 0, preheaterW: 0,
    mode: "Auto", resumeMode: "Auto", boostLeftS: null,
    bypassMode: "Auto", bypassLeftS: null, balanceMode: "Balanced", balanceLeftS: null,
    away: false, awayLeftS: null, profile: "Normal",
    // The registry's seeded defaults (pdo_table.py default_index), so a fresh mock reads like a
    // fresh device: Temperature Passive Off, Humidity Comfort Off, Humidity Protection Auto only.
    tempPassive: "Off", humidityComfort: "Off", humidityProtection: "Auto only",
    durations: { boost_duration: 60, bypass_duration: 60, balance_duration: 60, away_duration: 0 },
    energyYtd: 96, energyTotal: 1873, preheatYtd: 41, preheatTotal: 512,
    avoidedHeatYtd: 1290, avoidedHeatTotal: 15411, avoidedCoolYtd: 84, avoidedCoolTotal: 966,
    avoidedHeatW: 0, avoidedCoolW: 0, simS: 0,
  };
}

export function initialConfig(): MockConfig {
  return {
    wifi_ssid: "home-wifi", wifi_psk: "correct-horse",
    mqtt_host: "192.168.1.10", mqtt_port: 1883, mqtt_user: "ha", mqtt_password: "broker-secret",
    topic_prefix: "comfoair", ha_discovery: true, device_name: "ComfoAir Q", ui_password: "",
    permanent_manual: true,
  };
}

export function initialState(): MockState {
  return {
    scenario: defaultScenario(),
    unit: initialUnit("heating"),
    config: initialConfig(),
    net: { state: "connected", knownGood: true, connectingLeftMs: 0, windowLeftMs: 0 },
    uptimeMs: 0,
    mqttPublished: 0,
    probe: null,
    swVersion: "0.1.0",
    partition: "ota_0",
    rebootLeftMs: 0,
    bootInto: null,
    probationLeftMs: 0,
    log: ["I (312) main: mock ComfoAir Q up", "I (845) can: bus started, polling 67 PDOs"],
  };
}

/** Appends to the log ring, dropping the oldest line past COMFOAIR_LOG_LINES like the device. */
export function logLine(s: MockState, line: string): void {
  s.log.push(`I (${s.uptimeMs}) ${line}`);
  if (s.log.length > LOG_LINES) s.log.splice(0, s.log.length - LOG_LINES);
}
