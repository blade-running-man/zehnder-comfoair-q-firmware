// GET /api/status, on a clock rather than once -- the whole of the plumbing that could not go into
// status.ts because none of it exists outside a mounted component: timers, an in-flight guard, an
// unmount guard.
//
// A HOOK OF ITS OWN SINCE 2026-09-07, and the seam was named before it was cut: Settings.tsx was
// 520 lines against the project's 350 (AGENTS.md), and this is the ninety of them that belong
// together and to nothing else on the page. Nothing moved but text. Every DECISION the loop makes
// still lives in status.ts, where the host suite reaches it -- what a successful poll, a failed
// poll and an accepted submission each do to what is remembered, and whether there is anything left
// to watch for.
//
// WHY IT POLLS AT ALL: the device answers POST /api/provision BEFORE it starts connecting
// (api/client.ts ProvisionRequest; failure-model.md), so "wrong-password" or "network-not-found"
// only exists a few seconds after the one-shot load this page used to do would already have
// returned. A subscription would be the honest fix, but the WebSocket protocol has exactly three
// frame types -- state, delta, ping (api/ws.ts:33-36) -- and the firmware push never sends a fourth
// for provisioning (comfoair_http_ws.c, ws_send_state), so polling over REST is what is left.

import { useEffect, useRef, useState } from "preact/hooks";

import { getStatus, type ProvisionStatus } from "../../api/client";
import {
  afterBrokerSaveAccepted,
  afterPollFailure,
  afterPollSuccess,
  afterProvisionAccepted,
  POLL_INTERVAL_MS,
  POLL_STATE_INITIAL,
  shouldKeepPolling,
  statusAgeMs,
  type PollState,
} from "./status";

export interface ProvisionStatusFeed {
  /** The last status successfully read, or null while none has landed. */
  status: ProvisionStatus | null;
  /**
   * How long ago that was, in ms; Infinity when nothing has been read.
   *
   * Recomputed on every render, deliberately, and NOT held in state: a number in state would need
   * a timer of its own to stay true, and this page re-renders on every keystroke of the form -- so
   * a reading goes visibly stale exactly while the owner is editing the field whose save depends
   * on it. The press path does not rely on that at all; it re-reads. See refresh().
   */
  ageMs: number;
  /**
   * One read, now, outside the loop. Returns what it read, or null when the read failed.
   *
   * THE ANSWER IS RETURNED AND NOT MERELY STORED, because a caller that needs it needs it in the
   * same turn: setState is not visible until the next render, and the one caller there is -- the
   * broker confirmation in saveSettings() -- has to decide on this answer before it submits.
   */
  refresh: () => Promise<ProvisionStatus | null>;
  /**
   * A Wi-Fi submission was accepted -- the device went to try. Arms the grace period
   * (REPROVISION_GRACE_MS, status.ts) and re-polls immediately rather than waiting out the current
   * tick: the device has just started retuning, so the sooner this asks again, the sooner a failure
   * word replaces stale "Trying to join" text.
   */
  provisionAccepted: () => void;
  /**
   * A broker save was accepted. Remembers the epoch the answer on screen was rendered under
   * (afterBrokerSaveAccepted, status.ts) and re-polls at once -- and the re-poll is half the
   * point: on a device already on its network this loop STOPPED after its first tick, so without
   * it nothing would ever ask again and the verdict on the broker the owner just typed would
   * arrive on the next visit to this page.
   */
  brokerSaveAccepted: () => void;
}

export function useProvisionStatus(): ProvisionStatusFeed {
  // `status` is a render-only mirror, kept in sync by pollOnce() every time it changes.
  // pollStateRef is what the loop actually reads and writes: its own continuation reads the LATEST
  // answer after an await, and a captured render's copy would still be null on the very first tick.
  const [state, setState] = useState<PollState>(POLL_STATE_INITIAL);
  const ref = useRef<PollState>(POLL_STATE_INITIAL);

  // The three ways a naive setInterval gets this wrong. `stopped` keeps the loop from rescheduling
  // itself after unmount (a timer left running would poll a page nothing is showing it to, for
  // ever); `inFlight` keeps a slow answer from overlapping the next tick (the radio can take
  // seconds to retune, and this is also the guard against refresh() racing the loop); `timer` is
  // what a manual re-poll cancels and replaces so the two do not fight for the same tick. All
  // three are tied to THIS component's lifecycle, which is why none of them belongs in status.ts.
  const stopped = useRef(false);
  const inFlight = useRef(false);
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);

  /**
   * One GET /api/status. Applies the answer through status.ts's pure transitions and mirrors the
   * result into state for rendering. The ref is written synchronously, before setState, so the very
   * next tick -- which reads the ref, not the state -- sees this answer even if Preact has not
   * re-rendered yet.
   */
  async function pollOnce(): Promise<ProvisionStatus | null> {
    // NULL AND NOT `ref.current.status`, which is the answer of some earlier moment. This function
    // answers "what did WE just read", and the caller that cares -- the broker confirmation --
    // treats null as "we could not ask", which is the branch that confirms. Handing back a
    // remembered document here would be handing back exactly the stale reading this whole change
    // exists to stop trusting.
    if (inFlight.current) return null;
    inFlight.current = true;
    try {
      const next = await getStatus();
      ref.current = afterPollSuccess(ref.current, next, performance.now());
      return next;
    } catch {
      // DO NOT overwrite what is known on a failed poll -- afterPollFailure() is identity, and this
      // is why. The device answers /api/provision and THEN leaves the channel this page is served
      // over while it tries the new network (failure-model.md), so polls fail exactly while the
      // owner is staring at this section waiting for the answer. Overwriting it now would erase the
      // one thing the section exists to show.
      ref.current = afterPollFailure(ref.current);
      return null;
    } finally {
      setState(ref.current);
      inFlight.current = false;
    }
  }

  /** Cancels any pending tick and queues the next one `delay` ms out. */
  function schedule(delay: number) {
    if (timer.current !== null) clearTimeout(timer.current);
    timer.current = setTimeout(() => void loop(), delay);
  }

  /**
   * One tick: poll, then decide whether there is anything left to watch for.
   *
   * shouldKeepPolling() reads the ref rather than pollOnce()'s answer because a FAILED poll must
   * keep the loop alive too -- what it is waiting for has not arrived either way. `performance.now()`
   * and NOT `Date.now()`: the deadline is only a guarantee against a wall clock that can jump (an
   * NTP step), so the clock feeding it has to be the monotonic one, and every other site that
   * stamps or arms below has to agree with this reader on which clock that is.
   */
  async function loop() {
    timer.current = null;
    if (stopped.current) return;
    await pollOnce();
    if (stopped.current) return;
    const { status, reprovisionUntilMs, brokerEpochAtSave } = ref.current;
    if (!shouldKeepPolling(status, reprovisionUntilMs, performance.now(), brokerEpochAtSave))
      return;
    schedule(POLL_INTERVAL_MS);
  }

  useEffect(() => {
    stopped.current = false;
    ref.current = POLL_STATE_INITIAL;
    void loop();
    // The other half of the DO NOT above.
    return () => {
      stopped.current = true;
      if (timer.current !== null) {
        clearTimeout(timer.current);
        timer.current = null;
      }
    };
    // Once, on mount.
  }, []);

  return {
    status: state.status,
    ageMs: statusAgeMs(state, performance.now()),
    refresh: () => pollOnce(),
    provisionAccepted: () => {
      ref.current = afterProvisionAccepted(ref.current, performance.now());
      setState(ref.current);
      // Guarded on the unmount flag: an accepted submission can land after the owner has already
      // navigated away.
      if (!stopped.current) schedule(0);
    },
    brokerSaveAccepted: () => {
      ref.current = afterBrokerSaveAccepted(ref.current);
      setState(ref.current);
      if (!stopped.current) schedule(0);
    },
  };
}
