// What pressing Save does to the broker the device is talking to RIGHT NOW.
//
// The suite exists because of an INVERSION found on 2026-09-07: the page as first planned gave a
// warning on the harmless path (the broker is switched off, so nothing can be retracted and the
// old tree simply stays) and said nothing about the destructive one (the broker is LIVE, so
// changing its address runs a full WITHDRAW -- an empty retained config per entity -- and Home
// Assistant deletes the device and every entity together with its entity_id). The second is the one
// that cannot be undone: fixing the typo afterwards does not bring an entity_id back, and every
// dashboard, automation, template and statistic that named it is broken in silence.
//
// So every case below is a sentence the owner has to be able to act on BEFORE pressing the
// button, and the numbers in them are DERIVED from the generated entity list rather than typed:
// a registry that grows an entity moves the sentence with it.

import { eq, ok, report } from "./harness.ts";
import { ENTITIES } from "../../../api/entities.ts";
import {
  BROKER_LINK_FRESH_MS,
  brokerConfirming,
  brokerGate,
  brokerLinkFrom,
  describeBrokerSave,
  type BrokerLink,
  type BrokerNotice,
} from "../broker.ts";

const loaded = { mqtt_host: "192.168.1.10", mqtt_port: 1883, topic_prefix: "comfoair/9c139ed48ae4" };

function form(over: Partial<{ mqtt_host: string; mqtt_port: string; topic_prefix: string }> = {}) {
  return {
    mqtt_host: loaded.mqtt_host,
    mqtt_port: String(loaded.mqtt_port),
    topic_prefix: loaded.topic_prefix,
    ...over,
  };
}

function saved(over = {}, link: BrokerLink = "connected") {
  return describeBrokerSave(loaded, form(over), link, ENTITIES.length);
}

// --- the destructive path, which is the whole reason this file exists -------------------------

const movedHost = saved({ mqtt_host: "192.168.1.11" });
ok(movedHost !== null, "a live broker whose address changed produces a notice");
eq(movedHost?.move, "moved", "a changed host is a move, not a rekey");
eq(movedHost?.confirm, true, "the destructive save has to be confirmed, not merely narrated");
ok(
  movedHost?.lines.join(" ").includes(`${ENTITIES.length} entities`) === true,
  "the consequence is named with the NUMBER of entities Home Assistant will delete",
);
ok(
  movedHost?.headline.includes(loaded.mqtt_host) === true,
  "the headline names the OLD broker -- that is where the deletion happens",
);
ok(
  movedHost?.lines.join(" ").includes("entity_id") === true,
  "the sentence says what is actually lost: the entity_ids, not just the entities",
);

// A typo in the PORT is the same destruction by the same route: comfoair_mqtt_pending_cleanup()
// compares host AND port and answers WITHDRAW for either (comfoair_mqtt_client.c:107).
const movedPort = saved({ mqtt_port: "1884" });
eq(movedPort?.move, "moved", "a changed port moves the broker just as a changed host does");
eq(movedPort?.confirm, true, "and it has to be confirmed for the same reason");

// The two-step trap: the owner who did not get the result they wanted
// clears the host field next. That is a move too, and the destructive one.
const cleared = saved({ mqtt_host: "" });
eq(cleared?.move, "moved", "clearing the host is a move");
eq(cleared?.confirm, true, "clearing the host deletes the entities and must be confirmed");

// --- the paths that destroy nothing -----------------------------------------------------------

eq(saved(), null, "Save with nothing changed says nothing");
eq(
  saved({ topic_prefix: "attic/hrv" }),
  null,
  "a prefix change is a REKEY: the config topic carries no prefix, so the entities and their "
    + "history move across in place and there is nothing to warn about",
);
eq(
  saved({ mqtt_port: "notaport", mqtt_host: loaded.mqtt_host }),
  null,
  "an unparseable port is not a move: patchFromForm refuses the save before anything is sent",
);
eq(
  describeBrokerSave(
    { mqtt_host: "", mqtt_port: 1883, topic_prefix: "comfoair/9c139ed48ae4" },
    form({ mqtt_host: "192.168.1.10" }),
    "connected",
    ENTITIES.length,
  ),
  null,
  "a device that never had a broker cannot have left anything on one",
);
eq(
  saved({ mqtt_host: "  192.168.1.10  " }),
  null,
  "the host is trimmed before it is compared, exactly as patchFromForm trims it before sending",
);

// --- the broker that is switched off: hardware scenario HV-HA-17 ---------------------------------

const offline = saved({ mqtt_host: "192.168.1.11" }, "offline");
ok(offline !== null, "a move away from an unreachable broker still gets a notice");
eq(
  offline?.confirm,
  false,
  "and it does NOT block the save: an unreachable peer does not get to stop the owner changing "
    + "a setting (AGENTS.md)",
);
ok(
  offline?.command.startsWith("mosquitto_sub ") === true,
  "the notice carries the command that cleans up by hand, because the device cannot",
);
ok(
  offline?.command.includes(`'${loaded.topic_prefix}/#'`) === true,
  "and the command names this device's own prefix rather than a wildcard over the tree",
);
ok(
  offline?.command.includes("--retained-only") === true,
  "--remove-retained without --retained-only republishes live traffic back as empty messages",
);
ok(
  offline?.command.includes("homeassistant/#") === false,
  "the hand cleanup must never be given `homeassistant/#`: that would delete the discovery "
    + "documents of every OTHER device on the broker",
);

// The device id is what narrows the discovery half of the command to THIS device. Without one
// there is no safe pattern, so the command has to say so rather than print a dangerous one.
const withId = describeBrokerSave(
  loaded, form({ mqtt_host: "1.2.3.4" }), "offline", ENTITIES.length, "9c139ed48ae4",
);
ok(
  withId?.command.includes("-t 'homeassistant/+/9c139ed48ae4/#'") === true,
  "given the device id, the discovery half of the command is narrowed to this device alone",
);
ok(
  offline?.command.includes("homeassistant") === false,
  "and without one, no discovery pattern is printed at all -- a missing line beats a "
    + "destructive one",
);
eq(movedHost?.command, "", "the live-broker branch has nothing for the owner to run: the "
  + "device is about to do it itself");

// --- an answer nobody could obtain reads as danger, not as safety -----------------------------

const unknown = saved({ mqtt_host: "192.168.1.11" }, "unknown");
eq(
  unknown?.confirm,
  true,
  "a link nobody could read is confirmed like a live one: 'we could not ask' must never be "
    + "rendered as 'nothing will happen'",
);
ok(
  unknown?.lines.join(" ").includes("could not") === true,
  "and the sentence says which of the two situations the page is in",
);

// --- the number in the sentence is DERIVED, and this is how that is checked --------------------
//
// The old shape read ENTITIES.length inside describeBrokerSave() and every case compared the
// sentence against ENTITIES.length, so replacing the derivation with a literal count passed all of
// them: the test and the code agreed on a number neither could be wrong about on its own. As an
// argument it can be VARIED, and a call with 3 has to say 3 everywhere it says anything.
const three = describeBrokerSave(loaded, form({ mqtt_host: "192.168.1.11" }), "connected", 3);
ok(
  three?.headline.includes("its 3 entities") === true,
  "the headline counts the list it was given, not a number written into the module",
);
ok(
  three?.lines.join(" ").includes("3 empty discovery messages") === true,
  "and so does every sentence that names the consequence",
);
ok(
  [ENTITIES.length, ENTITIES.length - 1].every((n) => !three?.lines.join(" ").includes(String(n))),
  "with nothing left over from the size of the real registry",
);
ok(
  describeBrokerSave(loaded, form({ mqtt_host: "1.2.3.4" }), "offline", 3)
    ?.lines.join(" ").includes("all 3 entities") === true,
  "the offline branch counts the same way -- it is the same list being left behind",
);
// And what the page actually passes is still the generated list, which is what makes the numbers
// above real rather than arithmetic: the real entities, and no second list anywhere (AGENTS.md).
ok(ENTITIES.length > 0, "the generated registry is not empty, or every sentence above says zero");

// --- reading the link off the status document --------------------------------------------------
//
// FRESH, in every case that expects an answer. brokerLinkFrom() takes an age because the poll loop
// STOPS -- see its doc and Settings.tsx -- and a reading nobody has refreshed is not a reading.

eq(brokerLinkFrom(null, 0), "unknown", "no status document at all is unknown");
eq(brokerLinkFrom({}, 0), "unknown", "a status document with no mqtt block is unknown -- the block "
  + "is ABSENT rather than zeroed when nobody was in a position to look (comfoair_wire.h)");
eq(brokerLinkFrom({ mqtt: { connected: true } }, 0), "connected", "connected is connected");
eq(brokerLinkFrom({ mqtt: { connected: false } }, 0), "offline", "and not connected is offline");
eq(
  brokerLinkFrom({ mqtt: { connected: "yes" } as unknown as { connected: boolean } }, 0),
  "unknown",
  "anything that is not a boolean is unknown, not truthy: a firmware that renamed the field "
    + "must not be read as a live broker",
);

// --- and an answer that has gone stale is no longer an answer ----------------------------------
//
// THE DEFECT THIS CLOSES, found 2026-09-07. shouldKeepPolling() is a statement about WI-FI: on a
// device already on the network the first poll answers `connected: true` and the loop stops, so
// the mqtt block inside that one answer is frozen for the life of the page. The owner opens
// Settings BECAUSE the broker was down, is shown the soft branch, and while he edits the octet the
// retry ladder gets the link back. One press then deletes every entity having promised otherwise.

const live = { mqtt: { connected: true } };
const down = { mqtt: { connected: false } };
eq(brokerLinkFrom(live, BROKER_LINK_FRESH_MS - 1), "connected", "just inside the window, an "
  + "answer is still an answer");
eq(brokerLinkFrom(live, BROKER_LINK_FRESH_MS), "unknown", "and at the window it stops being one -- "
  + "the boundary belongs to staleness, because the safe-looking reading is the dangerous one");
eq(
  brokerLinkFrom(down, BROKER_LINK_FRESH_MS),
  "unknown",
  "AND IN BOTH DIRECTIONS. A stale `offline` is the exact reading that renders 'nothing can be "
    + "cleaned up' over a broker that has since come back -- it must not survive either",
);
eq(brokerLinkFrom(live, Number.POSITIVE_INFINITY), "unknown", "nothing has ever been read");
eq(brokerLinkFrom(live, Number.NaN), "unknown", "an age nobody could work out is not an age");
eq(brokerLinkFrom(live, -1), "unknown", "nor is a negative one: a clock that went backwards is a "
  + "clock, not a licence");

// The consequence, end to end: the same document that reads as a live broker now reads as one
// nobody asked -- and BOTH of those confirm, which is why this change is safe to make.
eq(
  describeBrokerSave(loaded, form({ mqtt_host: "1.2.3.4" }), brokerLinkFrom(down, 0),
                     ENTITIES.length)?.confirm,
  false,
  "a broker known to be down right now still does not block the save (AGENTS.md)",
);
eq(
  describeBrokerSave(loaded, form({ mqtt_host: "1.2.3.4" }),
                     brokerLinkFrom(down, BROKER_LINK_FRESH_MS), ENTITIES.length)?.confirm,
  true,
  "the SAME document, read a moment too long ago, confirms instead -- 'we asked a minute ago' is "
    + "not 'we know'",
);

// --- the two presses ---------------------------------------------------------------------------
//
// None of this was reachable by any case until 2026-09-07: it lived in a hook and in three lines
// of Settings.tsx. Three mutations of it were run against the tree and all three left 610 of 610
// green -- the confirmation label made identical to the ordinary one, the hold deleted from
// saveSettings(), the release deleted from patch(). The first and third are below.

const danger = saved({ mqtt_host: "192.168.1.11" }) as BrokerNotice;
const harmless = saved({ mqtt_host: "192.168.1.11" }, "offline") as BrokerNotice;

// THE LABEL, which no case mentioned before. A second press is a confirmation only if the button
// it lands on has stopped saying what the first one said; "Save settings" twice is a double click.
eq(danger.confirm, true, "the fixture for these cases is the branch that asks");
ok(danger.confirmLabel !== "", "a confirmation with no words on its button cannot be given");
ok(
  danger.confirmLabel !== "Save settings",
  "AND IT MUST NOT BE THE ORDINARY LABEL. An owner who meant to save the device name must not be "
    + "able to reach a deletion of every entity by pressing one word twice",
);
ok(
  danger.confirmLabel.toLowerCase().includes("delete"),
  "and it says what the press does, rather than merely differing",
);
eq(harmless.confirmLabel, "", "the branch that does not ask has no second button to name");

// The first press swallows, the second submits.
eq(brokerGate(false, danger, "press"), { confirming: true, submit: false },
   "the first press puts the warning up and sends NOTHING");
eq(brokerGate(true, danger, "press"), { confirming: false, submit: true },
   "the second press is the confirmation and submits");

// An edit between them retires it: the owner confirms a deletion on one address, corrects the
// address, and the next press must not carry the first one's authority.
eq(brokerGate(true, danger, "edit"), { confirming: false, submit: false },
   "an edit retires a standing confirmation and submits nothing of its own");
eq(brokerGate(true, danger, "cancel"), { confirming: false, submit: false }, "so does Cancel");
eq(brokerGate(true, danger, "saved"), { confirming: false, submit: false },
   "and a save that went through spends the confirmation it was given for");
const pressed = brokerGate(false, danger, "press");
const edited = brokerGate(pressed.confirming, danger, "edit");
eq(
  brokerGate(edited.confirming, danger, "press"),
  { confirming: true, submit: false },
  "so press -> edit -> press is a FIRST press again, and swallows again -- the whole sequence, "
    + "because that is the one an owner actually performs",
);

// A save with nothing destructive about it must not ask -- which is nearly every save this page
// sees, and the reason the confirmation is bearable at all.
eq(brokerGate(false, null, "press"), { confirming: false, submit: true },
   "no notice at all: the ordinary save goes through on one press");
eq(brokerGate(false, harmless, "press"), { confirming: false, submit: true },
   "and so does a move away from a broker that is not answering (AGENTS.md)");

// The link can change between the presses -- a poll, or the fresh read the press itself takes.
eq(brokerGate(true, harmless, "press"), { confirming: false, submit: true },
   "a confirmation standing over a notice that has stopped asking is spent as an ordinary save");
eq(brokerConfirming(true, harmless), false,
   "and the button must not say 'Delete them and save' over that notice either");
eq(brokerConfirming(true, null), false, "nor over no notice at all");
eq(brokerConfirming(true, danger), true, "it says it exactly while the warning asks for it");
eq(brokerConfirming(false, danger), false, "and not before the first press");

report("broker");
