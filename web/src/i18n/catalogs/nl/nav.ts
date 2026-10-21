// Dutch translation of ../en/nav.ts. Typed against the English object: a key missing
// here, or one English does not have, is a tsc error in THIS file. Draft wording; the owner
// reviews it.
import type { Message } from "../../format.ts";
import type { nav as source } from "../en/nav.ts";

export const nav: Record<keyof typeof source, Message> = {
  "nav.home": "Home",
  "nav.diagnostics": "Diagnose",
  "nav.probe": "Mogelijkheden",
  "nav.system": "Systeem",
  "nav.settings": "Instellingen",
  "nav.log": "Logboek",
  "nav.update": "Update",
  "nav.language": "Taal",
  "nav.theme.toggle": "Thema wisselen",
  "nav.theme.toLight": "Naar licht thema",
  "nav.theme.toDark": "Naar donker thema",

  "conn.open": "Live",
  "conn.connecting": "Verbinden",
  "conn.closed": "Offline",
  "conn.open.title": "Ontvangt live-updates van het apparaat",
  "conn.connecting.title": "Verbinding met het apparaat wordt geopend",
  "conn.closed.title": "Niet verbonden -- de waarden hieronder zijn mogelijk verouderd",
};
