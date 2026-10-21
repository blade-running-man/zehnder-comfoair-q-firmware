// What pressing Save does to the broker the device is talking to RIGHT NOW.
//
// Pure, and pinned by tests/broker.test.ts. Nothing here touches the DOM: the page renders what
// this file decides, and the decision is the part worth being wrong about.
//
// WHY THIS FILE EXISTS, because the shape of the mistake matters more than the code. The page
// as first planned warned in exactly one place -- the broker is switched OFF, so the device
// cannot retract what it published and the old tree stays behind (hardware scenario HV-HA-17,
// docs/hardware-verification/06b-home-assistant.md). It said nothing at all in the other place,
// where the broker is LIVE: there
// comfoair_mqtt_pending_cleanup() answers WITHDRAW (comfoair_mqtt_client.c:107-116), the device
// publishes an empty retained discovery document per entity, and Home Assistant DELETES the device
// together with every entity and its entity_id. Dashboards, automations, scripts, templates and
// long-term statistics that name those entity_ids stop working, quietly, and correcting the typo
// afterwards does not bring one of them back: after a registry removal HA does not promise the
// same entity_id for the same uniq_id (docs/interfaces/home-assistant-discovery.md, "Cleanup and
// retraction").
//
// So the harmless path was the one that spoke and the irreversible one was silent. This file
// inverts that back: the live broker gets a CONFIRMATION, the dead one gets a sentence and no
// obstacle -- an unreachable peer does not get to stop the owner changing a setting (AGENTS.md).
//
// Relative imports below carry the .ts extension, unlike the .tsx files in this page.
// That is what lets node resolve this module when the suite runs it directly
// (tests/harness.ts explains why the suite is run that way); Vite is indifferent, and
// `allowImportingTsExtensions` is already on in tsconfig.app.json.

import { parsePort } from "./model.ts";
import { t } from "../../i18n/index.ts";

/** What the save does to the identity the live client was built from. */
export type BrokerMove =
  /** Neither the host nor the port changed: the client keeps talking to the same broker. */
  | "none"
  /** Host or port changed -- comfoair_mqtt_pending_cleanup() answers WITHDRAW. */
  | "moved";

/**
 * Whether the device is talking to its broker, as far as this page can tell.
 *
 * THREE VALUES AND NOT A BOOLEAN. "connected" and "offline" lead to opposite sentences, and
 * "we could not read it" is a third situation that must not be collapsed into either -- least
 * of all into the safe one, because the safe reading here is the one that lets an irreversible
 * save through without a word.
 */
export type BrokerLink = "connected" | "offline" | "unknown";

export interface BrokerNotice {
  move: BrokerMove;
  link: BrokerLink;
  /**
   * True when Save must stop and ask first.
   *
   * Set for a live broker AND for a link this page could not read, and never for one known to
   * be down: nothing can be retracted from a broker that is not answering, so there is nothing
   * to confirm and nothing to lose.
   */
  confirm: boolean;
  /** The button the owner presses to go ahead. "" when `confirm` is false. */
  confirmLabel: string;
  headline: string;
  lines: string[];
  /**
   * A command the owner can paste, or "" when there is nothing to run.
   *
   * Separate from `lines` because it is not a sentence: the page renders it in a monospace
   * block that can be selected in one gesture, and a shell command wrapped inside a paragraph
   * is one that gets copied with half a word of prose attached to it.
   */
  command: string;
}

/**
 * How old a reading of GET /api/status may be and still be treated as an answer.
 *
 * TEN SECONDS, AND THE NUMBER IS THE BROKER'S TO SET, not the page's. The retry ladder's fastest
 * rung is measured in seconds (comfoair_mqtt_backoff.h; docs/interfaces/mqtt.md, "Reconnect and
 * backoff"), so a link can go from
 * down to up inside one of these windows -- which is the whole point: past ten seconds this page
 * has no business claiming to know. DO NOT raise it "to poll less". Nothing polls on this
 * constant; it only decides when a reading stops counting as one.
 */
export const BROKER_LINK_FRESH_MS = 10_000;

/**
 * The link, read off GET /api/status, AND HOW LONG AGO IT WAS READ.
 *
 * `mqtt` is ABSENT rather than zeroed when the caller had nothing to say about a broker
 * (comfoair_wire.h, comfoair_wire_render_provision) -- POST /api/provision's reply is that
 * case -- so a missing block is "unknown" and never "not connected". Anything that is not a
 * boolean is unknown too: a firmware that renamed the field must not read as a live broker
 * through a truthiness test.
 *
 * `ageMs` IS NOT AN OPTIONAL REFINEMENT, and it is the reason this signature changed on
 * 2026-09-07. Settings.tsx polls /api/status only while the device is not yet on the network
 * (shouldKeepPolling(), status.ts) -- which is a statement about WI-FI. On a device already on the
 * network the first poll answers `connected: true`, the loop stops, and the mqtt block inside that
 * one answer is then frozen for the life of the page. The owner opens this page BECAUSE the broker
 * was not answering, reads `mqtt.connected: false`, is shown the soft branch ("nothing to clean
 * up", "saving is not blocked", an ordinary button, no confirmation) -- and while he is editing the
 * last octet the retry ladder gets the link back, which is its job. One press then runs a full
 * WITHDRAW and deletes every entity and its entity_id, having promised on screen that it would
 * not. "We asked a minute ago" is not "we know", and past BROKER_LINK_FRESH_MS this says so: the
 * answer is "unknown", which is the branch that CONFIRMS.
 *
 * A NEGATIVE OR NaN AGE IS UNKNOWN TOO. Both mean the caller could not work out when it read, and
 * the safe-looking reading is the dangerous one here.
 */
export function brokerLinkFrom(
  status: { mqtt?: { connected?: boolean } | null } | null,
  ageMs: number,
): BrokerLink {
  if (!(ageMs >= 0) || ageMs >= BROKER_LINK_FRESH_MS) return "unknown";
  const connected = status?.mqtt?.connected;
  if (connected === true) return "connected";
  if (connected === false) return "offline";
  return "unknown";
}

/**
 * The sentence for a save that is about to change the broker's address, or null for one that
 * is not.
 *
 * `loaded` is the document GET /api/config returned -- the identity the LIVE client was built
 * from, which is what comfoair_mqtt_start_client() records in s_host/s_port
 * (comfoair_mqtt_client.c:229-230) and what the cleanup decision compares against. `form` is
 * what is on screen now.
 *
 * `entityCount` IS A PARAMETER AND NOT `ENTITIES.length` READ IN HERE, which it was until
 * 2026-09-07. The old shape was derived, and it was also unfalsifiable: every case compared the
 * sentence against `ENTITIES.length`, so replacing the derivation with a literal count passed all
 * of them -- the test and the code agreed on a number neither of them could be wrong about
 * separately. As an argument it can be VARIED, and tests/broker.test.ts varies it: a call with 3
 * has to produce sentences that say 3. The single place ENTITIES.length is now named is
 * useBrokerGuard.ts, where it is one line in front of a reader instead of one line inside a
 * function under test.
 *
 * `deviceId` is the twelve hex characters GET /api/status reports, and it is OPTIONAL because
 * the hand-cleanup command is safe without it and dangerous with a guess: the discovery half of
 * that command has to be narrowed to `homeassistant/+/<device_id>/#`, and the unnarrowed
 * `homeassistant/#` would delete the discovery documents of every other device on the owner's
 * broker. No id, no discovery pattern -- a missing line beats a destructive one.
 */
export function describeBrokerSave(
  loaded: { mqtt_host: string; mqtt_port: number; topic_prefix: string },
  form: { mqtt_host: string; mqtt_port: string; topic_prefix: string },
  link: BrokerLink,
  entityCount: number,
  deviceId = "",
): BrokerNotice | null {
  // A device that never had a broker cannot have left anything on one, and the first broker an
  // owner types in is the most common save this page sees.
  if (loaded.mqtt_host === "") return null;

  // Trimmed on both counts because patchFromForm() trims before it sends (model.ts): comparing
  // the untrimmed box would report a move for a pasted address with a trailing space, and then
  // warn about a deletion that is not going to happen.
  const host = form.mqtt_host.trim();
  // null is "not a port", and patchFromForm() refuses the save outright for it -- so nothing
  // reaches the device and nothing is retracted. Reporting a move here would put a deletion
  // warning in front of a save that cannot happen.
  const port = parsePort(form.mqtt_port);
  const moved = host !== loaded.mqtt_host || (port !== null && port !== loaded.mqtt_port);
  if (!moved) return null;

  // The OLD broker, because that is where the deletion happens and where the leftovers stay.
  // Named with its port: "the broker moved" is a claim about host AND port, and an owner who
  // changed only the port would otherwise read a sentence naming an address that did not change.
  const old = `${loaded.mqtt_host}:${loaded.mqtt_port}`;

  // Whatever the caller was given. Every entity in the generated registry has a Home Assistant
  // component -- test_a_withdrawal_is_the_discovery_side_and_the_state_side_together asserts
  // exactly that (test/test_ha_cleanup) -- so the number of discovery documents a WITHDRAW empties
  // is the length of that list, and useBrokerGuard.ts is where it is read. A literal here would be
  // the second entity list AGENTS.md forbids, one indirection along.
  const count = entityCount;

  if (link === "offline") {
    return {
      move: "moved",
      link,
      confirm: false,
      confirmLabel: "",
      headline: t("broker.move.offline.headline", { old }),
      lines: [
        t("broker.move.offline.line1", { old, count }),
        // A warning and not a gate: nothing waits on an absent peer (AGENTS.md).
        t("broker.move.offline.line2"),
        t("broker.move.offline.line3"),
      ],
      command: handCleanup(loaded.mqtt_host, loaded.mqtt_port, loaded.topic_prefix, deviceId),
    };
  }

  const couldNotAsk = link === "unknown"
    ? t("broker.move.couldNotAsk", { old })
    : t("broker.move.connected", { old });

  return {
    move: "moved",
    link,
    confirm: true,
    confirmLabel: t("broker.move.confirm"),
    headline: t("broker.move.headline", { old, count }),
    lines: [
      `${couldNotAsk} ${t("broker.move.line1", { count })}`,
      t("broker.move.line2", { old }),
      // The contrast is the useful half: it tells the owner which edit they actually wanted.
      t("broker.move.line3"),
    ],
    // Nothing to run: the device is about to do this itself, which is the whole difference
    // between this branch and the one above.
    command: "",
  };
}

/**
 * The command that does by hand what the device could not do for itself.
 *
 * `--remove-retained` needs `--retained-only`, or mosquitto_sub republishes live traffic back as
 * empty messages; `-W` is what makes it terminate rather than sit on the subscription for ever.
 *
 * The discovery half is included only when the device id is known, and it is narrowed to
 * `homeassistant/+/<device_id>/#` -- the node segment of a discovery topic is the device id
 * (comfoair_ha.c, comfoair_ha_discovery_topic). `homeassistant/#` would take every OTHER device's
 * discovery documents with it, which is a far larger version of the accident this whole file
 * exists to prevent.
 */
function handCleanup(host: string, port: number, prefix: string, deviceId: string): string {
  const discovery = deviceId === "" ? "" : ` -t 'homeassistant/+/${deviceId}/#'`;
  return `mosquitto_sub -h ${host} -p ${port} -W 3 --retained-only --remove-retained `
    + `-t '${prefix}/#'${discovery}`;
}

// --- the two presses ---------------------------------------------------------------------------
//
// WHY THE STATE MACHINE IS HERE RATHER THAN IN THE HOOK, and it is not tidiness. Until 2026-09-07
// the whole of it lived in useBrokerGuard.ts and in three lines of Settings.tsx, and NOTHING in the
// suite reached any of it: the thirty-one cases above are cases of a pure function, and a hook is
// not one. Three separate mutations were run against the tree and every one of them left 610 of 610
// green and `tsc` clean --
//
//   * `confirmLabel: "Delete them and save"` -> `"Save settings"`, which makes the second press
//     indistinguishable from the first: a double click deletes every entity;
//   * deleting `if (broker.hold()) return;` from saveSettings(), which removes the confirmation
//     entirely and reinstates the defect this whole file exists to close;
//   * deleting `broker.release()` from patch(), which lets a confirmation survive an edit -- the
//     owner confirms a deletion on one address and the press submits another.
//
// The first and third are now cases below. THE SECOND IS STILL INVISIBLE TO THIS SUITE: it is the
// call site, and no pure function can see whether it is called. Hardware scenario HV-HA-16
// (docs/hardware-verification/06b-home-assistant.md) is the owner's eyes confirming it.

/** What just happened to the form or the button. */
export type BrokerGateAction =
  /** The Save button was pressed. */
  | "press"
  /** Any field of either form changed. */
  | "edit"
  /** A save actually went through. */
  | "saved"
  /** The owner pressed Cancel. */
  | "cancel";

export interface BrokerGateResult {
  /** Whether a confirmation is now standing. */
  confirming: boolean;
  /** Whether the caller should submit. Only ever true for "press". */
  submit: boolean;
}

/**
 * Whether a standing confirmation still means anything.
 *
 * A poll can turn a live broker into a dead one between the two presses, and the second press then
 * goes through as the ordinary save it has become -- so the flag alone is never the answer, and the
 * button must not say "Delete them and save" over a notice that no longer asks for it.
 */
export function brokerConfirming(confirming: boolean, notice: BrokerNotice | null): boolean {
  return confirming && notice !== null && notice.confirm;
}

/**
 * The one stop between the owner and a deletion in somebody else's Home Assistant, as arithmetic.
 *
 * `confirming` is what the caller remembered from the last call; `notice` is describeBrokerSave()'s
 * answer for the save this action belongs to -- for "press" that must be an answer built from a
 * READING TAKEN AT THE PRESS, not the one on screen (see brokerLinkFrom()'s note on staleness).
 *
 * THE RULE WORTH GETTING WRONG: a confirmation belongs to the change it was read against, and ANY
 * edit retires it. Carrying it across one would let a second, unread change through on the first's
 * authority -- the owner confirms "delete N entities on 192.168.1.10", corrects the address, and
 * the next press submits a decision nobody was shown.
 */
export function brokerGate(
  confirming: boolean,
  notice: BrokerNotice | null,
  action: BrokerGateAction,
): BrokerGateResult {
  if (action !== "press") return { confirming: false, submit: false };
  // Swallow the press and put the warning up -- but only the FIRST one, and only over a notice
  // that asks. Everything else is an ordinary save, including the second press and including a
  // press over a notice that has since stopped asking.
  if (!brokerConfirming(true, notice) || confirming) return { confirming: false, submit: true };
  return { confirming: true, submit: false };
}
