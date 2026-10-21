import styles from "./Card.module.css";
import type { CardProps } from "./types";

export function Card({
  title,
  subtitle,
  children,
}: CardProps) {
  return (
    <div class="card">
      <h3 class={styles.title}>{title}</h3>
      {subtitle && (
        <p class={styles.subtitle}>{subtitle}</p>
      )}
      {children}
    </div>
  );
}
