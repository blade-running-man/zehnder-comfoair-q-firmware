import { Chip } from "../Chip";
import { commit, COMFORT, one, setSelect, TEMP_PROFILE_DISPLAY, type View } from "../../ventilation";
import shared from "../../dashboard.module.css";
import { rich } from "../../../../i18n/rich";
import { t } from "../../../../i18n";
import { optionLabel } from "../../../../i18n/option";
import styles from "./Comfort.module.css";

/**
 * Comfort & temperature — the controls the first dashboard pass left out.
 *
 * Two things a person expects to "set" here, and the firmware's honest answer to each:
 *  · Season — you CANNOT set it. heating/cooling season is read-only (PDO 210/211), derived by
 *    the unit from the running-mean outdoor temperature. So it is shown, with the reason, not
 *    offered as a control.
 *  · Temperature — you cannot set a room setpoint (the ComfoAir Q is not a thermostat, contract
 *    §3). What you CAN set is the temperature PROFILE (Cool/Normal/Warm), which shifts the
 *    target the unit aims for (profile_target_temp, shown). Plus three writable comfort selects.
 */
export function Comfort({ v }: { v: View }) {
  const heating = v.heating;

  return (
    <section class={shared.card} aria-labelledby="cmf-h">
      <div class={shared.cardHead}>
        <h2 class={shared.h2} id="cmf-h">{t("dashboard.comfort.title")}</h2>
      </div>

      <div class={styles.season}>
        <Chip tone={heating ? "on" : "good"}>
          {t(heating ? "dashboard.season.heating" : v.cooling ? "dashboard.season.cooling" : "dashboard.season.mild")}
        </Chip>
        <span class={styles.seasonNote}>
          {v.meanOutdoor !== null
            ? rich(t("dashboard.comfort.seasonNoteMean"), { mean: <b>{one(v.meanOutdoor)} °C</b> })
            : t("dashboard.comfort.seasonNote")}
        </span>
      </div>

      <div class={`${styles.row} ${styles.primary}`}>
        <div class={styles.rowHead}>
          <span class={styles.rowLabel}>{t("dashboard.comfort.profile")}</span>
          {v.targetTemp !== null ? <span class={styles.target}>{rich(t("dashboard.comfort.aiming"), { temp: <b>{one(v.targetTemp)} °C</b> })}</span> : null}
        </div>
        <div class={styles.seg} role="radiogroup" aria-label={t("dashboard.comfort.profile")}>
          {TEMP_PROFILE_DISPLAY.map((m) => (
            <button
              key={m}
              type="button"
              role="radio"
              aria-checked={v.tempProfile === m}
              onClick={() => commit(setSelect("temperature_profile", m))}
            >
              {optionLabel(m)}
            </button>
          ))}
        </div>
        <p class={styles.hint}>
          {t("dashboard.comfort.profileHint")}
        </p>
      </div>

      {COMFORT.map((spec) => (
        <div key={spec.key} class={styles.row}>
          <div class={styles.rowHead}><span class={styles.rowLabel}>{t(spec.label)}</span></div>
          <div class={`${styles.seg} ${styles.segSmall}`} role="radiogroup" aria-label={t(spec.label)}>
            {spec.options.map((o) => (
              <button
                key={o}
                type="button"
                role="radio"
                aria-checked={spec.value(v) === o}
                onClick={() => commit(setSelect(spec.key, o))}
              >
                {optionLabel(o)}
              </button>
            ))}
          </div>
          <p class={styles.hint}>{t(spec.help)}</p>
        </div>
      ))}
    </section>
  );
}
