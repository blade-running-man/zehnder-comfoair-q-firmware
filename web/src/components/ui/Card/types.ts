import type { ComponentChildren } from "preact";

export interface CardProps {
  title: string;
  subtitle?: string;
  children: ComponentChildren;
}
