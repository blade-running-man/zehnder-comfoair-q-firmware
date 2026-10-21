import { Card, FormRow, TextField, Toggle } from "../../../components/ui";
import { SecretField } from "./SecretField";
import { describeSecret, type SettingsForm } from "../model";
import { t } from "../../../i18n/index";
import styles from "./sections.module.css";

/**
 * The MQTT broker.
 *
 * Nothing here is validated on this side beyond what patchFromForm() has to do to build a
 * JSON number out of the port box. comfoair_config_check_host/user/prefix already hold the
 * real rules, and a copy of them here would be a second source of truth that can only ever be
 * wrong in the direction that matters -- refusing a value the device would have accepted, with
 * no way for the owner to get past it.
 *
 * THE TWO NOTES AT THE BOTTOM DESCRIBE comfoair_mqtt_pending_cleanup(), which answers exactly
 * three things (comfoair_mqtt_client.c:107-128) and none of them is "the owner pressed Save":
 * a changed HOST OR PORT is a WITHDRAW, a changed PREFIX is a REKEY, and a changed NAME -- or a
 * changed discovery switch -- is nothing at all. What each one empties is
 * comfoair_ha_cleanup.c: a REKEY touches the STATE side only, because the config topic is
 * homeassistant/<component>/<device_id>/<key>/config and carries no prefix, so the documents
 * are rewritten in place and the entities move across with their history. That is why the
 * prefix note no longer says Home Assistant keeps the old entities "until the device retracts
 * them": there are no old entities to keep, and there never were.
 *
 * BOTH NOTES ARE CONDITIONAL ON A LIVE CONNECTION and say so, because the firmware is:
 * pending_cleanup() returns false with no client or no CONNACK, deliberately (AGENTS.md: nothing waits or retries because a peer
 * is absent), and
 * comfoair_mqtt_retract() gives the
 * sweep RETRACT_BUDGET_MS = 2000 and stops there. An owner whose broker is already unreachable
 * must not read a promise of tidying here. The move that DELETES entities has its own
 * confirmation in front of it (broker.ts, BrokerNotice.tsx) and is not restated below.
 */
export function BrokerSection({
  form,
  loaded,
  disabled,
  onChange,
}: {
  form: SettingsForm;
  /** The document as GET returned it, for describeSecret(): the sentinel, or "". */
  loaded: { mqtt_password: string };
  disabled: boolean;
  onChange: (patch: Partial<SettingsForm>) => void;
}) {
  // One call, because the note and the placeholder are two halves of one statement and were
  // wrong for as long as they were computed apart (model.ts, SecretNote.placeholder).
  const password = describeSecret(loaded.mqtt_password, form.mqtt_password);
  return (
    <Card
      title={t("settings.broker.title")}
      subtitle={t("settings.broker.subtitle")}
    >
      <div class={styles.stack}>
        <FormRow>
          <TextField
            label={t("settings.broker.host.label")}
            value={form.mqtt_host}
            // A hostname is machine-read: "Homeassistant.local" and "homeassistant.local"
            // resolve the same, but a phone that autocorrects "nas" to "gnaws" does not.
            verbatim
            disabled={disabled}
            placeholder={t("settings.broker.host.placeholder")}
            onChange={(v) => onChange({ mqtt_host: v })}
          />
          <TextField
            label={t("settings.broker.port.label")}
            value={form.mqtt_port}
            verbatim
            disabled={disabled}
            placeholder={t("settings.broker.port.placeholder")}
            onChange={(v) => onChange({ mqtt_port: v })}
          />
        </FormRow>

        <TextField
          label={t("settings.broker.username.label")}
          value={form.mqtt_user}
          verbatim
          disabled={disabled}
          onChange={(v) => onChange({ mqtt_user: v })}
        />

        <SecretField
          label={t("settings.broker.password.label")}
          value={form.mqtt_password}
          note={password.text}
          placeholder={password.placeholder}
          disabled={disabled}
          onChange={(v) => onChange({ mqtt_password: v })}
        />

        {/*
          Said out loud on the screen where it is typed rather than only in the README:
          there is no TLS in v1, so this password crosses the LAN in the clear on every
          connection (docs/architecture/security.md, "Transport security"). The owner can
          only act on that
          if they are told before they choose the password, not after.
        */}
        <p class={styles.warn}>{t("settings.broker.password.warning")}</p>

        <TextField
          label={t("settings.broker.topicPrefix.label")}
          value={form.topic_prefix}
          verbatim
          disabled={disabled}
          placeholder={"comfoair/<device id>"}
          onChange={(v) => onChange({ topic_prefix: v })}
        />
        <p class={styles.hint}>{t("settings.broker.topicPrefix.hint")}</p>

        <Toggle
          label={t("settings.broker.discovery.label")}
          checked={form.ha_discovery}
          disabled={disabled}
          onChange={(v) => onChange({ ha_discovery: v })}
          note={t("settings.broker.discovery.note")}
        />
      </div>
    </Card>
  );
}
