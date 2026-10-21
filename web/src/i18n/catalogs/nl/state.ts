// Dutch translation of ../en/state.ts. Typed against the English object: a key missing here,
// or one English does not have, is a tsc error in THIS file. Keep every {placeholder} verbatim.
import type { Message } from "../../format.ts";
import type { state as source } from "../en/state.ts";

export const state: Record<keyof typeof source, Message> = {
  "state.loading": "Laden…",
  "state.readFailed": "Kan het apparaat niet uitlezen",
  "state.status.live": "Live",
  "state.status.waiting": "Verbonden, wacht op de eerste meting",
  "state.status.connecting": "Verbinden…",
  "state.status.disconnected": "Verbinding verbroken — de waarden hieronder kunnen verouderd zijn",
  "state.group.ventilation": "Ventilatie",
  "state.group.temperature": "Temperaturen",
  "state.group.humidity": "Luchtvochtigheid",
  "state.group.airflow": "Luchtstroom en ventilatoren",
  "state.group.energy": "Energie",
  "state.group.diagnostic": "Diagnose",
  "state.group.other": "Overig",
  "state.fan.on": "{preset} · stand {speed}",
  "state.value.on": "aan",
  "state.value.off": "uit",
  "state.value.yes": "ja",
  "state.value.no": "nee",

  // --- groups that groups.ts adds, the write-status note (control.ts), the broker card's fallback ---
  "state.group.bypass": "Bypass en seizoen",
  "state.group.filter": "Filter",
  "state.group.timers": "Timers",
  "state.group.settings": "Instellingen",
  "state.write.notSent": "Niet verzonden: {why}",
  "state.write.unconfirmed":
    "Verzonden. De waarde wordt bij het opstarten en daarna elke 6 uur uit de unit gelezen; een "
    + "wijziging wordt bij de volgende uitlezing bevestigd.",
  "state.write.sending": "{value} wordt verzonden…",
  "state.broker.savedFallback": "de opgeslagen broker",
};
