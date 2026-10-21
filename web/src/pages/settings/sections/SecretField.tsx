import { TextField } from "../../../components/ui";
import { CONFIG_UNCHANGED } from "../../../api/secrets";
import { t } from "../../../i18n/index";

/**
 * Password input for a config value the device refuses to send back.
 *
 * GET /api/config returns CONFIG_UNCHANGED in place of a secret that is set,
 * and "" when none is. Rendering the marker into the box would let the user
 * type on the end of it and store "__UNCHANGED__hunter2", so the box
 * is shown empty and the marker stays in state until something is typed:
 *
 *   - never touched  -> state keeps the marker -> device keeps the stored secret
 *   - typed into     -> state holds the new value -> device stores it
 *   - cleared        -> state holds ""           -> device clears the secret
 *
 * The placeholder is the only thing that tells the two empty-looking states
 * apart, so it has to say which one this is -- and there are THREE of them, not two: a stored
 * secret the owner has just emptied is also an empty box, and it means the opposite of "not
 * set". This component cannot see that difference, because it holds only the box; the caller
 * passes `placeholder` from describeSecret(loaded, value), which sees both values and writes
 * the placeholder and the note from the same pair so they cannot disagree. The fallback below
 * is for the Wi-Fi key box, whose empty state means "the network has no key" and has no stored
 * value behind it to delete (../wifi.ts, describeWifiKey).
 */
export function SecretField({
  label,
  value,
  onChange,
  note,
  placeholder,
  autocomplete,
  disabled,
}: {
  label: string;
  value: string;
  onChange: (v: string) => void;
  note?: string;
  /** From describeSecret().placeholder. Omitted only where an empty box has just one meaning. */
  placeholder?: string;
  autocomplete?: string;
  /**
   * Passed through like any other field's, and it matters more here than on most of them: a
   * password box that stays live on a device refusing every write is the one control on the
   * card that says nothing when nothing happens. There is no failed save to explain it -- Save
   * is disabled too -- so the owner types a password, presses nothing, and leaves believing
   * they set one.
   */
  disabled?: boolean;
}) {
  const isStored = value === CONFIG_UNCHANGED;
  return (
    <TextField
      label={label}
      type="password"
      value={isStored ? "" : value}
      placeholder={
        placeholder
        ?? (isStored ? t("settings.secret.stored.placeholder") : t("settings.secret.notSet.placeholder"))
      }
      // Not a prop the caller can turn off. Every value that reaches this component is a
      // credential by definition, and a credential a phone has capitalised fails with the
      // same message as one the owner mistyped -- see components/ui/TextField/verbatim.ts.
      verbatim
      note={note}
      autocomplete={autocomplete}
      disabled={disabled}
      onChange={onChange}
    />
  );
}
