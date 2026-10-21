import { useEffect, useRef, useState } from "preact/hooks";
import { Button } from "../../components/ui";
import { toast } from "../../components/Toast";
import {
  getConfig,
  provisionWifi,
  saveConfig,
  scanNetworks,
  type DeviceConfig,
  type ScanResult,
} from "../../api/client";
import { explainError, type Explanation } from "./errors";
import { useBrokerGuard } from "./useBrokerGuard";
import { useProvisionStatus } from "./useProvisionStatus";
import { formFromConfig, patchFromForm, type SettingsForm } from "./model";
import {
  applyScan,
  chooseNetwork,
  listedSsid,
  mergeScan,
  provisionRequest,
  seedWifi,
  type WifiForm,
} from "./wifi";
import {
  BrokerNotice, BrokerSection, DeviceSection, Notice, StatusSection, WifiSection,
} from "./sections";
import styles from "./Settings.module.css";
import { t } from "../../i18n/index";

/**
 * Settings, phase 5.
 *
 * Two forms and two buttons, because they are two different acts and one of them is not
 * reversible from here:
 *
 *   * The Wi-Fi form posts to /api/provision. It is the ONLY route an unprovisioned device
 *     accepts (comfoair_http_policy.c:31-46), it is the one with a rollback behind it
 *     (comfoair_provision.c:440-451), and it takes the device off the network this page is
 *     being served over.
 *   * Everything else posts the whole configuration document to /api/config, which cannot
 *     touch the network at all: the patch simply has no key for it (ConfigPatch in
 *     api/client.ts). Saving the broker can therefore never strand the device, however this
 *     page is misused.
 *
 * All the logic worth being wrong about lives in model.ts, wifi.ts, errors.ts, status.ts and
 * broker.ts, where the host suite in tests/ can reach it. Twice now a review round has found
 * defects in code that had settled in this file because it looked like plumbing -- the poll loop's
 * deadline arithmetic in one round, the broker confirmation's state machine in the next -- and
 * both times the reason no case caught them was that no case COULD: a component is not something
 * tests/harness.ts can call. Both are gone: the decisions to status.ts and broker.ts, the loop's
 * timers and guards to useProvisionStatus.ts. What is left here is state, wiring and layout.
 *
 * IT IS STILL OVER THE 350-LINE CEILING (AGENTS.md) and the next seam is named rather than left to
 * be found: the Wi-Fi half -- `wifi`, configuredRef, chooseSsid/chooseManual/runScan/saveWifi -- is
 * about eighty lines that talk to nothing else on this page.
 */
export function SettingsPage(_props: { path?: string }) {
  // The document as the device sent it. Kept beside the form because every secret decision --
  // "stored" against "not set", whether the stored key still belongs to the chosen network --
  // is a comparison between what arrived and what is on screen now.
  const [loaded, setLoaded] = useState<DeviceConfig | null>(null);
  const [form, setForm] = useState<SettingsForm | null>(null);
  const [loading, setLoading] = useState(true);
  const [loadFailure, setLoadFailure] = useState<Explanation | null>(null);

  // GET /api/status, on a clock rather than once, and every line of that lives in
  // useProvisionStatus.ts since 2026-09-07 -- the timers, the in-flight and unmount guards, the
  // render-only mirror -- because none of it is about this page and all of it was about ninety
  // lines of it. It hands back the last answer, HOW OLD that answer is, one read taken outside the
  // loop, and the arming an accepted Wi-Fi submission needs.
  const feed = useProvisionStatus();
  const status = feed.status;

  // THE THREE WI-FI BOXES ARE ONE ATOM, and that is what makes the two asynchronous paths on
  // this page correct rather than accidentally correct. Both of them -- the load and the scan --
  // resume after an await, in a closure that captured the values of a render the owner has
  // since typed over. Read from the closure, they reconcile against a form that no longer
  // exists: the network name typed during a sweep is replaced by the strongest thing the sweep
  // heard, and the SSID that arrives while a sweep is running is replaced the same way. As one
  // atom every such decision can be made from inside a setState UPDATER, which is handed the
  // value as it is now, and the rules themselves live in wifi.ts under test.
  //
  // DO NOT split these back into three useState calls "for readability". Three atoms cannot be
  // reconciled together, and the reconciliation is the whole subject.
  const [wifi, setWifi] = useState<WifiForm>({ ssid: "", manual: false, psk: "" });

  // The configured SSID as the device last reported it, in a ref because runScan() needs it
  // after its await and `loaded` would still be the render's copy -- the ordering this guards
  // is Scan pressed BEFORE GET /api/config answers, where the render's copy is "" and dropping
  // the stored key follows from that alone. Written the instant the document arrives, which is
  // the only place it can change.
  const configuredRef = useRef("");

  // null until a sweep has ANSWERED, which is not "the list is empty": mergeScan puts the
  // configured network in the list before any sweep has run, so a configured device would
  // otherwise offer to scan "again" before it had scanned once.
  const [scan, setScan] = useState<ScanResult[] | null>(null);
  const [scanning, setScanning] = useState(false);
  const [scanFailure, setScanFailure] = useState<Explanation | null>(null);

  const [provisioning, setProvisioning] = useState(false);
  const [provisionFailure, setProvisionFailure] = useState<Explanation | null>(null);
  const [accepted, setAccepted] = useState(false);

  const [saving, setSaving] = useState(false);
  const [saveFailure, setSaveFailure] = useState<Explanation | null>(null);

  /**
   * Reads the document. Called on mount, on the retry button, and after a successful save --
   * the last so that a password just typed comes back as the sentinel and its note stops
   * reading "will be replaced" over a value that already has been.
   *
   * The Wi-Fi boxes are seeded only while they are still empty, and seedWifi() decides that
   * from the boxes AS THEY ARE when the answer lands -- not as they were when this closure was
   * made. On the mount path those are always "" and "", so a guard on the captured values is
   * unconditionally true: the owner who starts typing while GET is in flight (WifiSection is
   * rendered immediately, deliberately -- see below) would watch the name disappear with
   * nothing on screen to say it had been discarded. Later reloads land in the same place for
   * the same reason: after a save of the broker settings, or on a retry of a load that failed
   * while the form was already being filled in.
   */
  async function load() {
    setLoading(true);
    try {
      const cfg = await getConfig();
      // Before the state updates, and not from a later render: runScan()'s continuation may
      // resume before this component renders again, and the key it keeps or drops depends on
      // this value being the one the device just sent.
      configuredRef.current = cfg.wifi_ssid;
      setLoaded(cfg);
      setForm(formFromConfig(cfg));
      setLoadFailure(null);
      setWifi((current) => seedWifi(current, cfg));
    } catch (err) {
      setLoadFailure(explainError(err));
    } finally {
      setLoading(false);
    }
  }

  useEffect(() => {
    void load();
    // Once, on mount. Re-running it on every render would fight the form for the input values.
  }, []);

  // What saving would do to the broker the live client was built from, and the confirmation that
  // guards it. Null notice for every save that changes neither host nor port -- nearly all. The
  // AGE goes in beside the reading: a status read a minute ago and never refreshed since is not an
  // answer to "is the broker up right now" (broker.ts, brokerLinkFrom).
  const broker = useBrokerGuard(loaded, form, status, feed.ageMs);

  const configuredSsid = loaded?.wifi_ssid ?? "";
  const readOnly = loaded?.read_only === true;
  const networks = mergeScan(scan ?? [], configuredSsid);

  function chooseSsid(next: string) {
    // A stored key belongs to the network it was stored for; a typed one belongs to whatever
    // is being typed. chooseNetwork() knows which is which -- and it runs on every keystroke of
    // the manual box, so it has to.
    setWifi((current) => chooseNetwork(current, next, configuredRef.current));
  }

  function chooseManual(next: boolean) {
    if (next) {
      setWifi((current) => ({ ...current, manual: true }));
      return;
    }
    // Going back to the list with a name that is not in it would leave the <select> showing
    // some other option while the form still held the typed name. Whatever the list can show
    // is what the form gets -- and since the list carries a "— choose a network —" row, that
    // now includes "nothing", so an unlisted name becomes no choice rather than the loudest
    // network on the air.
    setWifi((current) =>
      chooseNetwork(
        { ...current, manual: false },
        listedSsid(networks, current.ssid),
        configuredRef.current,
      ),
    );
  }

  async function runScan() {
    setScanning(true);
    setScanFailure(null);
    try {
      const found = await scanNetworks();
      setScan(found);
      // From the updater's argument and from the ref, NEVER from `wifi` or `configuredSsid`
      // above. A sweep is thirteen channels and takes seconds; in those seconds the owner may
      // have typed a hidden network's name, and GET /api/config may have answered with the
      // stored one and its key. Reconciling against the render this click came from discards
      // whichever of the two happened -- which is the exact failure the list is here to
      // prevent, arriving through the code that prevents it.
      setWifi((current) => applyScan(current, found, configuredRef.current));
    } catch (err) {
      // A failed scan is not a failed page. The list is a convenience; the name can always be
      // typed, and on this device the scan can fail simply because the radio left the channel
      // this page is being served on (see scanNetworks()).
      setScanFailure(explainError(err));
    } finally {
      setScanning(false);
    }
  }

  async function saveWifi() {
    setProvisioning(true);
    setProvisionFailure(null);
    setAccepted(false);
    try {
      // provisionRequest() is what keeps the sentinel off this route: the form holds it
      // whenever the owner is re-provisioning the network the stored key belongs to, and it is
      // a storable 13-byte string (wifi.ts, api/client.ts ProvisionRequest).
      await provisionWifi(provisionRequest(wifi));
      setAccepted(true);
      feed.provisionAccepted();
    } catch (err) {
      const why = explainError(err, { disconnectExpected: true });
      setProvisionFailure(why);
      // status === null means nothing answered, and after THIS request that is the ordinary
      // outcome: the device replies before it retunes (failure-model.md) and the reply can
      // lose the race with its own radio. A status means the device did answer, and answered
      // no -- which is a refusal, not a device that has gone to try.
      const wentToTry = why.status === null;
      setAccepted(wentToTry);
      // This IS the branch where the device accepted and went to try, just reached by "nothing
      // answered" instead of a 200 -- and it is also the branch most likely to end in a failure
      // word, which is exactly the answer the grace period exists to not miss.
      if (wentToTry) feed.provisionAccepted();
    } finally {
      setProvisioning(false);
    }
  }

  async function saveSettings() {
    if (form === null) return;
    // BEFORE patchFromForm: the only stop between the owner and an irreversible deletion in
    // somebody else's Home Assistant (useBrokerGuard.ts, broker.ts).
    //
    // AND IT ASKS THE DEVICE AGAIN FIRST, which is the whole of the 2026-09-07 fix. The poll loop
    // stops as soon as WI-FI is up (shouldKeepPolling(), status.ts), so on a device already on the
    // network the mqtt block on screen is the ONE answer this page ever got -- read, very often, at
    // the moment the owner opened this page BECAUSE the broker was not answering. The retry ladder
    // brings the link back while he edits the last octet, and the soft branch he is reading
    // ("nothing to clean up", "saving is not blocked", an ordinary button) then submits a WITHDRAW
    // of every entity and its entity_id. So the press pays for one round trip and decides on
    // THAT. A read that fails hands press() a null, which is "unknown", which is the branch that
    // CONFIRMS -- never the safe-looking one. DO NOT move this below patchFromForm to save a
    // request on invalid input: the confirmation belongs in front of the press, not the send.
    if (broker.press(await feed.refresh())) return;
    const built = patchFromForm(form);
    if (!built.ok) {
      // Not an Explanation from the device, and it must not look like one: nothing left this
      // page, so the device has said nothing about it.
      setSaveFailure({
        tone: "error",
        status: null,
        headline: t("settings.notSent"),
        detail: built.problem,
      });
      return;
    }

    setSaving(true);
    setSaveFailure(null);
    try {
      await saveConfig(built.patch);
      // BEFORE the reload below, and that order is the point: this remembers the applied_epoch of
      // the answer read a moment ago, up at press(), which is the pre-save one. Armed off a reading
      // taken AFTER the save, on a broker that comes up quickly, it would be waiting for a second
      // rebuild that nobody is going to make.
      feed.brokerSaveAccepted();
      broker.saved();
      toast(t("settings.saved"));
      await load();
    } catch (err) {
      setSaveFailure(explainError(err));
    } finally {
      setSaving(false);
    }
  }

  const patch = (fields: Partial<SettingsForm>) => {
    // Any edit retires a pending confirmation -- see brokerGate() in broker.ts for why.
    broker.edit();
    setForm((current) => (current === null ? current : { ...current, ...fields }));
  };

  return (
    <div class={styles.page}>
      <h2>{t("settings.title")}</h2>

      {readOnly && (
        <Notice
          what={{
            tone: "denied",
            status: null,
            headline: t("settings.readOnly.headline"),
            detail: t("settings.readOnly.detail"),
          }}
        />
      )}

      {/*
        Above the form it explains, deliberately: the owner who opens this page is usually here
        because something did not connect, and "why" belongs in front of the form that caused
        it, not after.
      */}
      <StatusSection status={status} />

      {/*
        The Wi-Fi card is rendered even when the configuration could not be loaded, and it is
        the ONE thing `read_only` does not disable. Both for the same reason: the device that
        most needs this form is the device whose settings cannot be read or written -- a fresh
        one, or one whose flash holds a document this build does not understand. The first
        invariant is that no state exists with no network, no access point and no way back
        (docs/architecture/failure-model.md, "Wi-Fi is down"), and a network form that switches
        itself off when the configuration is unreadable builds exactly that state.

        It also means the firmware must NOT gate POST /api/provision on read_only.
        comfoair_config_nvs_save_wifi() is already a separate write for a related reason
        (comfoair_config_nvs.h:57-60); this is the other one.

        Today GET /api/config answers 404 on every build of this firmware -- no handler is
        registered and asset_get refuses to hand back the SPA shell for an /api/ path
        (comfoair_http_assets.c:48-56) -- so this is not a rare path, it is the only one.
      */}
      <WifiSection
        networks={networks}
        configuredSsid={configuredSsid}
        // Absent until the device grows the field, and absent has to read as FALSE: the notice
        // it drives promises the device will put the old network back by itself, and a promise
        // made because a flag was missing is the one this page must never make.
        knownGood={loaded?.wifi_known_good === true}
        ssid={wifi.ssid}
        manual={wifi.manual}
        psk={wifi.psk}
        scanned={scan !== null}
        scanning={scanning}
        scanFailure={scanFailure}
        provisioning={provisioning}
        provisionFailure={provisionFailure}
        accepted={accepted}
        onSsid={chooseSsid}
        onManual={chooseManual}
        onPsk={(v) => setWifi((current) => ({ ...current, psk: v }))}
        onScan={() => void runScan()}
        onSave={() => void saveWifi()}
      />

      {loadFailure && (
        <div class="card">
          <div class={styles.stack}>
            <Notice what={loadFailure} />
            <p class={styles.hint}>{t("settings.load.hint")}</p>
            <div>
              <Button onClick={() => void load()} loading={loading}>
                {t("settings.tryAgain")}
              </Button>
            </div>
          </div>
        </div>
      )}

      {loading && form === null && !loadFailure && (
        <div class="card">{t("settings.loading")}</div>
      )}

      {form !== null && loaded !== null && (
        <>
          <BrokerSection form={form} loaded={loaded} disabled={readOnly} onChange={patch} />
          <DeviceSection
            form={form}
            loaded={loaded}
            passwordSet={loaded.ui_password_set}
            disabled={readOnly}
            onChange={patch}
          />

          {broker.notice !== null && <BrokerNotice what={broker.notice} />}

          {/*
            ONE button for both cards, because there is one document and one POST. Two buttons
            would imply two writes, and the second would silently resubmit the first card's
            values -- which is exactly the situation the sentinel scheme exists to make safe,
            and exactly the situation not worth relying on.

            IT CHANGES ITS WORDS rather than opening a modal. The second press is the
            confirmation, on a button that no longer says "Save settings": an owner who meant to
            save the device name cannot reach a deletion by pressing one word twice. Any edit to
            either form retires the pending confirmation (patch()).
          */}
          <div class={styles.saveBar}>
            <Button
              variant="primary"
              onClick={() => void saveSettings()}
              loading={saving}
              disabled={readOnly}
            >
              {broker.confirming && broker.notice !== null
                ? broker.notice.confirmLabel
                : t("settings.save")}
            </Button>
            {broker.confirming && <Button onClick={broker.cancel}>{t("common.cancel")}</Button>}
            <span class={styles.hint}>{t("settings.saveBar.hint")}</span>
          </div>

          {saveFailure && <Notice what={saveFailure} />}
        </>
      )}
    </div>
  );
}
