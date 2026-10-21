// The order the State page shows entities in.
//
// The State page renders its entities in hand-placed groups, each in an explicit order.
// It used to derive a group from protocol facts alone -- deviceClass, unit, kind -- which works
// but is not a page layout: it had no Filter or Timers group at all, scattered temperatures in
// PDO-table order instead of the order air actually moves through the heat exchanger, and dumped
// about a quarter of the entities into a bucket literally called "other", including the one
// number on the page that ever asks the owner to do something
// (filter_replacement_remaining_days). That regression is what this file fixes.
//
// GROUPS, the layout itself, lives in groups.ts; its header explains why a layout may name keys
// when a second entity list is forbidden, and the two guards that keep it from going stale. This
// file is the logic over it: the fallback for a key GROUPS does not name, and the read-out.

import type { EntityMeta } from "../../api/entities.ts";
import { GROUPS } from "./groups.ts";

export interface RenderedGroup {
  readonly id: string;
  readonly label: string;
  readonly entities: readonly EntityMeta[];
}

/**
 * Which group an entity belongs in when GROUPS (groups.ts) does not name its key.
 *
 * This is the ORIGINAL derivation State.tsx used before this file existed, kept as the fallback
 * that keeps a key GROUPS has never heard of from becoming invisible: derived from what the
 * device already says about the entity -- its device class, its unit and its kind -- rather than
 * from a table of keys. A key table would be a second entity list that drifts silently the first
 * time the firmware gains a sensor; this function is what makes GROUPS safe to hardcode keys in
 * anyway (see groups.ts's header).
 *
 * Every id this returns except "other" matches an id in GROUPS on purpose, so orderedGroups() can
 * append the fallback straight onto that group's listed entities with no translation table.
 * "other" has no counterpart in GROUPS -- no entity is placed in an "Other" group on purpose --
 * so orderedGroups() creates one, last, exactly as State.tsx used to.
 */
export function groupOf(meta: EntityMeta): string {
  if (meta.entityCategory === "diagnostic") return "diagnostic";
  if (meta.deviceClass === "temperature") return "temperature";
  if (meta.deviceClass === "humidity") return "humidity";
  if (meta.deviceClass === "power" || meta.deviceClass === "energy") return "energy";
  if (meta.deviceClass === "volume_flow_rate" || meta.unit === "m³/h" || meta.unit === "rpm")
    return "airflow";
  if (meta.kind === "fan" || meta.kind === "select" || meta.kind === "switch")
    return "ventilation";
  return "other";
}

const OTHER_GROUP_ID = "other";
const OTHER_GROUP_LABEL = "Other";

/**
 * The page's groups, in display order, each with its entities in display order.
 *
 * For each group in GROUPS, its listed keys come first, in the order GROUPS gives them, followed
 * by any entity that groupOf() maps onto that group but GROUPS does not mention -- a key
 * pdo_table.py gained after this file was last edited. An entity whose groupOf() is "other" (no
 * counterpart among GROUPS' ids) lands in a trailing "Other" group appended at the end,
 * the same place State.tsx used to put its one and only overflow bucket, so nothing this
 * function cannot yet place goes missing.
 *
 * A group with nothing in it (all of GROUPS' listed keys currently absent, e.g. this firmware
 * built without a feature) is omitted rather than rendered empty.
 */
export function orderedGroups(entities: readonly EntityMeta[]): RenderedGroup[] {
  const byKey = new Map<string, EntityMeta>();
  for (const meta of entities) byKey.set(meta.key, meta);

  // One mutable bucket per group id, seeded in GROUPS' display order so a group that ends up
  // empty (every listed key currently unbuilt, no overflow either) can simply be skipped when
  // the buckets are read out at the end, without disturbing the order of the groups around it.
  const buckets = new Map<string, EntityMeta[]>();
  for (const group of GROUPS) buckets.set(group.id, []);
  const knownGroupIds = new Set(GROUPS.map((group) => group.id));

  const placed = new Set<string>();
  for (const group of GROUPS) {
    for (const key of group.keys) {
      const meta = byKey.get(key);
      // A listed key the running firmware does not currently report (build without a feature,
      // or GROUPS ahead of a firmware image) is skipped here, not crashed on -- the two checks
      // in the file header (the type-level one and the runtime one) are what keep this from
      // masking real drift.
      if (!meta) continue;
      buckets.get(group.id)!.push(meta);
      placed.add(key);
    }
  }

  // Second pass, in the input's own order, appends anything GROUPS did not place after that
  // group's listed entities -- a key pdo_table.py gained after this file was last edited must
  // still appear (see this function's own doc comment above). `placed` is updated here too, the
  // same as in the first pass: two entities sharing a key (which pdo_table.py's generator does
  // not produce, but nothing downstream promises) would otherwise render twice, once from this
  // loop and once more from a later duplicate, with duplicate React/Preact keys as the visible
  // symptom -- one dedup rule for both passes, not one that only covers the listed path.
  for (const meta of entities) {
    if (placed.has(meta.key)) continue;
    const raw = groupOf(meta);
    // groupOf()'s only documented outputs are GROUPS' own ids and "other" (see its own doc
    // comment, and order.test.ts's "groupOf()'s range" check, which pins that live). If that
    // coupling is ever broken by an edit to one side and not the other, folding anything
    // unrecognised into Other keeps the entity visible instead of writing it into a bucket
    // nothing below ever reads out -- the same failure this whole file exists to prevent, just
    // one step removed.
    const id = knownGroupIds.has(raw) ? raw : OTHER_GROUP_ID;
    if (!buckets.has(id)) buckets.set(id, []);
    buckets.get(id)!.push(meta);
    placed.add(meta.key);
  }

  const result: RenderedGroup[] = [];
  for (const group of GROUPS) {
    const list = buckets.get(group.id)!;
    if (list.length > 0)
      result.push({
        id: group.id,
        label: group.label,
        entities: list,
      });
  }
  const other = buckets.get(OTHER_GROUP_ID);
  if (other && other.length > 0)
    result.push({
      id: OTHER_GROUP_ID,
      label: OTHER_GROUP_LABEL,
      entities: other,
    });

  return result;
}
