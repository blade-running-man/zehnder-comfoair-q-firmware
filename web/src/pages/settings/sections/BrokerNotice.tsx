import type { BrokerNotice as Notice } from "../broker";
import styles from "../Settings.module.css";

/**
 * What saving is about to do to the broker the device is talking to now.
 *
 * IN FRONT OF THE BUTTON, never after it. Every sentence is about something that has not happened
 * yet and cannot be undone once it has -- broker.ts carries the argument -- and a consequence
 * explained after the fact is an apology.
 *
 * Two volumes and not one: `confirm` is every entity about to be deleted from somebody's Home
 * Assistant registry, and its absence is a tree that will simply be left behind on a broker that
 * is not answering, which costs nothing and blocks nothing. The decision is broker.ts's, under
 * test; this component only renders it, which is why nothing here re-reads the form.
 *
 * The command is a `<code>` block rather than a sentence in the list: it is meant to be selected
 * in one gesture, and a shell command wrapped inside a paragraph gets copied with half a word of
 * prose attached.
 *
 * TWO ROLES FOR THE SAME REASON THERE ARE TWO VOLUMES. `status` is a polite live region: a screen
 * reader finishes what it is saying and mentions this afterwards, which is right for "the old tree
 * will be left behind on a broker that is not answering". It is wrong for the other branch, where
 * the next keystroke can delete every entity and its entity_id out of a working Home Assistant --
 * that is `alert`, which interrupts. The whole difference between the branches is that one of them
 * is about to do something irreversible, and an announcement queued behind other speech is an
 * announcement that arrives after the button has been pressed.
 */
export function BrokerNotice({ what }: { what: Notice }) {
  return (
    <div
      class={what.confirm ? styles.brokerDanger : styles.brokerWarn}
      role={what.confirm ? "alert" : "status"}
    >
      <strong>{what.headline}</strong>
      {what.lines.map((line) => (
        <span key={line}>{line}</span>
      ))}
      {what.command !== "" && <code class={styles.command}>{what.command}</code>}
    </div>
  );
}
