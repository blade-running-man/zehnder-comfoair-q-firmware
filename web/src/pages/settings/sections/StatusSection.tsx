import type { ProvisionStatus } from "../../../api/client";
import { describeFailure, describeWhere } from "../status.ts";
import { t } from "../../../i18n/index";
import styles from "./sections.module.css";

/**
 * Where the device is, and why it is not somewhere else.
 *
 * Shown ABOVE the Wi-Fi form deliberately: the owner who opens this page is usually here because
 * something did not work, and the answer to "why" belongs in front of the form that caused it.
 */
export function StatusSection({ status }: { status: ProvisionStatus | null }) {
  if (status === null) return null;
  const failure = describeFailure(status.failure, status.failure_reason);

  return (
    <div class="card">
      <h3>{t("settings.status.title")}</h3>
      <p>{describeWhere(status)}</p>
      {failure !== "" && <p class={styles.warn}>{failure}</p>}
      {status.has_credentials && !status.wifi_known_good && (
        <p class={styles.warn}>{t("settings.status.noKnownGood")}</p>
      )}
    </div>
  );
}
