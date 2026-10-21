// Dutch translation of ../en/tokens.ts. Typed against the English object: a key missing
// here, or one English does not have, is a tsc error in THIS file. Draft wording; the owner
// reviews it.
import type { Message } from "../../format.ts";
import type { tokens as source } from "../en/tokens.ts";

export const tokens: Record<keyof typeof source, Message> = {
  // ventilation_mode / fan presets, fan_level, operating_mode
  "token.opt.Auto": "Auto",
  "token.opt.Away": "Afwezig",
  "token.opt.Low": "Laag",
  "token.opt.Medium": "Middel",
  "token.opt.High": "Hoog",
  "token.opt.Boost": "Boost",
  "token.opt.Manual": "Handmatig",
  "token.opt.Manual (permanent)": "Handmatig (permanent)",

  // bypass_mode
  "token.opt.Open": "Open",
  "token.opt.Closed": "Gesloten",

  // temperature_profile, device_state, changing_filters
  "token.opt.Normal": "Normaal",
  "token.opt.Cool": "Koel",
  "token.opt.Warm": "Warm",

  // temperature_passive, humidity_comfort, humidity_protection
  "token.opt.Off": "Uit",
  "token.opt.Auto only": "Alleen auto",
  "token.opt.On": "Aan",

  // balance_mode, supply_fan_mode, exhaust_fan_mode
  "token.opt.Balanced": "Gebalanceerd",
  "token.opt.Supply Only": "Alleen toevoer",
  "token.opt.Extract Only": "Alleen afvoer",

  // changing_filters
  "token.opt.Changing Filter": "Filter vervangen",

  // device_state
  "token.opt.Initialising": "Initialiseren",
  "token.opt.Away (preset)": "Afwezig (stand)",
  "token.opt.Filter Wizard": "Filterwizard",
  "token.opt.Commissioning": "Inbedrijfstelling",
  "token.opt.Supplier Factory": "Fabrieksmodus leverancier",
  "token.opt.Zehnder Factory": "Fabrieksmodus Zehnder",
  "token.opt.Standby": "Stand-by",
  "token.opt.DFC": "DFC",

  // temperature_unit, airflow_unit
  "token.opt.Celsius": "Celsius",
  "token.opt.Fahrenheit": "Fahrenheit",
  "token.opt.kg/h": "kg/u",
  "token.opt.l/s": "l/s",
  "token.opt.m³/h": "m³/u",
};
