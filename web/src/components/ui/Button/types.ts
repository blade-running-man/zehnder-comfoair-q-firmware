import type { ComponentChildren } from "preact";

export interface ButtonProps {
  children: ComponentChildren;
  variant?: "primary" | "danger" | "default";
  onClick?: () => void | Promise<void>;
  disabled?: boolean;
  loading?: boolean;
}
