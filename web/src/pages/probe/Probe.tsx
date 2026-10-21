import { useEffect, useRef, useState } from "preact/hooks";
import { ApiError } from "../../api/client";
import { getProbe, startProbe, type ProbeReport } from "../../api/probe";
import { Button, Select } from "../../components/ui";
import { t, type MsgKey } from "../../i18n";
import { translateDetail } from "../../i18n/detail";
import type { Params } from "../../i18n/format";
import { downloadName, formatElapsed, hasContent, hex2 } from "./model";
import {
  ActiveErrorsSection, IdentitySection, OtherNodesSection, PropertiesSection, UnitsSection,
} from "./Sections";
import { PdosSection } from "./PdosSection";
import styles from "./Probe.module.css";

/**
 * Unit capabilities: start the firmware's read-only RMI walk and show its report.
 *
 * The walk runs ON THE DEVICE and its report lives there (in RAM, until the next run or a
 * reboot), so this page holds no state of its own beyond the last GET: leaving and coming back
 * mid-walk picks the same walk up again, and a second browser sees the same report.
 */

// GET /api/probe is polled while a walk runs -- the firmware pushes nothing about it (see
// docs/protocol/probe.md). Two seconds is fast enough to watch the unit counter move
// and slow enough that a three-minute walk costs the device ~90 small GETs, not thousands.
const POLL_MS = 2000;

const STATUS_KEY: Record<ProbeReport["state"], MsgKey | null> = {
  idle: null,
  running: "probe.status.running",
  done: "probe.status.done",
  failed: "probe.status.failed",
};

interface Notice { key: MsgKey; params?: Params; danger?: boolean }

/** A refused start, as a sentence. 409 is not a fault: a walk is already running (perhaps from
 *  another tab), and the page simply starts following it. */
function startNotice(err: unknown): Notice {
  if (err instanceof ApiError) {
    if (err.status === 409) return { key: "probe.start.running" };
    if (err.status === 503) return { key: "probe.start.noBus", danger: true };
    if (err.status === 401) return { key: "probe.start.login" };
    return { key: "probe.start.failed", params: { detail: translateDetail(err.message) }, danger: true };
  }
  const detail = err instanceof Error ? translateDetail(err.message) : String(err);
  return { key: "probe.start.failed", params: { detail }, danger: true };
}

function saveJson(raw: string): void {
  const url = URL.createObjectURL(new Blob([raw], { type: "application/json" }));
  const a = document.createElement("a");
  a.href = url;
  a.download = downloadName(new Date());
  a.click();
  // Revoked on the next turn, not at once: the click's navigation must read the URL first.
  setTimeout(() => URL.revokeObjectURL(url), 0);
}

export function ProbePage(_props: { path?: string }) {
  const [report, setReport] = useState<ProbeReport | null>(null);
  const [loadError, setLoadError] = useState<string | null>(null);
  const [notice, setNotice] = useState<Notice | null>(null);
  const [starting, setStarting] = useState(false);
  const [counter, setCounter] = useState<0 | 1>(0);
  const [unknownOnly, setUnknownOnly] = useState(false);
  const raw = useRef("");
  const timer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  const mounted = useRef(true);
  const lastState = useRef<ProbeReport["state"] | null>(null);

  // One GET, and the next one scheduled only while the walk is running. A failed GET mid-walk
  // (a Wi-Fi blip) is retried on the same clock rather than ending the watch: the walk goes on
  // on the device whether or not this page heard the last answer.
  async function refresh(): Promise<void> {
    clearTimeout(timer.current);
    try {
      const got = await getProbe();
      if (!mounted.current) return;
      raw.current = got.raw;
      lastState.current = got.report.state;
      setReport(got.report);
      setLoadError(null);
    } catch (e) {
      if (!mounted.current) return;
      setLoadError(e instanceof Error ? translateDetail(e.message) : String(e));
    }
    if (mounted.current && lastState.current === "running") timer.current = setTimeout(refresh, POLL_MS);
  }

  useEffect(() => {
    mounted.current = true;
    refresh();
    return () => {
      mounted.current = false;
      clearTimeout(timer.current);
    };
  }, []);

  async function start(): Promise<void> {
    setStarting(true);
    setNotice(null);
    try {
      await startProbe({ another_counter: counter });
    } catch (e) {
      setNotice(startNotice(e));
    }
    setStarting(false);
    // After a 202 AND after a 409: either way there is now a walk to follow.
    lastState.current = "running";
    await refresh();
  }

  const running = report?.state === "running";
  const statusKey = report ? STATUS_KEY[report.state] : null;

  return (
    <div class={styles.page}>
      <h2>{t("probe.heading")}</h2>

      <div class="card">
        <p class={styles.hint}>{t("probe.intro")}</p>
        <div class={styles.actions}>
          <Button variant="primary" onClick={start} disabled={running} loading={starting}>
            {t("probe.start")}
          </Button>
          {report && (hasContent(report) || report.pdos) && (
            <Button onClick={() => saveJson(raw.current)}>{t("probe.download")}</Button>
          )}
        </div>

        <details class={styles.advanced}>
          <summary>{t("probe.advanced")}</summary>
          <div class={styles.advancedBody}>
            <Select
              label={t("probe.counter.label")}
              value={counter}
              options={[{ value: 0, label: "0" }, { value: 1, label: "1" }]}
              onChange={(v) => setCounter(v === 1 ? 1 : 0)}
              disabled={running || starting}
            />
            <p class={styles.hint}>{t("probe.counter.hint")}</p>
          </div>
        </details>

        {notice && (
          <p class={`${styles.notice}${notice.danger ? ` ${styles.noticeDanger}` : ""}`}>
            {t(notice.key, notice.params)}
          </p>
        )}

        {report === null && loadError === null && <p class={styles.status}>{t("probe.loading")}</p>}
        {loadError !== null && (
          <p class={`${styles.notice} ${styles.noticeDanger}`}>
            {t("probe.readFailed")}: {loadError}
          </p>
        )}

        {report && (
          <>
            <div class={styles.status}>
              <span class={`${styles.dot} ${styles[report.state]}`} aria-hidden="true" />
              <span>{statusKey ? t(statusKey) : t("probe.idle")}</span>
            </div>
            {report.state !== "idle" && (
              <p class={styles.progress} aria-live="polite">
                {t("probe.progress", {
                  requests: report.progress.requests,
                  timeouts: report.progress.timeouts,
                  node: hex2(report.progress.node),
                  unit: hex2(report.progress.unit),
                  subunit: report.progress.subunit,
                  property: hex2(report.progress.property),
                  elapsed: formatElapsed(report.elapsed_ms),
                })}
              </p>
            )}
            {report.state === "failed" && (
              <p class={`${styles.notice} ${styles.noticeDanger}`}>
                {report.failure === "no_answer"
                  ? t("probe.failed.noAnswer")
                  : t("probe.failed.other", { failure: String(report.failure) })}
              </p>
            )}
            {report.truncated && <p class={styles.notice}>{t("probe.truncated")}</p>}
          </>
        )}
      </div>

      {report && hasContent(report) && (
        <>
          <IdentitySection report={report} />
          <UnitsSection report={report} />
          <PropertiesSection report={report} unknownOnly={unknownOnly} onUnknownOnly={setUnknownOnly} />
          <ActiveErrorsSection report={report} />
          <OtherNodesSection report={report} />
        </>
      )}

      {/* Outside hasContent: the census is heard from boot, walk or no walk. Older firmware
          sends no `pdos` key, and the card is simply not there. */}
      {report?.pdos && <PdosSection pdos={report.pdos} onRefresh={refresh} />}
    </div>
  );
}
