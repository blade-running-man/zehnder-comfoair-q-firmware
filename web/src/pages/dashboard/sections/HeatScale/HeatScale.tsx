import { useEffect, useRef, useState } from "preact/hooks";
import { band, one, type View } from "../../ventilation";
import { rich } from "../../../../i18n/rich";
import { t } from "../../../../i18n";
import styles from "./HeatScale.module.css";

const clamp01 = (n: number) => Math.max(0, Math.min(1, n));

/**
 * "Heat lift" — the single fact worth a person's glance under the big supply reading: how many
 * degrees the exchanger added to the incoming air, and how far that carried it from the cold
 * outdoors toward the temperature of the rooms.
 *
 * Rebuilt from a version that positioned three value-labels on one −20…+35 °C axis; indoor
 * temperatures cluster (supply ~19, extract ~21), so those labels collided in the normal case
 * and the block was unreadable. Now the ONLY thing positioned by temperature is the fill; the
 * number is degrees (the % lives on the Bypass card and the stat strip, so this is not a
 * duplicate), and the two endpoints are pinned to the edges where cold and warm can never meet.
 */
export function HeatScale({ v }: { v: View }) {
  const { outdoor, supply, extract, supplyStopped, bypassOpen } = v;

  const lift = supply !== null && outdoor !== null ? supply - outdoor : null;
  const avail = extract !== null && outdoor !== null ? extract - outdoor : null;
  // How far the incoming air was carried from outdoor toward room temperature (0..1). Only the
  // fill uses this; nothing readable is placed by it.
  const frac = lift !== null && avail !== null && avail > 0.5 ? clamp01(lift / avail) : 0;

  // A brief blur as the number re-reads on a device push, so it settles with the bar rather than
  // snapping ahead of it. Skipped under reduced-motion via CSS.
  const [flip, setFlip] = useState(false);
  const prev = useRef<number | null>(lift);
  useEffect(() => {
    if (prev.current !== lift) {
      prev.current = lift;
      setFlip(true);
      const t = setTimeout(() => setFlip(false), 60);
      return () => clearTimeout(t);
    }
  }, [lift]);

  const dark = supplyStopped || supply === null || outdoor === null;
  let big: string, note: string, showUnit = false;
  if (dark) {
    big = "—";
    note = t("dashboard.lift.nothing");
  } else if (bypassOpen) {
    big = t("dashboard.lift.bypassed");
    note = t("dashboard.lift.skips");
  } else {
    big = (lift! >= 0 ? "+" : "") + one(lift!);
    note = t(lift! >= 0 ? "dashboard.lift.warmer" : "dashboard.lift.cooler");
    showUnit = true;
  }

  const pctFrac = (frac * 100).toFixed(1) + "%";
  const fillColor = supply !== null ? band(supply) : "var(--c-cool)";
  const ariaLabel = dark
    ? t("dashboard.lift.aria.nothing")
    : bypassOpen
      ? t("dashboard.lift.aria.bypassed")
      : t("dashboard.lift.aria.warmed", {
        lift: big,
        outdoor: outdoor !== null ? one(outdoor) : "?",
        extract: extract !== null ? one(extract) : "?",
      });

  return (
    <div role="group" aria-label={ariaLabel}>
      <p class={styles.eyebrow}>{t("dashboard.lift.title")}</p>
      <div class={styles.figure}>
        <span class={`${styles.big} ${flip ? styles.flip : ""}`}>{big}</span>
        {showUnit ? <span class={styles.unit}>°C</span> : null}
        <span class={styles.note}>{note}</span>
      </div>
      <div class={styles.track} aria-hidden="true">
        <div class={styles.fill} style={`width:${pctFrac};background:${fillColor}`} />
        {!dark ? <div class={styles.marker} style={`left:${pctFrac};background:${fillColor}`} /> : null}
      </div>
      <div class={styles.ends} aria-hidden="true">
        <span>{outdoor !== null ? rich(t("dashboard.lift.outsideAt"), { temp: <b>{one(outdoor)}°</b> }) : t("dashboard.lift.outside")}</span>
        <span>{extract !== null ? rich(t("dashboard.lift.roomsAt"), { temp: <b>{one(extract)}°</b> }) : t("dashboard.lift.rooms")}</span>
      </div>
    </div>
  );
}
