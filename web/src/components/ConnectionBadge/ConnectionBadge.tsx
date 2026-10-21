import { connection } from "../../api/device";
import { t, type MsgKey } from "../../i18n";
import styles from "./ConnectionBadge.module.css";

// Colour is never the only carrier: the label says the same thing in words. A red dot on
// its own is unreadable to anyone who cannot separate it from the green one, and this
// badge is the only thing on screen that distinguishes live readings from stale ones.
// Catalog keys, resolved in render so the badge follows a language switch.
const LABELS = {
  open: "conn.open",
  connecting: "conn.connecting",
  closed: "conn.closed",
} as const satisfies Record<string, MsgKey>;

const TITLES = {
  open: "conn.open.title",
  connecting: "conn.connecting.title",
  closed: "conn.closed.title",
} as const satisfies Record<string, MsgKey>;

export function ConnectionBadge() {
  const status = connection.value;
  return (
    <span
      class={`${styles.badge} ${styles[status]}`}
      title={t(TITLES[status])}
      role="status"
      aria-live="polite"
    >
      <span class={styles.dot} aria-hidden="true" />
      {t(LABELS[status])}
    </span>
  );
}
