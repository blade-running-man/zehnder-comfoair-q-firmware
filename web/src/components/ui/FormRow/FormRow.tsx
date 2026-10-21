import type { FormRowProps } from "./types";

export function FormRow({
  children,
  style,
}: FormRowProps) {
  return <div class="form-row" style={style}>{children}</div>;
}
