import type { JSX } from "preact";
import { useEffect, useRef } from "preact/hooks";
import { BYPASS_MODES, commit, one, setBypassMode, type View } from "../../ventilation";
import { PixelUnit } from "../../pixel/pixelUnit";
import { t } from "../../../../i18n";
import { optionLabel } from "../../../../i18n/option";
import shared from "../../dashboard.module.css";
import p from "../../pixel/pixelPanel.module.css";
import s from "./Bypass.module.css";

/**
 * "Where the air goes" — the unit itself as a pixel scene (pixelUnit.ts), drawn after the real
 * ComfoAir Q with its front cover off: the exchanger and the two fans turning at the real airflow,
 * and the four ducts rising from its top — outdoors on the left, your home on the right —
 * carrying the air in and out. The colour of
 * the out-going air carries the measured recovery; with the bypass open it does not change.
 *
 * ONE CANVAS, REAL TEXT. The scene is decorative and aria-hidden; the four air points sit beside
 * the ducts they belong to, coloured like them, and the recovery and bypass state are in the
 * status line. A visually hidden sentence says the whole picture in words. Temperature is always
 * a legible number, so nothing depends on hue. `null` renders "—", never 0.
 */

const tv = (t: number | null) => (t === null ? "—" : one(t) + "°");
const rhv = (rh: number | null) => (rh === null ? "—" : Math.round(rh) + "%");
const flowv = (f: number | null) => (f === null ? "—" : String(Math.round(f)));

type Tone = "cold" | "warm";

/** One air point at a corner of the scene, coloured like the stream at that end. */
function Reading({ pos, label, t, rh, tone }: { pos: string; label: string; t: number | null; rh: number | null; tone: Tone }) {
  return (
    <div class={`${s.reading} ${pos}`}>
      <span class={s.rLabel}>{label}</span>
      <span class={s.rLine}>
        <b class={p[tone]}>{tv(t)}</b>
        <span class={s.rRh}>{rhv(rh)}</span>
      </span>
    </div>
  );
}

export function Bypass({ v }: { v: View }): JSX.Element {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const sceneRef = useRef<PixelUnit | null>(null);

  const open = v.bypassOpen;
  const state = v.bypassMode ?? (open ? "Open" : "Closed");
  const r = v.supplyStopped || v.recovery === null ? null : Math.round(v.recovery * 100);
  // With the bypass open nothing is exchanged, so the supply arrives as cold as it left and the
  // exhaust leaves as warm as it came — the corner colours follow what the scene draws.
  const supplyTone: Tone = open ? "cold" : "warm";
  const exhaustTone: Tone = open ? "warm" : "cold";

  useEffect(() => {
    if (!canvasRef.current) return;
    const scene = new PixelUnit(canvasRef.current);
    sceneRef.current = scene;
    scene.start();
    return () => {
      scene.stop();
      sceneRef.current = null;
    };
  }, []);

  useEffect(() => {
    sceneRef.current?.setInputs({
      supplyFlow: v.flow, exhaustFlow: v.exhaustFlow, supplyStopped: v.supplyStopped,
      bypassOpen: open, recovery: v.recovery,
    });
  }, [v.flow, v.exhaustFlow, v.supplyStopped, open, v.recovery]);

  const temps = { outdoor: tv(v.outdoor), supply: tv(v.supply), extract: tv(v.extract), exhaust: tv(v.exhaust) };
  const summary = t(open ? "dashboard.bypass.summaryOpen" : "dashboard.bypass.summaryClosed", temps);

  return (
    <section class={shared.card}>
      <div class={shared.cardHead}>
        <h2 class={shared.h2}>{t("dashboard.bypass.title")}</h2>
        <p class={shared.sub}>{t(v.heating ? "dashboard.season.heating" : v.cooling ? "dashboard.season.cooling" : "dashboard.season.mild")}</p>
      </div>

      <div class={p.panel}>
        <div class={p.status}>
          <span class={p.statusKey}>{t("dashboard.bypass.key")}</span>
          <span class={p.statusState}>{optionLabel(state)}</span>
          <span class={`${p.pill} ${open ? p.pillNotice : p.pillOn}`}>
            {open ? t("dashboard.bypass.bypassed") : r !== null ? t("dashboard.bypass.recovered", { pct: r }) : t("dashboard.bypass.exchange")}
          </span>
          {v.supplyStopped ? <span class={`${p.pill} ${p.pillNotice}`}>{t("dashboard.bypass.supplyStopped")}</span> : null}
        </div>

        <div class={s.stage}>
          <div class={s.scene}>
            <canvas ref={canvasRef} class={s.canvas} aria-hidden="true" />
            <p class={p.srOnly}>{summary}</p>
            <Reading pos={s.l1} label={"▶ " + t("dashboard.bypass.outdoorAir")} t={v.outdoor} rh={v.outdoorRh} tone="cold" />
            <Reading pos={s.l2} label={"◀ " + t("dashboard.bypass.exhaust")} t={v.exhaust} rh={v.exhaustRh} tone={exhaustTone} />
            <Reading pos={s.r1} label={t("dashboard.bypass.toRooms") + " ▶"} t={v.supply} rh={v.supplyRh} tone={supplyTone} />
            <Reading pos={s.r2} label={t("dashboard.bypass.fromRooms") + " ◀"} t={v.extract} rh={v.extractRh} tone="warm" />
            <span class={`${s.zone} ${s.zoneOut}`} aria-hidden="true">{t("dashboard.bypass.zoneOut")}</span>
            <span class={`${s.zone} ${s.zoneHome}`} aria-hidden="true">{t("dashboard.bypass.zoneHome")}</span>
            <span class={`${s.fanTag} ${s.fanL}`} aria-hidden="true">{t("dashboard.bypass.fanSupply")}</span>
            <span class={`${s.fanTag} ${s.fanR}`} aria-hidden="true">{t("dashboard.bypass.fanExtract")}</span>
          </div>
        </div>

        <div class={p.stats}>
          <div class={p.stat}>
            <span class={`${p.statVal} ${p.warm}`}>{flowv(v.flow)}<small>m³/h</small></span>
            <span class={p.statLabel}>{t("dashboard.bypass.supplyFlow")}</span>
          </div>
          <div class={p.stat}>
            <span class={`${p.statVal} ${p.cold}`}>{flowv(v.exhaustFlow)}<small>m³/h</small></span>
            <span class={p.statLabel}>{t("dashboard.bypass.exhaustFlow")}</span>
          </div>
          <div class={p.stat}>
            <span class={`${p.statVal} ${p.magenta}`}>{v.power !== null ? Math.round(v.power) : "—"}<small>W</small></span>
            <span class={p.statLabel}>{t("dashboard.stat.power")}</span>
          </div>
          <div class={p.stat}>
            <span class={`${p.statVal} ${p.yellow}`}>{v.meanOutdoor !== null ? one(v.meanOutdoor) : "—"}<small>°C</small></span>
            <span class={p.statLabel}>{t("dashboard.bypass.outdoorMean")}</span>
          </div>
        </div>
      </div>

      <div class={s.control}>
        <span class={s.controlLabel}>{t("dashboard.bypass.set")}</span>
        <div class={s.modes} role="radiogroup" aria-label={t("dashboard.bypass.modeAria")}>
          {BYPASS_MODES.map((m) => (
            <button key={m} type="button" role="radio" aria-checked={v.bypassMode === m} onClick={() => commit(setBypassMode(m))}>
              {optionLabel(m)}
            </button>
          ))}
        </div>
      </div>
    </section>
  );
}
