import { rich } from "../../../../i18n/rich";
import { t, type MsgKey } from "../../../../i18n";
import styles from "./Guide.module.css";

/**
 * The calm close (peak-end): user help, not designer notes. Most people never touch the unit,
 * so this says when each control actually earns its keep — and folds the "how it reads" detail
 * away for the curious rather than ending on a definition list.
 */

// Catalog keys, resolved in render: a table of prose frozen at import would never follow a
// language switch. The id doubles as the list key, stable across languages.
const ITEMS: Array<{ id: string; title: MsgKey; body: MsgKey }> = [
  { id: "auto", title: "dashboard.guide.auto.title", body: "dashboard.guide.auto.body" },
  { id: "away", title: "dashboard.guide.away.title", body: "dashboard.guide.away.body" },
  { id: "manual", title: "dashboard.guide.manual.title", body: "dashboard.guide.manual.body" },
  { id: "boost", title: "dashboard.guide.boost.title", body: "dashboard.guide.boost.body" },
  { id: "bypass", title: "dashboard.guide.bypass.title", body: "dashboard.guide.bypass.body" },
  { id: "filters", title: "dashboard.guide.filters.title", body: "dashboard.guide.filters.body" },
];

export function Guide() {
  return (
    <section class={styles.guide} aria-labelledby="guide-h">
      <h2 class={styles.h2} id="guide-h">{t("dashboard.guide.title")}</h2>
      <p class={styles.lede}>{t("dashboard.guide.lede")}</p>
      <div class={styles.grid}>
        {ITEMS.map((it) => (
          <div key={it.id} class={styles.item}>
            <h3><span class={styles.pip} aria-hidden="true" />{t(it.title)}</h3>
            <p>{t(it.body)}</p>
          </div>
        ))}
      </div>

      <details class={styles.about}>
        <summary>{t("dashboard.guide.about")}</summary>
        <dl>
          <dt>{t("dashboard.guide.shape.term")}</dt>
          <dd>{rich(t("dashboard.guide.shape.body"), { em: <strong>{t("dashboard.guide.shape.em")}</strong> })}</dd>
          <dt>{t("dashboard.guide.unit.term")}</dt>
          <dd>{rich(t("dashboard.guide.unit.body"), { em: <strong>{t("dashboard.guide.unit.em")}</strong> })}</dd>
        </dl>
      </details>
    </section>
  );
}
