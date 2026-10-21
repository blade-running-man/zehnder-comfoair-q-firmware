// What the log page decides, with no DOM anywhere near it.
//
// Run: node src/pages/log/tests/log.test.ts
import { eq, ok, report } from "../../settings/tests/harness.ts";
import { describeEmpty, levelOf } from "../log.ts";

// ESP-IDF's own format: one letter, then a space, then a timestamp in brackets.
eq(levelOf("E (1234) net: no credentials"), "error", "E is an error");
eq(levelOf("W (1234) mqtt: broker unreachable"), "warn", "W is a warning");
eq(levelOf("I (1234) comfoair: board m5stack"), "info", "I is info");
eq(levelOf("D (1234) http: PDO 16 unclaimed"), "debug", "D is debug");

// Anything this page cannot classify is INFO, never dropped. A line nobody can see is worse
// than a line shown at the wrong weight, and the ring is the only view of the device there is.
eq(levelOf("something without a prefix"), "info", "an unrecognised line is still shown");
eq(levelOf(""), "info", "an empty line is still a line");
eq(levelOf("Erroneous"), "info", "a word starting with E is not a level");

// The two empty states are different and a person needs to be told which one they are in.
eq(
  describeEmpty(true),
  "The device has not logged anything since it started.",
  "an empty ring is not a failure",
);
eq(
  describeEmpty(false),
  "The log could not be read. The device may have restarted just now.",
  "a failed fetch says so",
);

ok(levelOf("W (1) x") !== levelOf("I (1) x"), "the levels are distinguishable");

// report() takes the suite name -- it is what prints the "log: 10/10 passed" line and sets the
// exit code, so npm test can chain the suites with &&.
report("log");
