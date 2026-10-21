import { useEffect, useRef, useState } from "preact/hooks";
import { getSystem, type SystemInfo } from "../../api/system";
import { Button } from "../../components/ui";
import { toast } from "../../components/Toast";
import { t } from "../../i18n";
import { en } from "../../i18n/catalogs/en";
import { translateDetail } from "../../i18n/detail";
import { format } from "../../i18n/format";
import type { Tr } from "./model";
import { copyText, sections, type Row, type Section } from "./sections";
import styles from "./System.module.css";

/**
 * System: who the ventilation unit is, who this device is, and how Wi-Fi, the CAN bus, the
 * command queue and MQTT are doing -- the page an owner opens first when something looks wrong.
 *
 * Everything is GET /api/system, polled. The page keeps no state beyond the last answer.
 */

// Five seconds: the counters visibly move, and an idle open tab costs the device one small GET
// per five seconds -- and none at all while the tab is hidden.
const POLL_MS = 5000;

const screen: Tr = (key, params) => t(key, params);
// The copied summary is always English (en/system.ts says why), from the same catalog.
const english: Tr = (key, params) => format(en[key], "en", params);

function hidden(): boolean {
  return typeof document !== "undefined" && document.visibilityState === "hidden";
}

/**
 * Puts `text` on the clipboard. The device is served over plain HTTP on the LAN, which is not a
 * secure context, and there `navigator.clipboard` is undefined -- so the old selection-and-copy
 * route is the one that works on the device and the Clipboard API the one that works on
 * localhost. DO NOT drop the fallback as dead code: it is the device's only path.
 */
async function writeClipboard(text: string): Promise<boolean> {
  try {
    if (navigator.clipboard && window.isSecureContext) {
      await navigator.clipboard.writeText(text);
      return true;
    }
  } catch {
    // fall through to the selection route
  }
  const area = document.createElement("textarea");
  area.value = text;
  area.setAttribute("readonly", "");
  area.style.position = "fixed";
  area.style.opacity = "0";
  document.body.appendChild(area);
  area.select();
  let done = false;
  try {
    done = document.execCommand("copy");
  } catch {
    done = false;
  }
  area.remove();
  return done;
}

const TONE_CLASS = { ok: undefined, warning: styles.warning, problem: styles.problem } as const;

function RowItem({ row }: { row: Row }) {
  const cls = [row.mono ? styles.mono : "", row.tone ? TONE_CLASS[row.tone] ?? "" : ""].join(" ").trim();
  return (
    <div class={styles.row}>
      <dt>{row.label}</dt>
      <dd class={cls || undefined}>{row.value}</dd>
    </div>
  );
}

function Card({ section }: { section: Section }) {
  const e = section.errors;
  return (
    <section class="card">
      <div class={styles.head}>
        <h2>{section.title}</h2>
        {section.aside && <span class={styles.hint}>{section.aside}</span>}
      </div>
      {section.notes.map((n) => (
        <p key={n.text} class={n.tone === "warning" ? styles.notice : styles.hint}>{n.text}</p>
      ))}
      {section.rows.length > 0 && (
        <dl class={styles.grid}>
          {section.rows.map((r) => <RowItem key={r.label} row={r} />)}
        </dl>
      )}
      {e && (
        <div class={styles.errors}>
          <h3 class={styles.subTitle}>{e.title}</h3>
          {e.empty !== null
            ? <p class={e.unread ? styles.hint : undefined}>{e.empty}</p>
            : <ul class={styles.list}>{e.lines.map((l) => <li key={l} class={styles.problem}>{l}</li>)}</ul>}
        </div>
      )}
    </section>
  );
}

export function SystemPage(_props: { path?: string }) {
  const [info, setInfo] = useState<SystemInfo | null>(null);
  const [loadError, setLoadError] = useState<string | null>(null);
  const timer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  const mounted = useRef(true);
  const inFlight = useRef(false);

  // One GET, then the next one scheduled -- only while the tab is visible. A failed GET (a Wi-Fi
  // blip) keeps the clock running and the last good answer on screen, under the error line.
  // inFlight keeps a visibility flip mid-request from starting a second, parallel loop.
  async function poll(): Promise<void> {
    clearTimeout(timer.current);
    timer.current = undefined;
    if (!mounted.current || inFlight.current || hidden()) return;
    inFlight.current = true;
    try {
      const got = await getSystem();
      if (mounted.current) {
        setInfo(got);
        setLoadError(null);
      }
    } catch (err) {
      if (mounted.current) setLoadError(err instanceof Error ? translateDetail(err.message) : String(err));
    } finally {
      inFlight.current = false;
    }
    if (mounted.current && !hidden()) timer.current = setTimeout(poll, POLL_MS);
  }

  useEffect(() => {
    mounted.current = true;
    const onVisibility = () => {
      if (hidden()) clearTimeout(timer.current);
      else poll();
    };
    document.addEventListener("visibilitychange", onVisibility);
    poll();
    return () => {
      mounted.current = false;
      clearTimeout(timer.current);
      document.removeEventListener("visibilitychange", onVisibility);
    };
  }, []);

  async function copy(): Promise<void> {
    if (!info) return;
    toast(t((await writeClipboard(copyText(info, english))) ? "system.copied" : "system.copyFailed"));
  }

  return (
    <div class={styles.page}>
      <div class={styles.head}>
        <h2>{t("system.heading")}</h2>
        {info && <Button onClick={copy}>{t("system.copy")}</Button>}
      </div>
      {info === null && loadError === null && <p class={styles.hint}>{t("system.loading")}</p>}
      {loadError !== null && (
        <p class={`${styles.notice} ${styles.noticeDanger}`}>{t("system.readFailed")}: {loadError}</p>
      )}
      {info && sections(info, screen).map((s) => <Card key={s.key} section={s} />)}
    </div>
  );
}
