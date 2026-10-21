import { useState } from "preact/hooks";
import { Select, TextField, Toggle } from "../../components/ui";
import type { EntityMeta } from "../../api/entities";
import type { StateValue } from "../../api/ws";
import { controlKindOf, describeWrite, numberToWrite } from "./control.ts";
import { entityName } from "../../i18n/entityName";
import { optionChoices } from "../../i18n/option";
import styles from "./State.module.css";

/**
 * One writable entity, as something the owner can operate.
 *
 * The options come from the DEVICE (`meta.options`), in the device's order, and are never
 * reordered here. That order is the protocol's: the index IS the value on the wire, and
 * pdo_table.py's own comment on the temperature_profile row says what happens if somebody
 * "fixes" it to match the printed manual -- "'fixing' the order to match the display would
 * silently send Warm when the user picks Normal".
 */
export function EntityControl({
  meta,
  value,
  sent,
  failed,
  onWrite,
}: {
  meta: EntityMeta;
  value: StateValue | undefined;
  sent: string | null;
  failed: string | null;
  onWrite: (v: StateValue) => void;
}) {
  const kind = controlKindOf(meta);

  // A local draft for the number branch, held unconditionally (never behind the early returns
  // below) so this hook fires in the same order on every render. `draft` is the text the owner
  // is actively typing; `null` means "show what the device reports" instead. Only the number
  // branch below ever sets it, but a writable entity's `kind` cannot change across the lifetime
  // of the component that renders it -- it comes from the same registry entry every time -- so
  // no other branch ever needs it.
  const [draft, setDraft] = useState<string | null>(null);

  if (kind === "none") return null;
  // The fan has no control here yet -- kind "fan" matches none of the three branches below, so
  // without this it fell through to an empty <div class={styles.control}>, a blank hole between
  // a name column with nothing beside it. Task 5 is what fills this branch in (fanPresetOf and
  // the rest); until then it must render nothing at all, same as "none". State.tsx's caller now
  // never even reaches this branch for the fan -- it partitions its render loop on
  // control.ts's rendersControl(), which returns false for "fan" for exactly this reason, so the
  // fan renders through the ordinary read-only <dt>/<dd> row (with its own live reading, via
  // State.tsx's format()) instead of ever calling this component. This `return null` stays as
  // the belt to rendersControl()'s braces: a caller that skips rendersControl() and renders this
  // component for the fan directly still gets nothing back, not an empty box.
  if (kind === "fan") return null;

  const current = value === undefined || value === null ? "" : String(value);
  const note = describeWrite({ key: meta.key, sent, current, failed, kind });
  // Passed through the control's own `note` prop rather than rendered as a sibling <p>: a
  // paragraph placed after a control is not associated with it in the accessibility tree, so a
  // screen-reader user hears the label and nothing else -- the exact failure TextField.tsx's
  // `note` (and Toggle's, following it) exists to close, wiring the text to the input with
  // aria-describedby. That matters here more than it would elsewhere: three of these controls
  // are unconfirmed selects (temperature_passive, humidity_comfort, humidity_protection) whose
  // note says the unit's value is only read at start-up and every 6 hours, so the change waits
  // for the next read to be confirmed -- the sentence the page most needs read aloud.
  const noteProp = note !== "" ? note : undefined;
  // Shown in the owner's language; what is WRITTEN stays the device's own option string
  // (optionChoices() keeps the two halves apart -- see i18n/option.ts).
  const name = entityName(meta.key, meta.name);

  return (
    <div class={styles.control}>
      {kind === "select" && (
        <Select
          label={name}
          value={current}
          options={optionChoices(meta.options ?? [])}
          onChange={(v: string | number) => onWrite(String(v))}
          note={noteProp}
        />
      )}

      {kind === "switch" && (
        <Toggle
          label={name}
          checked={value === true}
          onChange={(v: boolean) => onWrite(v)}
          note={noteProp}
        />
      )}

      {/*
        A box, not a slider, and that is a finding rather than a preference: Away Duration
        spans 0-43200, and a slider there gives 43,201 positions to hit one minute on. DO NOT
        swap in <Slider> for these four.
      */}
      {kind === "number" && (
        <TextField
          label={`${name}${meta.unit ? ` (${meta.unit})` : ""}`}
          // The draft while the owner is typing, the device's own value once they are not. A
          // field controlled by `current` alone resets on every socket frame -- this page
          // re-renders on every PDO -- and eats whatever the owner had half-typed under the
          // cursor.
          value={draft ?? current}
          onChange={setDraft}
          // Committed on blur or Enter, not on every keystroke: these are duration minutes
          // (unit "min"), and each one lives on THIS node, not the Zehnder unit -- every
          // keystroke would otherwise be a live NVS write plus a WebSocket and MQTT broadcast
          // (comfoair_http_command.c:78-103 stores it directly, no bus frame, no queue), not merely
          // a wasted request. Typing "120" would commit three flash writes and broadcast two
          // numbers the owner never meant to set.
          onCommit={(v) => {
            const n = numberToWrite(v, meta);
            if (n !== null) onWrite(n);
            // Reverts to whatever the device reports, discarding the typed text either way: on
            // an accepted write there is nothing left worth keeping a draft of, and on a
            // refused one (numberToWrite returned null) the box must not keep showing text that
            // was never sent.
            setDraft(null);
          }}
          note={noteProp}
        />
      )}
    </div>
  );
}
