// Turning the device's provisioning status into sentences, and the policy for the poll loop in
// useProvisionStatus.ts that keeps that status current.
//
// Pure, and pinned by tests/status.test.ts. Two jobs live here rather than one: the firmware
// distinguishes FOUR reasons a network did not work, and each has something different to do about
// it -- collapsing them back into "it will not connect" would undo a phase 5 invariant in the one
// place the owner actually reads -- and the poll loop's own decisions (what a successful poll
// does to what is remembered, what a failed one does, what an accepted submission arms) are
// exactly the kind of thing worth being wrong about, which is the whole reason they live in a
// module a test can call rather than as ref-juggling inside the component. A review round found
// two of three defects in that ref-juggling, uncaught by any test because there was nothing here
// a test could reach -- see PollState and the three functions that transition it, below.
import type { ProvisionStatus } from "../../api/client";
import type { MqttLink } from "../../api/status";
import { t } from "../../i18n/index.ts";

/** The subset of ProvisionStatus these sentences need. */
export interface Where {
  connected: boolean;
  ip: string;
  wifi_ssid: string;
  ap_on_air: boolean;
}

export function describeFailure(failure: string, reason: number): string {
  switch (failure) {
    case "none":
      return "";
    case "network-not-found":
      // "no 5 GHz radio" is checkable, not folklore: the board is esp32-c6-devkitm-1
      // (platformio.ini:24), and the C6 has one 2.4 GHz radio and none at 5 GHz.
      return t("settings.status.failure.notFound");
    case "wrong-password":
      return t("settings.status.failure.wrongPassword");
    case "refused":
      return t("settings.status.failure.refused");
    case "no-address":
      return t("settings.status.failure.noAddress");
    default:
      // A word this page has no wording for is still shown. The firmware decides these names and
      // may add one; a page that answered nothing would hide the only diagnosis there is.
      return t("settings.status.failure.unknown", { failure, reason });
  }
}

export function describeWhere(w: Where): string {
  if (w.connected) return t("settings.status.where.connected", { ssid: w.wifi_ssid, ip: w.ip });
  if (w.ap_on_air) return t("settings.status.where.setupAp");
  if (w.wifi_ssid !== "") return t("settings.status.where.trying", { ssid: w.wifi_ssid });
  return t("settings.status.where.none");
}

/**
 * How often useProvisionStatus.ts polls GET /api/status while the device is not yet connected.
 *
 * 3000ms, not less: the device is one HTTP task serving a small number of sockets
 * (comfoair_http.c), and the moment this polling matters -- right after POST /api/provision --
 * is exactly the moment its radio is busiest, retuning to try the new network
 * (comfoair_http_config.c:104-108 on the request racing the retune -- it answers BEFORE the station
 * moves to the new network's channel, precisely so this poll has something to ask). A tighter
 * interval would spend more of that scarce budget asking "are you there yet" than the device has
 * to answer with.
 */
export const POLL_INTERVAL_MS = 3000;

/**
 * How long the loop keeps polling PAST a resubmission even though the last thing it read
 * says `connected: true` -- see shouldKeepPolling() below for why that reading can be a lie.
 *
 * The sum of two named firmware bounds, not a bare number picked to taste, and it has to be the
 * sum rather than either one alone -- a wrong password does not settle in one phase, it settles
 * in two, back to back:
 *
 *   * `trial_timeout_ms` (comfoair_provision.c:161, 90 * 1000) -- "How long a newly saved pair
 *     gets to produce an address before the known-good pair is put back" (comfoair_provision.h).
 *     This is where the NEW pair is given up on: comfoair_provision.c:439-451 (tick(), step 2)
 *     answers by starting a rollback (COMFOAIR_PROV_ACTION_RESTORE_CREDENTIALS) when a known-good
 *     pair exists, or by retrying the pair the owner just typed when it does not. Either way,
 *     `s->failure` is left exactly as the disconnect during the trial classified it
 *     (comfoair_provision.c:440-451 touches state, retry_armed, attempts and
 *     credentials_committed, and nothing else) -- the trial ending is NOT yet the device being
 *     reachable again with that answer.
 *   * `address_timeout_ms` (comfoair_provision.c:162, 30 * 1000) -- "How long an associated
 *     station gets to be given an address" -- is the rollback's OWN clock: the restored,
 *     known-good pair still has to associate and be handed an address before the device answers
 *     GET /api/status again. Only once that succeeds is `connected: true` with the standing
 *     `failure: wrong-password` actually on the wire for a poll to read.
 *
 * 120000ms total. That is the firmware's own bound on how long a freshly submitted pair gets to
 * either connect, or fail AND have the rollback that follows produce a reachable answer: past it,
 * the state machine has necessarily finished both phases, so a `connected: true` reading still
 * standing can be trusted at face value again.
 *
 * DO NOT shorten this to trial_timeout_ms alone "because that is when the pair is decided". The
 * pair being decided is not the device being reachable with that decision on it -- the owner's
 * browser is on the LAN the device just left for the whole trial, and the rollback that makes the
 * device reachable again happens AFTER the trial ends, not during it. A deadline that only covers
 * the trial expires seconds before the one poll that would have shown the answer, which is the
 * entire point of this file. DO NOT shorten it "to poll less" for any other reason either: a
 * shorter bound can equally lapse while the browser simply has not yet been able to reach the
 * device, at which point the loop would trust a `connected: true` reading that is still the OLD
 * network from before the attempt -- the original bug this constant exists to close.
 */
export const REPROVISION_GRACE_MS = 90_000 + 30_000;

/**
 * Whether the loop in useProvisionStatus.ts should still be polling.
 *
 * `last` is the last status read: `null` means nothing has been read yet -- keep polling, the
 * same as "read, but not connected": both are "we do not yet know it is on the network".
 *
 * `reprovisionUntilMs` and `nowMs` together are the second, necessary input, and they exist to
 * cover a specific lie `last.connected` can tell. POST /api/provision answers BEFORE the device
 * starts connecting (comfoair_http_config.c:104-108 answers; comfoair_net.c:300-305 puts the actual
 * connection event on the queue afterwards, off a separate task), and `connected` is
 * `g_net_have_address`, which stays true until the disconnect/IP-lost event fires
 * (comfoair_net.c:140,145). So the OWNER'S OWN device -- the common case, since this page is
 * normally served over the network being replaced -- reads back `connected: true` for a beat
 * after a resubmission that is about to fail: the poll answers before the radio has actually let
 * go of the old network. Stopping on that reading would freeze the section on the stale,
 * pre-attempt network forever, which is the one flow this whole file exists to get right.
 *
 * `reprovisionUntilMs` is a DEADLINE, not a flag, because a flag cleared by "observed
 * `connected: false`" can go uncleared for ever: that observation is a STATE, used here as proof
 * of a TRANSITION, and states can be missed entirely -- during exactly the window this exists
 * for, the browser is usually unable to reach the device at all (it is on the network being
 * left), so the poll that would have cleared a flag never lands, and the next one that succeeds
 * can just as well already show the NEW network's `connected: true`, which a flag would then
 * hold open for ever with nothing left to close it. Only a monotonic bound guarantees the
 * override itself ends. `null` means no resubmission is pending, i.e. the ordinary case: trust
 * `last.connected` outright.
 *
 * useProvisionStatus.ts sets the deadline to `nowMs + REPROVISION_GRACE_MS` the moment a
 * submission is
 * accepted (both the plain-200 path and the "no answer, which after a provision request means
 * accepted" path -- see saveWifi() in Settings.tsx), and additionally clears it early -- belt and braces --
 * the first time a poll comes back with `connected: false`, which when it happens IS reliable
 * proof the stale reading has been superseded by a real one. Either way, once cleared or expired,
 * `last.connected` is trusted outright again: a poll seen after that either reports the new
 * network (stop) or a `failure` word with `connected` still false (the ordinary `!last.connected`
 * clause already keeps polling for that, no override needed).
 *
 * `brokerEpochAtSave` is the THIRD input and the only one that is not about Wi-Fi at all. Every
 * clause above answers "is the device on the owner's network", and an owner who has just saved a
 * broker is not asking that: on a device already on its network the first poll is also the last,
 * so without this the verdict on the broker he just typed would arrive on his next visit to the
 * page. `null` -- no save is pending -- leaves the decision exactly as it was. See
 * brokerVerdictPending() below for what a pending one waits for.
 */
export function shouldKeepPolling(
  last: PollView | null,
  reprovisionUntilMs: number | null,
  nowMs: number,
  brokerEpochAtSave: number | null,
): boolean {
  if (last === null || !last.connected) return true;
  if (reprovisionUntilMs !== null && nowMs < reprovisionUntilMs) return true;
  return brokerVerdictPending(last.mqtt, brokerEpochAtSave);
}

/** What shouldKeepPolling() reads of a status: describeWhere()'s four fields, plus the two members
 *  of the broker block a save is waiting on. A whole ProvisionStatus satisfies it. */
export interface PollView extends Where {
  mqtt?: { link: MqttLink; applied_epoch: number };
}

/**
 * Whether the broker block is still the OLD client's -- the second reason to keep polling, and the
 * one `last.connected` cannot see, because it is a statement about Wi-Fi.
 *
 * The condition is the firmware's own, written on `applied_epoch` in comfoair_mqtt_backoff.h: a
 * browser that saved a broker remembers that number and polls until it CHANGES and `link` is no
 * longer "connecting". The epoch counts client rebuilds and is incremented AFTER the publisher
 * task has rebuilt, so until it moves, every other member of the block -- `connected`,
 * `last_connack`, `failure` -- still describes the client the save replaced, and a page that
 * stopped on it would show the previous broker's verdict as this one's.
 *
 * GREATER THAN, NOT DIFFERENT FROM. The counter only climbs within a boot, so a value at or below
 * the remembered one is not a rebuild: it is a device that has REBOOTED and started counting from
 * zero, and the fresh client's `link` answers a question nobody asked. `!==` would take that for
 * our verdict. The cost of `>` is named rather than hidden: after such a reboot nothing ever
 * beats the remembered number, so this keeps answering true until the page is closed. Polling on
 * is the survivable half of that trade -- the other half stops the loop on a verdict that is not
 * the one the owner is waiting for.
 *
 * `null` is "no save is pending", the ordinary case, and it is checked FIRST and by identity: 0 is
 * a legal remembered epoch (a device that has never been reconfigured reports it), so a guard
 * written `if (!brokerEpochAtSave)` reads a save that was made as one that was not.
 *
 * An ABSENT block takes the epoch out of the decision entirely rather than counting as a change.
 * "No block" is the answer of a document rendered where nobody could look at a broker
 * (comfoair_wire.h), and reading `undefined !== 4` as "it moved" would end the wait on a document
 * that says nothing about a broker at all.
 */
function brokerVerdictPending(
  mqtt: { link: MqttLink; applied_epoch: number } | undefined,
  brokerEpochAtSave: number | null,
): boolean {
  if (brokerEpochAtSave === null || mqtt === undefined) return false;
  if (mqtt.applied_epoch > brokerEpochAtSave) return mqtt.link === "connecting";
  return true;
}

/**
 * Everything the poll loop in useProvisionStatus.ts needs to remember between ticks -- the whole
 * of what
 * used to be three separate refs (the status itself, and two guards over when to trust it) is
 * one value now, and the four functions below are the only things allowed to produce a new one.
 * useProvisionStatus.ts owns exactly one ref of it; it never builds a PollState by hand.
 */
export interface PollState {
  /** The last status successfully read, or null if none has landed yet this mount. */
  status: ProvisionStatus | null;
  /**
   * When `status` was read, on the same monotonic clock the deadline below uses. null while
   * nothing has been read.
   *
   * IT IS NOT DECORATION AND IT IS NOT FOR THE POLL LOOP, which never reads it. It exists because
   * the loop STOPS: shouldKeepPolling() answers a question about Wi-Fi, so on a device already on
   * the network the first poll is the last one, and everything else that reads this status --
   * above all the broker block, which decides whether a save deletes every entity out of somebody's
   * Home Assistant -- would otherwise be reading a document of unbounded age with no way to tell.
   * See brokerLinkFrom() in broker.ts.
   */
  readAtMs: number | null;
  /** See REPROVISION_GRACE_MS and shouldKeepPolling(). null: no resubmission is pending. */
  reprovisionUntilMs: number | null;
  /**
   * The `mqtt.applied_epoch` standing in the last answer when a broker save was accepted -- the
   * number the next document has to BEAT before anything in that block describes the client the
   * save built. null: no save is pending, and the broker block decides nothing.
   *
   * It is remembered rather than recomputed for the same reason `reprovisionUntilMs` is: the
   * question is whether something CHANGED, and a change is only visible against a value written
   * down before it. See brokerVerdictPending().
   */
  brokerEpochAtSave: number | null;
}

/**
 * A poll answered. Records it, and -- the early half of "belt and braces" from
 * shouldKeepPolling()'s doc -- clears a pending deadline the moment it stops being needed: the
 * first CONFIRMED disconnect after a resubmission (`!next.connected`) proves the stale
 * "still on the old network" reading has been superseded by a real one, so there is no reason to
 * wait out the rest of REPROVISION_GRACE_MS. Clearing on anything looser (e.g. every successful
 * poll, connected or not) would clear it on that stale reading ITSELF and reopen the exact bug
 * REPROVISION_GRACE_MS exists to close -- the deadline is what still catches it when this early
 * clear never fires, which is the ordinary case (see REPROVISION_GRACE_MS's doc for why).
 */
export function afterPollSuccess(
  state: PollState,
  next: ProvisionStatus,
  nowMs: number,
): PollState {
  return {
    status: next,
    // Stamped from the caller's clock rather than read from one here, for the reason every
    // function in this file takes its `now`: a module that reads a clock is a module whose answer
    // cannot be written down in a test.
    readAtMs: nowMs,
    reprovisionUntilMs: next.connected ? state.reprovisionUntilMs : null,
    // Carried, never cleared here: this poll may BE the one still showing the old client's block,
    // and forgetting the number it has to beat would end the wait on exactly that document.
    brokerEpochAtSave: state.brokerEpochAtSave,
  };
}

/**
 * A poll failed. Identity -- the DO NOT in useProvisionStatus.ts's pollOnce() as code: a failed
 * poll must never erase a status that is already known (the device answers /api/provision and
 * THEN leaves the channel this page is served over, so polls fail while the owner is watching
 * for exactly the answer this section exists to show -- failure-model.md), and a pending
 * deadline is not touched by network noise either -- it is a bound on TIME, not on how many
 * polls have succeeded.
 *
 * readAtMs is untouched for the same reason and it matters more than it looks: a failed poll must
 * not be recorded as a fresh reading of a status it did not read. Age is age since the last
 * ANSWER, which is what makes an old answer visibly old.
 */
export function afterPollFailure(state: PollState): PollState {
  return state;
}

/**
 * A Wi-Fi submission was accepted -- the device went to try, whether that arrived as a plain 200
 * or as "nothing answered", which after a provision request means the same thing (see saveWifi()
 * in Settings.tsx). Arms the grace period REPROVISION_GRACE_MS out from `nowMs`, unconditionally
 * -- a second accepted submission (the owner tries again after a failure) restarts the clock
 * rather than inheriting whatever was left of the first, because it is itself a fresh attempt
 * with its own trial and its own possible rollback.
 */
export function afterProvisionAccepted(state: PollState, nowMs: number): PollState {
  return { ...state, reprovisionUntilMs: nowMs + REPROVISION_GRACE_MS };
}

/**
 * A broker save was accepted. Remembers the epoch of the LAST ANSWER -- the number the next
 * document has to beat -- and touches nothing else: a save is not a reading, and it says nothing
 * about Wi-Fi.
 *
 * null when there is no last answer, or when the one there is carried no broker block: an epoch
 * armed against a document this page never saw would be a number the device has no reason to
 * beat, and the wait would end on the first answer that arrived -- the very reading the epoch
 * exists to distrust. The loop then stops on Wi-Fi as it did before, which is where it was.
 */
export function afterBrokerSaveAccepted(state: PollState): PollState {
  return { ...state, brokerEpochAtSave: state.status?.mqtt?.applied_epoch ?? null };
}

/** Nothing read, nothing pending. The one PollState anybody is allowed to write by hand. */
export const POLL_STATE_INITIAL: PollState = {
  status: null,
  readAtMs: null,
  reprovisionUntilMs: null,
  brokerEpochAtSave: null,
};

/**
 * How long ago the status in `state` was read, on the caller's clock.
 *
 * Infinity when nothing has been read: "we have never had an answer" and "the answer is very old"
 * are the same thing to every reader of this number, and Infinity is the value that cannot be
 * mistaken for a small age by a comparison somebody writes later.
 */
export function statusAgeMs(state: PollState, nowMs: number): number {
  if (state.readAtMs === null) return Number.POSITIVE_INFINITY;
  return nowMs - state.readAtMs;
}
