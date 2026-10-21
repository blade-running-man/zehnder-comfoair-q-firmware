// German translation of ../en/common.ts. Typed against the English object: a key missing
// here, or one English does not have, is a tsc error in THIS file. Draft wording; the owner
// reviews it.
import type { Message } from "../../format.ts";
import type { common as source } from "../en/common.ts";

export const common: Record<keyof typeof source, Message> = {
  "common.set": "Setzen",
  "common.save": "Speichern",
  "common.saving": "Wird gespeichert…",
  "common.cancel": "Abbrechen",
  "common.apply": "Übernehmen",
  "common.close": "Schließen",
  "common.retry": "Wiederholen",
  "common.refresh": "Aktualisieren",
  "common.loading": "Wird geladen…",
  "common.yes": "Ja",
  "common.no": "Nein",
  "common.on": "Ein",
  "common.off": "Aus",
  "common.unknown": "Unbekannt",
  "common.error": "Fehler",
};
