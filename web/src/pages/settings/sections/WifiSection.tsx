import { Button, Card, Select, TextField } from "../../../components/ui";
import { SecretField } from "./SecretField";
import { Notice } from "./Notice";
import {
  MANUAL_SSID_OPTION,
  describeWifiKey,
  provisionNotice,
  signalWords,
  type NetworkOption,
} from "../wifi";
import type { Explanation } from "../errors";
import { t } from "../../../i18n/index";
import styles from "./sections.module.css";

/**
 * The network form.
 *
 * Two controls for one value: a list of what the device heard, and a box for a name it did not
 * -- a hidden network is invisible to every scan by definition, and a name the phone shows but
 * this radio cannot hear is the single most common support request for any 2.4 GHz device
 * (docs/guide/first-setup.md). Either one alone leaves a household unable to
 * connect its own device.
 *
 * Presentational and hook-free on purpose: the page holds the state, and every rule that could
 * be wrong -- which key belongs to which network, what the list may display, what the owner is
 * told before pressing the button -- lives in ../wifi.ts under test.
 */
export function WifiSection({
  networks,
  configuredSsid,
  knownGood,
  ssid,
  manual,
  psk,
  scanned,
  scanning,
  scanFailure,
  provisioning,
  provisionFailure,
  accepted,
  onSsid,
  onManual,
  onPsk,
  onScan,
  onSave,
}: {
  networks: NetworkOption[];
  configuredSsid: string;
  /** DeviceConfig.wifi_known_good: a stored pair has actually reached the owner's network. */
  knownGood: boolean;
  ssid: string;
  manual: boolean;
  psk: string;
  /**
   * True once a sweep has ANSWERED, which is not the same as `networks.length > 0`: mergeScan
   * seeds the list with the configured network, so on every configured device the list is
   * non-empty before the radio has been asked anything.
   */
  scanned: boolean;
  scanning: boolean;
  scanFailure: Explanation | null;
  provisioning: boolean;
  provisionFailure: Explanation | null;
  accepted: boolean;
  onSsid: (v: string) => void;
  onManual: (v: boolean) => void;
  onPsk: (v: string) => void;
  onScan: () => void;
  onSave: () => void;
}) {
  const chosen = networks.find((n) => n.ssid === ssid);
  const notice = provisionNotice(
    ssid === "" ? t("settings.wifi.notice.newNetworkFallback") : ssid,
    configuredSsid,
    knownGood,
  );

  // An empty list is not a choice. Until a scan comes back there is exactly one way to name a
  // network, and burying the box behind an "Other..." row in an empty dropdown would hide the
  // only working control on the card.
  const listUsable = networks.length > 0 && !manual;

  return (
    <Card
      title={t("settings.wifi.title")}
      subtitle={t("settings.wifi.subtitle")}
    >
      <div class={styles.stack}>
        {listUsable ? (
          <Select
            label={t("settings.wifi.network.label")}
            value={ssid}
            options={[
              // The row that means "I have not chosen". listedSsid() and reconcileChoice() in
              // ../wifi.ts depend on it existing: without it the form's "" matches no option,
              // a <select> renders its FIRST option instead, and those functions would have to
              // go back to picking the strongest network to keep the control honest -- which
              // leaves an owner who pressed Scan to look one press from provisioning the
              // device to the loudest network in the building. mergeScan() guarantees "" is
              // never a real SSID, so this row can never collide with one.
              { value: "", label: t("settings.wifi.network.choose") },
              ...networks.map((n) => ({
                value: n.ssid,
                label:
                  `${n.ssid} — ${signalWords(n.rssi)}`
                  + (n.configured ? ` · ${t("settings.wifi.network.currentlyConfigured")}` : "")
                  + (n.secure ? "" : ` · ${t("settings.wifi.network.open")}`),
              })),
              { value: MANUAL_SSID_OPTION, label: t("settings.wifi.network.manualOption") },
            ]}
            onChange={(v) => {
              const picked = String(v);
              if (picked === MANUAL_SSID_OPTION) {
                onManual(true);
                onSsid("");
              } else {
                onSsid(picked);
              }
            }}
          />
        ) : (
          <TextField
            label={t("settings.wifi.network.ssidLabel")}
            value={ssid}
            // A network name is not prose either. A phone that capitalises "netgear" names a
            // network that does not exist, and the device reports "network not found" -- true,
            // and no help at all in finding the missing capital.
            verbatim
            placeholder={t("settings.wifi.network.ssidPlaceholder")}
            onChange={onSsid}
          />
        )}

        <div class={styles.actions}>
          {/* `scanned`, not networks.length: on a configured device mergeScan puts the stored
              network in the list before any sweep has run, so the button offered to do again
              something it had never done once. */}
          <Button onClick={onScan} loading={scanning}>
            {scanned ? t("settings.wifi.scan.again") : t("settings.wifi.scan.start")}
          </Button>
          {networks.length > 0 && (
            <Button onClick={() => onManual(!manual)}>
              {manual ? t("settings.wifi.chooseFromList") : t("settings.wifi.typeInstead")}
            </Button>
          )}
        </div>

        <p class={styles.hint}>{t("settings.wifi.scan.hint")}</p>

        {scanFailure && <Notice what={scanFailure} />}

        <SecretField
          label={t("settings.wifi.password.label")}
          value={psk}
          note={describeWifiKey(
            psk,
            configuredSsid === "" ? t("settings.wifi.password.fallbackNetworkName") : configuredSsid,
          )}
          onChange={onPsk}
        />

        {/*
          Said at the moment it is being typed rather than only in the README
          (docs/architecture/security.md). The setup access point is open, on purpose --
          there is no way to give the owner a generated key they could read before they have a
          network -- and there is nothing in a browser that can make up for it: crypto.subtle
          does not exist on a plain http:// origin, and a self-signed certificate is rejected by
          the captive-portal WebView this page is most often opened in. The exposure is one
          form submission long, and it is real.
        */}
        <p class={styles.hint}>{t("settings.wifi.password.hintUnencrypted")}</p>

        {chosen && !chosen.secure && psk === "" && (
          <p class={styles.hint}>{t("settings.wifi.network.openHint", { ssid: chosen.ssid })}</p>
        )}
        {chosen && chosen.secure && psk === "" && (
          <p class={styles.warn}>
            {t("settings.wifi.network.securedWarning", { ssid: chosen.ssid })}
          </p>
        )}

        {/*
          Before the button, never after it. The reply to this form is the last thing this
          browser gets from the device, so anything printed afterwards is printed onto a page
          that has already stopped loading.
        */}
        <div class={styles.beforeYouPress}>
          <strong>{t("settings.wifi.beforeYouPress")}</strong>
          <ul>
            {notice.lines.map((line) => (
              <li key={line}>{line}</li>
            ))}
          </ul>
        </div>

        <div class={styles.actions}>
          <Button
            variant="primary"
            onClick={onSave}
            loading={provisioning}
            disabled={ssid === ""}
          >
            {t("settings.wifi.save")}
          </Button>
        </div>

        {provisionFailure && <Notice what={provisionFailure} />}

        {accepted && (
          <div class={styles.beforeYouPress}>
            <strong>{t("settings.wifi.accepted")}</strong>
            {/* The first line said "a confirmation means accepted, not connected". It has
                just been confirmed, so it is dropped and the rest still applies. */}
            <ul>
              {notice.lines.slice(1).map((line) => (
                <li key={line}>{line}</li>
              ))}
            </ul>
          </div>
        )}
      </div>
    </Card>
  );
}
