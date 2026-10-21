// German translation of ../en/update.ts. Typed against the English object: a key missing here,
// or one English does not have, is a tsc error in THIS file. Keep every {placeholder} verbatim.
import type { Message } from "../../format.ts";
import type { update as source } from "../en/update.ts";

export const update: Record<keyof typeof source, Message> = {
  "update.heading": "Firmware-Update",
  "update.title": "Firmware installieren",
  "update.instructions": "Wählen Sie die für dieses Gerät gebaute firmware.bin (.pio/build/<env>/firmware.bin). Das Gerät prüft die Datei, schreibt sie in seinen freien Slot und startet neu.",
  "update.fileAria": "Firmware-Datei (.bin) zum Installieren",
  "update.file": "{name} · {kib} KiB",
  "update.install": "Installieren",
  "update.uploading": "Wird hochgeladen… {percent} % ({sent} von {total} KiB)",
  "update.uploadDone": "Hochladen abgeschlossen; das Gerät installiert das Image.",
  "update.restarting": "Wird installiert… das Gerät startet neu.",
  "update.uncertain": "Die Verbindung brach ab, nachdem die ganze Datei gesendet war. Es wird gewartet, ob das Gerät sie installiert hat.",
  "update.waiting": "Warten, bis das Gerät wieder antwortet ({seconds} s).",
  "update.updated": "Aktualisiert: {before} → {after}",
  "update.backRunning": "Das Gerät ist wieder da und läuft mit {after}.",
  "update.confirmNote": "Die neue Firmware bestätigt sich nach 30 s fehlerfreiem Betrieb selbst; schlägt sie vorher fehl, kehrt das Gerät automatisch zur vorherigen Version zurück.",
  "update.same": "Das Gerät ist wieder da, läuft aber weiterhin mit {version}. Das Update wurde möglicherweise zurückgerollt.",
  "update.systemLink": "Die Seite System zeigt, welche Partition läuft.",
  "update.openSystem": "System öffnen",
  "update.gaveUp": "Das Gerät antwortet seit 90 s nicht. Es startet möglicherweise noch; laden Sie diese Seite in einer Minute neu und prüfen Sie die Seite System, sobald es antwortet.",
  "update.noPassword": "Firmware-Updates erfordern ein Gerätepasswort. Legen Sie eines in den Einstellungen fest und kommen Sie dann zurück.",
  "update.noPasswordWhy": "Ohne Passwort könnte jeder in Ihrem Netzwerk eigenen Code auf diesem Gerät installieren.",
  "update.openSettings": "Einstellungen öffnen",
  "update.login": "Dieses Gerät hat ein Passwort. Melden Sie sich an, wenn der Browser fragt, und versuchen Sie es erneut.",
  "update.refused": "Das Gerät hat das Update abgelehnt: {detail}",
  "update.failed": "Hochladen fehlgeschlagen — die Verbindung zum Gerät wurde unterbrochen.",
  "update.toastDone": "Firmware aktualisiert",
  "update.compare": "Installiert: {installed} → Diese Datei: {image}",
  "update.fileVersion": "Diese Datei: {image}",
  "update.verdict.newer": "Dies ist eine neuere Version",
  "update.verdict.same": "Dies ist dieselbe Version (Neuinstallation)",
  "update.verdict.older": "Dies ist eine ÄLTERE Version — das Gerät wird herabgestuft. Die Einstellungen bleiben erhalten.",

  "update.detail.noLength": "Der Upload hat die Dateigröße nicht angegeben; senden Sie ihn mit einer Content-Length.",
  "update.detail.tooSmall": "Die Datei ist zu klein für ein Firmware-Image.",
  "update.detail.tooLarge": "Die Datei ist größer als der Update-Slot.",
  "update.detail.notImage": "Dies ist kein Firmware-Image; wählen Sie firmware.bin aus .pio/build/<env>/, nicht die .elf oder eine komprimierte Datei.",
  "update.detail.wrongChip": "Diese Firmware wurde für einen anderen Chip gebaut.",
  "update.detail.truncated": "Der Upload brach ab, bevor die ganze Datei ankam; versuchen Sie es erneut.",
  "update.detail.verifyFailed": "Das hochgeladene Image hat die Prüfung nicht bestanden und wurde nicht installiert.",
  "update.detail.busy": "Es wird bereits ein anderes Update hochgeladen.",
  "update.detail.flashError": "Das Schreiben des Updates in den Flash ist fehlgeschlagen; die laufende Firmware ist unverändert.",
  "update.detail.noSlot": "Dieses Gerät hat keine Partition, in die ein Update installiert werden kann.",
  "update.detail.unconfirmed": "Die laufende Firmware wurde gerade aktualisiert und wird noch geprüft; versuchen Sie es in einer Minute erneut.",
  "update.detail.wrongProject": "Diese Datei ist Firmware für ein anderes Gerät, nicht für die ComfoAir-Q-Steuerung.",

  // --- the USB fallback card ---
  "update.usb.title": "Über USB",
  "update.usb.body":
    "Wenn der Browser das Gerät nicht erreicht, oder bei der Erstinstallation, wird es "
    + "über USB geflasht:",
  "update.usb.strap":
    "Wird die Taste beim Einschalten gehalten, startet der Bootloader, statt etwas "
    + "zurückzusetzen — dieser Pin ist der Download-Strap. Zum Zurücksetzen wird die Taste "
    + "{after} dem Start der Platine gehalten.",
  "update.usb.strapEm": "nach",
};
