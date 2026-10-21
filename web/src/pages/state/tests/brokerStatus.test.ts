// brokerStatus.ts, pinned sentence by sentence.
//
// Run: node src/pages/state/tests/brokerStatus.test.ts
//
// EVERY EXPECTED HEADLINE BELOW IS A LITERAL, and that is the whole design of this suite rather
// than a style preference. The defect this tree has produced most often is a guard that checked
// something other than what its comment promised, and the shape it takes in a test is an
// expectation built by CALLING the function under test, or by importing the very constant the
// function returns. The suite for the broker-notice work did exactly that and stayed 814/814
// green while one word of a verdict was swapped for a word of the neighbouring enumeration. A
// literal cannot do that: it is a second, independent writing of the sentence, and the diff
// between the two writings is the only thing that can catch a rename.
//
// So: nothing here imports a string constant from brokerStatus.ts, and nothing builds an
// expectation from describeBroker()'s own output. `toneLabel` is exercised the same way.

import type { MqttStatus } from "../../../api/status.ts";
import {
  BOOT_GRACE_MS,
  brokerAlert,
  describeBroker,
  toneLabel,
} from "../brokerStatus.ts";
import { eq, ok, report } from "../../settings/tests/harness.ts";

// --- fixtures --------------------------------------------------------------------------------

const ADDRESS = { host: "mqtt.lan", port: 1883 };

/** Long enough that the five-minute boot grace is over -- every case that does not name uptime
 *  is a device that has been up for an hour, which is the state an owner reads this card in. */
const UP = 3_600_000;

/** Every one of the sixteen members, so a case can override exactly what it is about. The values
 *  are a device that has just been switched on and has done nothing yet. */
function broker(over: Partial<MqttStatus> = {}): MqttStatus {
  return {
    configured: true,
    connected: false,
    published: 0,
    commands: 0,
    rejected: 0,
    reconnects: 0,
    last_connack: 0,
    last_connack_reason: "",
    link: "off",
    failure: "none",
    attempts: 0,
    retry_in_ms: null,
    subscriptions: "none",
    disconnects: 0,
    applied_epoch: 0,
    last_transport_errno: 0,
    ...over,
  };
}

/** A broker that is down for `failure`, with a retry pending -- the ordinary shape of every
 *  failure row in the table. */
function failing(failure: MqttStatus["failure"], over: Partial<MqttStatus> = {}): MqttStatus {
  return broker({ link: "backoff", failure, attempts: 3, retry_in_ms: 8000, ...over });
}

// --- the block is absent, which is not the same answer as a block of zeros --------------------
//
// comfoair_wire.h's contract as a test: NULL omits the member, and the reply to POST
// /api/provision is rendered before the radio has moved, so it carries no block at all. A page
// that read `mqtt === undefined` as "not connected" would accuse a device that was never asked.

const absent = describeBroker(undefined, ADDRESS, UP);
eq(absent.headline, "The device said nothing about a broker", "absent block: headline");
eq(
  absent.detail,
  "This answer carries no broker section at all, which is different from every counter being zero.",
  "absent block: detail",
);
eq(absent.tone, "pending", "absent block is not painted as a fault");

const zeros = describeBroker(broker(), ADDRESS, UP);
ok(
  zeros.headline !== absent.headline,
  "a block of zeros and no block at all do NOT get the same headline",
);

// --- configured=false: no broker was ever saved -----------------------------------------------

const off = describeBroker(broker({ configured: false }), ADDRESS, UP);
eq(off.headline, "MQTT is switched off", "configured=false: headline");
eq(
  off.detail,
  "No broker address is saved, so Home Assistant cannot see this device. "
    + "Fill in the broker on the Settings page and save.",
  "configured=false: detail",
);
eq(off.tone, "pending", "an unconfigured broker is a choice, not a fault");

// --- link=connecting --------------------------------------------------------------------------

const connecting = describeBroker(broker({ link: "connecting" }), ADDRESS, UP);
eq(connecting.headline, "Connecting to the broker", "link=connecting: headline");
eq(
  connecting.detail,
  "The device is dialling mqtt.lan. This normally takes a second or two.",
  "link=connecting: detail names the host from the config document",
);
eq(connecting.tone, "pending", "a dial in progress is waiting, not failing");

// --- link=online -------------------------------------------------------------------------------

const online = describeBroker(
  broker({
    link: "online",
    connected: true,
    subscriptions: "granted",
    published: 412,
    commands: 7,
    reconnects: 2,
  }),
  ADDRESS,
  UP,
);
eq(online.headline, "Connected to the broker", "link=online, subscriptions=granted: headline");
eq(
  online.detail,
  "Published 412 readings, accepted 7 commands. Reconnects: 2.",
  "link=online: the counters are the evidence",
);
eq(online.tone, "pending", "a working broker is the neutral tone of this vocabulary");

// --- subscriptions=refused, shown OVER link=online ---------------------------------------------
//
// The one case where "connected" and "not working" are both true. The headline has to be about
// the subscriptions; the publish counters stay in the card below as proof that half of it works.

const refused = describeBroker(
  broker({
    link: "online",
    connected: true,
    subscriptions: "refused",
    published: 412,
    commands: 0,
  }),
  ADDRESS,
  UP,
);
eq(
  refused.headline,
  "The broker will not let this device listen",
  "subscriptions=refused beats link=online for the headline",
);
eq(
  refused.detail,
  "Readings are being published, and commands from Home Assistant will not arrive: the broker "
    + "refused the command subscriptions. This is a broker ACL, not a device fault.",
  "subscriptions=refused: detail",
);
eq(refused.tone, "denied", "a refusal by the broker is denied, not a generic error");
ok(
  refused.headline !== online.headline,
  "a refused subscription does not report itself as a healthy connection",
);

// --- subscriptions=pending ---------------------------------------------------------------------

const pendingSubs = describeBroker(
  broker({ link: "online", connected: true, subscriptions: "pending" }),
  ADDRESS,
  UP,
);
eq(pendingSubs.headline, "Waiting for the broker to confirm", "subscriptions=pending: headline");
eq(
  pendingSubs.detail,
  "Connected; the command subscriptions have not been acknowledged yet.",
  "subscriptions=pending: detail",
);
eq(pendingSubs.tone, "pending", "an unacknowledged SUBACK is waiting, not a refusal");

// --- one headline per failure class --------------------------------------------------------------
//
// Each of these is a different thing to DO about it, which is the entire reason the firmware
// spends an enumeration on them. Two classes sharing one headline would throw that away in the
// one place the owner reads.

const dns = describeBroker(failing("dns"), ADDRESS, UP);
eq(dns.headline, "The broker's name does not resolve", "failure=dns: headline");
eq(
  dns.detail,
  "The device asked the router for mqtt.lan and got no answer. Check the spelling, or enter "
    + "the broker's IP address instead. Next attempt in 8 s.",
  "failure=dns: detail",
);
eq(dns.tone, "error", "failure=dns: tone");

const tcp = describeBroker(failing("tcp"), ADDRESS, UP);
eq(tcp.headline, "Nothing is answering at that address", "failure=tcp: headline");
eq(
  tcp.detail,
  "mqtt.lan:1883 did not accept a connection. The broker may be switched off, or the port may "
    + "be wrong. The device keeps trying — next attempt in 8 s.",
  "failure=tcp: detail carries host AND port, both from the config document",
);
eq(tcp.tone, "error", "failure=tcp: tone");

const unavailable = describeBroker(failing("unavailable"), ADDRESS, UP);
eq(unavailable.headline, "The broker is not ready yet", "failure=unavailable: headline");
eq(
  unavailable.detail,
  "It answered and said it is unavailable — usually a broker that is still starting. "
    + "Next attempt in 8 s.",
  "failure=unavailable: detail",
);
eq(unavailable.tone, "error", "failure=unavailable: tone");

const protocol = describeBroker(
  failing("protocol", {
    link: "rejected",
    retry_in_ms: 300000,
    last_connack: 1,
    last_connack_reason: "unacceptable protocol version",
  }),
  ADDRESS,
  UP,
);
eq(protocol.headline, "The broker refused the connection", "failure=protocol: headline");
eq(
  protocol.detail,
  'It answered "unacceptable protocol version" (code 1). This is not about the password: check '
    + "the broker's own settings. The device retries every five minutes.",
  "failure=protocol: detail quotes the broker's own words and its code",
);
eq(protocol.tone, "error", "failure=protocol: tone");

const badCreds = describeBroker(
  failing("bad-credentials", {
    link: "rejected",
    retry_in_ms: 300000,
    last_connack: 5,
    last_connack_reason: "not authorized",
  }),
  ADDRESS,
  UP,
);
eq(
  badCreds.headline,
  "The broker rejected the username or password",
  "failure=bad-credentials: headline",
);
eq(
  badCreds.detail,
  'It answered "not authorized" (code 5). Correct them on the Settings page and save — the '
    + "device retries at once, without waiting out the five-minute pause.",
  "failure=bad-credentials: detail",
);
eq(badCreds.tone, "denied", "failure=bad-credentials: tone");
ok(
  badCreds.headline !== protocol.headline,
  "a rejected password and a rejected protocol are not told with one sentence",
);

const dropped = describeBroker(
  failing("dropped", { disconnects: 9, reconnects: 3 }),
  ADDRESS,
  UP,
);
eq(dropped.headline, "The connection to the broker keeps dropping", "failure=dropped: headline");
eq(
  dropped.detail,
  "It accepted the device and then closed the link 9 times. Another device using the same "
    + "client id will do this. Next attempt in 8 s.",
  "failure=dropped: detail, with the two counters far apart",
);
eq(dropped.tone, "error", "failure=dropped: tone");

// Every headline in the table is its own sentence. Compared as a set, so a mutation that gives
// two classes one wording fails here even if it slips past the per-class cases above.
const headlines = [
  absent.headline,
  off.headline,
  connecting.headline,
  online.headline,
  refused.headline,
  pendingSubs.headline,
  dns.headline,
  tcp.headline,
  unavailable.headline,
  protocol.headline,
  badCreds.headline,
  dropped.headline,
];
eq(new Set(headlines).size, headlines.length, "no two verdicts share a headline");

// --- identity conflict: a sentence, because it is not a value the broker can send ---------------
//
// "A second device signed in with our client id" is not observable on the wire -- the broker
// sends an ordinary disconnect. It is observable as reconnects and disconnects climbing
// together, so it is a line of text and never an enumeration member.

const flapping = describeBroker(
  failing("dropped", { disconnects: 14, reconnects: 14 }),
  ADDRESS,
  UP,
);
eq(
  flapping.detail,
  "It accepted the device and then closed the link 14 times. Another device using the same "
    + "client id will do this. 14 reconnects against 14 disconnects: climbing together is what "
    + "that looks like from here, because the broker cannot report it any other way. "
    + "Next attempt in 8 s.",
  "reconnects and disconnects climbing together are named in words",
);
ok(
  !dropped.detail.includes("climbing together"),
  "the identity sentence is NOT printed when the two counters are far apart",
);

// --- retry_in_ms: null is "nothing is scheduled", never "in 0 s" ---------------------------------

const noRetry = describeBroker(failing("dns", { retry_in_ms: null }), ADDRESS, UP);
ok(!noRetry.detail.includes("0 s"), "retry_in_ms=null does not become 'next attempt in 0 s'");
ok(
  !noRetry.detail.toLowerCase().includes("next attempt"),
  "retry_in_ms=null drops the whole 'next attempt' sentence rather than printing a number",
);
eq(
  noRetry.detail,
  "The device asked the router for mqtt.lan and got no answer. Check the spelling, or enter "
    + "the broker's IP address instead.",
  "retry_in_ms=null: the rest of the sentence is unchanged",
);
eq(noRetry.headline, dns.headline, "a missing retry time does not change what went wrong");

// A retry under a second still reads as a whole second: rounding 400 ms to "0 s" would print the
// one sentence the sentinel exists to keep off the screen.
const soon = describeBroker(failing("dns", { retry_in_ms: 400 }), ADDRESS, UP);
ok(soon.detail.includes("Next attempt in 1 s."), "a sub-second retry rounds up to 1 s, not 0 s");

const rounded = describeBroker(failing("dns", { retry_in_ms: 8600 }), ADDRESS, UP);
ok(rounded.detail.includes("Next attempt in 9 s."), "the retry time is shown in whole seconds");

// --- host and port come from the config document, never from the status document ------------------
//
// The status document has no host in it at all, deliberately (the diagnostic struct is counters
// only), so this is not a preference between two sources -- it is the only source there is, and
// a case that pins it is what stops somebody adding the host to the wire "to help debugging".

const elsewhere = describeBroker(failing("tcp"), { host: "10.0.0.4", port: 8883 }, UP);
ok(
  elsewhere.detail.startsWith("10.0.0.4:8883 "),
  "the address in the sentence is the one the owner typed into Settings",
);
ok(!elsewhere.detail.includes("mqtt.lan"), "no other host leaks into the sentence");

// --- a word the firmware grew and this page did not ------------------------------------------------
//
// All three unions carry "unknown" because all three naming functions in the firmware return it
// for a value outside the enumeration. A page that quietly rendered one of those as "connected"
// would be the one lie this whole card exists to prevent.

const strange = describeBroker(broker({ link: "unknown", failure: "unknown" }), ADDRESS, UP);
eq(
  strange.headline,
  "The device reported a broker state this page does not recognise",
  "an unrecognised word gets its own verdict",
);
eq(strange.tone, "error", "an unrecognised word is a fault, not a wait");
ok(
  strange.detail.includes('link "unknown"'),
  "the unrecognised words are quoted, so the reader can report them",
);

const contradiction = describeBroker(
  broker({ link: "backoff", failure: "none", attempts: 4, retry_in_ms: 8000 }),
  ADDRESS,
  UP,
);
eq(
  contradiction.headline,
  "The device did not say why the broker is unreachable",
  "backing off with no failure recorded is reported as the contradiction it is",
);
eq(contradiction.tone, "error", "an unexplained backoff is a fault");

const idle = describeBroker(broker({ link: "off", failure: "none" }), ADDRESS, UP);
eq(
  idle.headline,
  "The device has not dialled the broker yet",
  "a saved broker with no attempt yet is its own verdict",
);
eq(idle.tone, "pending", "the seconds after a restart are a wait, not a fault");

// --- the five-minute boot grace ---------------------------------------------------------------------

const young = describeBroker(failing("tcp"), ADDRESS, 120_000);
ok(
  young.detail.endsWith(
    "The device has been running for under five minutes, so the house may still be coming up.",
  ),
  "inside the boot grace the card still says what broke, and adds why it may be too early to act",
);
eq(young.headline, tcp.headline, "the boot grace never changes WHAT is wrong");
eq(young.tone, tcp.tone, "the boot grace never repaints the tone");
ok(
  !dns.detail.includes("under five minutes"),
  "an hour-old device is not told the house may still be coming up",
);
ok(
  !describeBroker(broker({ link: "online", subscriptions: "granted" }), ADDRESS, 1000).detail
    .includes("under five minutes"),
  "a healthy broker gets no boot-grace excuse",
);

// --- the dashboard line: silent while all is well --------------------------------------------

eq(
  brokerAlert(broker({ link: "online", connected: true, subscriptions: "granted" }), UP),
  null,
  "a working broker puts NOTHING on the dashboard",
);
eq(
  brokerAlert(broker({ link: "connecting" }), UP),
  null,
  "a dial in progress puts nothing on the dashboard",
);
eq(
  brokerAlert(broker({ configured: false }), UP),
  null,
  "an owner who runs no broker is not nagged on every visit to the dashboard",
);
eq(
  brokerAlert(undefined, UP),
  null,
  "a document with no broker block accuses nobody on the dashboard",
);
eq(
  brokerAlert(broker({ link: "online", connected: true, subscriptions: "pending" }), UP),
  null,
  "an unacknowledged SUBACK is a second, not a fault to interrupt the dashboard with",
);
eq(
  brokerAlert(broker({ link: "off", failure: "none" }), UP),
  null,
  "a broker not yet dialled puts nothing on the dashboard",
);

// --- the dashboard line: it speaks, and it says what is broken ------------------------------------------

const alertTcp = brokerAlert(failing("tcp"), UP);
eq(
  alertTcp?.headline,
  "Home Assistant is not receiving data from this device",
  "the dashboard names the consequence, not 'no connection'",
);
eq(alertTcp?.detail, "Nothing is answering at that address", "the dashboard carries the cause");
eq(alertTcp?.tone, "error", "the dashboard line keeps the verdict's tone");

const alertDns = brokerAlert(failing("dns"), UP);
eq(alertDns?.detail, "The broker's name does not resolve", "a different cause, a different line");
ok(
  alertDns?.detail !== alertTcp?.detail,
  "the dashboard line is derived from the verdict, not written once for all failures",
);

const alertRefused = brokerAlert(
  broker({ link: "online", connected: true, subscriptions: "refused", published: 90 }),
  UP,
);
eq(
  alertRefused?.headline,
  "Home Assistant cannot control this device",
  "readings still arriving and commands refused is a DIFFERENT sentence from a dead link",
);
ok(
  alertRefused?.headline !== alertTcp?.headline,
  "the dashboard headline is not independent of the verdict",
);
eq(alertRefused?.tone, "denied", "the refusal keeps its own tone on the dashboard too");

const alertStrange = brokerAlert(broker({ link: "unknown", failure: "unknown" }), UP);
ok(alertStrange !== null, "a state the page cannot name is still worth interrupting the dashboard");

// The dashboard line takes no address, because no headline in the table names a host or a port --
// that is what lets the dashboard skip GET /api/config entirely. The placeholder brokerAlert()
// hands describeBroker() must therefore never reach a screen, and this is that claim as a case
// rather than as a reading of the code.
for (const alert of [alertTcp, alertDns, alertRefused, alertStrange]) {
  ok(
    !`${alert?.headline} ${alert?.detail}`.includes("no address was needed"),
    "the dashboard line never prints the placeholder address it was built with",
  );
}

// --- the dashboard line inside the boot grace ---------------------------------------------------------
//
// Same threshold as the LED's, same source (uptime_ms), and deliberately NOT the same rule --
// see brokerStatus.ts's comment on BOOT_GRACE_MS.

eq(
  brokerAlert(failing("tcp"), 120_000),
  null,
  "a broker that has been unreachable for two minutes of uptime does not interrupt the dashboard",
);
ok(
  brokerAlert(failing("tcp"), BOOT_GRACE_MS + 1) !== null,
  "past the grace, the dashboard says it",
);
ok(
  describeBroker(failing("tcp"), ADDRESS, 120_000).headline !== "",
  "the card never waits out the grace -- it is the screen the owner opened to be told",
);

// --- colour is never the only carrier ------------------------------------------------------------------
//
// The badge on the card and the dot on the dashboard both take their word from here, so the rule
// is checkable rather than only reviewable: a tone with no word would fail this.

eq(toneLabel("pending"), "Waiting", "tone pending has a word");
eq(toneLabel("denied"), "Refused", "tone denied has a word");
eq(toneLabel("error"), "Problem", "tone error has a word");
eq(toneLabel("offline"), "No answer", "tone offline has a word");
const labels = [toneLabel("pending"), toneLabel("denied"), toneLabel("error"), toneLabel("offline")];
eq(new Set(labels).size, 4, "four tones, four different words -- the colours are not the message");
ok(
  labels.every((l) => l.trim() !== ""),
  "no tone renders as a bare colour with an empty label",
);

report("brokerStatus");
