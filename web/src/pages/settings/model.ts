// The configuration document, between the wire and the form.
//
// Nothing here touches the DOM or the network: every rule below is one a host test can reach,
// and tests/model.test.ts is where each of them is pinned. The page component holds state and
// renders; this file decides what a submitted document means.
//
// Relative imports below carry the .ts extension, unlike the .tsx files in this page.
// That is what lets node resolve this module when the suite runs it directly
// (tests/harness.ts explains why the suite is run that way); Vite is indifferent, and
// `allowImportingTsExtensions` is already on in tsconfig.app.json.

import type { ConfigPatch, DeviceConfig } from "../../api/client.ts";
import { CONFIG_UNCHANGED } from "../../api/secrets.ts";
import { t } from "../../i18n/index.ts";

/**
 * The form's state.
 *
 * Every text control is a string, INCLUDING the port, because that is what an <input> holds.
 * A number bound to a text input cannot be empty -- clearing the box gives NaN, which renders
 * as "NaN" and cannot be typed out of. The conversion happens once, in patchFromForm(), where
 * it can be refused.
 *
 * The two secrets carry whatever GET returned until something is typed over them: the
 * sentinel for a stored value, "" for none. That is the whole write-only scheme, and
 * SecretField is the half that keeps the sentinel out of the box on screen.
 */
export interface SettingsForm {
  mqtt_host: string;
  mqtt_port: string;
  mqtt_user: string;
  mqtt_password: string;
  topic_prefix: string;
  ha_discovery: boolean;
  device_name: string;
  ui_password: string;
  permanent_manual: boolean;
}

export type PatchResult = { ok: true; patch: ConfigPatch } | { ok: false; problem: string };

/** What is about to happen to a secret when Save is pressed. */
export type SecretState = "stored" | "not-set" | "will-replace" | "will-clear" | "will-set";
export interface SecretNote {
  state: SecretState;
  /** A sentence for the owner. Never contains the sentinel. */
  text: string;
  /**
   * What the empty box says about itself, and "" for a box that is not empty.
   *
   * Here rather than in SecretField because it is a statement about the same five states as
   * `text`, and the two contradicted each other for exactly as long as they were computed in
   * two places: the component derived the placeholder from the box alone, so a stored secret
   * the owner had just emptied showed "not set" over a note reading "Saving will delete the
   * stored value". Deriving both from the same pair of values is what makes that impossible
   * rather than merely fixed.
   */
  placeholder: string;
}

export function formFromConfig(cfg: DeviceConfig): SettingsForm {
  return {
    mqtt_host: cfg.mqtt_host,
    mqtt_port: String(cfg.mqtt_port),
    mqtt_user: cfg.mqtt_user,
    mqtt_password: cfg.mqtt_password,
    topic_prefix: cfg.topic_prefix,
    ha_discovery: cfg.ha_discovery,
    device_name: cfg.device_name,
    ui_password: cfg.ui_password,
    permanent_manual: cfg.permanent_manual,
  };
}

/**
 * The one field this page judges for itself, and the reason it is the only one.
 *
 * A port has to leave here as a JSON NUMBER. Number("") is 0, Number("abc") is NaN, and
 * JSON.stringify turns NaN into `null` -- so a typo would arrive at the device as a port of
 * null or 0 and be refused with a message about a value the owner never typed. There is no
 * way to pass "abc" through as a number, so this conversion cannot be delegated.
 *
 * EXPORTED since 2026-09-07, for broker.ts. It has to answer "did the port change" with the same
 * rules the submission uses, or the warning and the save would disagree about what a port is --
 * and a second parser there would be exactly the drift this doc comment forbids below.
 *
 * Everything else IS delegated. comfoair_config_check_ssid/psk/host/user/prefix/name already
 * encode the real rules, down to "a 64-character key must be hex or wpa_supplicant silently
 * never associates" (comfoair_config_check.c:55-65), and a copy of them here would be a second
 * source of truth that goes stale. Worse, a copy that is STRICTER than the device refuses a
 * value the device would have taken, and the owner has no way around it. DO NOT add
 * length or character checks for the other fields; let the device answer and show what it
 * said.
 */
export function parsePort(text: string): number | null {
  // Trimmed because a pasted port arrives with whitespace, and refusing that would be a
  // refusal the owner cannot see. Rejected as a REGEXP rather than by Number(): Number
  // accepts "18.5", "0x75b", "1e3" and " " , none of which is a port anybody typed.
  const t = text.trim();
  if (!/^[0-9]{1,5}$/.test(t)) return null;
  const n = Number(t);
  return n >= 1 && n <= 65535 ? n : null;
}

export function patchFromForm(form: SettingsForm): PatchResult {
  const port = parsePort(form.mqtt_port);
  if (port === null)
    return { ok: false, problem: t("settings.error.portRange") };

  // Written out field by field rather than spread from the form, so that adding a field to
  // SettingsForm cannot silently start posting it. The keys here are the document; the type
  // says which ones, and this literal is what makes it true at runtime.
  return {
    ok: true,
    patch: {
      mqtt_host: form.mqtt_host.trim(),
      mqtt_port: port,
      mqtt_user: form.mqtt_user,
      mqtt_password: form.mqtt_password,
      topic_prefix: form.topic_prefix.trim(),
      ha_discovery: form.ha_discovery,
      device_name: form.device_name,
      ui_password: form.ui_password,
      permanent_manual: form.permanent_manual,
    },
  };
}

/**
 * Which of "stored" and "not set" the empty box in front of the owner is.
 *
 * SecretField draws both states as an empty input -- it must, because rendering the sentinel
 * would let someone type on the end of it and store "__UNCHANGED__hunter2" (SecretField.tsx).
 * Its placeholder distinguishes them, but it cannot do that from the box alone: "stored", "not
 * set" and "about to be deleted" are three different situations that all draw as an empty box,
 * and only two values -- what GET returned and what the form holds -- separate them. These
 * five states are all of them, and both sentences say what SAVING will do, because that is the
 * question the owner actually has.
 *
 * `loaded` is the value GET returned; `current` is what the form holds now.
 */
export function describeSecret(loaded: string, current: string): SecretNote {
  const wasSet = loaded === CONFIG_UNCHANGED;
  const untouched = current === loaded;

  if (wasSet && untouched)
    return {
      state: "stored",
      text: t("settings.secret.stored.text"),
      placeholder: t("settings.secret.stored.placeholder"),
    };
  if (wasSet && current === "")
    return {
      state: "will-clear",
      text: t("settings.secret.willClear.text"),
      // NOT "not set": there IS one, and this box is standing over it with a deletion pending.
      // The owner who emptied it deliberately reads a confirmation; the owner whose finger
      // slipped reads the one warning they are going to get.
      placeholder: t("settings.secret.willClear.placeholder"),
    };
  if (wasSet)
    return {
      state: "will-replace",
      text: t("settings.secret.willReplace.text"),
      // The box has characters in it, so no placeholder is ever drawn. "" rather than a
      // sentence nobody sees, because an unread sentence is one nobody keeps true either.
      placeholder: "",
    };
  if (current === "")
    return {
      state: "not-set",
      text: t("settings.secret.notSet.text"),
      placeholder: t("settings.secret.notSet.placeholder"),
    };
  return {
    state: "will-set",
    text: t("settings.secret.willSet.text"),
    placeholder: "",
  };
}
