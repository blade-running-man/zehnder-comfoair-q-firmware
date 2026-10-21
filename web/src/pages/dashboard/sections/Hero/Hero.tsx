import type { JSX } from "preact";
import { HeatScale } from "../HeatScale";
import { Chip, type ChipTone } from "../Chip";
import { commit, modeHelp, one, PRESETS, setMode, type View } from "../../ventilation";
import { rich } from "../../../../i18n/rich";
import { t } from "../../../../i18n";
import { optionLabel } from "../../../../i18n/option";
import styles from "./Hero.module.css";

/** The plain-language headline: what is happening, in a sentence read without translating.
    Each line is one catalog sentence with the outdoor reading as a bold {temp}, so a language
    can put the number wherever its word order wants it (i18n/rich.tsx). */
function statusLine(v: View): { status: string; line: JSX.Element } {
  const temp = (n: number) => <strong>{one(n)} °C</strong>;
  if (v.supplyStopped) {
    return {
      status: t("dashboard.hero.stopped.status"),
      line: <>{t("dashboard.hero.stopped.line")}</>,
    };
  }
  if (v.bypassOpen && v.outdoor !== null) {
    return {
      status: t("dashboard.hero.bypass.status"),
      line: rich(t("dashboard.hero.bypass.line"), { temp: temp(v.outdoor) }),
    };
  }
  if (v.recovery !== null && v.heating && v.supply !== null && v.outdoor !== null && v.extract !== null) {
    return {
      status: t("dashboard.hero.warmed.status"),
      line: rich(t("dashboard.hero.warmed.line", { avail: (v.extract - v.outdoor).toFixed(1) }), {
        temp: temp(v.outdoor),
        lift: <strong>{(v.supply - v.outdoor).toFixed(1)} °C</strong>,
      }),
    };
  }
  if (v.recovery !== null && v.outdoor !== null) {
    return {
      status: t("dashboard.hero.tempered.status"),
      line: rich(t("dashboard.hero.tempered.line"), { temp: temp(v.outdoor) }),
    };
  }
  if (v.outdoor !== null) {
    return {
      status: t("dashboard.hero.near.status"),
      line: rich(t("dashboard.hero.near.line"), { temp: temp(v.outdoor) }),
    };
  }
  return { status: t("dashboard.hero.waiting.status"), line: <>{t("dashboard.hero.waiting.line")}</> };
}

function chips(v: View): Array<{ tone: ChipTone; text: string }> {
  const out: Array<{ tone: ChipTone; text: string }> = [];
  if (v.supplyStopped) out.push({ tone: "bad", text: t("dashboard.chip.supplyStopped") });
  if (v.bypassOpen) out.push({ tone: "warn", text: t("dashboard.chip.bypassOpen") });
  out.push(v.heating
    ? { tone: "on", text: t("dashboard.season.heating") }
    : { tone: "good", text: t(v.cooling ? "dashboard.season.cooling" : "dashboard.season.mild") });
  if (v.filterDays !== null && v.filterDays <= 0) out.push({ tone: "bad", text: t("dashboard.chip.filtersOverdue") });
  else if (v.filterDays !== null && v.filterDays <= 30) out.push({ tone: "warn", text: t("dashboard.chip.filtersDue", { days: v.filterDays }) });
  return out;
}

function recovered(v: View): JSX.Element {
  if (v.bypassOpen) return <small style="margin:0">{t("dashboard.stat.bypassed")}</small>;
  if (v.recovery === null) return <>—</>;
  return <>{Math.round(v.recovery * 100)}<small>%</small></>;
}

/**
 * The hero: the plain-language status and the supply temperature on a dark neon display, with
 * the mode control below. The unit's own animation lives on the "Where the air goes" card
 * (Bypass.tsx), where the four air points it draws are read — one picture, not two copies.
 */
export function Hero({ v }: { v: View }) {
  const { status, line } = statusLine(v);
  const mode = v.mode;

  return (
    <section class={styles.hero} aria-labelledby="hero-status">
      {/* Synthwave backdrop behind the whole hero: a starfield in the deep band where the content
          sits, and a perspective grid on the floor meeting a glowing horizon. Decorative. */}
      <div class={styles.backdrop} aria-hidden="true">
        <div class={styles.stars} />
        <div class={`${styles.band} ${styles.floorBand}`}><div class={`${styles.grid} ${styles.floor}`} /></div>
        <div class={`${styles.horizon} ${styles.horizonBottom}`} />
      </div>
      <div class={styles.top}>
        <div class={styles.read}>
          <p class={styles.status} id="hero-status">{status}</p>
          <p class={styles.big}>
            <span>{v.supply !== null ? one(v.supply) : "—"}</span>
            <span class={styles.u}>°C</span>
            <span class={styles.cap}>{t("dashboard.hero.intoRooms")}</span>
          </p>
          <p class={styles.line}>{line}</p>
          <div class={styles.chips}>
            {chips(v).map((c) => <Chip key={c.text} tone={c.tone}>{c.text}</Chip>)}
          </div>
        </div>
      </div>

      <div class={styles.body}>
        <HeatScale v={v} />

        <div class={styles.modeBlock}>
          <div class={styles.modeLabel}>
            <p class={styles.modeLabelText}>{t("dashboard.mode.label")}</p>
            <span class={styles.modeHint}>
              {mode ? <><b>{optionLabel(mode)}</b> — {modeHelp(mode)}</> : t("dashboard.mode.choose")}
            </span>
          </div>
          <div class={styles.seg} role="radiogroup" aria-label={t("dashboard.mode.label")}>
            {PRESETS.map((p) => (
              <button
                key={p}
                type="button"
                role="radio"
                aria-checked={mode === p}
                onClick={() => commit(setMode(p))}
              >
                {optionLabel(p)}
              </button>
            ))}
          </div>
          <div class={styles.stats}>
            <div><div class={styles.k}>{t("dashboard.stat.airflow")}</div><div class={styles.sv}>{v.flow !== null && !v.supplyStopped ? <>{Math.round(v.flow)}<small>m³/h</small></> : "—"}</div></div>
            <div><div class={styles.k}>{t("dashboard.stat.power")}</div><div class={styles.sv}>{v.power !== null ? <>{Math.round(v.power)}<small>W</small></> : "—"}</div></div>
            <div><div class={styles.k}>{t("dashboard.stat.recovered")}</div><div class={styles.sv}>{recovered(v)}</div></div>
          </div>
        </div>
      </div>
    </section>
  );
}
