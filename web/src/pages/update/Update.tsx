import { useEffect, useRef, useState } from "preact/hooks";
import { ApiError, getConfig } from "../../api/client";
import { OtaLost, getSwVersion, uploadFirmware } from "../../api/ota";
import { Button } from "../../components/ui";
import { toast } from "../../components/Toast";
import { t } from "../../i18n";
import { translateDetail } from "../../i18n/detail";
import { rich } from "../../i18n/rich";
import { PREFLIGHT_BYTES, isThisFirmware, parseAppDesc } from "./image";
import {
  POLL_MS, REFUSAL_KEYS, busy, checkImage, kib, outcome, percent, step,
  type Event, type Failure, type Phase, type Picked,
} from "./model";
import { verdict, type Verdict } from "./semver";
import styles from "./Update.module.css";

/**
 * Firmware update: pick firmware.bin, check it here, upload it raw to POST /api/ota, then wait
 * for the device to come back and say which version it came back with.
 *
 * The decisions are model.ts's (host-tested); this file only reads the file, runs the XHR and the
 * poll clock, and maps each phase to words.
 */

function fileOf(phase: Phase): Picked | null {
  return phase.kind === "picked" || phase.kind === "uploading" || phase.kind === "failed" ? phase.file : null;
}

/** The 403 explanation, shown up front when GET /api/config says no password is set, and again
 *  if the device answers 403 anyway. Without a password the policy refuses OTA outright: there is
 *  no signature check, so an open OTA route would run anybody's code from the LAN. */
function NoPassword() {
  return (
    <div class={`${styles.notice} ${styles.noticeDanger}`}>
      <p>{t("update.noPassword")}</p>
      <p class={styles.hint}>{t("update.noPasswordWhy")}</p>
      <p><a href="/settings">{t("update.openSettings")}</a></p>
    </div>
  );
}

function FailureLine({ failure }: { failure: Failure }) {
  switch (failure.kind) {
    case "noPassword":
      return null;  // the NoPassword block above says it
    case "login":
      return <p class={styles.notice}>{t("update.login")}</p>;
    case "refused":
      return (
        <p class={`${styles.notice} ${styles.noticeDanger}`}>
          {t("update.refused", { detail: translateDetail(failure.detail) })}
        </p>
      );
    case "lost":
      return <p class={`${styles.notice} ${styles.noticeDanger}`}>{t("update.failed")}</p>;
  }
}

function Progress({ sent, total }: { sent: number; total: number }) {
  const pct = percent(sent, total);
  return (
    <div class={styles.progressBlock}>
      <div class={styles.bar} role="progressbar" aria-valuemin={0} aria-valuemax={100} aria-valuenow={pct}>
        <div class={styles.fill} style={{ width: `${pct}%` }} />
      </div>
      <p class={styles.status}>
        {pct >= 100
          ? t("update.uploadDone")
          : t("update.uploading", { percent: pct, sent: kib(sent), total: kib(total) })}
      </p>
    </div>
  );
}

const VERDICT_KEYS = {
  newer: "update.verdict.newer",
  same: "update.verdict.same",
  older: "update.verdict.older",
} as const satisfies Record<Verdict, string>;

/** What Install would do, said before it is pressed: the running version, the file's, and -- only
 *  when both are SemVer -- which way that goes. A downgrade is allowed (it is the owner's manual
 *  rollback) but warned in the notice's warning tone; a bare git hash from an older build gets
 *  both strings and no claim. */
function Versions({ installed, image }: { installed: string | null; image: string }) {
  const v = verdict(installed, image);
  const tone = v === "older" ? "" : v === "newer" ? styles.noticeOk : styles.noticeNeutral;
  return (
    <div class={`${styles.notice} ${tone}`}>
      <p class={styles.version}>
        {installed === null ? t("update.fileVersion", { image }) : t("update.compare", { installed, image })}
      </p>
      {v !== null && (v === "older" ? <p><strong>{t(VERDICT_KEYS[v])}</strong></p> : <p>{t(VERDICT_KEYS[v])}</p>)}
    </div>
  );
}

function Back({ before, after, expected }: { before: string | null; after: string; expected: string | null }) {
  const result = outcome(before, after, expected);
  if (result === "same") {
    return (
      <div class={styles.notice}>
        <p>{t("update.same", { version: after })}</p>
        <p class={styles.hint}>{t("update.systemLink")} <a href="/system">{t("update.openSystem")}</a></p>
      </div>
    );
  }
  return (
    <div class={`${styles.notice} ${styles.noticeOk}`}>
      <p class={styles.version}>
        {result === "updated"
          ? t("update.updated", { before: before ?? "", after })
          : t("update.backRunning", { after })}
      </p>
      <p class={styles.hint}>{t("update.confirmNote")}</p>
    </div>
  );
}

export function UpdatePage(_props: { path?: string }) {
  const [phase, setPhase] = useState<Phase>({ kind: "idle" });
  const [noPassword, setNoPassword] = useState(false);
  // The running version, read when a file is picked so the comparison is against what runs NOW
  // (another tab may have updated it); null until read, or when the read failed.
  const [installed, setInstalled] = useState<string | null>(null);
  // The phase lives in a ref as well: the XHR and poll callbacks outlive the render that started
  // them, and each step must start from the latest phase, not the one their closure captured.
  const current = useRef<Phase>(phase);
  const file = useRef<File | null>(null);
  const before = useRef<string | null>(null);
  const timer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  const mounted = useRef(true);

  function dispatch(ev: Event): Phase {
    const next = step(current.current, ev, before.current);
    current.current = next;
    if (mounted.current) setPhase(next);
    return next;
  }

  useEffect(() => {
    mounted.current = true;
    // Best effort: a failed read leaves the page as it would be without it, and the device's
    // own 403 still reaches the same explanation. Asked up front so the owner is not made to
    // upload 1.3 MB to learn the device was never going to take it.
    getConfig()
      .then((c) => { if (mounted.current) setNoPassword(!c.ui_password_set); })
      .catch(() => {});
    return () => {
      mounted.current = false;
      clearTimeout(timer.current);
    };
  }, []);

  async function pick(f: File): Promise<void> {
    const head = new Uint8Array(await f.slice(0, PREFLIGHT_BYTES).arrayBuffer());
    const desc = parseAppDesc(head);
    file.current = f;
    dispatch({
      kind: "pick",
      file: { name: f.name, size: f.size, version: isThisFirmware(desc) ? desc.version : null },
      refusal: checkImage(f.size, head),
    });
    const running = await getSwVersion().catch(() => null);
    if (mounted.current) setInstalled(running);
  }

  // Not paused while the tab is hidden, unlike the System page's poll: this one is bounded (90 s)
  // and the owner is waiting on its answer.
  function schedulePoll(): void {
    clearTimeout(timer.current);
    timer.current = setTimeout(poll, POLL_MS);
  }

  async function poll(): Promise<void> {
    if (!mounted.current) return;
    let next: Phase;
    try {
      next = dispatch({ kind: "pollAnswered", version: await getSwVersion(), nowMs: Date.now() });
    } catch {
      next = dispatch({ kind: "pollFailed", nowMs: Date.now() });
    }
    if (!mounted.current) return;
    if (next.kind === "restarting") schedulePoll();
    else if (next.kind === "back" && outcome(next.before, next.after, next.expected) === "updated") toast(t("update.toastDone"));
  }

  async function install(): Promise<void> {
    const f = file.current;
    if (!f || dispatch({ kind: "start" }).kind !== "uploading") return;
    // The version BEFORE, so the end can say "a -> b". A failed read is not a reason to refuse:
    // the end then says only what the device came back running.
    before.current = await getSwVersion().catch(() => null);
    try {
      const answer = await uploadFirmware(f, (sent, total) => dispatch({ kind: "progress", sent, total }));
      // The installed version as the device read it from the image; an older firmware's 200 (or
      // a body that was not JSON, which arrives as null) does not carry it.
      const version = typeof answer?.version === "string" ? answer.version : null;
      dispatch({ kind: "accepted", nowMs: Date.now(), version });
      schedulePoll();
    } catch (err) {
      if (err instanceof ApiError) {
        if (err.status === 403) setNoPassword(true);
        dispatch({ kind: "refused", status: err.status, detail: err.message });
        return;
      }
      const allSent = err instanceof OtaLost && err.allSent;
      if (dispatch({ kind: "lost", allSent, nowMs: Date.now() }).kind === "restarting") schedulePoll();
    }
  }

  const picked = fileOf(phase);
  const canInstall = !noPassword &&
    ((phase.kind === "picked" && phase.refusal === null) ||
     (phase.kind === "failed" && phase.failure.kind !== "noPassword"));

  return (
    <div class={styles.page}>
      <h2>{t("update.heading")}</h2>
      {noPassword && <NoPassword />}

      <div class="card">
        <h3 class={styles.cardHeading}>{t("update.title")}</h3>
        <p class={styles.hint}>{t("update.instructions")}</p>
        <input
          type="file"
          accept=".bin"
          aria-label={t("update.fileAria")}
          disabled={busy(phase)}
          onChange={(e) => {
            const f = (e.target as HTMLInputElement).files?.[0];
            if (f) void pick(f);
          }}
        />

        {picked && <p class={styles.file}>{t("update.file", { name: picked.name, kib: kib(picked.size) })}</p>}
        {phase.kind === "picked" && phase.refusal !== null && (
          <p class={`${styles.notice} ${styles.noticeDanger}`}>{t(REFUSAL_KEYS[phase.refusal])}</p>
        )}
        {phase.kind === "failed" && <FailureLine failure={phase.failure} />}
        {canInstall && picked?.version != null && <Versions installed={installed} image={picked.version} />}
        {canInstall && (
          <div class={styles.actions}>
            <Button variant="primary" onClick={install}>{t("update.install")}</Button>
          </div>
        )}

        {phase.kind === "uploading" && <Progress sent={phase.sent} total={phase.total} />}
        {phase.kind === "restarting" && (
          <div class={styles.notice}>
            <p>{t(phase.uncertain ? "update.uncertain" : "update.restarting")}</p>
            <p class={styles.hint}>{t("update.waiting", { seconds: Math.round(phase.elapsedMs / 1000) })}</p>
          </div>
        )}
        {phase.kind === "back" && <Back before={phase.before} after={phase.after} expected={phase.expected} />}
        {phase.kind === "gaveUp" && (
          <div class={`${styles.notice} ${styles.noticeDanger}`}>
            <p>{t("update.gaveUp")}</p>
            <p><a href="/system">{t("update.openSystem")}</a></p>
          </div>
        )}
      </div>

      {/* The way in when the browser has none: a device that no longer answers, or a first
          install. Carried over from the page that predated POST /api/ota, which told the owner
          only this; the strap warning matters here because the reset gesture and the bootloader
          gesture are the same button, held at different moments. */}
      <div class="card">
        <h3 class={styles.cardHeading}>{t("update.usb.title")}</h3>
        <p class={styles.hint}>
          {t("update.usb.body")}
          <code> pio run -e m5stack-nanoc6 -t upload</code>
        </p>
        <p class={styles.hint}>
          {rich(t("update.usb.strap"), { after: <em>{t("update.usb.strapEm")}</em> })}
        </p>
      </div>
    </div>
  );
}
