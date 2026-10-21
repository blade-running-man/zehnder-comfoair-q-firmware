// Ukrainian translation of ../en/state.ts. Typed against the English object: a key missing here,
// or one English does not have, is a tsc error in THIS file. Keep every {placeholder} verbatim.
import type { Message } from "../../format.ts";
import type { state as source } from "../en/state.ts";

export const state: Record<keyof typeof source, Message> = {
  "state.loading": "Завантаження…",
  "state.readFailed": "Не вдається прочитати пристрій",
  "state.status.live": "Наживо",
  "state.status.waiting": "Підключено, очікування першого показника",
  "state.status.connecting": "Підключення…",
  "state.status.disconnected": "Відключено — показники нижче можуть бути застарілими",
  "state.group.ventilation": "Вентиляція",
  "state.group.temperature": "Температури",
  "state.group.humidity": "Вологість",
  "state.group.airflow": "Потік повітря та вентилятори",
  "state.group.energy": "Енергія",
  "state.group.diagnostic": "Діагностика",
  "state.group.other": "Інше",
  "state.fan.on": "{preset} · швидкість {speed}",
  "state.value.on": "увімк.",
  "state.value.off": "вимк.",
  "state.value.yes": "так",
  "state.value.no": "ні",

  // --- groups that groups.ts adds, the write-status note (control.ts), the broker card's fallback ---
  "state.group.bypass": "Байпас і сезон",
  "state.group.filter": "Фільтр",
  "state.group.timers": "Таймери",
  "state.group.settings": "Налаштування",
  "state.write.notSent": "Не надіслано: {why}",
  "state.write.unconfirmed":
    "Надіслано. Значення зчитується з установки під час запуску й надалі кожні 6 годин; зміну "
    + "буде підтверджено під час наступного зчитування.",
  "state.write.sending": "Надсилання: {value}…",
  "state.broker.savedFallback": "збережений брокер",
};
