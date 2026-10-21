import type { JSX } from "preact";
import { useEffect, useRef } from "preact/hooks";
import { FILTER_CYCLE_DAYS, dateIn } from "../../ventilation";
import type { View } from "../../ventilation";
import { LIFE_CELLS, filterWear, spentCells, type Wear } from "../../filterWear";
import { PixelFilters } from "../../pixel/pixelFilters";
import shared from "../../dashboard.module.css";
import p from "../../pixel/pixelPanel.module.css";
import { t, tp, type MsgKey } from "../../../../i18n";
import styles from "./Filters.module.css";

/**
 * Read-only filter card, drawn as a pixel scene on the same dark panel as the exchanger: the two
 * cartridges (outdoor air and extract, changed together) greying and speckling with wear, a
 * segmented life bar, and the days left as the hero number. The unit reports days REMAINING
 * (null when unknown, negative once overdue); filterWear turns that one number into everything
 * shown, and this card never writes.
 *
 * ONE STATE, ONE COLOUR. The cartridge outlines, the number, the pill and the lit life cells all
 * take their colour from the same wear state, so the drawing and the numbers always agree on
 * severity — and each of them also says it in words or length, never by hue alone.
 */

// `pill` is a catalog key, resolved in render so the card follows a language switch.
const TEXT: Record<Wear, { pill: MsgKey; pillClass: string; tone: string }> = {
  unknown: { pill: "dashboard.filters.unknown", pillClass: "", tone: p.grey },
  good: { pill: "dashboard.filters.good", pillClass: p.pillGood, tone: p.green },
  soon: { pill: "dashboard.filters.soon", pillClass: p.pillNotice, tone: p.yellow },
  overdue: { pill: "dashboard.filters.overdue", pillClass: p.pillBad, tone: p.red },
};

export function Filters({ v }: { v: View }): JSX.Element {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const sceneRef = useRef<PixelFilters | null>(null);

  const w = filterWear(v.filterDays, FILTER_CYCLE_DAYS);
  const tx = TEXT[w.wear];
  const spent = w.known ? spentCells(w.used) : 0;

  useEffect(() => {
    if (!canvasRef.current) return;
    const scene = new PixelFilters(canvasRef.current);
    sceneRef.current = scene;
    scene.start();
    return () => {
      scene.stop();
      sceneRef.current = null;
    };
  }, []);

  useEffect(() => {
    sceneRef.current?.setInputs({ used: w.used, wear: w.wear });
  }, [w.used, w.wear]);

  const fitted = w.known ? dateIn(-(FILTER_CYCLE_DAYS - Math.max(0, w.days))) : "—";
  const due = w.known ? dateIn(w.days) : "—";
  const overdue = w.wear === "overdue";

  const note = overdue
    ? t("dashboard.filters.noteOverdue")
    : t("dashboard.filters.noteCycle", { cycle: FILTER_CYCLE_DAYS });
  // The caption agrees with the number beside it, so it is plural on that number.
  const shown = Math.abs(w.days);
  const cap = !w.known ? t("dashboard.filters.days")
    : tp(overdue ? "dashboard.filters.daysOverdue" : "dashboard.filters.daysLeft", shown);

  return (
    <section class={shared.card}>
      <div class={shared.cardHead}>
        <h2 class={shared.h2}>{t("dashboard.filters.title")}</h2>
      </div>

      <div class={p.panel}>
        <div class={p.status}>
          <span class={p.statusKey}>{t("dashboard.filters.title")}</span>
          <span class={`${p.pill} ${tx.pillClass}`}>{t(tx.pill)}</span>
        </div>

        <div class={styles.body}>
          <div class={styles.scene}>
            <canvas ref={canvasRef} class={styles.canvas} aria-hidden="true" />
            <span class={`${styles.tag} ${styles.tagOut}`} aria-hidden="true">{t("dashboard.filters.tagOutdoor")}</span>
            <span class={`${styles.tag} ${styles.tagExt}`} aria-hidden="true">{t("dashboard.filters.tagExtract")}</span>
          </div>

          <div class={styles.read}>
            <div class={`${styles.big} ${tx.tone}`}>{w.known ? shown : "—"}</div>
            <div class={styles.cap}>{cap}</div>
            <div class={styles.dates}>
              <span><b>{t("dashboard.filters.fitted")}</b> {fitted}</span>
              <span><b>{t("dashboard.filters.due")}</b> {due}</span>
            </div>
          </div>
        </div>

        {/* The life bar in ten-day cells: spent cells go dark, the life left stays lit in the
            state colour. Its length says what the number says, so it is decorative to a reader. */}
        <div class={styles.life} aria-hidden="true">
          {Array.from({ length: LIFE_CELLS }, (_, i) => (
            <span key={i} class={`${styles.cell} ${i < spent ? styles.spent : tx.tone}`} />
          ))}
        </div>
      </div>

      <p class={styles.note}>{note}</p>
    </section>
  );
}
