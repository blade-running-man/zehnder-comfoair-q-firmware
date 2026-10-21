import { brokerAlert, toneLabel } from "./brokerStatus.ts";
import { useBrokerFeed } from "./useBrokerFeed.ts";
import { t } from "../../i18n";
import styles from "./BrokerAlert.module.css";

/**
 * The dashboard's one line about the broker, and NOTHING AT ALL while the broker is working
 * (the owner's choice: a healthy broker earns no row on the dashboard).
 *
 * WHY IT EXISTS: the dashboard writes "Live" for as long as the WebSocket is up, which is true and
 * says nothing about Home Assistant. An owner whose broker alone has fallen off sees fresh
 * readings, no reason to doubt them, and an empty Home Assistant that has been empty since
 * yesterday. This line is the only thing on that screen that can say so.
 *
 * WHY IT LIVES IN pages/state/ AND NOT IN pages/dashboard/: it renders the verdict of
 * brokerStatus.ts and polls through useBrokerFeed.ts, both of which are here because the
 * diagnostics card is here. One verdict, two consumers -- putting a second copy of this markup
 * next to the dashboard would be the first step towards two screens that disagree. Dashboard.tsx
 * therefore gains an import and one element and no logic of its own.
 *
 * COLOUR IS NEVER THE ONLY CARRIER (ConnectionBadge.tsx:4-6, the fifth place to obey it): the
 * sentence names the consequence in words, the tone's own word is on the dot's title, and the text
 * itself is drawn at the page's ordinary colour rather than in the tone's.
 */
export function BrokerAlert() {
  const feed = useBrokerFeed();
  // A poll that did not answer says nothing about the BROKER -- it says the device is not
  // answering, which the dashboard's own status row already says in the words it has for it. Two
  // rows saying "offline" in different wordings would be worse than one.
  if (feed.unreachable !== null) return null;
  const alert = brokerAlert(feed.mqtt, feed.uptimeMs);
  if (alert === null) return null;
  return (
    <div class={`${styles.line} ${styles[alert.tone === "denied" ? "denied" : "error"]}`} role="status">
      <span class={styles.dot} title={toneLabel(alert.tone)} />
      <span class={styles.text}>
        <span class={styles.what}>{alert.headline}</span>
        {" — "}
        {t("broker.alert.tail", { detail: alert.detail })}
      </span>
    </div>
  );
}
