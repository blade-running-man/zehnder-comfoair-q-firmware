// web/mock/evolve.ts
// The dynamic model: one tick advances the simulated ComfoAir Q (and the device around it) by dt.
//
// Time constants are ACCELERATED -- the fans settle in seconds, the outdoor air swings through a
// full cycle in ten minutes -- so a developer sees the dashboard move while looking at it. The
// numbers are a dev aid, not a thermal model of a house.
import { logLine, type MockState, type Scenario, type Unit } from "./model.ts";
import { stepOta } from "./ota.ts";
import {
  LEVEL_FLOW, airPowerW, approach, clamp, efficiency, fanPowerW, r1,
} from "./air.ts";

/** The pre-heater holds incoming air at this floor so the core cannot ice. */
export const PREHEAT_FLOOR_C = -4;
/** Running-mean outdoor temperature limits for the unit's two seasons (factory defaults). */
export const HEATING_LIMIT_C = 12;
export const COOLING_LIMIT_C = 16;
const OUTDOOR_SWING_S = 600;      // one full outdoor swing, simulated seconds
const ROOM_HUMIDITY_SWING_S = 900;
export const PROVISION_CONNECT_MS = 4000;  // how long "joining the network" takes after POST /api/provision

/** Profile comfort targets, degrees C. The unit derives `profile_target_temp` from the profile. */
export function profileTarget(profile: string): number {
  return profile === "Cool" ? 20 : profile === "Warm" ? 23 : 21.5;
}

/** Ventilation level 0..4 (Away, Low, Medium, High, Boost) the unit is running at right now. */
export function levelOf(u: Unit): number {
  if (u.away) return 0;  // away MODE pins the Away level whatever the preset says
  switch (u.mode) {
    case "Away": return 0;
    case "Low": return 1;
    case "Medium": return 2;
    case "High": return 3;
    case "Boost": return 4;
    default:
      // Auto: the unit's own demand control. Humid extract air raises it a step when either
      // humidity function is allowed to act -- which is why the comfort selects matter here.
      return u.extractRh >= 58 && (u.humidityComfort !== "Off" || u.humidityProtection !== "Off") ? 2 : 1;
  }
}

/** Air entering the core: the outdoor air, or the pre-heater's floor while it runs. */
export function preheatedC(u: Unit): number {
  return u.preheaterW > 0 ? PREHEAT_FLOOR_C : u.outdoorC;
}

// Counts a timer down. Returns true on the tick it runs out, so the caller can release the mode.
function countDown(left: number | null, dtS: number): { left: number | null; expired: boolean } {
  if (left === null) return { left, expired: false };
  const next = left - dtS;
  return next <= 0 ? { left: null, expired: true } : { left: next, expired: false };
}

function stepTimers(u: Unit, dtS: number): void {
  const boost = countDown(u.boostLeftS, dtS);
  u.boostLeftS = boost.left;
  if (boost.expired && u.mode === "Boost") u.mode = u.resumeMode;
  const bypass = countDown(u.bypassLeftS, dtS);
  u.bypassLeftS = bypass.left;
  if (bypass.expired) u.bypassMode = "Auto";
  const balance = countDown(u.balanceLeftS, dtS);
  u.balanceLeftS = balance.left;
  if (balance.expired) u.balanceMode = "Balanced";
  const away = countDown(u.awayLeftS, dtS);
  u.awayLeftS = away.left;
  if (away.expired) u.away = false;
}

/** Advances the unit alone by dtS. Mutates `u`; deterministic for a given (u, scenario, dtS). */
export function stepUnit(u: Unit, sc: Scenario, dtS: number): void {
  u.simS += dtS;
  stepTimers(u, dtS);

  const swing = Math.sin((2 * Math.PI * u.simS) / OUTDOOR_SWING_S);
  u.outdoorC = r1(u.outdoorBase + 2 * swing);
  u.outdoorRh = Math.round(clamp(u.outdoorRhBase - 8 * swing, 15, 100));
  u.runningMeanC = approach(u.runningMeanC, u.outdoorC, dtS, 3 * 3600);

  // Fans: the level's flow, minus whatever the balance mode or a stopped fan takes away.
  const flow = LEVEL_FLOW[levelOf(u)];
  const supplyTarget = sc.supplyStopped || u.balanceMode === "Extract Only" ? 0 : flow;
  const exhaustTarget = u.balanceMode === "Supply Only" ? 0 : flow;
  // Kept as floats and SNAPPED within half a unit: rounding every step would park a stopping fan
  // at 2 m3/h for ever, because a first-order lag from 2 toward 0 rounds straight back to 2.
  u.supplyFlow = approach(u.supplyFlow, supplyTarget, dtS, 3);
  u.exhaustFlow = approach(u.exhaustFlow, exhaustTarget, dtS, 3);
  if (Math.abs(u.supplyFlow - supplyTarget) < 0.5) u.supplyFlow = supplyTarget;
  if (Math.abs(u.exhaustFlow - exhaustTarget) < 0.5) u.exhaustFlow = exhaustTarget;

  // Bypass. Auto opens it for free cooling: the house is warmer than the profile wants, the
  // outdoor air is cooler than the house, and it is not winter air (factory minimum 12 C).
  let bypassTarget = u.bypassMode === "Open" ? 100 : 0;
  if (u.bypassMode === "Auto") {
    const wantCool = u.extractC > profileTarget(u.profile) + 0.5 && u.outdoorC < u.extractC - 1 && u.outdoorC > 12;
    bypassTarget = wantCool ? 100 : 0;
  }
  const travel = 10 * dtS;  // %/s: the damper takes ten seconds end to end
  u.bypassPos = Math.round(clamp(u.bypassPos + clamp(bypassTarget - u.bypassPos, -travel, travel), 0, 100));

  // Pre-heater: present on this unit unless the scenario is the frost-protection one, whose
  // whole story is a unit WITHOUT one stopping its supply fan instead.
  const heaterOn = !sc.supplyStopped && u.outdoorC < PREHEAT_FLOOR_C && u.supplyFlow > 0;
  u.preheaterW = heaterOn ? Math.round(airPowerW(u.supplyFlow, PREHEAT_FLOOR_C - u.outdoorC)) : 0;
  const inC = preheatedC(u);

  // The exchanger. Bypassed air skips the core; an unbalanced pair recovers only what the
  // smaller stream can carry; a stopped stream exchanges nothing.
  const both = u.supplyFlow > 0 && u.exhaustFlow > 0;
  const eta = both ? efficiency((u.supplyFlow + u.exhaustFlow) / 2) * (1 - u.bypassPos / 100) : 0;
  const etaSupply = both ? eta * Math.min(1, u.exhaustFlow / u.supplyFlow) : 0;
  const etaExhaust = both ? eta * Math.min(1, u.supplyFlow / u.exhaustFlow) : 0;
  const still = (u.extractC + u.outdoorC) / 2;  // a sensor in air that is not moving
  const supplyTarget2 = u.supplyFlow > 0 ? inC + etaSupply * (u.extractC - inC) + 0.4 : still;
  const exhaustTarget2 = u.exhaustFlow > 0 ? u.extractC - etaExhaust * (u.extractC - inC) + 0.3 : still;
  u.supplyC = approach(u.supplyC, supplyTarget2, dtS, 8);
  u.exhaustC = approach(u.exhaustC, exhaustTarget2, dtS, 8);

  // The house: its own set-point, nudged by what the unit blows into it, plus occupants.
  const roomTarget = u.indoorBase + 0.0002 * u.supplyFlow * (u.supplyC - u.indoorBase);
  u.extractC = approach(u.extractC, roomTarget, dtS, 300);
  const humid = Math.sin((2 * Math.PI * u.simS) / ROOM_HUMIDITY_SWING_S);
  u.extractRh = Math.round(clamp(u.indoorRhBase + 6 * humid - 0.01 * (u.exhaustFlow - 110), 20, 90));

  // Energy. The avoided-heating figure is the recovered heat; avoided cooling only exists when
  // the outdoor air is the hotter one and the core keeps that heat out.
  const heating = u.runningMeanC < HEATING_LIMIT_C, cooling = u.runningMeanC > COOLING_LIMIT_C;
  u.avoidedHeatW = heating && u.supplyFlow > 0 ? Math.round(airPowerW(u.supplyFlow, Math.max(0, u.supplyC - inC))) : 0;
  u.avoidedCoolW = cooling && u.supplyFlow > 0 && u.outdoorC > u.extractC
    ? Math.round(airPowerW(u.supplyFlow, Math.max(0, u.outdoorC - u.supplyC))) : 0;
  const kwh = (w: number): number => (w * dtS) / 3.6e6;
  const fanKwh = kwh(fanPowerW(u.supplyFlow, u.exhaustFlow));
  u.energyYtd += fanKwh; u.energyTotal += fanKwh;
  u.preheatYtd += kwh(u.preheaterW); u.preheatTotal += kwh(u.preheaterW);
  u.avoidedHeatYtd += kwh(u.avoidedHeatW); u.avoidedHeatTotal += kwh(u.avoidedHeatW);
  u.avoidedCoolYtd += kwh(u.avoidedCoolW); u.avoidedCoolTotal += kwh(u.avoidedCoolW);
}

/**
 * Brings a freshly built unit to its steady state, so a preset reads right the moment it is
 * applied instead of after a minute of the fans spinning up. Ten simulated minutes.
 */
export function settle(u: Unit, sc: Scenario): void {
  for (let i = 0; i < 120; i++) stepUnit(u, sc, 5);
}

/** One tick of the whole device. Pure: returns a new state, `prev` is untouched. */
export function tick(prev: MockState, dtMs: number): MockState {
  const s: MockState = structuredClone(prev);
  s.uptimeMs += dtMs;
  stepUnit(s.unit, s.scenario, dtMs / 1000);
  stepOta(s, dtMs);

  // A submitted network is "joined" a few seconds later; that is the moment the policy starts
  // treating the device as provisioned, as comfoair_net_is_provisioned() does.
  if (s.net.connectingLeftMs > 0) {
    s.net.connectingLeftMs = Math.max(0, s.net.connectingLeftMs - dtMs);
    if (s.net.connectingLeftMs === 0) {
      s.net.state = "connected"; s.net.knownGood = true; s.scenario.provisioned = true;
      logLine(s, `net: connected to "${s.config.wifi_ssid}", access point off`);
    }
  }
  if (!s.scenario.provisioned && s.net.windowLeftMs > 0) s.net.windowLeftMs = Math.max(0, s.net.windowLeftMs - dtMs);
  if (s.scenario.provisioned && s.config.mqtt_host !== "" && s.scenario.readings === "live") s.mqttPublished += 3;
  return s;
}

