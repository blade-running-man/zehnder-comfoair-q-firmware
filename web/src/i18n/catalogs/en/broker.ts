// The broker's verdicts, wherever they are shown: the diagnostics card and the dashboard line
// (pages/state/brokerStatus.ts, BrokerStatus.tsx, BrokerAlert.tsx) and the save-time warning
// about moving the broker (pages/settings/broker.ts). ONE namespace for both pages because the
// two halves describe the same client and must not drift into two vocabularies.
//
// EN values are byte-for-byte what those modules used to build inline: brokerStatus.test.ts and
// broker.test.ts pin them under node (where the locale is English). DO NOT reword one without
// that suite. Prefix every key "broker."; every key here must exist in ../de, ../nl, ../uk.
import type { Message } from "../../format.ts";

export const broker = {
  "broker.tone.pending": "Waiting",
  "broker.tone.denied": "Refused",
  "broker.tone.error": "Problem",
  "broker.tone.offline": "No answer",
  "broker.bootGrace":
    "The device has been running for under five minutes, so the house may still be coming "
    + "up.",
  "broker.absent.headline": "The device said nothing about a broker",
  "broker.absent.detail":
    "This answer carries no broker section at all, which is different from every counter "
    + "being zero.",
  "broker.off.headline": "MQTT is switched off",
  "broker.off.detail":
    "No broker address is saved, so Home Assistant cannot see this device. Fill in the "
    + "broker on the Settings page and save.",
  "broker.unknownWord.headline": "The device reported a broker state this page does not recognise",
  "broker.unknownWord.detail":
    "The status document said link \"{link}\", failure \"{failure}\", subscriptions "
    + "\"{subscriptions}\". One of those words is not in this page's vocabulary, which means "
    + "the firmware grew one and this page did not.",
  "broker.subsRefused.headline": "The broker will not let this device listen",
  "broker.subsRefused.detail":
    "Readings are being published, and commands from Home Assistant will not arrive: the "
    + "broker refused the command subscriptions. This is a broker ACL, not a device fault.",
  "broker.subsPending.headline": "Waiting for the broker to confirm",
  "broker.subsPending.detail":
    "Connected; the command subscriptions have not been acknowledged yet.",
  "broker.online.headline": "Connected to the broker",
  "broker.online.detail":
    "Published {published} readings, accepted {commands} commands. Reconnects: "
    + "{reconnects}.",
  "broker.connecting.headline": "Connecting to the broker",
  "broker.connecting.detail": "The device is dialling {host}. This normally takes a second or two.",
  "broker.retry": "Next attempt in {secs} s.",
  "broker.dns.headline": "The broker's name does not resolve",
  "broker.dns.detail":
    "The device asked the router for {host} and got no answer. Check the spelling, or "
    + "enter the broker's IP address instead.",
  "broker.tcp.headline": "Nothing is answering at that address",
  "broker.tcp.detail":
    "{host}:{port} did not accept a connection. The broker may be switched off, or the "
    + "port may be wrong.",
  "broker.tcp.retry": "The device keeps trying — next attempt in {secs} s.",
  "broker.unavailable.headline": "The broker is not ready yet",
  "broker.unavailable.detail":
    "It answered and said it is unavailable — usually a broker that is still starting.",
  "broker.protocol.headline": "The broker refused the connection",
  "broker.protocol.detail":
    "It answered \"{reason}\" (code {code}). This is not about the password: check the "
    + "broker's own settings. The device retries every five minutes.",
  "broker.badCredentials.headline": "The broker rejected the username or password",
  "broker.badCredentials.detail":
    "It answered \"{reason}\" (code {code}). Correct them on the Settings page and save — "
    + "the device retries at once, without waiting out the five-minute pause.",
  "broker.dropped.headline": "The connection to the broker keeps dropping",
  "broker.dropped.detail":
    "It accepted the device and then closed the link {disconnects} times. Another device "
    + "using the same client id will do this.",
  "broker.identityConflict":
    "{reconnects} reconnects against {disconnects} disconnects: climbing together is what "
    + "that looks like from here, because the broker cannot report it any other way.",
  "broker.notDialled.headline": "The device has not dialled the broker yet",
  "broker.notDialled.detail":
    "A broker is saved and the connection has not been started yet. That is ordinary in "
    + "the first seconds after a restart, and a defect if it lasts.",
  "broker.noReason.headline": "The device did not say why the broker is unreachable",
  "broker.noReason.detail":
    "The link is \"{link}\" and the failure is \"none\", which cannot both be true. Attempts "
    + "so far: {attempts}.",
  "broker.alert.noControl": "Home Assistant cannot control this device",
  "broker.alert.noData": "Home Assistant is not receiving data from this device",
  "broker.alert.tail": "{detail}. Open Diagnostics for the broker's counters.",
  "broker.card.title": "Home Assistant & the broker",
  "broker.card.saved": "Saved broker:",
  "broker.card.none": "none",
  "broker.card.stale": "The numbers below are the last ones that arrived, not the ones now.",
  "broker.counter.link": "Link",
  "broker.counter.subscriptions": "Command subscriptions",
  "broker.counter.published": "Readings published",
  "broker.counter.commands": "Commands accepted",
  "broker.counter.rejected": "Commands refused",
  "broker.counter.reconnects": "Reconnects / disconnects",
  "broker.counter.attempts": "Attempts since a settled session",
  "broker.counter.connack": "Last CONNACK",
  "broker.counter.errno": "Socket error number",
  "broker.counter.epoch": "Client rebuilds",
  "broker.move.offline.headline":
    "Nothing can be cleaned up on {old} — the device is not connected to it",
  "broker.move.offline.line1":
    "The device cannot retract what it published on {old} while it cannot reach it. The "
    + "device card and all {count} entities stay in any Home Assistant reading that broker, "
    + "holding the readings they had when the device last spoke, and they will still be "
    + "there after you save.",
  "broker.move.offline.line2":
    "Saving is not blocked. A broker that is not answering does not get to stop you "
    + "changing a setting.",
  "broker.move.offline.line3":
    "To clear them by hand once that broker is back, run this from a machine that can "
    + "reach it:",
  "broker.move.couldNotAsk":
    "This page could not read whether the device is connected to {old} right now, so it "
    + "is assuming it is. If it is not, saving simply changes the address and leaves "
    + "everything on {old} untouched.",
  "broker.move.connected": "The device is connected to {old} right now.",
  "broker.move.confirm": "Delete them and save",
  "broker.move.headline":
    "Saving removes this device and its {count} entities from Home Assistant on {old}",
  "broker.move.line1":
    "Changing the address makes it retract everything it published there before it "
    + "leaves: {count} empty discovery messages, after which Home Assistant deletes the "
    + "device card and all {count} entities from its registry.",
  "broker.move.line2":
    "Their entity_ids go with them, and that is the part that cannot be undone. "
    + "Dashboards, automations, scripts, templates and long-term statistics that name those "
    + "entity_ids stop working, without an error anywhere. Pointing the device back at "
    + "{old} afterwards recreates the entities, but not necessarily under the same "
    + "entity_ids and never with their history.",
  "broker.move.line3":
    "Only the broker address does this. The topic prefix, the device name and the "
    + "discovery switch can all be changed without it — those keep the entities, their "
    + "entity_ids and their history.",
} satisfies Record<string, Message>;
