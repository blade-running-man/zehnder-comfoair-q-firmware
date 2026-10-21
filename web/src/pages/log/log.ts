// What the log page decides. Pure, so tests/log.test.ts can reach all of it.
//
// The device's whole log ring is served by GET /api/log, and on the setup access point that is
// readable by anyone in radio range. This page therefore shows what the device said and does not
// filter, summarise or reorder it: the ring is already the only view there is without a USB cable.

import { t } from "../../i18n/index.ts";

export type LogLevel = "error" | "warn" | "info" | "debug";

/**
 * The level of one line, from ESP-IDF's own format: one letter, a space, then `(timestamp)`.
 * `LOG_FORMAT` (components/log/include/esp_log_format.h:15 in the pinned IDF) renders it as
 * `letter " (%u) %s: " ...`, and comfoair_log.c's log_hook (~lines 105-117) vsnprintf's that
 * exact rendered line into the ring, unmodified, so what ships to /api/log is what the macro
 * produced.
 *
 * The regex holds only because sdkconfig.defaults leaves two IDF defaults alone:
 * CONFIG_LOG_COLORS is "n" (Kconfig.format), so no ANSI escape ever precedes the letter --
 * DO NOT enable it, or every line starts `\x1b[0;3Xm` and this regex stops matching anything.
 * CONFIG_LOG_TIMESTAMP_SOURCE defaults to RTOS ticks, i.e. decimal milliseconds since boot --
 * DO NOT switch it to a System Time source, whose `HH:MM:SS.sss` colons `\(\d+\)` cannot match.
 * Either change degrades every line to "info" with nothing failing to say so.
 *
 * ANYTHING UNRECOGNISED IS INFO, never dropped. A line nobody can see is worse than a line shown
 * at the wrong weight -- and "Erroneous" starting with E is exactly the accident a bare
 * first-character test would make.
 */
export function levelOf(line: string): LogLevel {
  if (!/^[EWID] \(\d+\)/.test(line)) return "info";
  switch (line[0]) {
    case "E":
      return "error";
    case "W":
      return "warn";
    case "D":
      return "debug";
    default:
      return "info";
  }
}

/**
 * The two empty states, which are different and must not look alike.
 *
 * `read` is whether the request succeeded. An empty ring on a device that has just booted is
 * normal; a failed read is a device that may have restarted mid-request, and telling the owner
 * "nothing logged" there would be a lie about the one screen they came to for answers.
 */
export function describeEmpty(read: boolean): string {
  return t(read ? "log.empty" : "log.unreadable");
}
