// Diagnostics page (pages/state, route /diagnostics).
//
// THIS object defines the namespace's key set. Every key added here must be added to
// ../de/state.ts, ../nl/state.ts and ../uk/state.ts too: those are typed
// `Record<keyof typeof state, Message>` against this object, so tsc fails the build in each of
// them until it is. Prefix every key "state." -- en.ts spreads all namespaces into one object,
// and a key two namespaces share would silently overwrite one of them (catalogs.test.ts fails
// on that). A count-dependent message is a plural object ({ one, other } in English); the
// other languages supply their own categories, which catalogs.test.ts checks.
import type { Message } from "../../format.ts";

export const state = {
  "state.loading": "Loading…",
  "state.readFailed": "Cannot read the device",
  "state.status.live": "Live",
  "state.status.waiting": "Connected, waiting for the first reading",
  "state.status.connecting": "Connecting…",
  "state.status.disconnected": "Disconnected — the readings below may be stale",
  "state.group.ventilation": "Ventilation",
  "state.group.temperature": "Temperatures",
  "state.group.humidity": "Humidity",
  "state.group.airflow": "Airflow and fans",
  "state.group.energy": "Energy",
  "state.group.diagnostic": "Diagnostics",
  "state.group.other": "Other",
  "state.fan.on": "{preset} · speed {speed}",
  "state.value.on": "on",
  "state.value.off": "off",
  "state.value.yes": "yes",
  "state.value.no": "no",

  // --- groups that groups.ts adds, the write-status note (control.ts), the broker card's fallback ---
  "state.group.bypass": "Bypass and season",
  "state.group.filter": "Filter",
  "state.group.timers": "Timers",
  "state.group.settings": "Settings",
  "state.write.notSent": "Not sent: {why}",
  "state.write.unconfirmed":
    "Sent. The unit's value is read at start-up and every 6 hours; a change is confirmed at the "
    + "next read.",
  "state.write.sending": "Sending {value}…",
  "state.broker.savedFallback": "the saved broker",
} satisfies Record<string, Message>;
