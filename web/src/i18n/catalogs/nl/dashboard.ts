// Dutch translation of ../en/dashboard.ts. Typed against the English object: a key missing here,
// or one English does not have, is a tsc error in THIS file. Keep every {placeholder} verbatim.
// Draft wording; the owner reviews it.
import type { Message } from "../../format.ts";
import type { dashboard as source } from "../en/dashboard.ts";

export const dashboard: Record<keyof typeof source, Message> = {
  "dashboard.title.suffix": "· Overzicht",
  "dashboard.tagline": "Uw ventilatie met warmteterugwinning in één oogopslag — wat er "
    + "binnenkomt, waar de lucht heen gaat en hoe u die stuurt.",
  "dashboard.conn.live": "Live",
  "dashboard.conn.waiting": "Verbonden, wacht op de eerste meting",
  "dashboard.conn.connecting": "Verbinden…",
  "dashboard.conn.closed": "Verbinding verbroken — de waarden hieronder kunnen verouderd zijn",

  "dashboard.season.heating": "Stookseizoen",
  "dashboard.season.cooling": "Koelseizoen",
  "dashboard.season.mild": "Tussenseizoen",

  "dashboard.hero.stopped.status": "Toevoer gepauzeerd — er komt niets binnen",
  "dashboard.hero.stopped.line": "De toevoerventilator staat stil, er komt geen verse lucht "
    + "binnen. De afzuiging loopt nog.",
  "dashboard.hero.bypass.status": "Koelt uw huis met nachtlucht",
  "dashboard.hero.bypass.line": "Buiten is het {temp} en de bypass staat open, dus deze lucht "
    + "kwam langs de warmtewisselaar binnen — het koelt het huis in plaats van het te verwarmen.",
  "dashboard.hero.warmed.status": "Verse lucht, onderweg opgewarmd",
  "dashboard.hero.warmed.line": "Buiten is het {temp}. De warmtewisselaar won {lift} terug van de "
    + "{avail} °C warmte die beschikbaar was.",
  "dashboard.hero.tempered.status": "Verse lucht, licht getemperd",
  "dashboard.hero.tempered.line": "Buiten is het {temp}. De warmtewisselaar bracht de "
    + "binnenkomende lucht dichter bij de temperatuur van uw kamers.",
  "dashboard.hero.near.status": "Verse lucht, dicht bij de buitentemperatuur",
  "dashboard.hero.near.line": "Buiten is het {temp}. Binnen en buiten liggen zo dicht bij elkaar "
    + "dat er weinig warmte terug te winnen is.",
  "dashboard.hero.waiting.status": "Wacht op de eerste meting",
  "dashboard.hero.waiting.line": "Het dashboard is verbonden en luistert naar het toestel.",
  "dashboard.hero.intoRooms": "naar uw kamers",

  "dashboard.chip.supplyStopped": "Toevoerventilator gestopt",
  "dashboard.chip.bypassOpen": "Bypass open",
  "dashboard.chip.filtersOverdue": "Filters over tijd",
  "dashboard.chip.filtersDue": "Filters over {days} d",

  "dashboard.mode.label": "Ventilatiemodus",
  "dashboard.mode.choose": "Kies hoeveel lucht",
  "dashboard.mode.help.Auto": "het toestel regelt het debiet zelf — laat het hier staan",
  "dashboard.mode.help.Away": "minimale lucht terwijl het huis leeg is",
  "dashboard.mode.help.Low": "stille achtergrondventilatie",
  "dashboard.mode.help.Medium": "dagelijkse verse lucht voor een bewoond huis",
  "dashboard.mode.help.High": "meer lucht voor een volle of vochtige kamer",
  "dashboard.mode.help.Boost": "maximale lucht voor een vaste tijd, daarna terug naar het schema",
  "dashboard.stat.airflow": "Debiet",
  "dashboard.stat.power": "Vermogen",
  "dashboard.stat.recovered": "Warmte teruggewonnen",
  "dashboard.stat.bypassed": "Bewust omzeild",

  "dashboard.lift.title": "Warmtewinst",
  "dashboard.lift.bypassed": "Omzeild",
  "dashboard.lift.nothing": "er komt niets binnen",
  "dashboard.lift.skips": "verse lucht omzeilt de warmtewisselaar",
  "dashboard.lift.warmer": "warmer dan buiten",
  "dashboard.lift.cooler": "koeler dan buiten",
  "dashboard.lift.aria.nothing": "Warmtewinst: er komt niets binnen.",
  "dashboard.lift.aria.bypassed": "Warmtewinst: omzeild — verse lucht omzeilt de "
    + "warmtewisselaar.",
  "dashboard.lift.aria.warmed": "Warmtewinst: de binnenkomende lucht werd {lift} °C opgewarmd, "
    + "van {outdoor} buiten richting {extract} in uw kamers.",
  "dashboard.lift.outside": "buiten",
  "dashboard.lift.outsideAt": "{temp} buiten",
  "dashboard.lift.rooms": "uw kamers",
  "dashboard.lift.roomsAt": "{temp} uw kamers",

  "dashboard.bypass.title": "Waar de lucht heen gaat",
  "dashboard.bypass.key": "Bypass",
  "dashboard.bypass.bypassed": "Warmtewisselaar omzeild",
  "dashboard.bypass.recovered": "{pct}% warmte teruggewonnen",
  "dashboard.bypass.exchange": "Warmte-uitwisseling",
  "dashboard.bypass.supplyStopped": "Toevoer gestopt",
  "dashboard.bypass.summaryOpen": "Bypass open: buitenlucht {outdoor} omzeilt de "
    + "warmtewisselaar; kamerlucht {extract} vertrekt met {exhaust}.",
  "dashboard.bypass.summaryClosed": "Tegenstroomwarmtewisselaar: buitenlucht {outdoor} wordt "
    + "onderweg naar binnen opgewarmd tot {supply}; kamerlucht {extract} wordt onderweg naar "
    + "buiten afgekoeld tot {exhaust}.",
  "dashboard.bypass.outdoorAir": "Buitenlucht",
  "dashboard.bypass.exhaust": "Uitblaas",
  "dashboard.bypass.fromRooms": "Uit de kamers",
  "dashboard.bypass.toRooms": "Naar de kamers",
  "dashboard.bypass.zoneOut": "Buiten",
  "dashboard.bypass.zoneHome": "Uw huis",
  "dashboard.bypass.fanSupply": "Toevoer",
  "dashboard.bypass.fanExtract": "Afvoer",
  "dashboard.bypass.supplyFlow": "Toevoerdebiet",
  "dashboard.bypass.exhaustFlow": "Uitblaasdebiet",
  "dashboard.bypass.outdoorMean": "Buitengemiddelde",
  "dashboard.bypass.set": "Bypass instellen",
  "dashboard.bypass.modeAria": "Bypassmodus",

  "dashboard.filters.title": "Filters",
  "dashboard.filters.unknown": "Nog geen meting",
  "dashboard.filters.good": "Filters zijn in orde",
  "dashboard.filters.soon": "Bestel vervanging",
  "dashboard.filters.overdue": "Vervang de filters",
  "dashboard.filters.tagOutdoor": "Buitenlucht",
  "dashboard.filters.tagExtract": "Afvoer",
  "dashboard.filters.days": "dagen",
  "dashboard.filters.daysLeft": { one: "dag over", other: "dagen over" },
  "dashboard.filters.daysOverdue": { one: "dag over tijd", other: "dagen over tijd" },
  "dashboard.filters.fitted": "Geplaatst",
  "dashboard.filters.due": "Vervangen",
  "dashboard.filters.noteOverdue": "Beide filters — buitenlucht en afvoer — worden samen "
    + "vervangen, en het toestel telt niet verder zodra het nul passeert.",
  "dashboard.filters.noteCycle": "Beide filters — buitenlucht en afvoer — worden samen "
    + "vervangen, volgens de cyclus van {cycle} dagen van het toestel.",

  "dashboard.comfort.title": "Comfort & temperatuur",
  "dashboard.comfort.seasonNote": "Automatisch ingesteld — het toestel volgt het 5-daagse "
    + "gemiddelde buiten, dus het seizoen kiest u niet zelf.",
  "dashboard.comfort.seasonNoteMean": "Automatisch ingesteld — het toestel volgt het 5-daagse "
    + "gemiddelde buiten ({mean}), dus het seizoen kiest u niet zelf.",
  "dashboard.comfort.profile": "Temperatuurprofiel",
  "dashboard.comfort.aiming": "Doel: {temp}",
  "dashboard.comfort.profileHint": "Koelere of warmere toevoerlucht. Het toestel is geen "
    + "thermostaat — het verschuift zijn eigen doel, een exacte kamertemperatuur houdt het niet "
    + "vast.",
  "dashboard.comfort.passive.label": "Passieve koeling",
  "dashboard.comfort.passive.help": "Laat koele buitenlucht de temperatuur helpen verlagen als "
    + "het seizoen het toelaat.",
  "dashboard.comfort.humidity.label": "Vochtcomfort",
  "dashboard.comfort.humidity.help": "Past het debiet aan om de luchtvochtigheid binnen "
    + "comfortabel te houden.",
  "dashboard.comfort.protection.label": "Vochtbescherming",
  "dashboard.comfort.protection.help": "Verhoogt het debiet om de vochtigheid laag te houden en "
    + "condensatie te voorkomen.",

  "dashboard.timers.title": "Timers",
  "dashboard.timers.sub": "Hoe lang elke tijdelijke modus duurt. Stel het in voordat u er een "
    + "start; het toestel telt af.",
  "dashboard.timers.boost.running": "Boost",
  "dashboard.timers.boost.what": "Hoe lang Boost op vol debiet draait voordat het toestel "
    + "terugvalt op zijn schema.",
  "dashboard.timers.boost.idle": "Bepaalt hoe lang de volgende Boost duurt.",
  "dashboard.timers.bypass.running": "Bypass-overschrijving",
  "dashboard.timers.bypass.what": "Hoe lang een handmatig Open of Gesloten geldt voordat de bypass "
    + "de regeling teruggeeft aan Auto.",
  "dashboard.timers.bypass.idle": "Bepaalt hoe lang een handmatige bypass geldt.",
  "dashboard.timers.away.running": "Afwezigheidsmodus",
  "dashboard.timers.away.what": "Negeert elk schema zolang het huis leeg is — voor een vakantie, "
    + "niet de snelle Afwezig-stand hierboven. Zonder einde blijft hij aan tot u hem beëindigt.",
  "dashboard.timers.away.idle": "Bepaalt hoe lang de afwezigheidsmodus duurt zodra u hem start.",
  "dashboard.timers.noEnd": "Geen einde ingesteld",
  "dashboard.timers.runsUntil": "{mode} loopt tot u hem beëindigt",
  "dashboard.timers.left": "nog {time}",
  "dashboard.timers.isRunning": "{mode} loopt",
  "dashboard.timers.notRunning": "Niet actief",
  "dashboard.timers.endNow": "Nu beëindigen",
  "dashboard.timers.startAway": "Afwezig starten",
  "dashboard.timers.untilEnd": "Tot ik hem beëindig",
  "dashboard.timers.inMinutes": "{name} in minuten",
  "dashboard.timers.min": "min",

  "dashboard.dur.indefinite": "Onbeperkt",
  "dashboard.dur.min": "{n} min",
  "dashboard.dur.days": { one: "{count} dag", other: "{count} dagen" },
  "dashboard.dur.h": "{h} u",
  "dashboard.dur.hm": "{h} u {m} m",
  "dashboard.dur.dh": "{d} d {h} u",
  "dashboard.dur.s": "{n} s",

  "dashboard.toast.notApplied": "Niet toegepast: {msg}",

  "dashboard.guide.title": "Uw ventilatie gebruiken",
  "dashboard.guide.lede": "U hoeft er zelden aan te komen — maar hier ziet u wanneer elke "
    + "bediening de moeite waard is.",
  "dashboard.guide.auto.title": "Laat het op Auto",
  "dashboard.guide.auto.body": "Het toestel meet vochtigheid en luchtkwaliteit en regelt het "
    + "debiet zelf. Dat is bijna altijd het juiste antwoord.",
  "dashboard.guide.away.title": "Afwezig als het huis leeg is",
  "dashboard.guide.away.body": "Zakt naar het minimum dat de lucht fris houdt en bespaart energie. "
    + "Het blijft daar tot u het beëindigt.",
  "dashboard.guide.manual.title": "Laag · Middel · Hoog met de hand",
  "dashboard.guide.manual.body": "Stel het achtergrondniveau zelf in als u dat wilt: stil 's "
    + "nachts, dagelijks, of meer lucht voor een volle of vochtige kamer.",
  "dashboard.guide.boost.title": "Boost om de lucht snel te verversen",
  "dashboard.guide.boost.body": "Vol debiet voor een vaste tijd — voor kookluchtjes of een "
    + "dampige badkamer — daarna valt het vanzelf terug op het schema.",
  "dashboard.guide.bypass.title": "Bypass koelt in warme nachten",
  "dashboard.guide.bypass.body": "Als het buiten koeler is dan binnen, leidt het toestel verse "
    + "lucht langs de warmtewisselaar om het huis te koelen. Dat regelt het voor u.",
  "dashboard.guide.filters.title": "Vervang de filters samen",
  "dashboard.guide.filters.body": "Als de teller afloopt, vervang dan beide filters tegelijk. "
    + "Schone filters betekenen schonere lucht en een stillere, zuinigere ventilator.",
  "dashboard.guide.about": "Hoe dit dashboard te lezen is",
  "dashboard.guide.shape.term": "Op vorm",
  "dashboard.guide.shape.em": "lengte, een positie of een route",
  "dashboard.guide.shape.body": "Elke grootheid wordt getekend als een {em} en daarnaast als "
    + "kleur — de grote meting, de terugwinningsbalk, de bypassroute, de filterlevensduur en elke "
    + "aftelring blijven leesbaar voor een kleurenblind oog.",
  "dashboard.guide.unit.term": "Op het toestel",
  "dashboard.guide.unit.em": "alleen wat het ventilatietoestel zelf meldt",
  "dashboard.guide.unit.body": "Het toont {em}, en de bediening schrijft rechtstreeks terug naar "
    + "het toestel via dezelfde API die elke andere client gebruikt.",
};
