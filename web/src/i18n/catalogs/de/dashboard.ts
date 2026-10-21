// German translation of ../en/dashboard.ts. Typed against the English object: a key missing here,
// or one English does not have, is a tsc error in THIS file. Keep every {placeholder} verbatim.
// Draft wording; the owner reviews it.
import type { Message } from "../../format.ts";
import type { dashboard as source } from "../en/dashboard.ts";

export const dashboard: Record<keyof typeof source, Message> = {
  "dashboard.title.suffix": "· Übersicht",
  "dashboard.tagline": "Ihre Lüftung mit Wärmerückgewinnung auf einen Blick — was ankommt, wohin "
    + "die Luft strömt und wie Sie sie steuern.",
  "dashboard.conn.live": "Live",
  "dashboard.conn.waiting": "Verbunden, warte auf den ersten Messwert",
  "dashboard.conn.connecting": "Verbinde…",
  "dashboard.conn.closed": "Getrennt — die Werte unten sind evtl. veraltet",

  "dashboard.season.heating": "Heizperiode",
  "dashboard.season.cooling": "Kühlperiode",
  "dashboard.season.mild": "Übergangszeit",

  "dashboard.hero.stopped.status": "Zuluft pausiert — es kommt nichts an",
  "dashboard.hero.stopped.line": "Der Zuluftventilator steht, es kommt keine Frischluft herein. "
    + "Die Abluft läuft weiter.",
  "dashboard.hero.bypass.status": "Kühlt Ihr Zuhause mit Nachtluft",
  "dashboard.hero.bypass.line": "Draußen sind es {temp} und der Bypass ist offen, die Luft kommt "
    + "also am Wärmetauscher vorbei herein — sie kühlt das Haus, statt es zu wärmen.",
  "dashboard.hero.warmed.status": "Frischluft, auf dem Weg herein erwärmt",
  "dashboard.hero.warmed.line": "Draußen sind es {temp}. Der Wärmetauscher hat {lift} der "
    + "verfügbaren {avail} °C Wärme zurückgewonnen.",
  "dashboard.hero.tempered.status": "Frischluft, sanft temperiert",
  "dashboard.hero.tempered.line": "Draußen sind es {temp}. Der Wärmetauscher hat die einströmende "
    + "Luft an die Temperatur Ihrer Räume angeglichen.",
  "dashboard.hero.near.status": "Frischluft, nahe der Außentemperatur",
  "dashboard.hero.near.line": "Draußen sind es {temp}. Innen und außen liegen so nah beieinander, "
    + "dass kaum Wärme zurückzugewinnen ist.",
  "dashboard.hero.waiting.status": "Warte auf den ersten Messwert",
  "dashboard.hero.waiting.line": "Das Dashboard ist verbunden und wartet auf das Gerät.",
  "dashboard.hero.intoRooms": "in Ihre Räume",

  "dashboard.chip.supplyStopped": "Zuluftventilator steht",
  "dashboard.chip.bypassOpen": "Bypass offen",
  "dashboard.chip.filtersOverdue": "Filterwechsel überfällig",
  "dashboard.chip.filtersDue": "Filter fällig in {days} T",

  "dashboard.mode.label": "Lüftungsmodus",
  "dashboard.mode.choose": "Wählen Sie die Luftmenge",
  "dashboard.mode.help.Auto": "das Gerät regelt die Luftmenge selbst — einfach so lassen",
  "dashboard.mode.help.Away": "minimale Lüftung, solange niemand zu Hause ist",
  "dashboard.mode.help.Low": "leise Grundlüftung",
  "dashboard.mode.help.Medium": "alltägliche Frischluft für ein bewohntes Zuhause",
  "dashboard.mode.help.High": "mehr Luft für einen vollen oder feuchten Raum",
  "dashboard.mode.help.Boost": "maximale Luft für eine feste Zeit, dann zurück zum Zeitplan",
  "dashboard.stat.airflow": "Luftmenge",
  "dashboard.stat.power": "Leistung",
  "dashboard.stat.recovered": "Wärmerückgewinnung",
  "dashboard.stat.bypassed": "Bewusst umgangen",

  "dashboard.lift.title": "Wärmegewinn",
  "dashboard.lift.bypassed": "Umgangen",
  "dashboard.lift.nothing": "es kommt nichts an",
  "dashboard.lift.skips": "Frischluft umgeht den Wärmetauscher",
  "dashboard.lift.warmer": "wärmer als draußen",
  "dashboard.lift.cooler": "kühler als draußen",
  "dashboard.lift.aria.nothing": "Wärmegewinn: es kommt nichts an.",
  "dashboard.lift.aria.bypassed": "Wärmegewinn: umgangen — Frischluft umgeht den Wärmetauscher.",
  "dashboard.lift.aria.warmed": "Wärmegewinn: die einströmende Luft wurde um {lift} °C erwärmt, "
    + "von {outdoor} draußen in Richtung {extract} in Ihren Räumen.",
  "dashboard.lift.outside": "draußen",
  "dashboard.lift.outsideAt": "{temp} draußen",
  "dashboard.lift.rooms": "Ihre Räume",
  "dashboard.lift.roomsAt": "{temp} Ihre Räume",

  "dashboard.bypass.title": "Wohin die Luft strömt",
  "dashboard.bypass.key": "Bypass",
  "dashboard.bypass.bypassed": "Wärmetauscher umgangen",
  "dashboard.bypass.recovered": "{pct}% Wärme zurückgewonnen",
  "dashboard.bypass.exchange": "Wärmeaustausch",
  "dashboard.bypass.supplyStopped": "Zuluft steht",
  "dashboard.bypass.summaryOpen": "Bypass offen: Außenluft {outdoor} umgeht den Wärmetauscher; "
    + "Raumluft {extract} geht mit {exhaust} hinaus.",
  "dashboard.bypass.summaryClosed": "Gegenstrom-Wärmetauscher: Außenluft {outdoor} wird auf dem "
    + "Weg herein auf {supply} erwärmt; Raumluft {extract} wird auf dem Weg hinaus auf {exhaust} "
    + "abgekühlt.",
  "dashboard.bypass.outdoorAir": "Außenluft",
  "dashboard.bypass.exhaust": "Fortluft",
  "dashboard.bypass.fromRooms": "Aus den Räumen",
  "dashboard.bypass.toRooms": "In die Räume",
  "dashboard.bypass.zoneOut": "Draußen",
  "dashboard.bypass.zoneHome": "Ihr Zuhause",
  "dashboard.bypass.fanSupply": "Zuluft",
  "dashboard.bypass.fanExtract": "Abluft",
  "dashboard.bypass.supplyFlow": "Zuluftmenge",
  "dashboard.bypass.exhaustFlow": "Fortluftmenge",
  "dashboard.bypass.outdoorMean": "Außenmittel",
  "dashboard.bypass.set": "Bypass einstellen",
  "dashboard.bypass.modeAria": "Bypass-Modus",

  "dashboard.filters.title": "Filter",
  "dashboard.filters.unknown": "Noch kein Messwert",
  "dashboard.filters.good": "Filter sind in Ordnung",
  "dashboard.filters.soon": "Ersatz bestellen",
  "dashboard.filters.overdue": "Filter wechseln",
  "dashboard.filters.tagOutdoor": "Außenluft",
  "dashboard.filters.tagExtract": "Abluft",
  "dashboard.filters.days": "Tage",
  "dashboard.filters.daysLeft": { one: "Tag übrig", other: "Tage übrig" },
  "dashboard.filters.daysOverdue": { one: "Tag überfällig", other: "Tage überfällig" },
  "dashboard.filters.fitted": "Eingesetzt",
  "dashboard.filters.due": "Fällig",
  "dashboard.filters.noteOverdue": "Beide Filter — Außenluft und Abluft — werden zusammen "
    + "gewechselt, und das Gerät zählt nach null nicht weiter.",
  "dashboard.filters.noteCycle": "Beide Filter — Außenluft und Abluft — werden zusammen "
    + "gewechselt, im {cycle}-Tage-Zyklus des Geräts.",

  "dashboard.comfort.title": "Komfort & Temperatur",
  "dashboard.comfort.seasonNote": "Automatisch gesetzt — das Gerät folgt dem 5-Tage-Mittel der "
    + "Außentemperatur, die Saison wählen Sie also nicht selbst.",
  "dashboard.comfort.seasonNoteMean": "Automatisch gesetzt — das Gerät folgt dem 5-Tage-Mittel der "
    + "Außentemperatur ({mean}), die Saison wählen Sie also nicht selbst.",
  "dashboard.comfort.profile": "Temperaturprofil",
  "dashboard.comfort.aiming": "Ziel: {temp}",
  "dashboard.comfort.profileHint": "Kühlere oder wärmere Zuluft. Das Gerät ist kein Thermostat — "
    + "es verschiebt sein eigenes Ziel, eine genaue Raumtemperatur kann es nicht halten.",
  "dashboard.comfort.passive.label": "Passive Kühlung",
  "dashboard.comfort.passive.help": "Kühle Außenluft hilft, die Temperatur zu senken, wenn die "
    + "Saison es zulässt.",
  "dashboard.comfort.humidity.label": "Feuchtekomfort",
  "dashboard.comfort.humidity.help": "Passt die Luftmenge an, damit die Raumfeuchte angenehm "
    + "bleibt.",
  "dashboard.comfort.protection.label": "Feuchteschutz",
  "dashboard.comfort.protection.help": "Erhöht die Luftmenge, um die Feuchte niedrig zu halten "
    + "und Kondensat zu vermeiden.",

  "dashboard.timers.title": "Timer",
  "dashboard.timers.sub": "Wie lange jeder befristete Modus dauert. Vor dem Start einstellen; "
    + "das Gerät zählt herunter.",
  "dashboard.timers.boost.running": "Boost",
  "dashboard.timers.boost.what": "Wie lange Boost mit voller Luftmenge läuft, bevor das Gerät zu "
    + "seinem Zeitplan zurückkehrt.",
  "dashboard.timers.boost.idle": "Legt fest, wie lange der nächste Boost dauert.",
  "dashboard.timers.bypass.running": "Bypass-Übersteuerung",
  "dashboard.timers.bypass.what": "Wie lange ein manuelles Offen oder Geschlossen gilt, bevor der "
    + "Bypass die Steuerung an Auto zurückgibt.",
  "dashboard.timers.bypass.idle": "Legt fest, wie lange ein manueller Bypass gilt.",
  "dashboard.timers.away.running": "Abwesenheitsmodus",
  "dashboard.timers.away.what": "Ignoriert jeden Zeitplan, solange das Haus leer ist — für den "
    + "Urlaub, nicht die schnelle Abwesend-Stufe oben. Ohne Ende gilt er, bis Sie ihn beenden.",
  "dashboard.timers.away.idle": "Legt fest, wie lange der Abwesenheitsmodus nach dem Start "
    + "dauert.",
  "dashboard.timers.noEnd": "Kein Ende gesetzt",
  "dashboard.timers.runsUntil": "{mode} läuft, bis Sie ihn beenden",
  "dashboard.timers.left": "noch {time}",
  "dashboard.timers.isRunning": "{mode} läuft",
  "dashboard.timers.notRunning": "Läuft nicht",
  "dashboard.timers.endNow": "Jetzt beenden",
  "dashboard.timers.startAway": "Abwesenheit starten",
  "dashboard.timers.untilEnd": "Bis ich ihn beende",
  "dashboard.timers.inMinutes": "{name} in Minuten",
  "dashboard.timers.min": "Min.",

  "dashboard.dur.indefinite": "Unbegrenzt",
  "dashboard.dur.min": "{n} Min.",
  "dashboard.dur.days": { one: "{count} Tag", other: "{count} Tage" },
  "dashboard.dur.h": "{h} Std.",
  "dashboard.dur.hm": "{h} Std. {m} Min.",
  "dashboard.dur.dh": "{d} T {h} Std.",
  "dashboard.dur.s": "{n} s",

  "dashboard.toast.notApplied": "Nicht übernommen: {msg}",

  "dashboard.guide.title": "So nutzen Sie Ihre Lüftung",
  "dashboard.guide.lede": "Sie müssen selten eingreifen — aber hier steht, wann sich jede "
    + "Einstellung lohnt.",
  "dashboard.guide.auto.title": "Auf Auto lassen",
  "dashboard.guide.auto.body": "Das Gerät misst Feuchte und Luftqualität und regelt die Luftmenge "
    + "selbst. Das ist fast immer die richtige Wahl.",
  "dashboard.guide.away.title": "Abwesend, wenn niemand zu Hause ist",
  "dashboard.guide.away.body": "Senkt auf das Minimum, das die Luft frisch hält, und spart Energie. "
    + "Es bleibt dabei, bis Sie es beenden.",
  "dashboard.guide.manual.title": "Niedrig · Mittel · Hoch von Hand",
  "dashboard.guide.manual.body": "Stellen Sie die Grundstufe selbst ein, wenn Sie möchten: leise "
    + "in der Nacht, alltäglich oder mehr Luft für einen vollen oder feuchten Raum.",
  "dashboard.guide.boost.title": "Boost, um schnell zu lüften",
  "dashboard.guide.boost.body": "Volle Luftmenge für eine feste Zeit — bei Kochgerüchen oder "
    + "einem dampfigen Bad — danach kehrt es von selbst zum Zeitplan zurück.",
  "dashboard.guide.bypass.title": "Bypass kühlt in warmen Nächten",
  "dashboard.guide.bypass.body": "Ist es draußen kühler als drinnen, leitet das Gerät die "
    + "Frischluft am Wärmetauscher vorbei, um das Haus zu kühlen. Das erledigt es selbst.",
  "dashboard.guide.filters.title": "Filter zusammen wechseln",
  "dashboard.guide.filters.body": "Wenn der Zähler abgelaufen ist, beide Filter auf einmal "
    + "tauschen. Saubere Filter bedeuten sauberere Luft und einen leiseren, effizienteren "
    + "Ventilator.",
  "dashboard.guide.about": "Wie dieses Dashboard zu lesen ist",
  "dashboard.guide.shape.term": "Nach Form",
  "dashboard.guide.shape.em": "Länge, Position oder Weg",
  "dashboard.guide.shape.body": "Jede Größe wird als {em} und zusätzlich als Farbe gezeigt — der "
    + "große Messwert, der Rückgewinnungsbalken, der Bypass-Weg, die Filterlebensdauer und jeder "
    + "Countdown-Ring bleiben auch für farbenblinde Augen lesbar.",
  "dashboard.guide.unit.term": "Nach dem Gerät",
  "dashboard.guide.unit.em": "nur, was das Lüftungsgerät selbst meldet",
  "dashboard.guide.unit.body": "Es zeigt {em}, und seine Bedienelemente schreiben direkt an das "
    + "Gerät zurück, über dieselbe API wie jeder andere Client.",
};
