// Log page (pages/log): its heading and log.ts's two empty states (log.test.ts pins both).
// Prefix every key "log."; every key here must exist in ../de, ../nl, ../uk.
import type { Message } from "../../format.ts";

export const log = {
  "log.title": "Log",
  "log.empty": "The device has not logged anything since it started.",
  "log.unreadable": "The log could not be read. The device may have restarted just now.",
} satisfies Record<string, Message>;
