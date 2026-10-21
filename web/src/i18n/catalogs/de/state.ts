// German translation of ../en/state.ts. Typed against the English object: a key missing here,
// or one English does not have, is a tsc error in THIS file. Keep every {placeholder} verbatim.
import type { Message } from "../../format.ts";
import type { state as source } from "../en/state.ts";

export const state: Record<keyof typeof source, Message> = {
  "state.loading": "Wird geladen…",
  "state.readFailed": "Gerät kann nicht gelesen werden",
  "state.status.live": "Live",
  "state.status.waiting": "Verbunden, warte auf den ersten Messwert",
  "state.status.connecting": "Verbinde…",
  "state.status.disconnected": "Getrennt — die Werte unten sind möglicherweise veraltet",
  "state.group.ventilation": "Lüftung",
  "state.group.temperature": "Temperaturen",
  "state.group.humidity": "Luftfeuchtigkeit",
  "state.group.airflow": "Luftstrom und Ventilatoren",
  "state.group.energy": "Energie",
  "state.group.diagnostic": "Diagnose",
  "state.group.other": "Sonstiges",
  "state.fan.on": "{preset} · Stufe {speed}",
  "state.value.on": "ein",
  "state.value.off": "aus",
  "state.value.yes": "ja",
  "state.value.no": "nein",

  // --- groups that groups.ts adds, the write-status note (control.ts), the broker card's fallback ---
  "state.group.bypass": "Bypass und Jahreszeit",
  "state.group.filter": "Filter",
  "state.group.timers": "Timer",
  "state.group.settings": "Einstellungen",
  "state.write.notSent": "Nicht gesendet: {why}",
  "state.write.unconfirmed":
    "Gesendet. Der Wert wird beim Start und danach alle 6 Stunden vom Gerät abgefragt; eine "
    + "Änderung wird bei der nächsten Abfrage bestätigt.",
  "state.write.sending": "Sende {value}…",
  "state.broker.savedFallback": "der gespeicherte Broker",
};
