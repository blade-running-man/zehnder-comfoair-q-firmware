import type { Explanation } from "../errors";
import styles from "./sections.module.css";

/**
 * One answer from the device, rendered at the volume it deserves.
 *
 * The tone comes from errors.ts and nothing here second-guesses it. The point of having four
 * tones is that "this firmware has not implemented that yet" and "this device is broken" must
 * not look the same: half of this API answers 501 or 404 today, and a page that paints those
 * red teaches the owner that red carries no information.
 *
 * The headline is this page's words; the detail is the DEVICE'S, unedited. Only the device
 * knows which field it refused and why -- comfoair_config_strerror() writes exactly those
 * sentences -- so paraphrasing would throw away the one actionable part of the message.
 */
export function Notice({ what }: { what: Explanation }) {
  return (
    <div class={`${styles.notice} ${styles[what.tone]}`} role="status">
      <strong class={styles.noticeHead}>{what.headline}</strong>
      <span class={styles.noticeBody}>
        {what.status !== null && <code class={styles.status}>HTTP {what.status}</code>}
        {what.detail}
      </span>
    </div>
  );
}
