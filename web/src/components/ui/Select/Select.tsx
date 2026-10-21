import type { SelectProps } from "./types";

export function Select({
  label,
  ariaLabel,
  value,
  options,
  onChange,
  disabled,
  note,
}: SelectProps) {
  // No visible label -> no id to bind a <label> to; the select names itself via aria-label
  // instead. DO NOT derive the id from ariaLabel: it is translated, so the id would change with
  // the language, and two label-less selects with one name would share an id.
  // With a label, the id carries TextField's caveat: two selects sharing a label would collide.
  const id = label ? label.toLowerCase().replace(/\s+/g, "-") : undefined;
  // A label-less select has no id to derive a note id from, so it cannot carry a note.
  const noteId = note && id ? `${id}-note` : undefined;
  return (
    <div class="form-group">
      {label ? <label htmlFor={id}>{label}</label> : null}
      <select
        id={id}
        aria-label={label ? undefined : ariaLabel}
        value={value}
        disabled={disabled}
        aria-describedby={noteId}
        onChange={(e) => {
          const raw = (e.target as HTMLSelectElement).value;
          const opt = options.find((o) => String(o.value) === raw);
          onChange(opt && typeof opt.value === "number" ? Number(raw) : raw);
        }}
      >
        {options.map((opt) => (
          <option key={opt.value} value={opt.value}>
            {opt.label}
          </option>
        ))}
      </select>
      {noteId && <small id={noteId} class="field-note">{note}</small>}
    </div>
  );
}
