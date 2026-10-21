// Dutch translation of ../en/log.ts. Typed against the English object: a key missing
// here, or one English does not have, is a tsc error in THIS file. Draft wording; the owner
// reviews it. Keep every {placeholder} verbatim.
import type { Message } from "../../format.ts";
import type { log as source } from "../en/log.ts";

export const log: Record<keyof typeof source, Message> = {
  "log.title": "Logboek",
  "log.empty": "Het apparaat heeft sinds het opstarten niets gelogd.",
  "log.unreadable":
    "Het logboek kon niet worden gelezen. Het apparaat is misschien net opnieuw "
    + "opgestart.",
};
