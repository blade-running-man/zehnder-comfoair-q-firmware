import type { ComponentChildren } from "preact";
import styles from "./Chip.module.css";

export type ChipTone = "neutral" | "on" | "warn" | "bad" | "good";

/** A status pill. `tone` maps to a colour AND the border/dot carry the same state as form,
    so the meaning survives when the hue does not. */
export function Chip({ tone = "neutral", children }: { tone?: ChipTone; children: ComponentChildren }) {
  const cls = tone === "neutral" ? styles.chip : `${styles.chip} ${styles[tone]}`;
  return (
    <span class={cls}>
      <span class={styles.dot} />
      {children}
    </span>
  );
}
