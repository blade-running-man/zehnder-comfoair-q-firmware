// German translation of ../en/nav.ts. Typed against the English object: a key missing
// here, or one English does not have, is a tsc error in THIS file. Draft wording; the owner
// reviews it.
import type { Message } from "../../format.ts";
import type { nav as source } from "../en/nav.ts";

export const nav: Record<keyof typeof source, Message> = {
  "nav.home": "Start",
  "nav.diagnostics": "Diagnose",
  "nav.probe": "Gerätefunktionen",
  "nav.system": "System",
  "nav.settings": "Einstellungen",
  "nav.log": "Protokoll",
  "nav.update": "Update",
  "nav.language": "Sprache",
  "nav.theme.toggle": "Design umschalten",
  "nav.theme.toLight": "Zum hellen Design wechseln",
  "nav.theme.toDark": "Zum dunklen Design wechseln",

  "conn.open": "Live",
  "conn.connecting": "Verbinde",
  "conn.closed": "Offline",
  "conn.open.title": "Empfängt Live-Daten vom Gerät",
  "conn.connecting.title": "Verbindung zum Gerät wird aufgebaut",
  "conn.closed.title": "Nicht verbunden -- die Werte unten sind möglicherweise veraltet",
};
