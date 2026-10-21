// German translation of ../en/tokens.ts. Typed against the English object: a key missing
// here, or one English does not have, is a tsc error in THIS file. Draft wording; the owner
// reviews it.
import type { Message } from "../../format.ts";
import type { tokens as source } from "../en/tokens.ts";

export const tokens: Record<keyof typeof source, Message> = {
  // ventilation_mode / fan presets, fan_level, operating_mode
  "token.opt.Auto": "Auto",
  "token.opt.Away": "Abwesend",
  "token.opt.Low": "Niedrig",
  "token.opt.Medium": "Mittel",
  "token.opt.High": "Hoch",
  "token.opt.Boost": "Boost",
  "token.opt.Manual": "Manuell",
  "token.opt.Manual (permanent)": "Manuell (dauerhaft)",

  // bypass_mode
  "token.opt.Open": "Offen",
  "token.opt.Closed": "Geschlossen",

  // temperature_profile, device_state, changing_filters
  "token.opt.Normal": "Normal",
  "token.opt.Cool": "Kühl",
  "token.opt.Warm": "Warm",

  // temperature_passive, humidity_comfort, humidity_protection
  "token.opt.Off": "Aus",
  "token.opt.Auto only": "Nur Auto",
  "token.opt.On": "Ein",

  // balance_mode, supply_fan_mode, exhaust_fan_mode
  "token.opt.Balanced": "Ausgeglichen",
  "token.opt.Supply Only": "Nur Zuluft",
  "token.opt.Extract Only": "Nur Abluft",

  // changing_filters
  "token.opt.Changing Filter": "Filterwechsel",

  // device_state
  "token.opt.Initialising": "Initialisierung",
  "token.opt.Away (preset)": "Abwesend (Stufe)",
  "token.opt.Filter Wizard": "Filterassistent",
  "token.opt.Commissioning": "Inbetriebnahme",
  "token.opt.Supplier Factory": "Werksmodus Lieferant",
  "token.opt.Zehnder Factory": "Werksmodus Zehnder",
  "token.opt.Standby": "Standby",
  "token.opt.DFC": "DFC",

  // temperature_unit, airflow_unit
  "token.opt.Celsius": "Celsius",
  "token.opt.Fahrenheit": "Fahrenheit",
  "token.opt.kg/h": "kg/h",
  "token.opt.l/s": "l/s",
  "token.opt.m³/h": "m³/h",
};
