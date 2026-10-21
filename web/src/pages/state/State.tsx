import { useEffect, useRef, useState } from "preact/hooks";
import { getConfig, getEntities, writeEntity } from "../../api/client";
import { connection, deviceState, stateReceived } from "../../api/device";
import type { EntityMeta } from "../../api/entities";
import type { StateValue } from "../../api/ws";
import { explainError, oneLineFragment } from "../settings/errors";
// The .tsx is not decoration: this tree sits on a case-insensitive filesystem, and a bare
// "./BrokerStatus" resolves to brokerStatus.ts -- the pure module already in the program -- which
// tsc -b rejects as two files differing only in casing. The extension names the file exactly.
import { BrokerStatus } from "./BrokerStatus.tsx";
import { type BrokerAddress, type BrokerVerdict, describeBroker } from "./brokerStatus.ts";
import { useBrokerFeed } from "./useBrokerFeed.ts";
import { rendersControl } from "./control.ts";
import { afterWriteFailure, beginWrite, NO_WRITES, settleSent } from "./writes.ts";
import type { Writes } from "./writes.ts";
import { EntityControl } from "./EntityControl";
import { orderedGroups } from "./order.ts";
import { t, type MsgKey } from "../../i18n";
import { entityName } from "../../i18n/entityName";
import { optionLabel } from "../../i18n/option";
import { translateDetail } from "../../i18n/detail";
import styles from "./State.module.css";

/**
 * Everything the unit is reporting, right now.
 *
 * The values arrive over the push socket and the metadata over `GET /api/entities`; the
 * page joins them by key. It does not know the name of a single entity -- there is no list
 * here, because a second entity list is the one thing AGENTS.md forbids outright. Add a
 * PDO to `pdo_table.py` and it appears on this page with no change to this file.
 *
 * Which group an entity renders in, and in what order within it, is decided entirely by
 * order.ts and groups.ts -- this component is a renderer, not a place for that logic
 * (groups.ts's own header explains why an ordering is allowed to name keys when a second entity
 * list is not).
 */

// Catalog keys by groups.ts's group id: groups.ts keeps the English names (its suite pins them),
// and the heading is translated here, at render, so a language
// switch re-renders it. A group id with no entry shows groups.ts's own label rather than vanishing.
const GROUP_LABELS: Record<string, MsgKey> = {
  ventilation: "state.group.ventilation",
  temperature: "state.group.temperature",
  humidity: "state.group.humidity",
  airflow: "state.group.airflow",
  bypass: "state.group.bypass",
  energy: "state.group.energy",
  filter: "state.group.filter",
  timers: "state.group.timers",
  settings: "state.group.settings",
  diagnostic: "state.group.diagnostic",
  other: "state.group.other",
};

/** The fan is an object, not a scalar: on/off, a speed and a preset that may be absent. */
function isFanValue(v: unknown): v is { on: boolean; speed: number; preset: string | null } {
  return typeof v === "object" && v !== null && "on" in v && "speed" in v;
}

function format(value: StateValue | undefined, meta: EntityMeta): string {
  // Absent and null are different: absent means the device has never mentioned this entity,
  // null means it has and there is no reading. Both render as a dash, because to a person
  // they mean the same thing -- but only one of them is a bug, so they are not conflated in
  // the data, only in the display.
  if (value === undefined || value === null) return "—";
  // Option strings (a fan preset, a select's current value) are translated for DISPLAY only;
  // optionLabel() passes a string it does not know through unchanged.
  if (isFanValue(value))
    return value.on
      ? t("state.fan.on", {
          preset: value.preset === null ? t("state.value.on") : optionLabel(value.preset),
          speed: value.speed,
        })
      : t("state.value.off");
  if (typeof value === "boolean") return value ? t("state.value.yes") : t("state.value.no");
  if (typeof value === "number") {
    // Trailing zeros come from the divider, not from precision: 21.5 stays 21.5 and 3 does
    // not become 3.0.
    const shown = Number.isInteger(value) ? value.toString() : value.toFixed(1);
    return meta.unit ? `${shown} ${meta.unit}` : shown;
  }
  return optionLabel(String(value));
}

export function StatePage(_props: { path?: string; default?: boolean }) {
  const [entities, setEntities] = useState<EntityMeta[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  // What every in-flight or refused write looks like, by key -- one value, not two independent
  // `useState` calls, because afterWriteFailure() (writes.ts) has to update `sent` and `failed`
  // for the SAME key in one atomic transition; two separate setters could not express that
  // (writes.ts's own section header, above the Writes interface, explains why in full).
  const [writes, setWrites] = useState<Writes>(NO_WRITES);
  // A per-key attempt counter, read and bumped synchronously, before any `await` -- it has to be
  // a ref rather than something writes.ts can generate, because it must be assignable the
  // instant a write is DISPATCHED (a fast second click can happen between renders), which is
  // before any state update a pure function could observe has committed. This is exactly the
  // "genuinely tied to THIS component's lifecycle" class of ref Settings.tsx's own
  // `statusInFlightRef` doc names -- an in-flight request -- and not something status.ts's
  // PollState pattern absorbs either, for the same reason.
  const writeSeqRef = useRef<Record<string, number>>({});
  // GET /api/status on a five-second clock for as long as this page is mounted -- the broker card
  // below is the only place in the SPA that reads it continuously.
  const broker = useBrokerFeed();
  // The broker's host and port, which the STATUS document deliberately does not carry: the
  // diagnostic struct is counters only, so that no projection of it can ever hold a secret
  // (comfoair_wire.h:246-248). The config document is where the owner typed them.
  const [address, setAddress] = useState<BrokerAddress | null>(null);

  useEffect(() => {
    getConfig()
      .then((c) => setAddress({ host: c.mqtt_host, port: c.mqtt_port }))
      // A config that would not load must not take the diagnosis with it: the address only makes
      // two of the sentences more specific, and every verdict is decided without it.
      .catch(() => setAddress({ host: t("state.broker.savedFallback"), port: 0 }));
  }, []);

  useEffect(() => {
    // Fetched once: it is sixteen kilobytes and only changes when the firmware does.
    getEntities()
      .then(setEntities)
      .catch((e) => setError(e instanceof Error ? e.message : String(e)));
  }, []);

  const status = connection.value;
  const values = deviceState.value;

  // Sweeps `sent` the moment a write settles -- see settleSent()'s own doc in writes.ts for the
  // lie this prevents (a control stuck reading "Sending X…" forever once the device changes that
  // same value on its own, well after the page's own write finished). Keyed on `values`, which
  // is a fresh object on every "state"/"delta" frame (api/device.ts), so this runs on every
  // incoming reading, the same "transition on new information" shape status.ts's poll loop uses.
  useEffect(() => {
    setWrites((w) => settleSent(w, values));
  }, [values]);

  async function write(key: string, value: StateValue) {
    const shown = String(value);
    const seq = (writeSeqRef.current[key] ?? 0) + 1;
    writeSeqRef.current[key] = seq;
    setWrites((w) => beginWrite(w, key, shown));
    try {
      await writeEntity(key, value);
    } catch (err) {
      // The device DOES answer a refusal -- 422 for a value it will not take, 503 when the bus
      // is busy (comfoair_http_command.c:58-62, 75-76, 106-112, comfoair_http_entity_post). That is
      // the one case where silence would be wrong: everything else about a write is silent by
      // design.
      //
      // `oneLineFragment(explainError(err))` rather than `err.message` raw, for two reasons
      // errors.ts's own doc on each function covers: explainError() draws the line between the
      // DEVICE talking (an ApiError's `.detail`, its own sentence) and the BROWSER talking (a
      // rejected `fetch()` throws a bare TypeError whose `.message` is "Failed to fetch", which
      // is not the device refusing anything) -- reusing it here rather than re-deriving the same
      // split is what keeps a network failure from reading as a device refusal. oneLineFragment()
      // then folds in `.headline` for 401/403, whose `.detail` alone is a single bare word
      // ("unauthorized", "forbidden") with the actionable half of the sentence living only in the
      // headline.
      //
      // afterWriteFailure() is what makes this safe against an EARLIER write's late answer
      // clobbering a LATER one's bookkeeping for the same key -- see its own doc in writes.ts
      // for the reproduced sequence. `seq` was captured above, synchronously, before this
      // function's `await`; `writeSeqRef.current[key]` read again HERE, after the await, is
      // whatever the ref holds NOW -- higher than `seq` exactly when a later write to this same
      // key has been dispatched since.
      const message = oneLineFragment(explainError(err));
      setWrites((w) => afterWriteFailure(w, key, seq, writeSeqRef.current[key], message));
    }
  }

  if (error) {
    return (
      <div class={styles.page}>
        <div class="card">
          <h2>{t("state.readFailed")}</h2>
          <p>{translateDetail(error)}</p>
        </div>
      </div>
    );
  }

  if (!entities) {
    return (
      <div class={styles.page}>
        <p>{t("state.loading")}</p>
      </div>
    );
  }

  const groups = orderedGroups(entities);

  // The card's verdict, composed here rather than inside the card, which draws and decides
  // nothing. The transport failure is the last case: when /api/status itself does not
  // answer there is no broker block to judge, so the existing explainError() speaks instead --
  // the same sentences the rest of the SPA uses for a device that has gone quiet.
  const brokerVerdict: BrokerVerdict | null =
    !broker.settled || address === null
      ? null
      : broker.unreachable !== null
        ? {
            tone: "offline",
            headline: broker.unreachable.headline,
            detail: broker.unreachable.detail,
          }
        : describeBroker(broker.mqtt, address, broker.uptimeMs);

  return (
    <div class={styles.page}>
      <div class={styles.status}>
        <span class={`${styles.dot} ${styles[status]}`} aria-hidden="true" />
        <span>
          {status === "open"
            ? stateReceived.value
              ? t("state.status.live")
              : t("state.status.waiting")
            : status === "connecting"
              ? t("state.status.connecting")
              : t("state.status.disconnected")}
        </span>
      </div>

      {brokerVerdict !== null && (
        <BrokerStatus
          verdict={brokerVerdict}
          mqtt={broker.mqtt}
          address={address}
          stale={broker.unreachable !== null}
        />
      )}

      {groups.map((group) => {
        // Split rather than rendered inline: a writable entity draws its own label (Toggle,
        // Select and TextField all render a <label>), so putting its wrapper inside the <dl>
        // below -- next to <div>s that DO contain a <dt>/<dd> pair -- would be a <div> the <dl>
        // content model does not allow. (A <dl> may only contain dt/dd groups, each optionally
        // wrapped in a <div> that itself contains such a group -- this is a summary of that
        // rule, not a quotation of it.) Lifting controls into their own block above the <dl>,
        // still inside the same `section.card`, keeps the term/value list a real term/value
        // list.
        //
        // Partitioned on `rendersControl()` (control.ts), not `meta.writable`: the fan is
        // writable but draws no control here yet (EntityControl.tsx's own interim guard), and
        // `meta.writable` alone sent it into this block, got back `null`, and produced an empty
        // bordered row with its live reading gone from the page entirely -- see
        // rendersControl()'s own doc for the full account. A key where `rendersControl()` is
        // false always still renders, in the <dl> below, through format()'s ordinary read path.
        //
        // This does move controls ahead of read-only rows they used to sit beside in GROUPS'
        // listed order (e.g. "ventilation": ventilation_mode and fan lead the group in GROUPS
        // too, but fan_level/operating_mode/device_state sit BETWEEN those and away_mode there --
        // so the rendered order is not GROUPS' per-row order, only "controls read before
        // sensors" as a group-level shape). Nothing here reorders WITHIN the controls block or
        // WITHIN the read-only rows -- both keep group.entities' own order, order.ts's to set.
        const controls = group.entities.filter((meta) => rendersControl(meta));
        const readOnly = group.entities.filter((meta) => !rendersControl(meta));
        return (
          <section key={group.id} class="card">
            <h2>{GROUP_LABELS[group.id] ? t(GROUP_LABELS[group.id]) : group.label}</h2>
            {controls.length > 0 && (
              <div class={styles.controls}>
                {controls.map((meta) => (
                  <div key={meta.key} class={styles.controlRow}>
                    <EntityControl
                      meta={meta}
                      value={values[meta.key]}
                      sent={writes.sent[meta.key] ?? null}
                      failed={writes.failed[meta.key] ?? null}
                      onWrite={(v) => void write(meta.key, v)}
                    />
                  </div>
                ))}
              </div>
            )}
            {readOnly.length > 0 && (
              <dl class={styles.grid}>
                {readOnly.map((meta) => (
                  <div key={meta.key} class={styles.row}>
                    <dt title={meta.key}>{entityName(meta.key, meta.name)}</dt>
                    <dd>{format(values[meta.key], meta)}</dd>
                  </div>
                ))}
              </dl>
            )}
          </section>
        );
      })}
    </div>
  );
}
