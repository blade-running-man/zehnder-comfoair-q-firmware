// Ukrainian translation of ../en/tokens.ts. Typed against the English object: a key missing
// here, or one English does not have, is a tsc error in THIS file. Draft wording; the owner
// reviews it.
import type { Message } from "../../format.ts";
import type { tokens as source } from "../en/tokens.ts";

export const tokens: Record<keyof typeof source, Message> = {
  // ventilation_mode / fan presets, fan_level, operating_mode
  "token.opt.Auto": "Авто",
  "token.opt.Away": "Не вдома",
  "token.opt.Low": "Низький",
  "token.opt.Medium": "Середній",
  "token.opt.High": "Високий",
  "token.opt.Boost": "Інтенсивний",
  "token.opt.Manual": "Ручний",
  "token.opt.Manual (permanent)": "Ручний (постійно)",

  // bypass_mode
  "token.opt.Open": "Відкрито",
  "token.opt.Closed": "Закрито",

  // temperature_profile, device_state, changing_filters
  "token.opt.Normal": "Звичайний",
  "token.opt.Cool": "Прохолодний",
  "token.opt.Warm": "Теплий",

  // temperature_passive, humidity_comfort, humidity_protection
  "token.opt.Off": "Вимкнено",
  "token.opt.Auto only": "Лише в режимі «Авто»",
  "token.opt.On": "Увімкнено",

  // balance_mode, supply_fan_mode, exhaust_fan_mode
  "token.opt.Balanced": "Збалансовано",
  "token.opt.Supply Only": "Лише приплив",
  "token.opt.Extract Only": "Лише витяжка",

  // changing_filters
  "token.opt.Changing Filter": "Заміна фільтрів",

  // device_state
  "token.opt.Initialising": "Ініціалізація",
  "token.opt.Away (preset)": "Не вдома (рівень)",
  "token.opt.Filter Wizard": "Майстер заміни фільтрів",
  "token.opt.Commissioning": "Введення в експлуатацію",
  "token.opt.Supplier Factory": "Заводський режим постачальника",
  "token.opt.Zehnder Factory": "Заводський режим Zehnder",
  "token.opt.Standby": "Очікування",
  "token.opt.DFC": "DFC",

  // temperature_unit, airflow_unit
  "token.opt.Celsius": "Цельсій",
  "token.opt.Fahrenheit": "Фаренгейт",
  "token.opt.kg/h": "кг/год",
  "token.opt.l/s": "л/с",
  "token.opt.m³/h": "м³/год",
};
