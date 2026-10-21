// Dutch translation of ../en/broker.ts. Typed against the English object: a key missing
// here, or one English does not have, is a tsc error in THIS file. Draft wording; the owner
// reviews it. Keep every {placeholder} verbatim.
import type { Message } from "../../format.ts";
import type { broker as source } from "../en/broker.ts";

export const broker: Record<keyof typeof source, Message> = {
  "broker.tone.pending": "Wachten",
  "broker.tone.denied": "Geweigerd",
  "broker.tone.error": "Probleem",
  "broker.tone.offline": "Geen antwoord",
  "broker.bootGrace":
    "Het apparaat draait nog geen vijf minuten; het huis komt mogelijk nog op gang.",
  "broker.absent.headline": "Het apparaat zei niets over een broker",
  "broker.absent.detail":
    "Dit antwoord bevat helemaal geen brokersectie — dat is iets anders dan dat elke "
    + "teller nul is.",
  "broker.off.headline": "MQTT staat uit",
  "broker.off.detail":
    "Er is geen brokeradres opgeslagen, dus Home Assistant kan dit apparaat niet zien. "
    + "Vul de broker in op de pagina Instellingen en sla op.",
  "broker.unknownWord.headline": "Het apparaat meldt een brokerstatus die deze pagina niet kent",
  "broker.unknownWord.detail":
    "Het statusdocument meldde link \"{link}\", failure \"{failure}\", subscriptions "
    + "\"{subscriptions}\". Een van die woorden kent deze pagina niet — de firmware heeft er "
    + "een bij gekregen, deze pagina niet.",
  "broker.subsRefused.headline": "De broker laat dit apparaat niet luisteren",
  "broker.subsRefused.detail":
    "Meetwaarden worden gepubliceerd, maar opdrachten uit Home Assistant komen niet aan: "
    + "de broker heeft de opdrachtabonnementen geweigerd. Dit is een ACL van de broker, "
    + "geen apparaatfout.",
  "broker.subsPending.headline": "Wachten op bevestiging van de broker",
  "broker.subsPending.detail": "Verbonden; de opdrachtabonnementen zijn nog niet bevestigd.",
  "broker.online.headline": "Verbonden met de broker",
  "broker.online.detail":
    "Gepubliceerde meetwaarden: {published}, aanvaarde opdrachten: {commands}. "
    + "Herverbindingen: {reconnects}.",
  "broker.connecting.headline": "Verbinden met de broker",
  "broker.connecting.detail":
    "Het apparaat belt {host} aan. Dat duurt normaal een seconde of twee.",
  "broker.retry": "Volgende poging over {secs} s.",
  "broker.dns.headline": "De naam van de broker wordt niet opgelost",
  "broker.dns.detail":
    "Het apparaat vroeg de router om {host} en kreeg geen antwoord. Controleer de "
    + "spelling, of vul in plaats daarvan het IP-adres van de broker in.",
  "broker.tcp.headline": "Op dat adres antwoordt niets",
  "broker.tcp.detail":
    "{host}:{port} nam geen verbinding aan. De broker staat misschien uit, of de poort "
    + "klopt niet.",
  "broker.tcp.retry": "Het apparaat blijft het proberen — volgende poging over {secs} s.",
  "broker.unavailable.headline": "De broker is nog niet klaar",
  "broker.unavailable.detail":
    "Hij antwoordde dat hij niet beschikbaar is — meestal een broker die nog aan het "
    + "opstarten is.",
  "broker.protocol.headline": "De broker heeft de verbinding geweigerd",
  "broker.protocol.detail":
    "Hij antwoordde \"{reason}\" (code {code}). Dit gaat niet over het wachtwoord: "
    + "controleer de instellingen van de broker zelf. Het apparaat probeert het elke vijf "
    + "minuten opnieuw.",
  "broker.badCredentials.headline": "De broker heeft de gebruikersnaam of het wachtwoord geweigerd",
  "broker.badCredentials.detail":
    "Hij antwoordde \"{reason}\" (code {code}). Corrigeer ze op de pagina Instellingen en "
    + "sla op — het apparaat probeert het meteen opnieuw, zonder de pauze van vijf minuten "
    + "af te wachten.",
  "broker.dropped.headline": "De verbinding met de broker valt steeds weg",
  "broker.dropped.detail":
    "Hij nam het apparaat aan en sloot daarna {disconnects} keer de verbinding. Dat "
    + "gebeurt als een ander apparaat dezelfde client-id gebruikt.",
  "broker.identityConflict":
    "{reconnects} herverbindingen tegenover {disconnects} verbrekingen: dat ze samen "
    + "oplopen is hoe het er van hieruit uitziet, want de broker kan het niet anders "
    + "melden.",
  "broker.notDialled.headline": "Het apparaat heeft de broker nog niet aangebeld",
  "broker.notDialled.detail":
    "Er is een broker opgeslagen en de verbinding is nog niet gestart. In de eerste "
    + "seconden na een herstart is dat normaal; als het blijft, is het een fout.",
  "broker.noReason.headline": "Het apparaat zei niet waarom de broker onbereikbaar is",
  "broker.noReason.detail":
    "De link is \"{link}\" en de fout is \"none\"; die kunnen niet allebei waar zijn. "
    + "Pogingen tot nu toe: {attempts}.",
  "broker.alert.noControl": "Home Assistant kan dit apparaat niet bedienen",
  "broker.alert.noData": "Home Assistant ontvangt geen gegevens van dit apparaat",
  "broker.alert.tail": "{detail}. Open Diagnose voor de tellers van de broker.",
  "broker.card.title": "Home Assistant & de broker",
  "broker.card.saved": "Opgeslagen broker:",
  "broker.card.none": "geen",
  "broker.card.stale": "De getallen hieronder zijn de laatst ontvangen, niet die van nu.",
  "broker.counter.link": "Link",
  "broker.counter.subscriptions": "Opdrachtabonnementen",
  "broker.counter.published": "Gepubliceerde meetwaarden",
  "broker.counter.commands": "Aanvaarde opdrachten",
  "broker.counter.rejected": "Geweigerde opdrachten",
  "broker.counter.reconnects": "Herverbindingen / verbrekingen",
  "broker.counter.attempts": "Pogingen sinds een stabiele sessie",
  "broker.counter.connack": "Laatste CONNACK",
  "broker.counter.errno": "Socketfoutnummer",
  "broker.counter.epoch": "Client-herbouwingen",
  "broker.move.offline.headline":
    "Op {old} kan niets worden opgeruimd — het apparaat is er niet mee verbonden",
  "broker.move.offline.line1":
    "Het apparaat kan niet intrekken wat het op {old} heeft gepubliceerd zolang het die "
    + "niet bereikt. De apparaatkaart en alle {count} entiteiten blijven staan in elke Home "
    + "Assistant die die broker leest, met de waarden van het laatste contact — en ze staan "
    + "er na het opslaan nog steeds.",
  "broker.move.offline.line2":
    "Opslaan wordt niet geblokkeerd. Een broker die niet antwoordt, mag u niet beletten "
    + "een instelling te wijzigen.",
  "broker.move.offline.line3":
    "Om ze met de hand op te ruimen zodra die broker terug is, voert u dit uit op een "
    + "machine die hem kan bereiken:",
  "broker.move.couldNotAsk":
    "Deze pagina kon niet lezen of het apparaat nu met {old} verbonden is, en gaat er "
    + "daarom van uit dat het zo is. Is dat niet zo, dan wijzigt opslaan alleen het adres "
    + "en blijft alles op {old} onaangeroerd.",
  "broker.move.connected": "Het apparaat is nu met {old} verbonden.",
  "broker.move.confirm": "Verwijderen en opslaan",
  "broker.move.headline":
    "Opslaan verwijdert dit apparaat en zijn {count} entiteiten uit Home Assistant op "
    + "{old}",
  "broker.move.line1":
    "Door het adres te wijzigen trekt het alles in wat het daar heeft gepubliceerd "
    + "voordat het vertrekt: {count} lege discoveryberichten, waarna Home Assistant de "
    + "apparaatkaart en alle {count} entiteiten uit zijn register verwijdert.",
  "broker.move.line2":
    "Hun entity_ids gaan mee, en dat deel kan niet ongedaan worden gemaakt. Dashboards, "
    + "automatiseringen, scripts, templates en langetermijnstatistieken die die entity_ids "
    + "noemen, werken niet meer — zonder ergens een foutmelding. Het apparaat daarna weer "
    + "op {old} richten maakt de entiteiten opnieuw aan, maar niet per se onder dezelfde "
    + "entity_ids en nooit met hun geschiedenis.",
  "broker.move.line3":
    "Alleen het brokeradres doet dit. Het topicvoorvoegsel, de apparaatnaam en de "
    + "discoveryschakelaar kunnen allemaal zonder dit worden gewijzigd — die behouden de "
    + "entiteiten, hun entity_ids en hun geschiedenis.",
};
