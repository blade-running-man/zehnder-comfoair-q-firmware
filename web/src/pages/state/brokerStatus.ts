// The broker's sixteen counters as one sentence an owner can act on. Pure, so
// tests/brokerStatus.test.ts reaches every branch of it.
//
// WHY IT IS NOT CALLED status.ts: `pages/settings/status.ts` already owns that name, is imported
// as "./status.ts" from three places, and answers a different question -- the PROVISIONING
// machine's state and the poll loop's policy.
//
// ONE VERDICT, TWO CONSUMERS, and that is the point rather than an accident. The card on
// /diagnostics (BrokerStatus.tsx) and the one line on the dashboard (dashboard/Dashboard.tsx,
// silent while the broker works) read the SAME describeBroker(), so the two screens cannot disagree
// about whether the broker works. They differ only in how much room they have -- see brokerAlert().
//
// The sentences were fixed when this card was designed and are kept word for word bar two
// deliberate departures: the originals said "Fill in the broker below" and "Correct them below"
// while the card was still planned to sit above BrokerSection in Settings. The owner moved it to
// /diagnostics, where there is no "below", so both say "on the Settings page" instead.

import type { MqttStatus } from "../../api/status.ts";
import type { ErrorTone } from "../settings/errors.ts";
import { t } from "../../i18n/index.ts";

export interface BrokerVerdict {
  /** From errors.ts's vocabulary, not a second one: the card renders beside sections that already
   *  speak it, and a fifth tone would be a fifth colour nobody had defined. There is no "ok" tone
   *  and none is added here -- `pending` is this vocabulary's neutral, and a working broker is
   *  neutral rather than loud. */
  tone: ErrorTone;
  headline: string;
  detail: string;
}

/** The broker's address as the OWNER typed it, from GET /api/config.
 *
 *  NEVER from the status document, and not because one source was nicer: that document has no host
 *  in it at all. comfoair_wire.h:246-248 makes the diagnostic struct counters-only in so many
 *  words, precisely because it is the shape somebody later adds "the host, to help with debugging"
 *  to -- and counters-only is what proves no secret can leak through this screen. */
export interface BrokerAddress {
  host: string;
  port: number;
}

/** How often State.tsx re-reads GET /api/status while the diagnostics page is mounted. Five
 *  seconds because nothing in the block moves faster than once a second and the countdown between
 *  polls is drawn from `retry_in_ms` locally; the device is one HTTP task. */
export const BROKER_POLL_MS = 5000;

/**
 * The first five minutes of uptime, during which a broker that is not answering is very likely the
 * house still coming up rather than anything the owner has to do something about.
 *
 * THE SAME NUMBER AS `GRACE_MS` IN components/comfoair_led/comfoair_led_pattern.c:41, THE SAME
 * SOURCE (`uptime_ms`), AND NOT THE SAME RULE -- written down because the next reader will
 * otherwise see two 300000s and "fix" one of them:
 *
 *   * The LED has a second carrier. Inside its grace it calms only the COLOUR and keeps the
 *     rhythm; its own comment (led_pattern.c:236-238) states the division of labour outright --
 *     "the rhythm still says what fell off, /api/status still says it in words".
 *   * The card on /diagnostics therefore never waits anything out: it IS "/api/status in words",
 *     and withholding the diagnosis for the five minutes after a restart -- when people look --
 *     would make it useless. Inside the grace it adds a sentence and changes nothing else.
 *   * The dashboard line has no second carrier at all -- it is either there or not -- so it is the
 *     one that waits. An owner who has just power-cycled the unit does not need the home screen
 *     shouting about a broker the device has not finished dialling.
 *
 * If the phase machine's 30-second NO_BROKER hysteresis (COMFOAIR_PHASE_NO_BROKER_MS) ever
 * reaches the SPA, this constant is what it replaces, not something to sit beside. */
export const BOOT_GRACE_MS = 300_000;

/** The word beside the colour. ConnectionBadge.tsx:4-6 states the rule for the whole SPA -- a red
 *  dot alone is unreadable to anyone who cannot separate it from the green one -- and the card and
 *  the dashboard line are the fourth and fifth places that obey it. Exported so the suite can pin
 *  it: a label gone empty would leave colour as the only carrier, which review catches only if a
 *  review happens. */
export function toneLabel(tone: ErrorTone): string {
  switch (tone) {
    case "pending":
      return t("broker.tone.pending");
    case "denied":
      return t("broker.tone.denied");
    case "error":
      return t("broker.tone.error");
    case "offline":
      return t("broker.tone.offline");
  }
}

/**
 * Whole seconds until the next attempt, or null when none is scheduled.
 *
 * `retry_in_ms === null` is "NO attempt is scheduled", never "in 0 ms" (api/status.ts on the
 * member; `fallback_ap_in_ms` beside it solves the same problem the same way), so every caller
 * below drops its whole "next attempt" sentence rather than printing a number -- "next attempt in
 * 0 s" tells the owner a dial is imminent when nothing at all is going to happen.
 *
 * The floor of one second is that same rule seen from the other end: 400 ms rounds to zero, and a
 * rounding artefact must not print the one sentence the sentinel exists to keep off the screen.
 */
function seconds(retryInMs: number | null): number | null {
  if (retryInMs === null) return null;
  return Math.max(1, Math.round(retryInMs / 1000));
}

/**
 * What the broker block says, in one verdict. Two orderings below are load-bearing:
 *
 *   * `subscriptions: "refused"` is answered BEFORE `link: "online"` is allowed to say "connected".
 *     It is the only case where "connected" and "does not work" are both true -- a broker whose ACL
 *     forbids SUBSCRIBE accepts the connection and every publish, so the readings arrive and not one
 *     button in Home Assistant does anything (api/status.ts on Subscriptions). The headline has to
 *     be about the subscriptions; the publish counters stay in the card as proof half of it works.
 *   * A word none of the three enumerations knows is answered before everything else. All three
 *     unions carry "unknown" because all three naming functions in the firmware return it for a
 *     value outside the enumeration, and no generator keeps the firmware's spellings and this
 *     page's in step (api/status.ts's header). Rendering an unrecognised word as "connected" is
 *     the one lie this card exists to prevent.
 */
export function describeBroker(
  mqtt: MqttStatus | undefined,
  at: BrokerAddress,
  uptimeMs: number,
): BrokerVerdict {
  const v = verdict(mqtt, at);
  // The grace sentence goes on faults only. Adding it to "Connected to the broker" would be an
  // excuse for something that has not happened.
  if (uptimeMs < BOOT_GRACE_MS && (v.tone === "error" || v.tone === "denied"))
    return { ...v, detail: `${v.detail} ${t("broker.bootGrace")}` };
  return v;
}

function verdict(mqtt: MqttStatus | undefined, at: BrokerAddress): BrokerVerdict {
  // ABSENT IS ITS OWN ANSWER, never "not connected". comfoair_wire.h's contract: a NULL block omits
  // the member rather than rendering zeros, and the reply to POST /api/provision is rendered before
  // the radio has moved to the owner's network, so it carries no block at all. Zeros are "the
  // device looked and nothing has happened yet"; no block is "rendered where there was nobody to
  // look". Opposite answers, so opposite sentences.
  if (mqtt === undefined)
    return {
      tone: "pending",
      headline: t("broker.absent.headline"),
      detail: t("broker.absent.detail"),
    };

  if (!mqtt.configured)
    return {
      tone: "pending",
      headline: t("broker.off.headline"),
      detail: t("broker.off.detail"),
    };

  if (mqtt.link === "unknown" || mqtt.failure === "unknown" || mqtt.subscriptions === "unknown")
    return {
      tone: "error",
      headline: t("broker.unknownWord.headline"),
      detail: t("broker.unknownWord.detail", {
        link: mqtt.link, failure: mqtt.failure, subscriptions: mqtt.subscriptions,
      }),
    };

  if (mqtt.link === "online") {
    if (mqtt.subscriptions === "refused")
      return {
        tone: "denied",
        headline: t("broker.subsRefused.headline"),
        detail: t("broker.subsRefused.detail"),
      };
    if (mqtt.subscriptions !== "granted")
      return {
        tone: "pending",
        headline: t("broker.subsPending.headline"),
        detail: t("broker.subsPending.detail"),
      };
    return {
      tone: "pending",
      headline: t("broker.online.headline"),
      detail: t("broker.online.detail", {
        published: mqtt.published, commands: mqtt.commands, reconnects: mqtt.reconnects,
      }),
    };
  }

  // "An attempt is in flight" is this pair -- link "connecting" plus retry_in_ms null -- and not a
  // `link` value of its own: a verdict of "unreachable" on a twelve-second timer was withdrawn
  // because esp-mqtt spends network_timeout_ms twice in one attempt, so the floor is about 22 s and
  // the verdict would have been announced inside the first try.
  if (mqtt.link === "connecting")
    return {
      tone: "pending",
      headline: t("broker.connecting.headline"),
      detail: t("broker.connecting.detail", { host: at.host }),
    };

  const secs = seconds(mqtt.retry_in_ms);
  const retry = secs === null ? "" : ` ${t("broker.retry", { secs })}`;

  switch (mqtt.failure) {
    case "dns":
      return {
        tone: "error",
        headline: t("broker.dns.headline"),
        detail: t("broker.dns.detail", { host: at.host }) + retry,
      };
    case "tcp":
      // The class collapses ECONNREFUSED, ETIMEDOUT and EHOSTUNREACH, which ask three different
      // things of the owner -- that is what `last_transport_errno` is for, and the card prints it
      // among the counters rather than turning it into a fourth sentence here.
      return {
        tone: "error",
        headline: t("broker.tcp.headline"),
        detail:
          t("broker.tcp.detail", { host: at.host, port: at.port })
          + (secs === null ? "" : ` ${t("broker.tcp.retry", { secs })}`),
      };
    case "unavailable":
      return {
        tone: "error",
        headline: t("broker.unavailable.headline"),
        detail: t("broker.unavailable.detail") + retry,
      };
    case "protocol":
      // `last_connack_reason` is the FIRMWARE's own sentence for `last_connack`
      // (comfoair_wire_connack_name), so the number an owner can look up and the words they read
      // cannot come to disagree. Both are printed: the words for the owner, the code for whoever
      // they end up asking.
      return {
        tone: "error",
        headline: t("broker.protocol.headline"),
        detail: t("broker.protocol.detail", { reason: mqtt.last_connack_reason, code: mqtt.last_connack }),
      };
    case "bad-credentials":
      return {
        tone: "denied",
        headline: t("broker.badCredentials.headline"),
        detail: t("broker.badCredentials.detail", { reason: mqtt.last_connack_reason, code: mqtt.last_connack }),
      };
    case "dropped":
      return {
        tone: "error",
        headline: t("broker.dropped.headline"),
        detail: t("broker.dropped.detail", { disconnects: mqtt.disconnects }) + identityConflict(mqtt) + retry,
      };
    case "none":
      break;
  }

  if (mqtt.link === "off")
    return {
      tone: "pending",
      headline: t("broker.notDialled.headline"),
      detail: t("broker.notDialled.detail"),
    };

  return {
    tone: "error",
    headline: t("broker.noReason.headline"),
    detail: t("broker.noReason.detail", { link: mqtt.link, attempts: mqtt.attempts }) + retry,
  };
}

/**
 * "A second device signed in with our client id", as a SENTENCE.
 *
 * Not a value of `failure`, and the reason is the wire and not taste: over MQTT that eviction is
 * unobservable -- the broker sends an ordinary disconnect and says nothing about why. What IS
 * observable is `reconnects` and `disconnects` climbing together while `connected` flickers:
 * a pattern in two numbers, not a state the device can be in. A firmware that named it would be
 * inventing a diagnosis it cannot make.
 *
 * Within one, not equal: the block is a snapshot of counters a publisher task keeps moving and no
 * reader may treat two members as simultaneous (comfoair_wire.h:250-254), so demanding equality
 * would make this sentence appear and vanish with the timing of the poll. */
function identityConflict(mqtt: MqttStatus): string {
  const climbingTogether =
    mqtt.reconnects > 0 && mqtt.disconnects > 0
    && Math.abs(mqtt.reconnects - mqtt.disconnects) <= 1;
  if (!climbingTogether) return "";
  return ` ${t("broker.identityConflict", { reconnects: mqtt.reconnects, disconnects: mqtt.disconnects })}`;
}

/**
 * The dashboard's one line, or null for "say nothing" while the broker works.
 *
 * SILENCE IS THE DEFAULT AND IT IS THE FEATURE. The dashboard already writes "Live" for as long as
 * the WebSocket is up, so an owner whose broker alone has fallen off sees fresh readings and not
 * one reason to doubt them, while Home Assistant has been empty for a day. Both statements are
 * true and neither contradicts the other on screen; this line exists to make the second one
 * visible. A line that appeared when nothing was wrong would be back to teaching the owner to
 * ignore it.
 *
 * Driven off the verdict's TONE rather than a second list of link values, so a class of failure
 * added to describeBroker() cannot be silently missing here: `error` and `denied` are the two tones
 * that mean something is wrong, and `pending` covers every state that is merely early -- switched
 * off, dialling, not yet dialled, waiting for a SUBACK, and no block at all.
 *
 * The headline names the CONSEQUENCE, because the reader was not looking for this; the detail is
 * the verdict's own headline, so the two screens say one thing at two lengths. Which consequence
 * depends on the verdict: with the link online and only the subscriptions refused the readings
 * genuinely are arriving and only control is lost, and "not receiving data" would send that owner
 * hunting a fault that is not there.
 *
 * NO ADDRESS PARAMETER, deliberately: this returns headlines only, and no headline in the table
 * names a host or a port, which is what lets the dashboard show this line without fetching GET
 * /api/config at all. The placeholder below therefore cannot reach a screen, and the suite asserts
 * that rather than trusting the reading. */
export function brokerAlert(
  mqtt: MqttStatus | undefined,
  uptimeMs: number,
): BrokerVerdict | null {
  if (uptimeMs < BOOT_GRACE_MS) return null;
  const v = describeBroker(mqtt, { host: "«no address was needed»", port: -1 }, uptimeMs);
  if (v.tone !== "error" && v.tone !== "denied") return null;
  const stillPublishing = mqtt !== undefined && mqtt.link === "online";
  return {
    tone: v.tone,
    headline: t(stillPublishing ? "broker.alert.noControl" : "broker.alert.noData"),
    detail: v.headline,
  };
}
