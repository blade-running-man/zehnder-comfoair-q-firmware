// Nav and app-chrome strings: the nav links, the theme toggle, the language switcher's label,
// and ConnectionBadge's label and title per socket state (keyed by api/device's status).
import type { Message } from "../../format.ts";

export const nav = {
  "nav.home": "Home",
  "nav.diagnostics": "Diagnostics",
  "nav.probe": "Unit capabilities",
  "nav.system": "System",
  "nav.settings": "Settings",
  "nav.log": "Log",
  "nav.update": "Update",
  "nav.language": "Language",
  "nav.theme.toggle": "Toggle theme",
  "nav.theme.toLight": "Switch to light theme",
  "nav.theme.toDark": "Switch to dark theme",

  "conn.open": "Live",
  "conn.connecting": "Connecting",
  "conn.closed": "Offline",
  "conn.open.title": "Receiving live updates from the device",
  "conn.connecting.title": "Opening the connection to the device",
  "conn.closed.title": "Not connected -- readings below may be out of date",
} satisfies Record<string, Message>;
