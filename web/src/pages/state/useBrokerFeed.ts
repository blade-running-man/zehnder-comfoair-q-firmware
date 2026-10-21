// GET /api/status on a clock, for the two screens that show a broker verdict.
//
// The plumbing only: timers, an in-flight guard and an unmount guard, none of which exists outside
// a mounted component. Every DECISION about what the answer MEANS is in brokerStatus.ts, where the
// host suite can reach it -- the same division useProvisionStatus.ts draws against
// settings/status.ts, and for the same reason (a review round found two of three defects in
// ref-juggling that no test could see).
//
// WHY NOT useProvisionStatus(): that loop STOPS as soon as the device is provisioned and connected
// (shouldKeepPolling), which is exactly the state a diagnostics page spends all its time in. It is
// watching for the end of a provisioning attempt; this is watching a broker that can fall over an
// hour from now.
//
// It does NOT fetch GET /api/config. The address the card prints comes from the config document
// its own page already loads, and the dashboard line needs no address at all (brokerAlert).

import { useEffect, useRef, useState } from "preact/hooks";

import { getStatus, type ProvisionStatus } from "../../api/client.ts";
import type { MqttStatus } from "../../api/status.ts";
import { explainError, type Explanation } from "../settings/errors.ts";
import { BROKER_POLL_MS } from "./brokerStatus.ts";

export interface BrokerFeed {
  /** The broker block of the last answer that arrived, or undefined when the last answer carried
   *  none -- which is its own answer and never "not connected" (api/status.ts). */
  mqtt: MqttStatus | undefined;
  /** From the same answer, so the five-minute boot grace and the counters are read off one
   *  snapshot rather than two moments. 0 until the first answer lands. */
  uptimeMs: number;
  /**
   * Set when the LAST poll failed, cleared by the next one that succeeds.
   *
   * DELIBERATELY UNLIKE useProvisionStatus.ts, whose afterPollFailure() is identity: there, polls
   * fail precisely because the device is retuning its radio at the owner's request, so a failure
   * is expected and erasing the section would remove the answer being waited for. Nothing on this
   * page asks the device to go away, so a poll that stopped answering IS the news -- and a
   * diagnostics card that kept showing a cheerful verdict read four hours ago would be the exact
   * lie it exists to prevent. The last block is kept alongside, as evidence, labelled as old.
   */
  unreachable: Explanation | null;
  /** False until the first poll, success or failure, has settled. */
  settled: boolean;
}

export function useBrokerFeed(): BrokerFeed {
  const [feed, setFeed] = useState<BrokerFeed>({
    mqtt: undefined,
    uptimeMs: 0,
    unreachable: null,
    settled: false,
  });

  // The same three guards useProvisionStatus.ts names one by one: `stopped` keeps the loop from
  // rescheduling itself onto a page nobody is looking at, `inFlight` keeps a slow answer from
  // overlapping the next tick, `timer` is the handle the unmount cancels.
  const stopped = useRef(false);
  const inFlight = useRef(false);
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const last = useRef<ProvisionStatus | null>(null);

  async function pollOnce() {
    if (inFlight.current) return;
    inFlight.current = true;
    try {
      const next = await getStatus();
      last.current = next;
      if (!stopped.current)
        setFeed({
          mqtt: next.mqtt,
          uptimeMs: next.uptime_ms,
          unreachable: null,
          settled: true,
        });
    } catch (err) {
      if (!stopped.current)
        setFeed({
          mqtt: last.current?.mqtt,
          uptimeMs: last.current?.uptime_ms ?? 0,
          unreachable: explainError(err),
          settled: true,
        });
    } finally {
      inFlight.current = false;
    }
  }

  useEffect(() => {
    stopped.current = false;
    async function loop() {
      timer.current = null;
      if (stopped.current) return;
      await pollOnce();
      if (stopped.current) return;
      timer.current = setTimeout(() => void loop(), BROKER_POLL_MS);
    }
    void loop();
    return () => {
      stopped.current = true;
      if (timer.current !== null) {
        clearTimeout(timer.current);
        timer.current = null;
      }
    };
    // Once, on mount.
  }, []);

  return feed;
}
