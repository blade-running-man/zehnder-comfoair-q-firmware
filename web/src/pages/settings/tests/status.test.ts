// Run: node src/pages/settings/tests/status.test.ts
import { eq, report } from "./harness.ts";
import type { ProvisionStatus } from "../../../api/client.ts";
import type { MqttLink, MqttStatus } from "../../../api/status.ts";
import {
  afterBrokerSaveAccepted,
  afterPollFailure,
  afterPollSuccess,
  afterProvisionAccepted,
  POLL_STATE_INITIAL,
  statusAgeMs,
  describeFailure,
  describeWhere,
  POLL_INTERVAL_MS,
  REPROVISION_GRACE_MS,
  shouldKeepPolling,
} from "../status.ts";

// The four answers, each with something different to do about it.
eq(
  describeFailure("network-not-found", 201),
  "That network was not found. Check the name, and that it is a 2.4 GHz network — this device has no 5 GHz radio.",
  "network absent",
);
eq(
  describeFailure("wrong-password", 15),
  "The network refused the key. Retype it; the stored one is never shown back.",
  "wrong key",
);
eq(
  describeFailure("refused", 5),
  "The router answered and turned the device away. Its access point may be full, or it may be an enterprise network this form cannot express.",
  "refused",
);
eq(
  describeFailure("no-address", 0),
  "The device joined the network and was never given an address. That is the router's DHCP, not the password.",
  "no address",
);
eq(describeFailure("none", 0), "", "no failure says nothing");

// A word the firmware might add later must not produce an empty screen.
eq(
  describeFailure("something-new", 42),
  "The device reported a failure this page does not have wording for: something-new (reason 42).",
  "an unknown answer is still shown",
);

eq(describeWhere({ connected: true, ip: "192.168.1.42", wifi_ssid: "Kitchen", ap_on_air: false }),
   "On Kitchen, at 192.168.1.42.", "connected");
eq(describeWhere({ connected: false, ip: "", wifi_ssid: "Kitchen", ap_on_air: true }),
   "Serving its own setup network. Anyone in range can reach it, so writing is refused until it joins a network.",
   "access point");
eq(describeWhere({ connected: false, ip: "", wifi_ssid: "Kitchen", ap_on_air: false }),
   "Trying to join Kitchen.", "trying");
eq(describeWhere({ connected: false, ip: "", wifi_ssid: "", ap_on_air: false }),
   "No network is configured.", "nothing stored");

// shouldKeepPolling: never-loaded and not-connected both mean keep going; connected means stop
// -- UNLESS a resubmission's grace period has not yet expired, in which case "connected" can
// still be describing the network the resubmission is trying to replace (see status.ts's doc
// comment), and the loop must not take that reading at face value until the deadline says so.
const CONNECTED = { connected: true, ip: "192.168.1.42", wifi_ssid: "Kitchen", ap_on_air: false };
const NOT_CONNECTED = { connected: false, ip: "", wifi_ssid: "Kitchen", ap_on_air: false };
const T = 1_000_000; // an arbitrary "now", in ms -- only its relation to the deadline matters

// The fourth argument is the broker's, and `null` is "no save is pending" -- every case in this
// block is about WI-FI, so the broker block decides nothing in any of them. The block of its own
// is further down.
eq(shouldKeepPolling(null, null, T, null), true, "nothing read yet -- keep polling");
eq(shouldKeepPolling(NOT_CONNECTED, null, T, null), true, "not connected -- keep polling");
eq(shouldKeepPolling(CONNECTED, null, T, null), false, "connected, no resubmission pending -- stop");

// The regression this file exists to pin: the owner reprovisions a device they are currently
// connected through, the immediate re-poll still reads the OLD network's `connected: true` (the
// device answers before it retunes), and the loop must survive that reading -- rather than
// declaring victory on the network the attempt is trying to leave -- until the deadline says the
// attempt has had long enough to be decided either way.
eq(
  shouldKeepPolling(CONNECTED, T + 1, T, null),
  true,
  "connected, but the grace deadline has not passed -- a stale reading must not stop the loop",
);
eq(
  shouldKeepPolling(NOT_CONNECTED, T + 1, T, null),
  true,
  "not connected, deadline pending -- still keep polling regardless",
);

// The other half of this file's reason to exist: the deadline must actually expire. A flag that
// only clears on an observation which may never land polls the device for ever; a numeric
// deadline cannot fail to pass.
eq(
  shouldKeepPolling(CONNECTED, T - 1, T, null),
  false,
  "connected, deadline already passed -- trust it and stop",
);
eq(
  shouldKeepPolling(CONNECTED, T, T, null),
  false,
  "connected, deadline exactly now -- passed, not pending -- stop",
);
eq(
  shouldKeepPolling(NOT_CONNECTED, T - 1, T, null),
  true,
  "not connected even after the deadline -- the ordinary clause keeps polling on its own",
);

eq(POLL_INTERVAL_MS, 3000, "poll interval is 3000ms");
eq(
  REPROVISION_GRACE_MS,
  120_000,
  "reprovision grace = firmware trial_timeout_ms (90s) + address_timeout_ms (30s)",
);

// afterPollSuccess / afterPollFailure / afterProvisionAccepted: the loop's own memory, pinned
// here so the bugs review found in the equivalent ref-juggling (dead code, an unbounded
// override) cannot come back the same way in a different shape. Full ProvisionStatus-shaped
// fixtures, not the four-field Where used above -- these functions carry the real status
// through to `status` state in Settings.tsx, not just the fields describeWhere() reads.
// TRANSCRIBED FROM A LIVE DEVICE (the document in a1ce433's hand-over), not composed here. A
// fixture somebody writes out of the type proves the type agrees with that person; the only
// disagreement worth catching is with the FIRMWARE, and this is the one form of it a .ts file can
// hold. The `: ProvisionStatus` annotation is what does the checking, at build time: a key the
// wire sends and the type has not declared is refused as an excess property, and a key the type
// declares as required and the wire does not send cannot be left out. It catches NOTHING at
// runtime -- a word misspelled in both places, or a field the firmware drops tomorrow, passes.
const MQTT_ONLINE: MqttStatus = {
  configured: true, connected: true, published: 453, commands: 0, rejected: 0, reconnects: 1,
  last_connack: 0, last_connack_reason: "no refusal", link: "online", failure: "none",
  attempts: 0, retry_in_ms: null, subscriptions: "none", disconnects: 0, applied_epoch: 1,
  last_transport_errno: 0,
};
const CONNECTED_STATUS: ProvisionStatus = {
  state: "connected",
  mode: "station",
  phase: "online",
  provisioned: true,
  has_credentials: true,
  wifi_known_good: true,
  connected: true,
  wifi_ssid: "Wi-Fight Club",
  ip: "192.168.50.122",
  ap_ssid: "comfoair-8ae5",
  ap_on_air: false,
  setup_window_open: false,
  setup_window_remaining_ms: null,
  fallback_ap_in_ms: null,
  uptime_ms: 192837000,
  failure: "none",
  failure_reason: 0,
  rssi: -58,
  device_id: "9c139ed48ae4",
  mac: "9c:13:9e:d4:8a:e4",
  sw_version: "1.0.0",
  mqtt: MQTT_ONLINE,
};
// The same device mid-trial: spread rather than typed out a second time, because the fields that
// DIFFER are the case and a second hand-written copy is a second thing to keep in step.
const NOT_CONNECTED_STATUS: ProvisionStatus = {
  ...CONNECTED_STATUS,
  state: "trial", phase: "trial", connected: false, ip: "", rssi: null,
  failure: "wrong-password", failure_reason: 15,
};
// The other half of the contract, and the half no invented fixture can express: NULL omits the
// whole `mqtt` member and the three identity keys rather than rendering zeros or empty strings
// (comfoair_wire.h on comfoair_wire_render_provision; comfoair_http_config.c passes NULL for `mqtt`
// on the POST /api/provision reply). This assignment IS the assertion -- if any of the four were
// declared required, the line would not compile -- and it too checks nothing at runtime.
const { mqtt: _mqtt, device_id: _id, mac: _mac, sw_version: _sw, ...WITHOUT_BLOCK } =
  CONNECTED_STATUS;
const BARE_STATUS: ProvisionStatus = WITHOUT_BLOCK;

// The live document, through the sentence the owner actually reads. Cheap, and it is the one case
// here that fails if the transcription above is wrong about a NAME rather than about a type.
eq(describeWhere(CONNECTED_STATUS), "On Wi-Fight Club, at 192.168.50.122.",
   "the document a device really answered, turned into the line above the form");

// afterPollSuccess: always records the answer AND WHEN IT ARRIVED; only clears a pending deadline
// on a CONFIRMED disconnect, never on a connected reading, which is the one shouldKeepPolling()
// warns can be stale.
eq(
  afterPollSuccess(POLL_STATE_INITIAL, CONNECTED_STATUS, T),
  { status: CONNECTED_STATUS, readAtMs: T, reprovisionUntilMs: null, brokerEpochAtSave: null },
  "ordinary connected poll, nothing pending -- recorded, still nothing pending",
);
eq(
  afterPollSuccess({ status: null, readAtMs: null, reprovisionUntilMs: T + 1,
                     brokerEpochAtSave: null }, CONNECTED_STATUS, T),
  { status: CONNECTED_STATUS, readAtMs: T, reprovisionUntilMs: T + 1, brokerEpochAtSave: null },
  "connected reading during a pending grace period -- deadline survives, NOT cleared",
);
eq(
  afterPollSuccess({ status: CONNECTED_STATUS, readAtMs: T - 9, reprovisionUntilMs: T + 1,
                     brokerEpochAtSave: null }, NOT_CONNECTED_STATUS, T),
  { status: NOT_CONNECTED_STATUS, readAtMs: T, reprovisionUntilMs: null, brokerEpochAtSave: null },
  "confirmed disconnect -- deadline cleared early, belt half of belt-and-braces, and the stamp "
    + "moves to this answer rather than staying on the one it replaced",
);

// afterPollFailure: identity, always -- a failed poll changes nothing, first ever or not.
eq(
  afterPollFailure(POLL_STATE_INITIAL),
  POLL_STATE_INITIAL,
  "first poll ever fails -- still null, no special-casing needed",
);
eq(
  afterPollFailure({ status: CONNECTED_STATUS, readAtMs: T - 9, reprovisionUntilMs: T + 1,
                     brokerEpochAtSave: null }),
  { status: CONNECTED_STATUS, readAtMs: T - 9, reprovisionUntilMs: T + 1,
    brokerEpochAtSave: null },
  "a later poll fails -- the last known answer, its age and the deadline all survive untouched. "
    + "The STAMP is the half worth naming: a failed poll that refreshed it would make an old "
    + "answer look new, which is exactly what brokerLinkFrom() reads to decide whether it knows",
);

// afterProvisionAccepted: arms nowMs + REPROVISION_GRACE_MS, unconditionally -- a second
// accepted submission restarts the clock rather than inheriting whatever was left of the first.
eq(
  afterProvisionAccepted(POLL_STATE_INITIAL, T),
  { status: null, readAtMs: null, reprovisionUntilMs: T + REPROVISION_GRACE_MS,
    brokerEpochAtSave: null },
  "first submission accepted -- grace period armed from now",
);
eq(
  afterProvisionAccepted({ status: CONNECTED_STATUS, readAtMs: T - 9,
                           reprovisionUntilMs: T - 500, brokerEpochAtSave: null }, T),
  { status: CONNECTED_STATUS, readAtMs: T - 9, reprovisionUntilMs: T + REPROVISION_GRACE_MS,
    brokerEpochAtSave: null },
  "a second accepted submission restarts the clock, not extends the old one -- and arms nothing "
    + "about the reading itself, which is not what a submission has just refreshed",
);

// --- the broker's verdict, the SECOND thing this loop waits for --------------------------------
//
// The three arguments above are all about WI-FI, and an owner who has just saved a broker is not
// asking about Wi-Fi. What to wait for is written in the firmware, on comfoair_mqtt_backoff.h's
// `applied_epoch`: "a browser that saved a broker remembers this number, then polls until it
// changes AND link is no longer connecting". `brokerEpochAtSave` is that remembered number.
//
// `broker()` moves the only two members the decision reads and leaves the rest of the live
// document alone -- `connected: true` above all, so that in every case below the WI-FI clauses say
// "stop" and the answer is the broker's alone.
function broker(applied_epoch: number, link: MqttLink): ProvisionStatus {
  return { ...CONNECTED_STATUS, mqtt: { ...MQTT_ONLINE, applied_epoch, link } };
}

eq(shouldKeepPolling(CONNECTED_STATUS, null, T, null), false,
   "no save is pending -- the block is read by nobody here and the loop stops on Wi-Fi as before");
eq(shouldKeepPolling(broker(0, "connecting"), null, T, null), false,
   "nothing armed, and the broker happens to be dialling: still stop. `null` is the answer to a "
     + "question nobody asked, and a comparison that reads it as an epoch (0 is not null, so it "
     + "moved) turns every ordinary reconnect into an endless poll");
eq(shouldKeepPolling(broker(4, "online"), null, T, 4), true,
   "the epoch has NOT moved: the publisher task has not rebuilt the client yet, so this `online` "
     + "is the PREVIOUS client's verdict -- stopping here reports it as the new one's");
eq(shouldKeepPolling(broker(5, "connecting"), null, T, 4), true,
   "rebuilt, attempt in flight -- the firmware's own condition is changed AND no longer connecting");
eq(shouldKeepPolling(broker(5, "rejected"), null, T, 4), false,
   "rebuilt and refused -- a verdict the owner can act on, and polling on tells nobody anything");
eq(shouldKeepPolling(broker(5, "online"), null, T, 4), false,
   "rebuilt and connected -- the answer the wait was for");
eq(shouldKeepPolling(broker(5, "backoff"), null, T, 4), false,
   "rebuilt, the attempt failed, a rung is being waited out. `backoff` is a verdict with a wait "
     + "attached, not an open question -- and this is the case that separates 'link is connecting' "
     + "from the looser 'link is not online'");
eq(shouldKeepPolling(broker(5, "off"), null, T, 4), false,
   "the owner cleared the host: the epoch moves for that too (hardware scenario HV-MQTT-21) and "
     + "`off` is the whole answer -- a page waiting for `online` would wait for ever");
eq(shouldKeepPolling(broker(0, "connecting"), null, T, 0), true,
   "epoch 0 is a LEGAL remembered value -- a device that has never been reconfigured reports 0 -- "
     + "so a guard written `if (!brokerEpochAtSave)` reads a save that was made as one that was not");
eq(shouldKeepPolling(broker(0, "online"), null, T, 7), true,
   "the counter went BACKWARDS: it counts rebuilds since boot, so 0 under a remembered 7 is a "
     + "device that rebooted, and this `online` answers a question nobody asked. `!==` takes it "
     + "for our verdict and stops; `>` keeps waiting for the rebuild we are owed");
eq(shouldKeepPolling(BARE_STATUS, null, T, 4), false,
   "no `mqtt` member at all: there is no epoch to compare, and reading its absence as a change "
     + "(undefined is not 4, so it moved) ends the wait on a document that says nothing at all "
     + "about a broker");
eq(shouldKeepPolling({ ...broker(5, "rejected"), connected: false }, null, T, 4), true,
   "Wi-Fi is gone: the first clause still wins, and a settled broker cannot stop a loop that is "
     + "waiting for a network");
eq(shouldKeepPolling(broker(5, "rejected"), T + 1, T, 4), true,
   "a resubmission's grace period is still open -- that override outranks a settled broker too, "
     + "because the document it is settled in may be the pre-attempt one");

// afterBrokerSaveAccepted: remembers the number standing in the LAST ANSWER, which is the one the
// next document has to beat. Nothing else about the state is a save's business.
eq(
  afterBrokerSaveAccepted({ status: CONNECTED_STATUS, readAtMs: T - 9, reprovisionUntilMs: null,
                            brokerEpochAtSave: null }),
  { status: CONNECTED_STATUS, readAtMs: T - 9, reprovisionUntilMs: null, brokerEpochAtSave: 1 },
  "the epoch of the last answer is what a save remembers -- the reading itself, its age and any "
    + "Wi-Fi deadline are untouched",
);
eq(
  afterBrokerSaveAccepted(POLL_STATE_INITIAL),
  POLL_STATE_INITIAL,
  "nothing read yet: there is no number to remember, and arming 0 against a document never seen "
    + "would end the wait on the first answer that arrives",
);
eq(
  afterBrokerSaveAccepted({ status: CONNECTED_STATUS, readAtMs: T - 9,
                            reprovisionUntilMs: T + 1, brokerEpochAtSave: null }),
  { status: CONNECTED_STATUS, readAtMs: T - 9, reprovisionUntilMs: T + 1,
    brokerEpochAtSave: 1 },
  "a Wi-Fi resubmission is still inside its grace period and a broker save lands on top of it: "
    + "the deadline is NOT the broker's to clear -- dropping it here would let the pre-attempt "
    + "`connected: true` stop the loop the moment the broker settles",
);
eq(
  afterBrokerSaveAccepted({ status: BARE_STATUS, readAtMs: T, reprovisionUntilMs: null,
                            brokerEpochAtSave: 3 }),
  { status: BARE_STATUS, readAtMs: T, reprovisionUntilMs: null, brokerEpochAtSave: null },
  "the last answer carried no broker block -- there is nothing to remember, and the epoch armed "
    + "by an earlier save is not the number this one has to beat",
);
eq(
  afterPollSuccess({ status: null, readAtMs: null, reprovisionUntilMs: null,
                     brokerEpochAtSave: 3 }, CONNECTED_STATUS, T),
  { status: CONNECTED_STATUS, readAtMs: T, reprovisionUntilMs: null, brokerEpochAtSave: 3 },
  "a poll answering does not forget what the page is waiting past: the wait ends when the epoch "
    + "moves, and the poll carrying that answer is the one best placed to lose it",
);

// --- how old the answer is ---------------------------------------------------------------------
//
// It exists because the loop STOPS. shouldKeepPolling() is a statement about Wi-Fi, so a device
// already on the network is polled exactly once and everything downstream reads a document of
// unbounded age. broker.ts turns this number into "connected" or "we could not ask", and that is
// the difference between a save that deletes every entity silently and one that stops to ask.
eq(statusAgeMs(POLL_STATE_INITIAL, T), Number.POSITIVE_INFINITY,
   "nothing read yet is INFINITELY old, not zero: 'we have never had an answer' and 'the answer "
     + "is ancient' are the same thing to everyone downstream, and only one of them survives a "
     + "comparison somebody writes later");
eq(statusAgeMs({ status: CONNECTED_STATUS, readAtMs: T, reprovisionUntilMs: null,
                 brokerEpochAtSave: null }, T), 0,
   "an answer that has just landed is not old at all");
eq(statusAgeMs({ status: CONNECTED_STATUS, readAtMs: T - 4500, reprovisionUntilMs: null,
                 brokerEpochAtSave: null }, T), 4500,
   "and after that it is exactly as old as the clock says -- not rounded, not clamped");

report("status");
