// German translation of ../en/broker.ts. Typed against the English object: a key missing
// here, or one English does not have, is a tsc error in THIS file. Draft wording; the owner
// reviews it. Keep every {placeholder} verbatim.
import type { Message } from "../../format.ts";
import type { broker as source } from "../en/broker.ts";

export const broker: Record<keyof typeof source, Message> = {
  "broker.tone.pending": "Warten",
  "broker.tone.denied": "Abgewiesen",
  "broker.tone.error": "Problem",
  "broker.tone.offline": "Keine Antwort",
  "broker.bootGrace":
    "Das Gerät läuft seit weniger als fünf Minuten; das Haus fährt möglicherweise noch "
    + "hoch.",
  "broker.absent.headline": "Das Gerät hat nichts über einen Broker gesagt",
  "broker.absent.detail":
    "Diese Antwort enthält überhaupt keinen Broker-Abschnitt — das ist etwas anderes, als "
    + "wenn jeder Zähler null wäre.",
  "broker.off.headline": "MQTT ist ausgeschaltet",
  "broker.off.detail":
    "Es ist keine Broker-Adresse gespeichert, daher kann Home Assistant dieses Gerät "
    + "nicht sehen. Tragen Sie den Broker auf der Seite Einstellungen ein und speichern "
    + "Sie.",
  "broker.unknownWord.headline":
    "Das Gerät meldet einen Broker-Zustand, den diese Seite nicht kennt",
  "broker.unknownWord.detail":
    "Das Statusdokument meldete link \"{link}\", failure \"{failure}\", subscriptions "
    + "\"{subscriptions}\". Eines dieser Wörter kennt diese Seite nicht — die Firmware hat "
    + "ein neues bekommen, diese Seite nicht.",
  "broker.subsRefused.headline": "Der Broker lässt dieses Gerät nicht zuhören",
  "broker.subsRefused.detail":
    "Messwerte werden veröffentlicht, aber Befehle aus Home Assistant kommen nicht an: "
    + "Der Broker hat die Befehls-Abonnements abgelehnt. Das ist eine ACL des Brokers, kein "
    + "Gerätefehler.",
  "broker.subsPending.headline": "Warte auf die Bestätigung des Brokers",
  "broker.subsPending.detail": "Verbunden; die Befehls-Abonnements sind noch nicht bestätigt.",
  "broker.online.headline": "Mit dem Broker verbunden",
  "broker.online.detail":
    "Veröffentlichte Messwerte: {published}, angenommene Befehle: {commands}. "
    + "Neuverbindungen: {reconnects}.",
  "broker.connecting.headline": "Verbinde mit dem Broker",
  "broker.connecting.detail":
    "Das Gerät wählt {host} an. Das dauert normalerweise ein bis zwei Sekunden.",
  "broker.retry": "Nächster Versuch in {secs} s.",
  "broker.dns.headline": "Der Name des Brokers lässt sich nicht auflösen",
  "broker.dns.detail":
    "Das Gerät hat den Router nach {host} gefragt und keine Antwort bekommen. Prüfen Sie "
    + "die Schreibweise oder geben Sie stattdessen die IP-Adresse des Brokers ein.",
  "broker.tcp.headline": "Unter dieser Adresse antwortet nichts",
  "broker.tcp.detail":
    "{host}:{port} hat keine Verbindung angenommen. Der Broker ist vielleicht "
    + "ausgeschaltet, oder der Port ist falsch.",
  "broker.tcp.retry": "Das Gerät versucht es weiter — nächster Versuch in {secs} s.",
  "broker.unavailable.headline": "Der Broker ist noch nicht bereit",
  "broker.unavailable.detail":
    "Er hat geantwortet und sich als nicht verfügbar gemeldet — meist ein Broker, der "
    + "noch startet.",
  "broker.protocol.headline": "Der Broker hat die Verbindung abgelehnt",
  "broker.protocol.detail":
    "Er antwortete „{reason}“ (Code {code}). Mit dem Passwort hat das nichts zu tun: "
    + "Prüfen Sie die Einstellungen des Brokers selbst. Das Gerät versucht es alle fünf "
    + "Minuten erneut.",
  "broker.badCredentials.headline": "Der Broker hat Benutzername oder Passwort abgelehnt",
  "broker.badCredentials.detail":
    "Er antwortete „{reason}“ (Code {code}). Korrigieren Sie sie auf der Seite "
    + "Einstellungen und speichern Sie — das Gerät versucht es sofort erneut, ohne die "
    + "fünfminütige Pause abzuwarten.",
  "broker.dropped.headline": "Die Verbindung zum Broker bricht immer wieder ab",
  "broker.dropped.detail":
    "Er hat das Gerät angenommen und die Verbindung dann {disconnects}-mal geschlossen. "
    + "Das passiert, wenn ein anderes Gerät dieselbe Client-ID benutzt.",
  "broker.identityConflict":
    "{reconnects} Neuverbindungen gegenüber {disconnects} Trennungen: Dass beide "
    + "gemeinsam steigen, ist von hier aus das einzige Zeichen dafür, denn der Broker kann "
    + "es nicht anders melden.",
  "broker.notDialled.headline": "Das Gerät hat den Broker noch nicht angewählt",
  "broker.notDialled.detail":
    "Ein Broker ist gespeichert, aber die Verbindung wurde noch nicht gestartet. In den "
    + "ersten Sekunden nach einem Neustart ist das normal, auf Dauer ein Fehler.",
  "broker.noReason.headline": "Das Gerät hat nicht gesagt, warum der Broker nicht erreichbar ist",
  "broker.noReason.detail":
    "Der Link ist „{link}“ und der Fehler „none“ — beides zugleich kann nicht stimmen. "
    + "Bisherige Versuche: {attempts}.",
  "broker.alert.noControl": "Home Assistant kann dieses Gerät nicht steuern",
  "broker.alert.noData": "Home Assistant empfängt keine Daten von diesem Gerät",
  "broker.alert.tail": "{detail}. Die Zähler des Brokers stehen unter Diagnose.",
  "broker.card.title": "Home Assistant & der Broker",
  "broker.card.saved": "Gespeicherter Broker:",
  "broker.card.none": "keiner",
  "broker.card.stale": "Die Zahlen unten sind die zuletzt eingetroffenen, nicht die aktuellen.",
  "broker.counter.link": "Link",
  "broker.counter.subscriptions": "Befehls-Abonnements",
  "broker.counter.published": "Veröffentlichte Messwerte",
  "broker.counter.commands": "Angenommene Befehle",
  "broker.counter.rejected": "Abgelehnte Befehle",
  "broker.counter.reconnects": "Neuverbindungen / Trennungen",
  "broker.counter.attempts": "Versuche seit der letzten stabilen Sitzung",
  "broker.counter.connack": "Letztes CONNACK",
  "broker.counter.errno": "Socket-Fehlernummer",
  "broker.counter.epoch": "Client-Neuaufbauten",
  "broker.move.offline.headline":
    "Auf {old} kann nichts aufgeräumt werden — das Gerät ist nicht damit verbunden",
  "broker.move.offline.line1":
    "Das Gerät kann nicht zurücknehmen, was es auf {old} veröffentlicht hat, solange es "
    + "ihn nicht erreicht. Die Gerätekarte und alle {count} Entitäten bleiben in jedem Home "
    + "Assistant, der diesen Broker liest, mit den Werten vom letzten Kontakt — und sie "
    + "sind auch nach dem Speichern noch da.",
  "broker.move.offline.line2":
    "Speichern ist nicht blockiert. Ein Broker, der nicht antwortet, darf Sie nicht daran "
    + "hindern, eine Einstellung zu ändern.",
  "broker.move.offline.line3":
    "Um sie von Hand zu entfernen, sobald der Broker wieder da ist, führen Sie dies auf "
    + "einem Rechner aus, der ihn erreicht:",
  "broker.move.couldNotAsk":
    "Diese Seite konnte nicht lesen, ob das Gerät gerade mit {old} verbunden ist, und "
    + "geht deshalb davon aus. Ist es das nicht, ändert Speichern nur die Adresse und lässt "
    + "alles auf {old} unberührt.",
  "broker.move.connected": "Das Gerät ist gerade mit {old} verbunden.",
  "broker.move.confirm": "Löschen und speichern",
  "broker.move.headline":
    "Speichern entfernt dieses Gerät und seine {count} Entitäten aus Home Assistant auf "
    + "{old}",
  "broker.move.line1":
    "Wird die Adresse geändert, nimmt es vor dem Wechsel alles zurück, was es dort "
    + "veröffentlicht hat: {count} leere Discovery-Nachrichten, woraufhin Home Assistant "
    + "die Gerätekarte und alle {count} Entitäten aus seiner Registry löscht.",
  "broker.move.line2":
    "Ihre entity_ids gehen mit, und das lässt sich nicht rückgängig machen. Dashboards, "
    + "Automationen, Skripte, Templates und Langzeitstatistiken, die diese entity_ids "
    + "nennen, funktionieren nicht mehr — ohne irgendeine Fehlermeldung. Wird das Gerät "
    + "danach wieder auf {old} gerichtet, entstehen die Entitäten neu, aber nicht unbedingt "
    + "unter denselben entity_ids und nie mit ihrem Verlauf.",
  "broker.move.line3":
    "Das tut nur die Broker-Adresse. Topic-Präfix, Gerätename und der Discovery-Schalter "
    + "lassen sich alle ohne das ändern — dabei bleiben die Entitäten, ihre entity_ids und "
    + "ihr Verlauf erhalten.",
};
