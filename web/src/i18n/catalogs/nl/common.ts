// Dutch translation of ../en/common.ts. Typed against the English object: a key missing
// here, or one English does not have, is a tsc error in THIS file. Draft wording; the owner
// reviews it.
import type { Message } from "../../format.ts";
import type { common as source } from "../en/common.ts";

export const common: Record<keyof typeof source, Message> = {
  "common.set": "Instellen",
  "common.save": "Opslaan",
  "common.saving": "Bezig met opslaan…",
  "common.cancel": "Annuleren",
  "common.apply": "Toepassen",
  "common.close": "Sluiten",
  "common.retry": "Opnieuw proberen",
  "common.refresh": "Vernieuwen",
  "common.loading": "Laden…",
  "common.yes": "Ja",
  "common.no": "Nee",
  "common.on": "Aan",
  "common.off": "Uit",
  "common.unknown": "Onbekend",
  "common.error": "Fout",
};
