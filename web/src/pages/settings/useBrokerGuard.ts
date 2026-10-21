// The one stop between the owner and a deletion in somebody else's Home Assistant, wired up.
//
// A HOOK THAT DECIDES NOTHING, which is the change of 2026-09-07. It used to hold the state machine
// -- what a press does, what an edit does, when a standing confirmation stops counting -- and a
// hook is not something the suite in tests/ can call, so none of that was under test: three
// separate mutations of it left every case green. All of it moved to broker.ts as brokerGate(),
// where it is pure and pinned. What is left here is one piece of component state, the two impure
// inputs (the clock, and a fresh read of /api/status) and the wiring.

import { useRef, useState } from "preact/hooks";

import { ENTITIES } from "../../api/entities.ts";
import {
  brokerConfirming,
  brokerGate,
  brokerLinkFrom,
  describeBrokerSave,
  type BrokerNotice,
} from "./broker.ts";

/** What GET /api/status carries that this guard reads. */
export interface BrokerStatus {
  mqtt?: { connected?: boolean } | null;
  device_id?: string;
}

export interface BrokerGuard {
  /** What to show above the Save button, or null when this save changes no broker address. */
  notice: BrokerNotice | null;
  /** True while the warning has been shown and the owner has not pressed again. */
  confirming: boolean;
  /**
   * Call at the top of Save, with a status read JUST NOW -- null when that read failed, which is
   * treated as "unknown" and therefore as the branch that confirms. True means the press was
   * swallowed and the warning is now on screen; the caller must return without submitting.
   */
  press: (fresh: BrokerStatus | null) => boolean;
  /** Any edit to either form retires a pending confirmation. */
  edit: () => void;
  /** The Cancel button. */
  cancel: () => void;
  /** A save went through; the confirmation it was given for is spent. */
  saved: () => void;
}

export function useBrokerGuard(
  loaded: { mqtt_host: string; mqtt_port: number; topic_prefix: string } | null,
  form: { mqtt_host: string; mqtt_port: string; topic_prefix: string } | null,
  status: BrokerStatus | null,
  /** How long ago `status` was read. Infinity when nothing has been read yet. */
  statusAgeMs: number,
): BrokerGuard {
  const [confirming, setConfirming] = useState(false);
  // A MIRROR IN A REF, because press() resumes after an await. The state variable it can see is the
  // one from the render the click came from, and in the round trip that press pays for the owner
  // can retire the confirmation -- an edit to either form, or Cancel. Reading the render's copy
  // would let a press submit on an authority that was withdrawn while it was in flight. The ref is
  // written by the only two places that ever change this.
  const confirmingRef = useRef(false);
  const remember = (next: boolean) => {
    confirmingRef.current = next;
    setConfirming(next);
  };

  // ENTITIES.length IS NAMED HERE AND NOWHERE ELSE. describeBrokerSave() takes the count as an
  // argument precisely so that a test can vary it; this is the one line that says which list it is
  // counting, and it is the line a reviewer has to read. DO NOT write a number here -- it would be
  // the second entity list AGENTS.md forbids, and no case in tests/ can tell a literal from
  // ENTITIES.length while the registry happens to hold that many.
  //
  // `loaded` is the identity the device recorded when it built the client
  // (comfoair_mqtt_client.c), which is exactly what comfoair_mqtt_pending_cleanup() compares the
  // new settings against.
  const describe = (against: BrokerStatus | null, ageMs: number) =>
    loaded === null || form === null
      ? null
      : describeBrokerSave(
          loaded,
          form,
          brokerLinkFrom(against, ageMs),
          ENTITIES.length,
          against?.device_id ?? status?.device_id ?? "",
        );

  const notice = describe(status, statusAgeMs);

  const apply = (action: "edit" | "cancel" | "saved") => {
    remember(brokerGate(confirmingRef.current, notice, action).confirming);
  };

  return {
    notice,
    confirming: brokerConfirming(confirming, notice),
    // AGAINST THE FRESH READING AND NOT AGAINST `notice`. The notice is built from whatever the
    // last poll left behind, and that can be a minute old -- see brokerLinkFrom()'s note. The press
    // is the moment the question actually matters, so the caller reads /api/status again first and
    // hands the answer in; a read that failed arrives here as null, which is "unknown", which
    // confirms.
    press: (fresh) => {
      const next = brokerGate(confirmingRef.current, describe(fresh, 0), "press");
      remember(next.confirming);
      return !next.submit;
    },
    edit: () => apply("edit"),
    cancel: () => apply("cancel"),
    saved: () => apply("saved"),
  };
}
