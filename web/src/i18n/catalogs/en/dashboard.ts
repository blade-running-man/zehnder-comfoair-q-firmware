// Dashboard page (pages/dashboard, route /).
//
// THIS object defines the namespace's key set. Every key added here must be added to
// ../de/dashboard.ts, ../nl/dashboard.ts and ../uk/dashboard.ts too: those are typed
// `Record<keyof typeof dashboard, Message>` against this object, so tsc fails the build in each of
// them until it is. Prefix every key "dashboard." -- en.ts spreads all namespaces into one object,
// and a key two namespaces share would silently overwrite one of them (catalogs.test.ts fails
// on that). A count-dependent message is a plural object ({ one, other } in English); the
// other languages supply their own categories, which catalogs.test.ts checks.
//
// A {token} that wraps a reading in markup (<strong>/<b>) is filled by i18n/rich.tsx,
// not by t(): keep it in the sentence, anywhere the language's word order wants it. Device option
// strings (Auto, Open, Cool, "Auto only", ...) are NOT here -- they are tokens.ts, via optionLabel().
import type { Message } from "../../format.ts";

export const dashboard = {
  // Page chrome (Dashboard.tsx).
  "dashboard.title.suffix": "· Home",
  "dashboard.tagline": "Your heat-recovery ventilation at a glance — what's arriving, where the "
    + "air is going, and how to steer it.",
  "dashboard.conn.live": "Live",
  "dashboard.conn.waiting": "Connected, waiting for the first reading",
  "dashboard.conn.connecting": "Connecting…",
  "dashboard.conn.closed": "Disconnected — the readings below may be stale",

  // The season, read-only (Hero chip, Bypass card subtitle, Comfort chip).
  "dashboard.season.heating": "Heating season",
  "dashboard.season.cooling": "Cooling season",
  "dashboard.season.mild": "Mild season",

  // Hero: the plain-language headline (statusLine()). {temp} is a bold reading.
  "dashboard.hero.stopped.status": "Supply paused — nothing is arriving",
  "dashboard.hero.stopped.line": "The supply fan is stopped, so no fresh air is coming in. "
    + "Extract is still running.",
  "dashboard.hero.bypass.status": "Cooling your home with night air",
  "dashboard.hero.bypass.line": "Outside it is {temp} and the bypass is open, so this air came "
    + "in around the exchanger — cooling the house instead of warming it.",
  "dashboard.hero.warmed.status": "Fresh air, warmed on the way in",
  "dashboard.hero.warmed.line": "Outside it is {temp}. The exchanger put back {lift} of the "
    + "{avail} °C of warmth it had to work with.",
  "dashboard.hero.tempered.status": "Fresh air, gently tempered",
  "dashboard.hero.tempered.line": "Outside it is {temp}. The exchanger evened the incoming air "
    + "toward the temperature of your rooms.",
  "dashboard.hero.near.status": "Fresh air, near the outdoor temperature",
  "dashboard.hero.near.line": "Outside it is {temp}. Inside and out are close enough that there "
    + "is little heat to recover.",
  "dashboard.hero.waiting.status": "Waiting for the first reading",
  "dashboard.hero.waiting.line": "The dashboard is connected and listening for the unit.",
  "dashboard.hero.intoRooms": "into your rooms",

  // Hero chips.
  "dashboard.chip.supplyStopped": "Supply fan stopped",
  "dashboard.chip.bypassOpen": "Bypass open",
  "dashboard.chip.filtersOverdue": "Filters overdue",
  "dashboard.chip.filtersDue": "Filters due in {days} d",

  // Hero mode control and stat strip.
  "dashboard.mode.label": "Ventilation mode",
  "dashboard.mode.choose": "Choose how much air",
  "dashboard.mode.help.Auto": "the unit sets airflow itself — leave it here",
  "dashboard.mode.help.Away": "minimum air while the house is empty",
  "dashboard.mode.help.Low": "quiet background ventilation",
  "dashboard.mode.help.Medium": "everyday fresh air for an occupied home",
  "dashboard.mode.help.High": "more air for a full or humid room",
  "dashboard.mode.help.Boost": "maximum air for a set time, then back to schedule",
  "dashboard.stat.airflow": "Airflow",
  "dashboard.stat.power": "Power",
  "dashboard.stat.recovered": "Heat recovered",
  "dashboard.stat.bypassed": "Bypassed on purpose",

  // HeatScale (the "heat lift" figure under the big reading).
  "dashboard.lift.title": "Heat lift",
  "dashboard.lift.bypassed": "Bypassed",
  "dashboard.lift.nothing": "nothing arriving",
  "dashboard.lift.skips": "fresh air skips the exchanger",
  "dashboard.lift.warmer": "warmer than outside",
  "dashboard.lift.cooler": "cooler than outside",
  "dashboard.lift.aria.nothing": "Heat lift: nothing arriving.",
  "dashboard.lift.aria.bypassed": "Heat lift: bypassed — fresh air skips the exchanger.",
  "dashboard.lift.aria.warmed": "Heat lift: the incoming air was warmed {lift} °C, from "
    + "{outdoor} outside toward {extract} in your rooms.",
  "dashboard.lift.outside": "outside",
  "dashboard.lift.outsideAt": "{temp} outside",
  "dashboard.lift.rooms": "your rooms",
  "dashboard.lift.roomsAt": "{temp} your rooms",

  // Bypass card ("Where the air goes").
  "dashboard.bypass.title": "Where the air goes",
  "dashboard.bypass.key": "Bypass",
  "dashboard.bypass.bypassed": "Exchanger bypassed",
  "dashboard.bypass.recovered": "{pct}% heat recovered",
  "dashboard.bypass.exchange": "Heat exchange",
  "dashboard.bypass.supplyStopped": "Supply stopped",
  "dashboard.bypass.summaryOpen": "Bypass open: outdoor air {outdoor} skips the exchanger; room "
    + "air {extract} leaves at {exhaust}.",
  "dashboard.bypass.summaryClosed": "Counterflow exchanger: outdoor air {outdoor} is warmed to "
    + "{supply} on the way in; room air {extract} is cooled to {exhaust} on the way out.",
  "dashboard.bypass.outdoorAir": "Outdoor air",
  "dashboard.bypass.exhaust": "Exhaust",
  "dashboard.bypass.fromRooms": "From rooms",
  "dashboard.bypass.toRooms": "To rooms",
  "dashboard.bypass.zoneOut": "Outdoors",
  "dashboard.bypass.zoneHome": "Your home",
  "dashboard.bypass.fanSupply": "Supply",
  "dashboard.bypass.fanExtract": "Extract",
  "dashboard.bypass.supplyFlow": "Supply flow",
  "dashboard.bypass.exhaustFlow": "Exhaust flow",
  "dashboard.bypass.outdoorMean": "Outdoor mean",
  "dashboard.bypass.set": "Set the bypass",
  "dashboard.bypass.modeAria": "Bypass mode",

  // Filters card. The caption follows the big number; English keeps one wording for both forms.
  "dashboard.filters.title": "Filters",
  "dashboard.filters.unknown": "No reading yet",
  "dashboard.filters.good": "Filters are fine",
  "dashboard.filters.soon": "Order a replacement",
  "dashboard.filters.overdue": "Replace the filters",
  "dashboard.filters.tagOutdoor": "Outdoor air",
  "dashboard.filters.tagExtract": "Extract",
  "dashboard.filters.days": "days",
  "dashboard.filters.daysLeft": { one: "day left", other: "days left" },
  "dashboard.filters.daysOverdue": { one: "day overdue", other: "days overdue" },
  "dashboard.filters.fitted": "Fitted",
  "dashboard.filters.due": "Due",
  "dashboard.filters.noteOverdue": "Both filters — outside air and extract — are changed "
    + "together, and the unit stops counting once it passes zero.",
  "dashboard.filters.noteCycle": "Both filters — outside air and extract — are changed "
    + "together, on the unit's {cycle}-day cycle.",

  // Comfort & temperature card. {mean}/{temp} are bold readings.
  "dashboard.comfort.title": "Comfort & temperature",
  "dashboard.comfort.seasonNote": "Set automatically — the unit follows the 5-day average "
    + "outdoors, so the season isn't something you choose.",
  "dashboard.comfort.seasonNoteMean": "Set automatically — the unit follows the 5-day average "
    + "outdoors ({mean}), so the season isn't something you choose.",
  "dashboard.comfort.profile": "Temperature profile",
  "dashboard.comfort.aiming": "Aiming for {temp}",
  "dashboard.comfort.profileHint": "Cooler or warmer supply air. The unit isn't a thermostat — "
    + "it nudges its own target, it can't hold an exact room temperature.",
  "dashboard.comfort.passive.label": "Passive cooling",
  "dashboard.comfort.passive.help": "Let cool outdoor air help lower the temperature when the "
    + "season allows.",
  "dashboard.comfort.humidity.label": "Humidity comfort",
  "dashboard.comfort.humidity.help": "Nudge airflow to keep indoor humidity comfortable.",
  "dashboard.comfort.protection.label": "Humidity protection",
  "dashboard.comfort.protection.help": "Ramp airflow up to hold humidity down and prevent "
    + "condensation.",

  // Timers card.
  "dashboard.timers.title": "Timers",
  "dashboard.timers.sub": "How long each temporary mode lasts. Set it before you start one; the "
    + "unit counts it down.",
  "dashboard.timers.boost.running": "Boost",
  "dashboard.timers.boost.what": "How long Boost runs at full airflow before the unit drops back "
    + "to its schedule.",
  "dashboard.timers.boost.idle": "Sets how long the next Boost lasts.",
  "dashboard.timers.bypass.running": "Bypass override",
  "dashboard.timers.bypass.what": "How long a manual Open or Closed holds before the bypass "
    + "hands control back to Auto.",
  "dashboard.timers.bypass.idle": "Sets how long a manual bypass holds.",
  "dashboard.timers.away.running": "Away mode",
  "dashboard.timers.away.what": "Ignores every schedule while the house is empty — for a "
    + "holiday, not the quick Away preset above. Leave it open-ended and it holds until you end "
    + "it.",
  "dashboard.timers.away.idle": "Sets how long Away Mode lasts once you start it.",
  "dashboard.timers.noEnd": "No end set",
  "dashboard.timers.runsUntil": "{mode} runs until you end it",
  "dashboard.timers.left": "{time} left",
  "dashboard.timers.isRunning": "{mode} is running",
  "dashboard.timers.notRunning": "Not running",
  "dashboard.timers.endNow": "End now",
  "dashboard.timers.startAway": "Start away",
  "dashboard.timers.untilEnd": "Until I end it",
  "dashboard.timers.inMinutes": "{name} in minutes",
  "dashboard.timers.min": "min",

  // Durations (fmtDur) and countdowns (fmtLeft), ventilation.ts.
  "dashboard.dur.indefinite": "Indefinite",
  "dashboard.dur.min": "{n} min",
  "dashboard.dur.days": { one: "{count} day", other: "{count} days" },
  "dashboard.dur.h": "{h} h",
  "dashboard.dur.hm": "{h} h {m} m",
  "dashboard.dur.dh": "{d} d {h} h",
  "dashboard.dur.s": "{n} s",

  // A write the device refused (commit(), ventilation.ts).
  "dashboard.toast.notApplied": "Not applied: {msg}",

  // Guide (the calm close).
  "dashboard.guide.title": "Using your ventilation",
  "dashboard.guide.lede": "You rarely need to touch it — but here's when each control earns its "
    + "keep.",
  "dashboard.guide.auto.title": "Leave it on Auto",
  "dashboard.guide.auto.body": "The unit reads humidity and air quality and sets the airflow "
    + "itself. This is the right answer nearly all the time.",
  "dashboard.guide.away.title": "Away when the house is empty",
  "dashboard.guide.away.body": "Drops to the minimum that keeps the air from going stale, and "
    + "saves energy. It stays there until you end it.",
  "dashboard.guide.manual.title": "Low · Medium · High by hand",
  "dashboard.guide.manual.body": "Set the background level yourself when you want to: quiet at "
    + "night, everyday, or more air for a full or humid room.",
  "dashboard.guide.boost.title": "Boost to clear the air fast",
  "dashboard.guide.boost.body": "Full airflow for a set time — for cooking smells or a steamy "
    + "bathroom — then it drops back to the schedule on its own.",
  "dashboard.guide.bypass.title": "Bypass cools on warm nights",
  "dashboard.guide.bypass.body": "When it's cooler outside than in, the unit routes fresh air "
    + "around the heat exchanger to cool the house. It handles this for you.",
  "dashboard.guide.filters.title": "Change filters together",
  "dashboard.guide.filters.body": "When the counter runs down, swap both filters at once. Clean "
    + "filters mean cleaner air and a quieter, more efficient fan.",
  "dashboard.guide.about": "How this dashboard reads",
  "dashboard.guide.shape.term": "By shape",
  "dashboard.guide.shape.em": "length, a position or a route",
  "dashboard.guide.shape.body": "Every quantity is drawn as a {em} as well as a colour — the big "
    + "reading, the recovery bar, the bypass route, the filter life and every countdown ring all "
    + "still read for a colour-blind eye.",
  "dashboard.guide.unit.term": "By the unit",
  "dashboard.guide.unit.em": "only what the ventilation unit itself reports",
  "dashboard.guide.unit.body": "It shows {em}, and its controls write straight back to the unit "
    + "over the same API every other client uses.",
} satisfies Record<string, Message>;
