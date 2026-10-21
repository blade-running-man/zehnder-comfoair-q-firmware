import { Card, TextField, Toggle } from "../../../components/ui";
import { SecretField } from "./SecretField";
import { describeSecret, type SettingsForm } from "../model";
import { t } from "../../../i18n/index";
import { rich } from "../../../i18n/rich";
import styles from "./sections.module.css";

/**
 * The name, how it behaves, and the password that locks this page.
 *
 * The warning under the password is not decoration: the way back has to be on the screen
 * where the password is SET, before it is set and not after. A password nobody can undo, on a
 * device mounted behind the front panel of a running ventilation unit, is a way to lose the
 * device -- which is also why the password is optional at all (docs/device/button.md, "The
 * two-level reset").
 *
 * The gesture is described without naming a pin: AGENTS.md keeps GPIO numbers inside
 * components/board, and "the button on the device" is what the owner can act on anyway. The
 * "after it has started" half is the part that must not be dropped -- that button is also the
 * download strap, so holding it while power comes up enters the bootloader instead and looks
 * exactly like a device that has died (docs/device/button.md, "The GPIO9 boot-strap caveat").
 *
 * EVERY NUMBER AND EVERY CONSEQUENCE BELOW IS READ OFF THE FIRMWARE, not off a design note,
 * and the two disagreed until 2026-09-07: this block promised a five-second reset for
 * as long as no firmware performed one. The ladder is k_windows in comfoair_button.c (1 s, the
 * dead zone to 5 s, 5 s, 15 s, and 25 s where the button is declared stuck); what each tier
 * destroys is comfoair_config_reset_erases() (soft = cfg_wifi + cfg_owner + the legacy
 * namespace, so cfg_app -- broker, prefix, discovery, name, durations -- survives; hard = every
 * namespace); the colours are button_style() in comfoair_led_pattern.c, and the ramp between
 * thresholds is COMFOAIR_LED_RAMP. THE CONFIRMING FLASHES ARE NOT GREEN and this text must not
 * say they are: the implementation flashes the tier's own colour (amber, red) and says why
 * (comfoair_button_reset.c, flash_step); hardware scenarios HV-BTN-07/HV-BTN-10 check it.
 *
 * The 15 s tier leaves the retained Home Assistant tree standing -- comfoair_button_erase_and_
 * restart() calls no retraction, and hardware scenario HV-BTN-11 records that the phantom device
 * comes back after HA restarts. That is why the last paragraph names the only order that avoids it.
 */
export function DeviceSection({
  form,
  loaded,
  passwordSet,
  disabled,
  onChange,
}: {
  form: SettingsForm;
  loaded: { ui_password: string };
  /** comfoair_config_public_t.ui_password_set -- derived by the device from the stored record. */
  passwordSet: boolean;
  disabled: boolean;
  onChange: (patch: Partial<SettingsForm>) => void;
}) {
  // One call: the note and the placeholder are two halves of one statement (model.ts).
  const password = describeSecret(loaded.ui_password, form.ui_password);
  return (
    <Card title={t("settings.device.title")} subtitle={t("settings.device.subtitle")}>
      <div class={styles.stack}>
        <TextField
          label={t("settings.device.name.label")}
          value={form.device_name}
          // NOT verbatim: this one IS prose. "Kitchen ventilation" should keep its capital,
          // and nothing reads this string but a human.
          disabled={disabled}
          placeholder={t("settings.device.name.placeholder")}
          onChange={(v) => onChange({ device_name: v })}
        />
        <p class={styles.hint}>{t("settings.device.name.hint")}</p>

        <Toggle
          label={t("settings.device.permanentManual.label")}
          checked={form.permanent_manual}
          disabled={disabled}
          onChange={(v) => onChange({ permanent_manual: v })}
          note={t("settings.device.permanentManual.note")}
        />

        <SecretField
          label={t("settings.device.password.label")}
          value={form.ui_password}
          note={password.text}
          placeholder={password.placeholder}
          disabled={disabled}
          // Distinct from the household Wi-Fi key and from the broker's: this one is the
          // credential for this page, and it is the only one a password manager should be
          // offering to create.
          autocomplete="new-password"
          onChange={(v) => onChange({ ui_password: v })}
        />

        <div class={styles.warnBlock}>
          <strong>{t("settings.device.password.warnTitle")}</strong>
          {/* Markup inside a sentence goes through rich(): one template per paragraph, so each
              language puts the emphasised fragment where its grammar wants it. The last
              paragraph keeps its older split around the <em>; each half carries its own spacing. */}
          <p>
            {rich(t("settings.device.button.p1"), {
              after: <em>{t("settings.device.button.p1em")}</em>,
            })}
          </p>
          <p>
            {rich(t("settings.device.button.p2"), {
              short: <strong>{t("settings.device.button.p2short")}</strong>,
              ap: <code>comfoair-xxxx</code>,
              gap: <strong>{t("settings.device.button.p2gap")}</strong>,
            })}
          </p>
          <p>
            {rich(t("settings.device.button.p3"), {
              soft: <strong>{t("settings.device.button.p3strong")}</strong>,
            })}
          </p>
          <p>
            {rich(t("settings.device.button.p4"), {
              hard: <strong>{t("settings.device.button.p4strong")}</strong>,
            })}
          </p>
          <p>
            {t("settings.device.password.warn2a")}
            <em>{t("settings.device.password.warn2em")}</em>
            {t("settings.device.password.warn2b")}
          </p>
        </div>

        {!passwordSet && (
          <p class={styles.hint}>{t("settings.device.password.noneSet")}</p>
        )}
      </div>
    </Card>
  );
}
