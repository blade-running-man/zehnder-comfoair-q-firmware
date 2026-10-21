// Dutch translation of ../en/settings.ts. Typed against the English object: a key missing here,
// or one English does not have, is a tsc error in THIS file. Keep every {placeholder} verbatim.
// The settings.detail.* block translates the firmware's own error sentences (firmwareText.ts);
// it keeps the device's lowercase sentence starts where the English has them.
import type { Message } from "../../format.ts";
import type { settings as source } from "../en/settings.ts";

export const settings: Record<keyof typeof source, Message> = {
  "settings.title": "Instellingen",
  "settings.readOnly.headline": "Deze instellingen zijn geschreven door een nieuwere firmware",
  "settings.readOnly.detail":
    "Deze build begrijpt de indeling van de opgeslagen configuratie niet en weigert daarom "
    + "die te overschrijven in plaats van te beschadigen. Installeer de firmware die dit "
    + "apparaat eerder had opnieuw, of wis de instellingen met de knop, voordat u hier iets "
    + "wijzigt.",
  "settings.load.hint":
    "De broker- en apparaatinstellingen kunnen pas worden getoond als ze gelezen kunnen "
    + "worden. Het wififormulier hierboven hangt daar niet van af.",
  "settings.tryAgain": "Opnieuw proberen",
  "settings.loading": "Configuratie van het apparaat wordt gelezen…",
  "settings.save": "Instellingen opslaan",
  "settings.saved": "Instellingen opgeslagen",
  "settings.saveBar.hint": "Slaat de broker- en apparaatinstellingen samen op. Het netwerk wordt niet aangeraakt.",
  "settings.notSent": "Niet verzonden — er heeft niets deze pagina verlaten",

  "settings.broker.title": "MQTT-broker",
  "settings.broker.subtitle": "Waar het apparaat publiceert, en waar Home Assistant het vindt.",
  "settings.broker.host.label": "Broker-host",
  "settings.broker.host.placeholder": "192.168.1.10 of homeassistant.local",
  "settings.broker.port.label": "Broker-poort",
  "settings.broker.port.placeholder": "1883",
  "settings.broker.username.label": "Broker-gebruikersnaam",
  "settings.broker.password.label": "Broker-wachtwoord",
  "settings.broker.password.warning":
    "Deze verbinding is in deze versie niet versleuteld, dus dit wachtwoord is zichtbaar "
    + "voor alles op uw netwerk. Hergebruik geen wachtwoord dat u ook elders gebruikt.",
  "settings.broker.topicPrefix.label": "Topic-prefix",
  "settings.broker.discovery.label": "Home Assistant-discovery publiceren",

  "settings.device.title": "Apparaat",
  "settings.device.subtitle": "Hoe het heet, hoe het zich gedraagt en wie het mag wijzigen.",
  "settings.device.name.label": "Apparaatnaam",
  "settings.device.name.placeholder": "Ventilatie",
  "settings.device.name.hint":
    "Alleen een weergavenaam. De identiteit van het apparaat — zijn broker-client-id en "
    + "zijn Home Assistant-apparaat-id — wordt afgeleid van zijn MAC-adres, dus hernoemen "
    + "hier kan geen tweede apparaat in Home Assistant opleveren.",
  "settings.device.password.label": "Wachtwoord voor de webinterface",
  "settings.device.password.warnTitle": "Er is geen \"wachtwoord vergeten\" voor dit apparaat.",
  "settings.device.password.warn2a": "De knop ingedrukt houden ",
  "settings.device.password.warn2em": "terwijl",
  "settings.device.password.warn2b":
    " het apparaat opstart doet iets heel anders — het zet de chip in zijn firmware-lader, "
    + "wat er precies uitziet als een apparaat dat kapot is. Laat het eerst opstarten, wacht, "
    + "houd dan pas in.",
  "settings.device.password.noneSet":
    "Er is geen wachtwoord ingesteld. Iedereen die dit apparaat op het netwerk kan "
    + "bereiken, kan deze instellingen wijzigen — een redelijke keuze voor een apparaat op "
    + "een thuisnetwerk, en bewust de standaardinstelling.",

  "settings.wifi.title": "Wifi",
  "settings.wifi.subtitle": "De lijst toont wat dit apparaat kan horen, niet wat uw telefoon toont.",
  "settings.wifi.network.label": "Netwerk",
  "settings.wifi.network.choose": "— kies een netwerk —",
  "settings.wifi.network.currentlyConfigured": "momenteel geconfigureerd",
  "settings.wifi.network.open": "open",
  "settings.wifi.network.manualOption": "Ander / verborgen netwerk — typ de naam…",
  "settings.wifi.network.ssidLabel": "Netwerknaam (SSID)",
  "settings.wifi.network.ssidPlaceholder": "precies zoals de router hem uitzendt",
  "settings.wifi.scan.again": "Opnieuw scannen",
  "settings.wifi.scan.start": "Zoeken naar netwerken",
  "settings.wifi.chooseFromList": "Kiezen uit de lijst",
  "settings.wifi.typeInstead": "Naam in plaats daarvan typen",
  "settings.wifi.scan.hint":
    "Een naam die op uw telefoon staat maar niet in deze lijst, is de gebruikelijke reden "
    + "waarom een apparaat een duidelijk aanwezig netwerk \"niet kan vinden\": de meeste "
    + "routers zenden één naam uit op beide banden, en dit apparaat heeft geen 5 GHz-radio. "
    + "Typ hem in dat geval in — verborgen netwerken verschijnen in geen enkele scan, ze "
    + "kunnen alleen worden getypt.",
  "settings.wifi.password.label": "Wifi-wachtwoord",
  "settings.wifi.password.fallbackNetworkName": "het opgeslagen netwerk",
  "settings.wifi.notice.newNetworkFallback": "het nieuwe netwerk",
  "settings.wifi.password.hintUnencrypted":
    "Als u zich op het eigen installatienetwerk van het apparaat bevindt, is dat netwerk "
    + "open: deze sleutel steekt het eenmalig onversleuteld over. Niets in een via gewoon "
    + "HTTP geserveerde browserpagina kan dat voorkomen.",
  "settings.wifi.network.openHint": "{ssid} is open — laat het wachtwoordveld leeg.",
  "settings.wifi.network.securedWarning":
    "{ssid} is beveiligd en het wachtwoordveld is leeg. Het apparaat zal proberen te "
    + "verbinden en zal worden geweigerd.",
  "settings.wifi.beforeYouPress": "Voordat u hierop drukt",
  "settings.wifi.save": "Wifi opslaan en opnieuw verbinden",
  "settings.wifi.accepted": "Geaccepteerd — het apparaat probeert het nu",

  "settings.wifi.signal.notSeen": "niet gezien in deze scan",
  "settings.wifi.signal.strong": "sterk",
  "settings.wifi.signal.good": "goed",
  "settings.wifi.signal.weak": "zwak",
  "settings.wifi.signal.veryWeak": "zeer zwak",

  "settings.wifi.key.stored":
    "De sleutel die al voor {ssid} is opgeslagen, wordt opnieuw gebruikt. Hij wordt nooit "
    + "teruggestuurd naar deze pagina, dus het veld is leeg; typ om hem te vervangen.",
  "settings.wifi.key.empty":
    "Leeg: het apparaat probeert zonder sleutel te verbinden. Correct voor een open "
    + "netwerk, en voor niets anders.",
  "settings.wifi.key.typed": "Deze sleutel wordt zoals getypt naar het apparaat verzonden.",

  "settings.wifi.notice.line1":
    "Het apparaat beantwoordt dit formulier voordat het begint te verbinden, dus een "
    + "bevestiging hier betekent dat de gegevens zijn geaccepteerd — niet dat {target} ze "
    + "heeft overgenomen.",
  "settings.wifi.notice.line2":
    "Terwijl het probeert, delen het installatienetwerk waarop u nu zit en {target} één "
    + "radio. Deze pagina zal hoogstwaarschijnlijk binnen enkele seconden niet meer reageren "
    + "— dat is hier het normale resultaat, geen storing.",
  "settings.wifi.notice.line3":
    "Daarna bevindt het apparaat zich op {target}, met welk adres uw router het ook "
    + "toewijst. De lijst met verbonden clients van uw router is de plek om te zoeken.",
  "settings.wifi.notice.fallback":
    "Als de sleutel onjuist is, zet het apparaat {previous} zelf terug en keert daarnaar "
    + "terug — het is het laatste netwerk dat dit apparaat daadwerkelijk een adres gaf, wat "
    + "het de enige reden geeft om ernaar terug te keren. Er hoeft niets gereset te worden.",
  "settings.wifi.notice.noFallback":
    "Dit apparaat heeft nog nooit een netwerk bereikt, dus er is niets om naar terug te "
    + "keren. Als de sleutel onjuist is, blijft het proberen, en het eigen "
    + "installatienetwerk blijft ongeveer 15 minuten actief vanaf het moment dat het "
    + "verscheen. Daarna is het installatienetwerk weg totdat het apparaat opnieuw wordt "
    + "gestart, wat het voor een paar minuten terugbrengt.",

  "settings.secret.stored.text":
    "Opgeslagen op het apparaat. Wordt nooit teruggestuurd, dus het veld is leeg; typ om "
    + "het te vervangen.",
  "settings.secret.stored.placeholder": "ongewijzigd — typ om te vervangen",
  "settings.secret.willClear.text": "Geleegd. Opslaan verwijdert de opgeslagen waarde.",
  "settings.secret.willClear.placeholder": "leeg — opslaan verwijdert de opgeslagen waarde",
  "settings.secret.willReplace.text": "Opslaan vervangt de opgeslagen waarde.",
  "settings.secret.notSet.text": "Niet ingesteld.",
  "settings.secret.notSet.placeholder": "niet ingesteld",
  "settings.secret.willSet.text": "Opslaan stelt deze waarde voor het eerst in.",
  "settings.error.portRange": "De broker-poort moet een geheel getal tussen 1 en 65535 zijn.",

  "settings.error.notImplemented.headline": "Het apparaat heeft deze route, maar er zit nog geen code achter",
  "settings.error.noHandler.headline": "Deze firmware-versie bedient dat eindpunt niet",
  "settings.error.passwordNeeded.headline": "Het apparaat wil zijn wachtwoord voor de webinterface",
  "settings.error.writeRefused.headline": "Het apparaat heeft dit schrijven geweigerd",
  "settings.error.badRequest.headline": "Het apparaat wilde dit niet accepteren",
  "settings.error.fault.headline": "Het apparaat meldt een storing",
  "settings.error.browserGaveNoReason": "De browser gaf geen reden.",
  "settings.error.disconnectExpected.headline": "Geen antwoord — en dat is hier precies het succes",
  "settings.error.disconnectExpected.detail":
    "Het apparaat antwoordt voordat het zijn radio opnieuw afstemt, dus een verzoek dat "
    + "halverwege afbreekt betekent meestal dat het al bezig is. ({browserSaid})",
  "settings.error.offline.headline": "Geen antwoord van het apparaat",
  "settings.error.offline.detail": "Niets heeft het apparaat bereikt, dus het heeft hier niets over gezegd. ({browserSaid})",

  "settings.detail.ssid": "de netwerknaam moet 1 tot 32 tekens lang zijn",
  "settings.detail.psk":
    "het netwerkwachtwoord moet 8 tot 63 tekens lang zijn, precies 64 hexadecimale "
    + "cijfers, of leeg voor een open netwerk",
  "settings.detail.wifiPair":
    "de netwerknaam en het wachtwoord worden als één record opgeslagen: stuur beide, en "
    + "voer het wachtwoord opnieuw in zodra de naam verandert",
  "settings.detail.host":
    "het brokeradres moet een hostnaam of een IP-adres zijn, zonder schema en zonder "
    + "poort",
  "settings.detail.port": "de brokerpoort moet tussen 1 en 65535 liggen",
  "settings.detail.user": "de gebruikersnaam voor de broker is langer dan dit apparaat kan opslaan",
  "settings.detail.brokerPassword": "het brokerwachtwoord is langer dan dit apparaat kan opslaan",
  "settings.detail.prefix":
    "het topic-prefix moet 1 tot 64 tekens lang zijn, zonder spaties, + of #, en zonder "
    + "leeg niveau",
  "settings.detail.name":
    "de apparaatnaam moet 1 tot 32 tekens lang zijn en mag geen stuurtekens bevatten",
  "settings.detail.uiPassword":
    "het webwachtwoord moet 8 tot 128 tekens lang zijn en mag geen stuurtekens bevatten",
  "settings.detail.noHash":
    "dit apparaat kon het wachtwoord zojuist niet beveiligen; er is niets gewijzigd",
  "settings.detail.readOnly":
    "deze instellingen zijn door een nieuwere firmware geschreven en kunnen pas worden "
    + "gewijzigd nadat het apparaat is gereset",
  "settings.detail.noDocument": "het verzoek bevatte geen instellingen, dus er is niets opgeslagen",
  "settings.detail.refused": "de instellingen zijn geweigerd",
  "settings.detail.badBody":
    "De inhoud van het verzoek is geen JSON-object dat dit apparaat kan lezen.",
  "settings.detail.badField":
    "Een veld ontbreekt, heeft het verkeerde type of is langer dan dit apparaat kan "
    + "opslaan.",
  "settings.detail.wrongRoute": "Het netwerk wordt ingesteld via POST /api/provision, niet hier.",
  "settings.detail.notApplied": "Het verzoek kon niet worden toegepast.",
  "settings.detail.unauthorized": "niet geautoriseerd",
  "settings.detail.forbidden": "niet toegestaan",
  "settings.detail.notImplemented": "nog niet geïmplementeerd",
  "settings.detail.noEndpoint": "dit eindpunt bestaat niet",
  "settings.detail.bodyTooLarge": "inhoud van het verzoek te groot",
  "settings.detail.truncatedBody": "inhoud van het verzoek onvolledig",
  "settings.detail.scanUnavailable": "scan niet beschikbaar",
  "settings.detail.scanTooLarge": "scanresultaat past niet in het antwoord",
  "settings.detail.busy": "bezet",
  "settings.detail.statusTooLarge": "status past niet in het antwoord",
  "settings.detail.configTooLarge": "configuratie past niet in het antwoord",
  "settings.detail.emptyBody": "Het apparaat antwoordde {1} met een lege inhoud.",
  "settings.detail.unreadableBody": "Het apparaat antwoordde {1} met een inhoud die deze pagina niet kan lezen.",

  // --- broker/device notes, the reset-button ladder, the Connection card (status.ts) ---
  "settings.broker.topicPrefix.hint":
    "Het voorvoegsel wijzigen verplaatst elk status-topic. De entiteiten in Home "
    + "Assistant volgen en behouden hun geschiedenis — wat ze voor Home Assistant "
    + "beschrijft bevat het voorvoegsel niet, dus het wordt herschreven in plaats van "
    + "vervangen, en u ziet geen tweede set. Is het apparaat bij het opslaan met de broker "
    + "verbonden, dan leegt het onderweg de topics onder het oude voorvoegsel; zo niet, dan "
    + "blijven die daar staan als retained berichten die niemand meer leest.",
  "settings.broker.discovery.note":
    "Uit: het apparaat publiceert niet langer wat het voor Home Assistant beschrijft, "
    + "maar trekt niet in wat het eerder publiceerde. Het apparaat en zijn entiteiten "
    + "blijven daar en worden verder bijgewerkt — ze opruimen betekent die retained "
    + "berichten op de broker met de hand verwijderen.",
  "settings.device.permanentManual.label": "Ventilatiestand vasthouden",
  "settings.device.permanentManual.note":
    "Aan: een hier of in Home Assistant gekozen stand blijft staan. Uit: de unit keert "
    + "binnen twee uur terug naar AUTO, en de vocht- en temperatuurregelingen blijven "
    + "actief — de handmatige modus schakelt die uit.",
  "settings.device.button.p1":
    "De weg terug is de knop op het apparaat, en hoe lang u die vasthoudt is alles. Druk "
    + "erop {after} en let op het lampje: het wordt wit zodra de druk wordt opgemerkt, "
    + "krijgt tussen één en vijf seconden weer zijn gewone kleur, wordt amber bij vijf en "
    + "rood bij vijftien seconden, en wordt feller naarmate de volgende stap nadert.",
  "settings.device.button.p1em": "nadat het apparaat is opgestart",
  "settings.device.button.p2":
    "{short} en er wordt niets gewist: het zet een paar minuten zijn eigen netwerk {ap} "
    + "in de lucht. Handig als het apparaat de wifi van het huis kwijt is — maar dit "
    + "wachtwoord geldt daar nog steeds. {gap} en er gebeurt helemaal niets. Die "
    + "tussenruimte is bewust: een stoot of een mouw kan niets wissen.",
  "settings.device.button.p2short": "Laat los binnen één seconde",
  "settings.device.button.p2gap": "Laat los tussen één en vijf seconden",
  "settings.device.button.p3":
    "{soft}, terwijl het lampje amber is, en het wist de wifi-gegevens en dit wachtwoord "
    + "samen — en behoudt de brokerinstellingen, het topicvoorvoegsel, de apparaatnaam en "
    + "de duren. Het knippert drie keer, herstart en komt terug met datzelfde netwerk in de "
    + "lucht en deze pagina voor iedereen open.",
  "settings.device.button.p3strong": "Laat los na vijf seconden",
  "settings.device.button.p4":
    "{hard}, tot in het rood, en het wist in plaats daarvan alles, inclusief de "
    + "brokerinstellingen: die stap is voor een apparaat dat het huis verlaat. Het ruimt op "
    + "de broker niet achter zich op, dus Home Assistant houdt het apparaat en zijn "
    + "entiteiten — richt het apparaat eerst op een andere broker, zolang het deze nog "
    + "bereikt, als u ze weg wilt hebben. Na vijfentwintig seconden besluit het dat de knop "
    + "vastzit en doet het helemaal niets; laat los en begin opnieuw.",
  "settings.device.button.p4strong": "Blijf langer dan vijftien seconden vasthouden",
  "settings.status.title": "Verbinding",
  "settings.status.noKnownGood":
    "Dit netwerk heeft nog nooit een adres opgeleverd, dus er is niets om naar terug te "
    + "keren als het volgende mislukt.",
  "settings.status.failure.notFound":
    "Dat netwerk is niet gevonden. Controleer de naam, en dat het een 2,4 GHz-netwerk is "
    + "— dit apparaat heeft geen 5 GHz-radio.",
  "settings.status.failure.wrongPassword":
    "Het netwerk weigerde de sleutel. Typ hem opnieuw; de opgeslagen sleutel wordt nooit "
    + "teruggetoond.",
  "settings.status.failure.refused":
    "De router antwoordde en wees het apparaat af. Het toegangspunt is misschien vol, of "
    + "het is een enterprise-netwerk dat dit formulier niet kan uitdrukken.",
  "settings.status.failure.noAddress":
    "Het apparaat werd lid van het netwerk maar kreeg nooit een adres. Dat is de DHCP van "
    + "de router, niet het wachtwoord.",
  "settings.status.failure.unknown":
    "Het apparaat meldt een fout waarvoor deze pagina geen omschrijving heeft: {failure} "
    + "(reden {reason}).",
  "settings.status.where.connected": "Op {ssid}, op {ip}.",
  "settings.status.where.setupAp":
    "Biedt zijn eigen installatienetwerk aan. Iedereen binnen bereik kan het bereiken, "
    + "dus schrijven wordt geweigerd totdat het lid wordt van een netwerk.",
  "settings.status.where.trying": "Probeert lid te worden van {ssid}.",
  "settings.status.where.none": "Er is geen netwerk ingesteld.",
};
