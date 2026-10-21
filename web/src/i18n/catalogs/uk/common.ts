// Ukrainian translation of ../en/common.ts. Typed against the English object: a key missing
// here, or one English does not have, is a tsc error in THIS file. Draft wording; the owner
// reviews it.
import type { Message } from "../../format.ts";
import type { common as source } from "../en/common.ts";

export const common: Record<keyof typeof source, Message> = {
  "common.set": "Встановити",
  "common.save": "Зберегти",
  "common.saving": "Збереження…",
  "common.cancel": "Скасувати",
  "common.apply": "Застосувати",
  "common.close": "Закрити",
  "common.retry": "Повторити",
  "common.refresh": "Оновити",
  "common.loading": "Завантаження…",
  "common.yes": "Так",
  "common.no": "Ні",
  "common.on": "Увімк.",
  "common.off": "Вимк.",
  "common.unknown": "Невідомо",
  "common.error": "Помилка",
};
