// German translation of ../en/log.ts. Typed against the English object: a key missing
// here, or one English does not have, is a tsc error in THIS file. Draft wording; the owner
// reviews it. Keep every {placeholder} verbatim.
import type { Message } from "../../format.ts";
import type { log as source } from "../en/log.ts";

export const log: Record<keyof typeof source, Message> = {
  "log.title": "Protokoll",
  "log.empty": "Das Gerät hat seit dem Start nichts protokolliert.",
  "log.unreadable":
    "Das Protokoll konnte nicht gelesen werden. Das Gerät wurde vielleicht gerade neu "
    + "gestartet.",
};
