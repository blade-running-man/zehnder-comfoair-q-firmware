export interface TextFieldProps {
  label: string;
  value: string | number;
  onChange: (v: string) => void;
  type?: "text" | "password";
  placeholder?: string;
  /**
   * True for a value the machine reads: a key, an SSID, a hostname, a topic. Applies
   * VERBATIM_INPUT, whose comment says which failure each attribute prevents. Opt-in rather
   * than the default because a device name IS prose and should keep its capitalisation.
   */
  verbatim?: boolean;
  /** A sentence under the field, wired to the input with aria-describedby. */
  note?: string;
  autocomplete?: string;
  disabled?: boolean;
  /**
   * Fires once, when the owner is done editing -- on blur, or Enter -- with the field's current
   * text. `onChange` still fires on every keystroke exactly as before: this is additive, not a
   * replacement, so every existing caller (the settings page, all of them committed by their own
   * Save button) is untouched by leaving it unset. It exists for a caller where every keystroke
   * would otherwise be sent somewhere -- EntityControl.tsx's number branch uses it to hold a
   * local draft and write only once, because for those fields every keystroke is a live NVS
   * write plus a WebSocket and MQTT broadcast, not just a wasted request.
   */
  onCommit?: (v: string) => void;
}
