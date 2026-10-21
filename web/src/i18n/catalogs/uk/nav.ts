// Ukrainian translation of ../en/nav.ts. Typed against the English object: a key missing
// here, or one English does not have, is a tsc error in THIS file. Draft wording; the owner
// reviews it.
import type { Message } from "../../format.ts";
import type { nav as source } from "../en/nav.ts";

export const nav: Record<keyof typeof source, Message> = {
  "nav.home": "Головна",
  "nav.diagnostics": "Діагностика",
  "nav.probe": "Можливості установки",
  "nav.system": "Система",
  "nav.settings": "Налаштування",
  "nav.log": "Журнал",
  "nav.update": "Оновлення",
  "nav.language": "Мова",
  "nav.theme.toggle": "Перемкнути тему",
  "nav.theme.toLight": "Увімкнути світлу тему",
  "nav.theme.toDark": "Увімкнути темну тему",

  "conn.open": "Наживо",
  "conn.connecting": "Підключення",
  "conn.closed": "Офлайн",
  "conn.open.title": "Отримує оновлення від пристрою наживо",
  "conn.connecting.title": "Відкривається з'єднання з пристроєм",
  "conn.closed.title": "Немає з'єднання -- показники нижче можуть бути застарілими",
};
