import type { ProbeProperty, ProbeReport, ProbeUnit } from "../../api/probe";
import { Toggle } from "../../components/ui";
import { t, type MsgKey } from "../../i18n";
import {
  OUR_NODE, activeErrorsCell, answerCell, groupProperties, hex2, identityRows, otherNodes,
  rangeCell, rawHex, targetUnits, type Cell,
} from "./model";
import styles from "./Probe.module.css";

/**
 * The report's five sections. Every word is translated here, at render; model.ts hands over
 * Cells so the pure half never touches the language. RMI names (NODE, "Airflow preset low",
 * not_gettable) are the firmware's and are shown as they come.
 */

const CELL_CLASS: Record<Cell["kind"], string | undefined> = {
  text: undefined,
  none: undefined,
  error: styles.cellError,
  timeout: styles.cellTimeout,
  redacted: styles.cellHidden,
};

function cellText(c: Cell): string {
  switch (c.kind) {
    case "text": return c.text;
    case "error": return c.name;
    case "timeout": return t("probe.value.timeout");
    case "redacted": return t("probe.value.hidden");
    case "none": return "—";
  }
}

function CellTd({ cell }: { cell: Cell }) {
  return <td class={CELL_CLASS[cell.kind]}>{cellText(cell)}</td>;
}

function Name({ name }: { name: string | null }) {
  return name === null
    ? <span class={styles.unknown}>{t("probe.value.unknown")}</span>
    : <>{name}</>;
}

function Empty() {
  return <p class={styles.hint}>{t("probe.empty")}</p>;
}

/** Name/value rows: the identity block, here and per other node. */
function IdentityTable({ rows }: { rows: ProbeProperty[] }) {
  return (
    <div class={styles.scroll}>
      <table class={styles.table}>
        <thead><tr><th>{t("probe.col.name")}</th><th>{t("probe.col.value")}</th></tr></thead>
        <tbody>
          {rows.map((p) => (
            <tr key={p.property}>
              <td title={hex2(p.property)}><Name name={p.name} /></td>
              <CellTd cell={answerCell(p.value, p.unit_label, p.format)} />
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

export function IdentitySection({ report }: { report: ProbeReport }) {
  const rows = identityRows(report, report.node);
  return (
    <section class="card">
      <h2>{t("probe.identity.heading")}</h2>
      {rows.length ? <IdentityTable rows={rows} /> : <Empty />}
    </section>
  );
}

const STATUS_KEY: Record<ProbeUnit["status"], MsgKey> = {
  present: "probe.unit.present",
  absent: "probe.unit.absent",
  timeout: "probe.unit.timeout",
};

const STATUS_CLASS: Record<ProbeUnit["status"], string | undefined> = {
  present: styles.statusPresent,
  absent: undefined,
  timeout: styles.statusTimeout,
};

export function UnitsSection({ report }: { report: ProbeReport }) {
  const units = targetUnits(report);
  return (
    <section class="card">
      <h2>{t("probe.units.heading")}</h2>
      {units.length === 0 ? <Empty /> : (
        <div class={styles.scroll}>
          <table class={styles.table}>
            <thead>
              <tr>
                <th>{t("probe.col.unit")}</th><th>{t("probe.col.name")}</th>
                <th>{t("probe.col.status")}</th><th>{t("probe.col.subunits")}</th>
                <th>{t("probe.col.absent")}</th>
              </tr>
            </thead>
            <tbody>
              {units.map((u) => (
                <tr key={u.unit} class={u.status === "absent" ? styles.muted : undefined}>
                  <td class={styles.mono}>{hex2(u.unit)}</td>
                  <td><Name name={u.name} /></td>
                  <td class={STATUS_CLASS[u.status]}>{t(STATUS_KEY[u.status])}</td>
                  <td>{u.status === "present" ? u.subunits : "—"}</td>
                  <td>{u.status === "present" ? u.absent_properties : "—"}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </section>
  );
}

function PropertyRow({ p }: { p: ProbeProperty }) {
  return (
    <tr>
      <td>{p.subunit}</td>
      <td class={styles.mono}>{hex2(p.property)}</td>
      <td><Name name={p.name} /></td>
      <CellTd cell={answerCell(p.value, p.unit_label, p.format)} />
      <CellTd cell={rangeCell(p.range, p.unit_label)} />
      <CellTd cell={answerCell(p.step, p.unit_label, p.format)} />
      <td>{p.access === null ? "—" : t(p.access === "rw" ? "probe.access.rw" : "probe.access.ro")}</td>
      <td class={styles.raw}>{rawHex(p.value)}</td>
    </tr>
  );
}

export function PropertiesSection(props: {
  report: ProbeReport;
  unknownOnly: boolean;
  onUnknownOnly: (v: boolean) => void;
}) {
  const groups = groupProperties(props.report, props.unknownOnly);
  return (
    <section class="card">
      <div class={styles.sectionHead}>
        <h2>{t("probe.properties.heading")}</h2>
        <Toggle label={t("probe.unknownOnly")} checked={props.unknownOnly} onChange={props.onUnknownOnly} />
      </div>
      {groups.length === 0 ? <Empty /> : groups.map((g) => (
        <div key={g.unit}>
          <h3 class={styles.groupTitle}>
            {g.name ?? t("probe.unitHex", { id: hex2(g.unit) })}
            {g.name !== null && <span class={styles.hint}> · {hex2(g.unit)}</span>}
          </h3>
          <div class={styles.scroll}>
            <table class={styles.table}>
              <thead>
                <tr>
                  <th>{t("probe.col.subunit")}</th><th>{t("probe.col.property")}</th>
                  <th>{t("probe.col.name")}</th><th>{t("probe.col.value")}</th>
                  <th>{t("probe.col.range")}</th><th>{t("probe.col.step")}</th>
                  <th>{t("probe.col.access")}</th><th>{t("probe.col.raw")}</th>
                </tr>
              </thead>
              <tbody>
                {g.rows.map((p) => <PropertyRow key={`${p.subunit}.${p.property}`} p={p} />)}
              </tbody>
            </table>
          </div>
        </div>
      ))}
    </section>
  );
}

export function ActiveErrorsSection({ report }: { report: ProbeReport }) {
  const cell = activeErrorsCell(report.active_errors);
  let body: string;
  if (cell.kind === "ids") body = cell.ids.length ? cell.ids.join(", ") : t("probe.errors.none");
  else if (cell.kind === "none") body = t("probe.errors.notRead");
  else body = cellText(cell);
  return (
    <section class="card">
      <h2>{t("probe.errors.heading")}</h2>
      <p class={cell.kind === "ids" || cell.kind === "none" ? undefined : CELL_CLASS[cell.kind]}>{body}</p>
    </section>
  );
}

export function OtherNodesSection({ report }: { report: ProbeReport }) {
  const nodes = otherNodes(report);
  return (
    <section class="card">
      <h2>{t("probe.nodes.heading")}</h2>
      {nodes.length === 0 ? <Empty /> : nodes.map(({ node, rows }) => (
        <div key={node}>
          <h3 class={styles.nodeTitle}>
            {node === OUR_NODE ? t("probe.nodes.this", { node: hex2(node) }) : hex2(node)}
          </h3>
          {rows.length ? <IdentityTable rows={rows} /> : <p class={styles.hint}>{t("probe.nodes.noRows")}</p>}
        </div>
      ))}
    </section>
  );
}
