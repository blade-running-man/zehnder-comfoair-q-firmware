import { useEffect, useState } from "preact/hooks";
import {
  commit, fmtDur, fmtLeft, setDuration, TIMERS, type TimerSpec, type View,
} from "../../ventilation";
import shared from "../../dashboard.module.css";
import { t } from "../../../../i18n";
import { entityName } from "../../../../i18n/entityName";
import styles from "./Timers.module.css";

const RING = 2 * Math.PI * 18;

/**
 * One timer row: a stored duration (a writable `number`) paired with the live countdown that
 * duration produces (a read-only sensor). The set value and the running one are two halves of
 * one question and the firmware publishes both, so the row shows both.
 *
 * The ring interpolates locally between the device's pushes: a countdown that only moved when a
 * frame arrived would look stalled, which is the very failure this pairing exists to fix. Each
 * push re-seeds it, so local drift never accumulates.
 */
function TimerRow({ spec, v }: { spec: TimerSpec; v: View }) {
  const source = spec.countdown(v);
  const running = spec.running(v);
  const setMin = spec.duration(v);
  const indefinite = !!spec.indefinite && v.awayMode && v.awayDuration === 0;
  const name = entityName(spec.nameEntity, spec.name);
  const mode = t(spec.runningLabel);

  const [left, setLeft] = useState<number | null>(source);
  // Re-seed whenever the device's own figure changes; the local ticker only fills the gaps.
  useEffect(() => { setLeft(source); }, [source]);
  useEffect(() => {
    if (!running || left === null || indefinite) return;
    const id = setInterval(() => setLeft((x) => (x === null ? null : Math.max(0, x - 1))), 1000);
    return () => clearInterval(id);
  }, [running, indefinite, left === null]);

  function writeFine(e: Event) {
    const el = e.currentTarget as HTMLInputElement;
    // Clamp rather than reject: an out-of-range value is a slip, and silently refusing it is
    // the error message this UI is not going to write.
    const raw = Math.round(Number(el.value));
    const val = Number.isFinite(raw) ? Math.max(spec.min, Math.min(spec.max, raw)) : spec.min;
    el.value = String(val);
    commit(setDuration(spec.durationKey, val));
  }

  let live;
  if (indefinite) {
    live = (
      <div class={styles.live}>
        <svg class={styles.ring} viewBox="0 0 40 40" aria-hidden="true">
          <circle class={styles.trk} cx="20" cy="20" r="18" stroke-width="3.5" />
          <circle class={styles.val} cx="20" cy="20" r="18" stroke-width="3.5" stroke="var(--accent-strong)" stroke-dasharray={RING.toFixed(1)} stroke-dashoffset="0" />
        </svg>
        <div class={styles.txt}>
          <div class={styles.lead}>{t("dashboard.timers.noEnd")}</div>
          <div class={styles.cap}>{t("dashboard.timers.runsUntil", { mode })}</div>
        </div>
      </div>
    );
  } else if (running && left !== null) {
    const totalSec = (setMin ?? 1) * 60 || 1;
    const frac = Math.max(0, Math.min(1, left / totalSec));
    live = (
      <div class={styles.live}>
        <svg class={styles.ring} viewBox="0 0 40 40" aria-hidden="true" style="transform:rotate(-90deg)">
          <circle class={styles.trk} cx="20" cy="20" r="18" stroke-width="3.5" />
          <circle class={styles.val} cx="20" cy="20" r="18" stroke-width="3.5" stroke="var(--accent-strong)" stroke-dasharray={RING.toFixed(1)} stroke-dashoffset={(RING * (1 - frac)).toFixed(1)} />
        </svg>
        <div class={styles.txt}>
          <div class={styles.lead}>{t("dashboard.timers.left", { time: fmtLeft(left) })}</div>
          <div class={styles.cap}>{t("dashboard.timers.isRunning", { mode })}</div>
        </div>
      </div>
    );
  } else {
    live = (
      <div class={`${styles.live} ${styles.idle}`}>
        <div class={styles.txt}>
          <div class={styles.lead}>{t("dashboard.timers.notRunning")}</div>
          <div class={styles.cap}>{t(spec.idle)}</div>
        </div>
      </div>
    );
  }

  return (
    <div class={styles.timer}>
      <div>
        <div class={styles.name}>{name}</div>
        <p class={styles.what}>{t(spec.what)}</p>
        <div class={styles.set}>
          {spec.toggle && spec.isOn ? (
            <button
              type="button"
              class={styles.toggleBtn}
              aria-pressed={spec.isOn(v)}
              onClick={() => commit(spec.toggle!(!spec.isOn!(v)))}
            >
              {t(spec.isOn(v) ? "dashboard.timers.endNow" : "dashboard.timers.startAway")}
            </button>
          ) : null}
          {spec.indefinite ? (
            <button type="button" aria-pressed={setMin === 0} onClick={() => commit(setDuration(spec.durationKey, 0))}>
              {t("dashboard.timers.untilEnd")}
            </button>
          ) : null}
          {spec.presets.map((p) => (
            <button key={p} type="button" aria-pressed={setMin === p} onClick={() => commit(setDuration(spec.durationKey, p))}>
              {fmtDur(p)}
            </button>
          ))}
          <span class={styles.fine}>
            <input
              type="number"
              inputMode="numeric"
              value={setMin ?? spec.min}
              min={spec.min}
              max={spec.max}
              step={1}
              aria-label={t("dashboard.timers.inMinutes", { name })}
              onChange={writeFine}
            />
            <span class={styles.unit}>{t("dashboard.timers.min")}</span>
          </span>
          <span class={styles.range}>{spec.min}–{spec.max}</span>
        </div>
      </div>
      {live}
    </div>
  );
}

export function Timers({ v }: { v: View }) {
  return (
    <section class={shared.card} aria-labelledby="tim-h">
      <div class={shared.cardHead}>
        <h2 class={shared.h2} id="tim-h">{t("dashboard.timers.title")}</h2>
        <p class={shared.sub}>{t("dashboard.timers.sub")}</p>
      </div>
      <div style="margin-top:8px">
        {TIMERS.map((spec) => <TimerRow key={spec.id} spec={spec} v={v} />)}
      </div>
    </section>
  );
}
