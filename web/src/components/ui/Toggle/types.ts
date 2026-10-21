export interface ToggleProps {
  label: string;
  checked: boolean;
  onChange: (v: boolean) => void;
  disabled?: boolean;
  /**
   * A sentence under the control, wired to the input with aria-describedby -- same pattern as
   * TextField's `note`. Without it, a sibling paragraph placed after this component is orphaned:
   * a screen reader announces the label and nothing else, which is exactly the failure this prop
   * exists to close.
   */
  note?: string;
}
