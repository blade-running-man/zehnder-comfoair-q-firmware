import { VERBATIM_INPUT } from "./verbatim";
import type { TextFieldProps } from "./types";

export function TextField({
  label,
  value,
  onChange,
  type = "text",
  placeholder,
  verbatim = false,
  note,
  autocomplete,
  disabled,
  onCommit,
}: TextFieldProps) {
  // Derived from the label, which makes the label text load-bearing: two fields labelled
  // "Password" on one page produce one id twice, and then clicking either label focuses the
  // first input. That is why the settings page spells its labels out in full.
  const id = label.toLowerCase().replace(/\s+/g, "-");
  const noteId = `${id}-note`;
  const typeAttr = type === "password" ? { type: "password" as const } : { type: "text" as const };
  return (
    <div class="form-group">
      <label htmlFor={id}>{label}</label>
      <input
        id={id}
        {...typeAttr}
        value={value}
        placeholder={placeholder}
        autocomplete={autocomplete}
        disabled={disabled}
        aria-describedby={note ? noteId : undefined}
        {...(verbatim ? VERBATIM_INPUT : {})}
        onInput={(e) => onChange((e.target as HTMLInputElement).value)}
        onBlur={onCommit && ((e) => onCommit((e.target as HTMLInputElement).value))}
        onKeyDown={
          onCommit
            && ((e) => {
              if (e.key === "Enter") onCommit((e.target as HTMLInputElement).value);
            })
        }
      />
      {note && <small id={noteId} class="field-note">{note}</small>}
    </div>
  );
}
