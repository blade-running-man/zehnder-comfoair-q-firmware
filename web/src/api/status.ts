// The `mqtt` block of GET /api/status: its three vocabularies and its sixteen members, and
// nothing else. The call that fetches the document is getStatus() in client.ts, where it already
// was; only the type of this one member moved out.
//
// WHY A FILE OF ITS OWN, arithmetic rather than taste: client.ts stands at 426 lines against the
// 350-line ceiling for a hand-written file (AGENTS.md), so it is the one file in this directory
// that must not grow by sixteen members and their reasons. getStatus() cannot come here instead --
// three modules import it and ProvisionStatus by name.
//
// THESE WORDS ARE THE FOURTH WRITING OF THIS VOCABULARY, and nothing checks the four against each
// other. comfoair_mqtt_backoff.h declares the three enumerations, comfoair_mqtt_backoff.c spells
// them (comfoair_mqtt_link_name, comfoair_mqtt_fail_name, comfoair_mqtt_subs_name),
// test/test_mqtt_backoff pins those spellings, and this file repeats them for TypeScript. The
// entity list has a generator and cannot drift; this vocabulary has none and is not given one
// here. So a rename in the firmware lands as a page that quietly stops recognising a word -- no
// red build on either side, no failing test, and the first person to notice is an owner reading a
// verdict the page could not name. What catches it is a human: the MQTT hardware scenarios
// (docs/hardware-verification/06-mqtt.md, e.g. HV-MQTT-10/16/21) read /api/status off a live
// device -- compare its words against these by eye.
//
// `unknown` is in all three unions because all three naming functions can RETURN it -- for a value
// outside the enumeration (comfoair_mqtt_backoff.c, the fallthrough under each switch). A reader
// that switches over one of these needs a branch for it, and a union without it would tell that
// reader the branch is unreachable.

/** mqtt.link -- comfoair_mqtt_link_name(). "link" and not "state": the document's top-level
 *  `state` is the PROVISIONING machine's, and two members named alike meaning different things is
 *  a prepared confusion (comfoair_mqtt_backoff.h). */
export type MqttLink = "off" | "connecting" | "online" | "backoff" | "rejected" | "unknown";

/** mqtt.failure -- comfoair_mqtt_fail_name(). NOT the `failure` at the top level of the same
 *  document: that one is the Wi-Fi's, and the pair is told apart by position, which is the one
 *  thing the nested block gives for free. */
export type MqttFailure =
  | "none" | "dns" | "tcp" | "unavailable" | "protocol" | "bad-credentials" | "dropped"
  | "unknown";

/** mqtt.subscriptions -- comfoair_mqtt_subs_name(). It answers what no other member does: a broker
 *  whose ACL forbids SUBSCRIBE accepts the connection and every publish, so `link` is `online`,
 *  the readings arrive, and not one button in Home Assistant does anything. */
export type Subscriptions = "none" | "pending" | "granted" | "refused" | "unknown";

/**
 * The broker's counters as the device reports them -- comfoair_wire_mqtt_t, member for member and
 * in its order.
 *
 * A SNAPSHOT AND NOT AN INSTANT: every counter is read from a publisher task that keeps moving
 * them, so two members may disagree by one publish and no reader may treat them as simultaneous
 * (comfoair_wire.h).
 *
 * Whoever holds one of these must have got it from a document that HAD the block. Absent is its
 * own answer, and it is spelled by `mqtt` being optional on the status type, never by a value in
 * here.
 */
export interface MqttStatus {
  /** A broker host is stored -- NOT that one was ever reached. `configured: true` beside
   *  `connected: false` is a diagnosis, and one flag covering both would erase it. */
  configured: boolean;
  connected: boolean;
  published: number;
  /** Accepted from the broker. */
  commands: number;
  /** Arrived and were refused: bad topic, bad payload, or retained. */
  rejected: number;
  reconnects: number;
  /** The CONNACK refusal code, 0 when none was recorded. MQTT 3.1.1 section 3.2.2.3. */
  last_connack: number;
  /** The same code as a sentence, from the firmware's own comfoair_wire_connack_name() -- the
   *  number an owner can look up and the words they can read cannot come to disagree. */
  last_connack_reason: string;
  link: MqttLink;
  failure: MqttFailure;
  /** Failed attempts since the last SETTLED session -- sixty seconds of one, not a connection that
   *  merely happened. Saturates with the delay. */
  attempts: number;
  /** null is "NO attempt is scheduled", never "in 0 ms": the first is a dial already in flight.
   *  Same sentinel, same spelling as `fallback_ap_in_ms` at the top level. */
  retry_in_ms: number | null;
  subscriptions: Subscriptions;
  /** Sessions lost. The PAIR with `reconnects` is the point: a second device dialling the broker
   *  with the same client id is not observable on the wire, but it is observable as these two
   *  climbing together while `connected` flickers. */
  disconnects: number;
  /** How many times the publisher task has rebuilt the client, counted AFTER the rebuild. An
   *  epoch of APPLICATION and not of request: a page that saved a broker remembers this number and
   *  polls until it changes AND `link` is no longer "connecting" -- until then the rest of this
   *  block still describes the client the save replaced. See shouldKeepPolling() in
   *  pages/settings/status.ts, which is the only reader of it. */
  applied_epoch: number;
  /** The raw esp_transport_sock_errno; ECONNREFUSED, ETIMEDOUT and EHOSTUNREACH ask three
   *  different things of the owner and the class `tcp` collapses all three. A NUMBER AND NEVER
   *  null: 0 is "none recorded", which a connect that timed out produces by construction, because
   *  esp-mqtt dispatches no error event for it at all. */
  last_transport_errno: number;
}
