import { connection, deviceState, stateReceived } from "../../api/device";
import { readView } from "./ventilation";
import { Bypass, Comfort, Filters, Guide, Hero, Timers } from "./sections";
import { t } from "../../i18n";
import { BrokerAlert } from "../state/BrokerAlert";
import styles from "./dashboard.module.css";

/**
 * The home dashboard — the default screen. It reads the live state once (readView touches
 * deviceState.value, which is what subscribes this component to the push socket) and hands each
 * section a plain view model, so no section knows an entity key. The generic, registry-driven
 * screen still exists, one nav step away at /diagnostics, for everything this view does not name.
 */
export function DashboardPage(_props: { path?: string; default?: boolean }) {
  const status = connection.value;
  const ready = stateReceived.value;
  // Read the signal so this component re-renders on every frame; the value is unused directly.
  void deviceState.value;
  const v = readView();

  const dotClass =
    status === "open" ? styles.statusOpen
    : status === "connecting" ? styles.statusConnecting
    : styles.statusClosed;
  const statusText =
    status === "open"
      ? t(ready ? "dashboard.conn.live" : "dashboard.conn.waiting")
      : t(status === "connecting" ? "dashboard.conn.connecting" : "dashboard.conn.closed");

  return (
    <div class={styles.shell}>
      <header class={styles.masthead}>
        <div class={styles.wordmark}>
          <span class={styles.mark} aria-hidden="true">
            <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">
              <path d="M3 12h11a3 3 0 1 0-3-3" />
              <path d="M3 17h14a3 3 0 1 1-3 3" />
            </svg>
          </span>
          <h1 class={styles.title}>ComfoAir Q <span>{t("dashboard.title.suffix")}</span></h1>
        </div>
        <p class={styles.tagline}>{t("dashboard.tagline")}</p>
      </header>

      <div class={styles.status}>
        <span class={`${styles.statusDot} ${dotClass}`} aria-hidden="true" />
        <span>{statusText}</span>
      </div>

      {/* Silent on purpose: it draws NOTHING while the broker is working. The row above is
          about the socket this page is fed by; a broker that has fallen off leaves that row
          saying "Live" and Home Assistant empty, and both statements are true. BrokerAlert owns
          when to speak and what to say -- see pages/state/brokerStatus.ts. */}
      <BrokerAlert />

      <div class={styles.grid}>
        <div class={styles.rise}><Hero v={v} /></div>
        <div class={styles.rise}><Bypass v={v} /></div>
        <div class={styles.rise}><Filters v={v} /></div>
        <div class={styles.rise}><Comfort v={v} /></div>
        <div class={styles.rise}><Timers v={v} /></div>
      </div>

      <Guide />
    </div>
  );
}
