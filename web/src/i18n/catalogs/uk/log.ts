// Ukrainian translation of ../en/log.ts. Typed against the English object: a key missing
// here, or one English does not have, is a tsc error in THIS file. Draft wording; the owner
// reviews it. Keep every {placeholder} verbatim.
import type { Message } from "../../format.ts";
import type { log as source } from "../en/log.ts";

export const log: Record<keyof typeof source, Message> = {
  "log.title": "Журнал",
  "log.empty": "Від запуску пристрій нічого не записав у журнал.",
  "log.unreadable": "Не вдалося прочитати журнал. Можливо, пристрій щойно перезапустився.",
};
