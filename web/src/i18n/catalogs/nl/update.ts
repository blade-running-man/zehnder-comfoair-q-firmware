// Dutch translation of ../en/update.ts. Typed against the English object: a key missing here,
// or one English does not have, is a tsc error in THIS file. Keep every {placeholder} verbatim.
import type { Message } from "../../format.ts";
import type { update as source } from "../en/update.ts";

export const update: Record<keyof typeof source, Message> = {
  "update.heading": "Firmware-update",
  "update.title": "Firmware installeren",
  "update.instructions": "Kies de firmware.bin die voor dit apparaat is gebouwd (.pio/build/<env>/firmware.bin). Het apparaat controleert het bestand, schrijft het naar zijn vrije slot en start opnieuw op.",
  "update.fileAria": "Firmwarebestand (.bin) om te installeren",
  "update.file": "{name} · {kib} KiB",
  "update.install": "Installeren",
  "update.uploading": "Uploaden… {percent} % ({sent} van {total} KiB)",
  "update.uploadDone": "Upload voltooid; het apparaat installeert het image.",
  "update.restarting": "Installeren… het apparaat start opnieuw op.",
  "update.uncertain": "De verbinding viel weg nadat het hele bestand was verzonden. Er wordt gewacht of het apparaat het heeft geïnstalleerd.",
  "update.waiting": "Wachten tot het apparaat weer antwoordt ({seconds} s).",
  "update.updated": "Bijgewerkt: {before} → {after}",
  "update.backRunning": "Het apparaat is terug en draait {after}.",
  "update.confirmNote": "De nieuwe firmware bevestigt zichzelf na 30 s storingsvrij draaien; faalt ze daarvoor, dan keert het apparaat automatisch terug naar de vorige versie.",
  "update.same": "Het apparaat is terug, maar draait nog steeds {version}. De update is mogelijk teruggedraaid.",
  "update.systemLink": "De pagina Systeem toont welke partitie draait.",
  "update.openSystem": "Systeem openen",
  "update.gaveUp": "Het apparaat antwoordt al 90 s niet. Het start mogelijk nog op; herlaad deze pagina over een minuut en kijk op de pagina Systeem zodra het antwoordt.",
  "update.noPassword": "Firmware-updates vereisen een apparaatwachtwoord. Stel er een in bij Instellingen en kom dan terug.",
  "update.noPasswordWhy": "Zonder wachtwoord kan iedereen op uw netwerk eigen code op dit apparaat installeren.",
  "update.openSettings": "Instellingen openen",
  "update.login": "Dit apparaat heeft een wachtwoord. Log in wanneer de browser erom vraagt en probeer het opnieuw.",
  "update.refused": "Het apparaat weigerde de update: {detail}",
  "update.failed": "Upload mislukt — de verbinding met het apparaat is verbroken.",
  "update.toastDone": "Firmware bijgewerkt",
  "update.compare": "Geïnstalleerd: {installed} → Dit bestand: {image}",
  "update.fileVersion": "Dit bestand: {image}",
  "update.verdict.newer": "Dit is een nieuwere versie",
  "update.verdict.same": "Dit is dezelfde versie (herinstallatie)",
  "update.verdict.older": "Dit is een OUDERE versie — het apparaat wordt gedowngraded. De instellingen blijven behouden.",

  "update.detail.noLength": "De upload gaf de bestandsgrootte niet op; verstuur hem met een Content-Length.",
  "update.detail.tooSmall": "Het bestand is te klein om een firmware-image te zijn.",
  "update.detail.tooLarge": "Het bestand is groter dan het update-slot.",
  "update.detail.notImage": "Dit is geen firmware-image; kies firmware.bin uit .pio/build/<env>/, niet de .elf of een gecomprimeerd bestand.",
  "update.detail.wrongChip": "Deze firmware is gebouwd voor een andere chip.",
  "update.detail.truncated": "De upload stopte voordat het hele bestand was aangekomen; probeer het opnieuw.",
  "update.detail.verifyFailed": "Het geüploade image is niet door de controle gekomen en is niet geïnstalleerd.",
  "update.detail.busy": "Er wordt al een andere update geüpload.",
  "update.detail.flashError": "Het schrijven van de update naar flash is mislukt; de draaiende firmware is ongewijzigd.",
  "update.detail.noSlot": "Dit apparaat heeft geen partitie om een update in te installeren.",
  "update.detail.unconfirmed": "De draaiende firmware is net bijgewerkt en wordt nog gecontroleerd; probeer het over een minuut opnieuw.",
  "update.detail.wrongProject": "Dit bestand is firmware voor een ander apparaat, niet voor de ComfoAir Q-controller.",

  // --- the USB fallback card ---
  "update.usb.title": "Via USB",
  "update.usb.body":
    "Als de browser het apparaat niet bereikt, of bij een eerste installatie, wordt het "
    + "via USB geflasht:",
  "update.usb.strap":
    "De knop ingedrukt houden terwijl het bord stroom krijgt, start de bootloader in "
    + "plaats van iets te resetten — die pin is de download-strap. Voor een reset houdt u "
    + "de knop ingedrukt {after} het bord is opgestart.",
  "update.usb.strapEm": "nadat",
};
