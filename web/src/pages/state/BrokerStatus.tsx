import type { MqttStatus } from "../../api/status.ts";
import { type BrokerAddress, type BrokerVerdict, toneLabel } from "./brokerStatus.ts";
import { t } from "../../i18n";
import styles from "./BrokerStatus.module.css";

/**
 * The broker diagnostics card. MARKUP ONLY -- every word and every tone was decided by
 * brokerStatus.ts, which a host suite can run; nothing here chooses between two sentences.
 *
 * It lives on /diagnostics rather than in Settings (the owner's choice). The original argument
 * for Settings -- "the owner comes back to where they configured it" -- survived; what died was the
 * counter-argument that the other page was for readings only, because that page is now called
 * Diagnostics.
 *
 * COLOUR IS NEVER THE ONLY CARRIER. The badge prints the tone's own word beside its colour
 * (toneLabel), the headline says what happened in a sentence, and the counters below are numbers.
 * ConnectionBadge.tsx:4-6 states the rule for the whole SPA; this is the fourth place to obey it.
 *
 * The counters stay on screen for every verdict, including the good one, and that is the point of
 * having them: "the broker will not let this device listen" is only believable next to a publish
 * count that is climbing, and "nothing is answering" is only actionable next to the socket errno
 * that separates ECONNREFUSED from ETIMEDOUT.
 */
export function BrokerStatus({
  verdict,
  mqtt,
  address,
  stale,
}: {
  verdict: BrokerVerdict;
  mqtt: MqttStatus | undefined;
  address: BrokerAddress | null;
  /** The counters below were read before the device stopped answering. */
  stale: boolean;
}) {
  return (
    <section class="card" aria-labelledby="broker-h">
      <h2 id="broker-h">{t("broker.card.title")}</h2>

      <div class={styles.head} role="status">
        <span class={`${styles.badge} ${styles[verdict.tone]}`}>
          <span class={styles.dot} aria-hidden="true" />
          {toneLabel(verdict.tone)}
        </span>
        <strong class={styles.headline}>{verdict.headline}</strong>
      </div>
      <p class={styles.detail}>{verdict.detail}</p>

      {address !== null && (
        // From GET /api/config, which this page loads anyway. The status document carries no host
        // at all, deliberately -- comfoair_wire.h:246-248 keeps the diagnostic struct to counters
        // so that no projection of it can ever carry a secret.
        <p class={styles.address}>
          {t("broker.card.saved")}{" "}
          <code>{address.host === "" ? t("broker.card.none") : `${address.host}:${address.port}`}</code>
        </p>
      )}

      {stale && (
        <p class={styles.stale}>{t("broker.card.stale")}</p>
      )}

      {mqtt !== undefined && (
        <dl class={styles.counters}>
          <div class={styles.counter}>
            <dt>{t("broker.counter.link")}</dt>
            <dd>{mqtt.link}</dd>
          </div>
          <div class={styles.counter}>
            <dt>{t("broker.counter.subscriptions")}</dt>
            <dd>{mqtt.subscriptions}</dd>
          </div>
          <div class={styles.counter}>
            <dt>{t("broker.counter.published")}</dt>
            <dd>{mqtt.published}</dd>
          </div>
          <div class={styles.counter}>
            <dt>{t("broker.counter.commands")}</dt>
            <dd>{mqtt.commands}</dd>
          </div>
          <div class={styles.counter}>
            <dt>{t("broker.counter.rejected")}</dt>
            <dd>{mqtt.rejected}</dd>
          </div>
          <div class={styles.counter}>
            <dt>{t("broker.counter.reconnects")}</dt>
            <dd>
              {mqtt.reconnects} / {mqtt.disconnects}
            </dd>
          </div>
          <div class={styles.counter}>
            <dt>{t("broker.counter.attempts")}</dt>
            <dd>{mqtt.attempts}</dd>
          </div>
          <div class={styles.counter}>
            <dt>{t("broker.counter.connack")}</dt>
            {/* The code and the firmware's own sentence for it, together: the number an owner can
                look up and the words they can read cannot come to disagree. */}
            <dd>
              {mqtt.last_connack}
              {mqtt.last_connack_reason !== "" && ` — ${mqtt.last_connack_reason}`}
            </dd>
          </div>
          <div class={styles.counter}>
            <dt>{t("broker.counter.errno")}</dt>
            {/* 0 is "none recorded", which a connect that timed out produces BY CONSTRUCTION --
                esp-mqtt dispatches no error event for that case at all (api/status.ts). So this
                reading 0 is not evidence that nothing went wrong. */}
            <dd>{mqtt.last_transport_errno}</dd>
          </div>
          <div class={styles.counter}>
            <dt>{t("broker.counter.epoch")}</dt>
            <dd>{mqtt.applied_epoch}</dd>
          </div>
        </dl>
      )}
    </section>
  );
}
