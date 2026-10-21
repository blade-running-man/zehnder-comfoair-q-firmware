export interface SelectProps {
  /** A visible field label. Omit for a bare, label-less select (e.g. the nav language switcher)
   *  and pass `ariaLabel` instead, so the control still names itself to assistive tech. */
  label?: string;
  /** Accessible name used when there is no visible `label`. Ignored when `label` is present. */
  ariaLabel?: string;
  value: string | number;
  /** `value` is what onChange hands back (for a device option: the device's own string);
   *  `label` is only what is shown. DO NOT put a translated label in `value`. */
  options: Array<{ value: string | number; label: string }>;
  onChange: (v: number | string) => void;
  /** Leaves the control visible but inert, e.g. while a write is in flight. */
  disabled?: boolean;
  /** A sentence under the field, wired to the select with aria-describedby -- same pattern as
   *  TextField's `note`. Needs a visible `label` (the note id is derived from it). */
  note?: string;
}
