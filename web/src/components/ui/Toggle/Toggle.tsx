import type { ToggleProps } from "./types";

export function Toggle({
  label,
  checked,
  onChange,
  disabled,
  note,
}: ToggleProps) {
  // Derived from the label, same convention and the same caveat as TextField's id: two toggles
  // sharing a label would collide.
  const id = label.toLowerCase().replace(/\s+/g, "-");
  const noteId = `${id}-note`;
  return (
    <div class="form-group">
      <label>
        <input
          id={id}
          type="checkbox"
          checked={checked}
          disabled={disabled}
          aria-describedby={note ? noteId : undefined}
          onChange={(e) => onChange((e.target as HTMLInputElement).checked)}
        />
        {label}
      </label>
      {note && <small id={noteId} class="field-note">{note}</small>}
    </div>
  );
}
