import styles from "./Button.module.css";
import type { ButtonProps } from "./types";

export function Button({
  children,
  variant,
  onClick,
  disabled,
  loading,
}: ButtonProps) {
  const cls =
    variant === "primary" ? styles.primary :
    variant === "danger" ? styles.danger :
    undefined;

  return (
    // The label stays visible while loading -- a button that blanks to "..." loses
    // the one word that says what it does. aria-busy is what tells assistive tech
    // the action is in flight; the trailing ellipsis is the same signal for sighted
    // users and is hidden from the accessibility tree so it is not read as text.
    <button
      class={cls}
      onClick={onClick}
      disabled={disabled || loading}
      aria-busy={loading || undefined}
    >
      {children}
      {loading && <span aria-hidden="true"> …</span>}
    </button>
  );
}
