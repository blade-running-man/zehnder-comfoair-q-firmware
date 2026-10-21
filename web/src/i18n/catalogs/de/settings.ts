// German translation of ../en/settings.ts. Typed against the English object: a key missing here,
// or one English does not have, is a tsc error in THIS file. Keep every {placeholder} verbatim.
// The settings.detail.* block translates the firmware's own error sentences (firmwareText.ts);
// it keeps the device's lowercase sentence starts where the English has them.
import type { Message } from "../../format.ts";
import type { settings as source } from "../en/settings.ts";

export const settings: Record<keyof typeof source, Message> = {
  "settings.title": "Einstellungen",
  "settings.readOnly.headline": "Diese Einstellungen wurden von einer neueren Firmware geschrieben",
  "settings.readOnly.detail":
    "Diese Firmware-Version versteht das Layout der gespeicherten Konfiguration nicht und "
    + "verweigert daher das Überschreiben, um sie nicht zu beschädigen. Spielen Sie die "
    + "vorherige Firmware erneut auf, oder setzen Sie die Einstellungen über die Taste zurück, "
    + "bevor Sie hier etwas ändern.",
  "settings.load.hint":
    "Broker- und Geräteeinstellungen können erst angezeigt werden, wenn sie gelesen werden "
    + "können. Das WLAN-Formular oben hängt nicht davon ab.",
  "settings.tryAgain": "Erneut versuchen",
  "settings.loading": "Konfiguration des Geräts wird gelesen…",
  "settings.save": "Einstellungen speichern",
  "settings.saved": "Einstellungen gespeichert",
  "settings.saveBar.hint": "Speichert Broker- und Geräteeinstellungen zusammen. Das Netzwerk wird nicht berührt.",
  "settings.notSent": "Nicht gesendet — nichts hat diese Seite verlassen",

  "settings.broker.title": "MQTT-Broker",
  "settings.broker.subtitle": "Wohin das Gerät veröffentlicht, und wo Home Assistant es findet.",
  "settings.broker.host.label": "Broker-Host",
  "settings.broker.host.placeholder": "192.168.1.10 oder homeassistant.local",
  "settings.broker.port.label": "Broker-Port",
  "settings.broker.port.placeholder": "1883",
  "settings.broker.username.label": "Broker-Benutzername",
  "settings.broker.password.label": "Broker-Passwort",
  "settings.broker.password.warning":
    "Diese Verbindung ist in dieser Version nicht verschlüsselt, daher ist dieses Passwort "
    + "für alles in Ihrem Netzwerk sichtbar. Verwenden Sie kein Passwort, das Sie auch "
    + "anderswo benutzen.",
  "settings.broker.topicPrefix.label": "Topic-Präfix",
  "settings.broker.discovery.label": "Home-Assistant-Discovery veröffentlichen",

  "settings.device.title": "Gerät",
  "settings.device.subtitle": "Wie es heißt, wie es sich verhält und wer es ändern darf.",
  "settings.device.name.label": "Gerätename",
  "settings.device.name.placeholder": "Lüftung",
  "settings.device.name.hint":
    "Nur ein Anzeigename. Die Identität des Geräts — seine Broker-Client-ID und seine "
    + "Home-Assistant-Geräte-ID — wird von seiner MAC-Adresse abgeleitet, sodass eine "
    + "Umbenennung hier kein zweites Gerät in Home Assistant erzeugen kann.",
  "settings.device.password.label": "Passwort für die Weboberfläche",
  "settings.device.password.warnTitle": "Für dieses Gerät gibt es kein „Passwort vergessen“.",
  "settings.device.password.warn2a": "Wird die Taste ",
  "settings.device.password.warn2em": "während",
  "settings.device.password.warn2b":
    " das Gerät hochfährt gehalten, passiert etwas ganz anderes — der Chip wechselt in "
    + "seinen Firmware-Lader, was genauso aussieht wie ein defektes Gerät. Erst hochfahren "
    + "lassen, warten, dann halten.",
  "settings.device.password.noneSet":
    "Es ist kein Passwort gesetzt. Jeder, der dieses Gerät im Netzwerk erreichen kann, "
    + "kann diese Einstellungen ändern — für ein Gerät im Heimnetz eine vertretbare Wahl, "
    + "und bewusst die Voreinstellung.",

  "settings.wifi.title": "WLAN",
  "settings.wifi.subtitle": "Die Liste zeigt, was dieses Gerät hören kann — nicht, was Ihr Telefon zeigt.",
  "settings.wifi.network.label": "Netzwerk",
  "settings.wifi.network.choose": "— Netzwerk wählen —",
  "settings.wifi.network.currentlyConfigured": "derzeit konfiguriert",
  "settings.wifi.network.open": "offen",
  "settings.wifi.network.manualOption": "Anderes / verstecktes Netzwerk — Namen eingeben…",
  "settings.wifi.network.ssidLabel": "Netzwerkname (SSID)",
  "settings.wifi.network.ssidPlaceholder": "genau so, wie der Router ihn ausstrahlt",
  "settings.wifi.scan.again": "Erneut suchen",
  "settings.wifi.scan.start": "Nach Netzwerken suchen",
  "settings.wifi.chooseFromList": "Aus der Liste wählen",
  "settings.wifi.typeInstead": "Namen stattdessen eingeben",
  "settings.wifi.scan.hint":
    "Ein Name, der auf Ihrem Telefon erscheint, aber nicht in dieser Liste steht, ist der "
    + "übliche Grund, warum ein Gerät ein eindeutig vorhandenes Netzwerk „nicht findet“: die "
    + "meisten Router senden einen Namen auf beiden Bändern, und dieses Gerät hat kein "
    + "5-GHz-Funkmodul. Geben Sie ihn in diesem Fall ein — versteckte Netzwerke erscheinen in "
    + "keinem Scan, sie können nur eingegeben werden.",
  "settings.wifi.password.label": "WLAN-Passwort",
  "settings.wifi.password.fallbackNetworkName": "das gespeicherte Netzwerk",
  "settings.wifi.notice.newNetworkFallback": "das neue Netzwerk",
  "settings.wifi.password.hintUnencrypted":
    "Wenn Sie sich im eigenen Einrichtungsnetzwerk des Geräts befinden, ist dieses Netzwerk "
    + "offen: dieser Schlüssel überquert es einmal unverschlüsselt. Nichts in einer über "
    + "reines HTTP ausgelieferten Browserseite kann das verhindern.",
  "settings.wifi.network.openHint": "{ssid} ist offen — Passwortfeld leer lassen.",
  "settings.wifi.network.securedWarning":
    "{ssid} ist gesichert und das Passwortfeld ist leer. Das Gerät wird sich verbinden "
    + "wollen und abgewiesen werden.",
  "settings.wifi.beforeYouPress": "Bevor Sie das drücken",
  "settings.wifi.save": "WLAN speichern und neu verbinden",
  "settings.wifi.accepted": "Angenommen — das Gerät versucht es jetzt",

  "settings.wifi.signal.notSeen": "in diesem Scan nicht gesehen",
  "settings.wifi.signal.strong": "stark",
  "settings.wifi.signal.good": "gut",
  "settings.wifi.signal.weak": "schwach",
  "settings.wifi.signal.veryWeak": "sehr schwach",

  "settings.wifi.key.stored":
    "Der bereits für {ssid} gespeicherte Schlüssel wird erneut verwendet. Er wird nie an "
    + "diese Seite zurückgesendet, daher ist das Feld leer; tippen Sie, um ihn zu ersetzen.",
  "settings.wifi.key.empty":
    "Leer: Das Gerät versucht, ohne Schlüssel beizutreten. Richtig für ein offenes "
    + "Netzwerk, und für nichts sonst.",
  "settings.wifi.key.typed": "Dieser Schlüssel wird so, wie eingegeben, an das Gerät gesendet.",

  "settings.wifi.notice.line1":
    "Das Gerät antwortet auf dieses Formular, bevor es sich verbindet — eine Bestätigung "
    + "hier bedeutet also, dass die Zugangsdaten angenommen wurden, nicht dass {target} sie "
    + "übernommen hat.",
  "settings.wifi.notice.line2":
    "Während des Versuchs teilen sich das Einrichtungsnetzwerk, in dem Sie sich gerade "
    + "befinden, und {target} ein einziges Funkmodul. Diese Seite wird höchstwahrscheinlich "
    + "innerhalb weniger Sekunden nicht mehr reagieren — das ist hier der normale Ausgang, "
    + "kein Fehler.",
  "settings.wifi.notice.line3":
    "Danach befindet sich das Gerät in {target}, mit welcher Adresse auch immer Ihr Router "
    + "ihm zuweist. In der Liste der verbundenen Geräte Ihres Routers finden Sie es.",
  "settings.wifi.notice.fallback":
    "Ist der Schlüssel falsch, stellt das Gerät {previous} von selbst wieder her und kehrt "
    + "dorthin zurück — es ist das letzte Netzwerk, das diesem Gerät tatsächlich eine Adresse "
    + "gegeben hat, was es allein zu einem Rückkehrziel macht. Nichts muss zurückgesetzt "
    + "werden.",
  "settings.wifi.notice.noFallback":
    "Dieses Gerät hat noch nie ein Netzwerk erreicht, es gibt also nichts, wohin es "
    + "zurückkehren könnte. Ist der Schlüssel falsch, versucht es weiter, und sein eigenes "
    + "Einrichtungsnetzwerk bleibt etwa 15 Minuten ab dem Erscheinen aktiv. Danach ist das "
    + "Einrichtungsnetzwerk weg, bis das Gerät neu gestartet wird, wodurch es für ein paar "
    + "Minuten wiederkehrt.",

  "settings.secret.stored.text":
    "Auf dem Gerät gespeichert. Wird nie zurückgesendet, daher ist das Feld leer; tippen "
    + "Sie, um es zu ersetzen.",
  "settings.secret.stored.placeholder": "unverändert — tippen zum Ersetzen",
  "settings.secret.willClear.text": "Geleert. Speichern löscht den gespeicherten Wert.",
  "settings.secret.willClear.placeholder": "leer — Speichern löscht den gespeicherten Wert",
  "settings.secret.willReplace.text": "Speichern ersetzt den gespeicherten Wert.",
  "settings.secret.notSet.text": "Nicht gesetzt.",
  "settings.secret.notSet.placeholder": "nicht gesetzt",
  "settings.secret.willSet.text": "Speichern legt diesen Wert erstmals fest.",
  "settings.error.portRange": "Der Broker-Port muss eine ganze Zahl zwischen 1 und 65535 sein.",

  "settings.error.notImplemented.headline": "Das Gerät hat diese Route, aber noch keinen Code dahinter",
  "settings.error.noHandler.headline": "Diese Firmware-Version bedient diesen Endpunkt nicht",
  "settings.error.passwordNeeded.headline": "Das Gerät verlangt sein Passwort für die Weboberfläche",
  "settings.error.writeRefused.headline": "Das Gerät hat diesen Schreibzugriff abgelehnt",
  "settings.error.badRequest.headline": "Das Gerät wollte das nicht annehmen",
  "settings.error.fault.headline": "Das Gerät meldet einen Fehler",
  "settings.error.browserGaveNoReason": "Der Browser hat keinen Grund angegeben.",
  "settings.error.disconnectExpected.headline": "Keine Antwort — und genau das ist hier der Erfolg",
  "settings.error.disconnectExpected.detail":
    "Das Gerät antwortet, bevor es seinen Funk neu einstellt, daher bedeutet eine mitten "
    + "im Flug abbrechende Anfrage meist, dass es bereits versucht. ({browserSaid})",
  "settings.error.offline.headline": "Keine Antwort vom Gerät",
  "settings.error.offline.detail": "Nichts hat das Gerät erreicht, daher hat es dazu nichts gesagt. ({browserSaid})",

  "settings.detail.ssid": "der Netzwerkname muss 1 bis 32 Zeichen lang sein",
  "settings.detail.psk":
    "das Netzwerkpasswort muss 8 bis 63 Zeichen lang sein, genau 64 Hexadezimalziffern "
    + "umfassen oder für ein offenes Netzwerk leer sein",
  "settings.detail.wifiPair":
    "Netzwerkname und Passwort werden als ein Datensatz gespeichert: senden Sie beide, "
    + "und geben Sie das Passwort erneut ein, wann immer sich der Name ändert",
  "settings.detail.host":
    "die Broker-Adresse muss ein Hostname oder eine IP-Adresse sein, ohne Schema und ohne "
    + "Port",
  "settings.detail.port": "der Broker-Port muss zwischen 1 und 65535 liegen",
  "settings.detail.user": "der Broker-Benutzername ist länger, als dieses Gerät speichern kann",
  "settings.detail.brokerPassword":
    "das Broker-Passwort ist länger, als dieses Gerät speichern kann",
  "settings.detail.prefix":
    "das Topic-Präfix muss 1 bis 64 Zeichen lang sein, ohne Leerzeichen, + oder # und "
    + "ohne leere Ebene",
  "settings.detail.name":
    "der Gerätename muss 1 bis 32 Zeichen lang sein und darf keine Steuerzeichen "
    + "enthalten",
  "settings.detail.uiPassword":
    "das Web-Passwort muss 8 bis 128 Zeichen lang sein und darf keine Steuerzeichen "
    + "enthalten",
  "settings.detail.noHash":
    "dieses Gerät konnte das Passwort gerade nicht sichern; es wurde nichts geändert",
  "settings.detail.readOnly":
    "diese Einstellungen wurden von einer neueren Firmware geschrieben und können erst "
    + "geändert werden, wenn das Gerät zurückgesetzt wird",
  "settings.detail.noDocument":
    "die Anfrage enthielt keine Einstellungen, daher wurde nichts gespeichert",
  "settings.detail.refused": "die Einstellungen wurden abgelehnt",
  "settings.detail.badBody":
    "Der Inhalt der Anfrage ist kein JSON-Objekt, das dieses Gerät lesen kann.",
  "settings.detail.badField":
    "Ein Feld fehlt, hat den falschen Typ oder ist länger, als dieses Gerät speichern "
    + "kann.",
  "settings.detail.wrongRoute":
    "Das Netzwerk wird über POST /api/provision eingestellt, nicht hier.",
  "settings.detail.notApplied": "Die Anfrage konnte nicht angewendet werden.",
  "settings.detail.unauthorized": "nicht autorisiert",
  "settings.detail.forbidden": "verboten",
  "settings.detail.notImplemented": "noch nicht implementiert",
  "settings.detail.noEndpoint": "kein solcher Endpunkt",
  "settings.detail.bodyTooLarge": "Inhalt der Anfrage zu groß",
  "settings.detail.truncatedBody": "Inhalt der Anfrage unvollständig",
  "settings.detail.scanUnavailable": "Scan nicht verfügbar",
  "settings.detail.scanTooLarge": "Scan-Ergebnis passt nicht in die Antwort",
  "settings.detail.busy": "beschäftigt",
  "settings.detail.statusTooLarge": "Status passt nicht in die Antwort",
  "settings.detail.configTooLarge": "Konfiguration passt nicht in die Antwort",
  "settings.detail.emptyBody": "Das Gerät antwortete {1} mit einer leeren Antwort.",
  "settings.detail.unreadableBody": "Das Gerät antwortete {1} mit einer Antwort, die diese Seite nicht lesen kann.",

  // --- broker/device notes, the reset-button ladder, the Connection card (status.ts) ---
  "settings.broker.topicPrefix.hint":
    "Eine Änderung des Präfixes verschiebt jedes Status-Topic. Die Entitäten in Home "
    + "Assistant folgen und behalten ihren Verlauf — was sie für Home Assistant beschreibt, "
    + "enthält das Präfix nicht, wird also umgeschrieben statt ersetzt, und Sie sehen "
    + "keinen zweiten Satz. Ist das Gerät beim Speichern mit dem Broker verbunden, leert es "
    + "dabei die Topics unter dem alten Präfix; wenn nicht, bleiben sie dort als "
    + "gespeicherte (retained) Nachrichten, die niemand mehr liest.",
  "settings.broker.discovery.note":
    "Aus: Das Gerät veröffentlicht nicht mehr, was es für Home Assistant beschreibt, "
    + "nimmt aber nicht zurück, was es vorher veröffentlicht hat. Gerät und Entitäten "
    + "bleiben dort und werden weiter aktualisiert — um sie zu entfernen, müssen diese "
    + "retained-Nachrichten am Broker von Hand gelöscht werden.",
  "settings.device.permanentManual.label": "Lüftungsstufe beibehalten",
  "settings.device.permanentManual.note":
    "An: Eine hier oder in Home Assistant gewählte Stufe bleibt. Aus: Das Gerät kehrt "
    + "innerhalb von zwei Stunden zu AUTO zurück, und seine Feuchte- und "
    + "Temperaturregelungen bleiben aktiv — der manuelle Modus schaltet sie ab.",
  "settings.device.button.p1":
    "Der Weg zurück ist die Taste am Gerät, und alles hängt davon ab, wie lange Sie sie "
    + "halten. Drücken Sie sie {after} und beobachten Sie das Licht: Es wird weiß, sobald "
    + "der Druck erkannt ist, kehrt zwischen einer und fünf Sekunden zu seiner üblichen "
    + "Farbe zurück, wird bei fünf Sekunden bernsteinfarben und bei fünfzehn rot und wird "
    + "heller, je näher die nächste Stufe kommt.",
  "settings.device.button.p1em": "nachdem das Gerät hochgefahren ist",
  "settings.device.button.p2":
    "{short}, und es wird nichts gelöscht: Das Gerät schaltet für einige Minuten sein "
    + "eigenes Netz {ap} ein. Nützlich, wenn es das WLAN des Hauses verloren hat — aber "
    + "dieses Passwort gilt auch dort. {gap}, und es passiert gar nichts. Diese Lücke ist "
    + "Absicht: Ein Anstoßen oder ein Ärmel kann nichts löschen.",
  "settings.device.button.p2short": "Vor einer Sekunde loslassen",
  "settings.device.button.p2gap": "Zwischen einer und fünf Sekunden loslassen",
  "settings.device.button.p3":
    "{soft}, solange das Licht bernsteinfarben ist, und es löscht die WLAN-Zugangsdaten "
    + "und dieses Passwort zusammen — und behält die Broker-Einstellungen, das "
    + "Topic-Präfix, den Gerätenamen und die Dauern. Es blinkt dreimal, startet neu und "
    + "kommt mit demselben Netz in der Luft und dieser Seite für jeden offen zurück.",
  "settings.device.button.p3strong": "Nach fünf Sekunden loslassen",
  "settings.device.button.p4":
    "{hard}, bis ins Rote, und es löscht stattdessen alles, einschließlich der "
    + "Broker-Einstellungen: Diese Stufe ist für ein Gerät, das das Haus verlässt. Es räumt "
    + "am Broker nicht hinter sich auf, daher behält Home Assistant das Gerät und seine "
    + "Entitäten — richten Sie das Gerät zuerst auf einen anderen Broker, solange es diesen "
    + "noch erreicht, wenn sie verschwinden sollen. Nach fünfundzwanzig Sekunden gilt die "
    + "Taste als klemmend und es passiert gar nichts; loslassen und neu beginnen.",
  "settings.device.button.p4strong": "Über fünfzehn Sekunden hinaus halten",
  "settings.status.title": "Verbindung",
  "settings.status.noKnownGood":
    "Dieses Netz hat noch nie eine Adresse geliefert; schlägt das nächste fehl, gibt es "
    + "nichts, wohin zurückgekehrt werden kann.",
  "settings.status.failure.notFound":
    "Dieses Netz wurde nicht gefunden. Prüfen Sie den Namen und dass es ein 2,4-GHz-Netz "
    + "ist — dieses Gerät hat keinen 5-GHz-Funk.",
  "settings.status.failure.wrongPassword":
    "Das Netz hat den Schlüssel abgelehnt. Geben Sie ihn neu ein; der gespeicherte wird "
    + "nie zurückgezeigt.",
  "settings.status.failure.refused":
    "Der Router hat geantwortet und das Gerät abgewiesen. Sein Zugangspunkt ist "
    + "vielleicht voll, oder es ist ein Enterprise-Netz, das dieses Formular nicht abbilden "
    + "kann.",
  "settings.status.failure.noAddress":
    "Das Gerät ist dem Netz beigetreten, hat aber nie eine Adresse bekommen. Das liegt am "
    + "DHCP des Routers, nicht am Passwort.",
  "settings.status.failure.unknown":
    "Das Gerät meldet einen Fehler, für den diese Seite keine Formulierung hat: {failure} "
    + "(Grund {reason}).",
  "settings.status.where.connected": "In {ssid}, unter {ip}.",
  "settings.status.where.setupAp":
    "Stellt sein eigenes Einrichtungsnetz bereit. Jeder in Reichweite erreicht es, daher "
    + "wird Schreiben abgelehnt, bis es einem Netz beitritt.",
  "settings.status.where.trying": "Versucht, {ssid} beizutreten.",
  "settings.status.where.none": "Es ist kein Netz eingerichtet.",
};
