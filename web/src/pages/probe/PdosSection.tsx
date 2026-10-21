import type { ProbePdoRow, ProbePdos } from "../../api/probe";
import { Button } from "../../components/ui";
import { t, type MsgKey } from "../../i18n";
import { formatElapsed, hex2 } from "./model";
import {
  ageMs, humanAge, listText, neverHeard, spacedHex, tooEarly, unlistedCount,
  type AgeUnit, type NeverHeard,
} from "./pdos";
import styles from "./Probe.module.css";

/**
 * The PDO census: what the device has HEARD on the bus since boot, beside what its entity table
 * expects. It is passive -- no walk is needed -- so the page shows it whenever the firmware sends
 * one, walk or no walk. Firmware without a census sends no `pdos` key and the card is absent.
 */

const AGO_KEY: Record<AgeUnit, MsgKey> = {
  s: "probe.pdos.ago.s",
  min: "probe.pdos.ago.min",
  h: "probe.pdos.ago.h",
  d: "probe.pdos.ago.d",
};

const DEFAULT_KEY: Record<NeverHeard["defaults"], MsgKey> = {
  on: "probe.pdos.default.on",
  off: "probe.pdos.default.off",
  mixed: "probe.pdos.default.mixed",
};

function Summary({ pdos }: { pdos: ProbePdos }) {
  const items: Array<[MsgKey, string | number, boolean]> = [
    ["probe.pdos.uptime", formatElapsed(pdos.uptime_ms), false],
    ["probe.pdos.frames", pdos.frames, false],
    ["probe.pdos.distinct", pdos.distinct, false],
    ["probe.pdos.unlisted", unlistedCount(pdos), false],
    ["probe.pdos.overflow", pdos.overflow, pdos.overflow > 0],
  ];
  return (
    <dl class={styles.summary}>
      {items.map(([key, value, warn]) => (
        <div key={key}>
          <dt>{t(key)}</dt>
          <dd class={warn ? styles.cellError : undefined}>{value}</dd>
        </div>
      ))}
    </dl>
  );
}

function HeardRow({ row, uptime }: { row: ProbePdoRow; uptime: number }) {
  const age = humanAge(ageMs(uptime, row.last_ms));
  return (
    <tr class={row.known ? undefined : styles.unlisted}>
      <td class={styles.mono}>{row.pdo}</td>
      <td>{t(row.known ? "probe.pdos.known" : "probe.pdos.unknown")}</td>
      <td>{row.count}</td>
      <td>{listText(row.dlc)}</td>
      <td class={styles.mono}>{listText(row.nodes, hex2)}</td>
      <td>{t(AGO_KEY[age.unit], { n: age.n })}</td>
      <td class={styles.raw}>{spacedHex(row.last)}</td>
    </tr>
  );
}

function NeverHeardList({ pdos }: { pdos: ProbePdos }) {
  const missing = neverHeard(pdos);
  return (
    <>
      <h3 class={styles.subTitle}>{t("probe.pdos.never.heading")}</h3>
      <p class={styles.hint}>{t("probe.pdos.never.hint")}</p>
      {missing.length === 0 ? <p class={styles.hint}>{t("probe.pdos.never.all")}</p> : (
        <div class={styles.scroll}>
          <table class={styles.table}>
            <thead>
              <tr>
                <th>{t("probe.pdos.col.pdo")}</th><th>{t("probe.pdos.col.entities")}</th>
                <th>{t("probe.pdos.col.default")}</th>
              </tr>
            </thead>
            <tbody>
              {missing.map((m) => (
                <tr key={m.pdo} class={m.defaults === "off" ? styles.muted : undefined}>
                  <td class={styles.mono}>{m.pdo}</td>
                  <td class={styles.mono}>{m.entities.map((e) => e.key).join(", ")}</td>
                  <td>{t(DEFAULT_KEY[m.defaults])}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </>
  );
}

export function PdosSection({ pdos, onRefresh }: { pdos: ProbePdos; onRefresh: () => void }) {
  return (
    <section class="card">
      <div class={styles.sectionHead}>
        <h2>{t("probe.pdos.heading")}</h2>
        <Button onClick={onRefresh}>{t("probe.pdos.refresh")}</Button>
      </div>
      <p class={styles.hint}>{t("probe.pdos.intro")}</p>
      <Summary pdos={pdos} />
      {tooEarly(pdos) && <p class={styles.notice}>{t("probe.pdos.early")}</p>}
      {pdos.overflow > 0 && (
        <p class={`${styles.notice} ${styles.noticeDanger}`}>
          {t("probe.pdos.overflowNote", { overflow: pdos.overflow })}
        </p>
      )}
      {pdos.rows.length === 0 ? <p class={styles.hint}>{t("probe.pdos.none")}</p> : (
        <div class={styles.scroll}>
          <table class={styles.table}>
            <thead>
              <tr>
                <th>{t("probe.pdos.col.pdo")}</th><th>{t("probe.pdos.col.table")}</th>
                <th>{t("probe.pdos.col.count")}</th><th>{t("probe.pdos.col.dlc")}</th>
                <th>{t("probe.pdos.col.nodes")}</th><th>{t("probe.pdos.col.lastSeen")}</th>
                <th>{t("probe.pdos.col.last")}</th>
              </tr>
            </thead>
            <tbody>
              {pdos.rows.map((r) => <HeardRow key={r.pdo} row={r} uptime={pdos.uptime_ms} />)}
            </tbody>
          </table>
        </div>
      )}
      <NeverHeardList pdos={pdos} />
    </section>
  );
}
