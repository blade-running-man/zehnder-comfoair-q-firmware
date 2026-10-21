// The device's stable option vocabulary: every option string of every select, enum and fan
// preset entity in api/entities.ts (generated from pdo_table.py), keyed `token.opt.<option>`
// with the option spelled EXACTLY as the device sends and accepts it. Read through
// i18n/option.ts's optionLabel(); tests/tokens.test.ts fails when an entity gains an option
// this file does not have, or this file keeps one no entity has.
//
// DO NOT reword the keys: a key is the wire value, and only the DISPLAY label is translated.
// One word gets one translation across every entity that uses it ("Away" is the same
// in ventilation_mode, fan_level and operating_mode); a page that needs a different
// wording in one context says so in its own namespace.
import type { Message } from "../../format.ts";

export const tokens = {
  // ventilation_mode / fan presets, fan_level, operating_mode ("Away" is the fan level; device_state's 7 is "Away (preset)")
  "token.opt.Auto": "Auto",
  "token.opt.Away": "Away",
  "token.opt.Low": "Low",
  "token.opt.Medium": "Medium",
  "token.opt.High": "High",
  "token.opt.Boost": "Boost",
  "token.opt.Manual": "Manual",
  "token.opt.Manual (permanent)": "Manual (permanent)",

  // bypass_mode
  "token.opt.Open": "Open",
  "token.opt.Closed": "Closed",

  // temperature_profile, device_state, changing_filters
  "token.opt.Normal": "Normal",
  "token.opt.Cool": "Cool",
  "token.opt.Warm": "Warm",

  // temperature_passive, humidity_comfort, humidity_protection
  "token.opt.Off": "Off",
  "token.opt.Auto only": "Auto only",
  "token.opt.On": "On",

  // balance_mode, supply_fan_mode, exhaust_fan_mode
  "token.opt.Balanced": "Balanced",
  "token.opt.Supply Only": "Supply Only",
  "token.opt.Extract Only": "Extract Only",

  // changing_filters
  "token.opt.Changing Filter": "Changing Filter",

  // device_state
  "token.opt.Initialising": "Initialising",
  "token.opt.Away (preset)": "Away (preset)",
  "token.opt.Filter Wizard": "Filter Wizard",
  "token.opt.Commissioning": "Commissioning",
  "token.opt.Supplier Factory": "Supplier Factory",
  "token.opt.Zehnder Factory": "Zehnder Factory",
  "token.opt.Standby": "Standby",
  "token.opt.DFC": "DFC",

  // temperature_unit, airflow_unit
  "token.opt.Celsius": "Celsius",
  "token.opt.Fahrenheit": "Fahrenheit",
  "token.opt.kg/h": "kg/h",
  "token.opt.l/s": "l/s",
  "token.opt.m³/h": "m³/h",
} satisfies Record<string, Message>;
