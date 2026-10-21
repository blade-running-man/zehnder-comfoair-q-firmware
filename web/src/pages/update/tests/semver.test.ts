// SemVer 2.0.0 precedence and the Update page's verdict line.
//
// Run: node src/pages/update/tests/semver.test.ts
import { eq, ok, report } from "../../settings/tests/harness.ts";
import { compareSemVer, parseSemVer, verdict } from "../semver.ts";

function cmp(a: string, b: string): number {
  const pa = parseSemVer(a);
  const pb = parseSemVer(b);
  if (pa === null || pb === null) throw new Error(`not SemVer: ${a} / ${b}`);
  return compareSemVer(pa, pb);
}

// --- parsing --------------------------------------------------------------------------------
for (const v of ["0.1.0", "0.1.1-dev.3+gabc1234", "0.1.1-dev.3+gabc1234.dirty", "0.0.0+unknown", "0.2.0-rc.1",
  "1.10.10-dev.999+g1234567.dirty", "10.20.30", "1.0.0-0A.is.legal"]) {
  ok(parseSemVer(v) !== null, `${v} is SemVer`);
}
for (const v of ["cda52b6", "", "1.0", "01.0.0", "1.0.0-01", "1.0.0-", "1.0.0+", "v1.0.0", "1.0.0-a..b", " 1.0.0"]) {
  eq(parseSemVer(v), null, `${JSON.stringify(v)} is not SemVer`);
}
eq(parseSemVer("0.1.1-dev.3+gabc1234"), { core: ["0", "1", "1"], pre: ["dev", "3"] }, "build metadata is dropped");

// --- the release ordering (docs/development/releasing.md), every adjacent pair and both ways --
const ORDER = ["0.1.0", "0.1.1-dev.3", "0.1.1", "0.2.0-rc.1", "0.2.0"];
for (let i = 0; i < ORDER.length; i++) {
  for (let j = 0; j < ORDER.length; j++) {
    eq(cmp(ORDER[i]!, ORDER[j]!), Math.sign(i - j), `${ORDER[i]} vs ${ORDER[j]}`);
  }
}

// --- semver.org §11's own example chain ------------------------------------------------------
const SPEC = ["1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2",
  "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0"];
for (let i = 1; i < SPEC.length; i++) eq(cmp(SPEC[i - 1]!, SPEC[i]!), -1, `${SPEC[i - 1]} < ${SPEC[i]}`);

// --- the rules one at a time ----------------------------------------------------------------
eq(cmp("0.1.1-dev.3+gabc1234", "0.1.1-dev.3+gdef5678.dirty"), 0, "build metadata does not order");
eq(cmp("0.1.1-dev.3+gabc1234", "0.1.1-dev.3"), 0, "and its absence neither");
eq(cmp("0.1.1-dev.10", "0.1.1-dev.9"), 1, "numeric identifiers numerically: dev.10 > dev.9");
eq(cmp("1.0.0-1", "1.0.0-alpha"), -1, "a numeric identifier is lower than an alphanumeric one");
eq(cmp("1.0.0-alpha", "1.0.0-1"), 1, "and the other way round");
eq(cmp("1.0.0-Beta", "1.0.0-alpha"), -1, "alphanumeric identifiers in ASCII order (B < a)");
eq(cmp("1.0.0-alpha", "1.0.0-alpha.0"), -1, "the longer pre-release set is higher when the rest is equal");
eq(cmp("1.0.0-1a", "1.0.0-2"), 1, "1a is alphanumeric, not the number 1");
eq(cmp("1.10.0", "1.9.0"), 1, "core numerically, not as text");
eq(cmp("2.0.0", "1.99.99"), 1, "major outranks the rest");
eq(cmp("12345678901234567890.0.0", "12345678901234567891.0.0"), -1, "past 2^53 still ordered");

// --- the verdict --------------------------------------------------------------------------------
eq(verdict("0.1.0", "0.1.1-dev.3+gabc1234"), "newer", "a dev build after the release is newer");
eq(verdict("0.1.1", "0.1.1-dev.3+gabc1234"), "older", "the dev build before a release is older");
eq(verdict("0.1.1-dev.3+gabc1234", "0.1.1-dev.3+gabc1234.dirty"), "same", "dirty is the same version (reinstall)");
eq(verdict("0.1.0", "0.1.0"), "same", "the same release is a reinstall");
eq(verdict("0.2.0", "0.1.0"), "older", "a downgrade is called out");
eq(verdict("cda52b6", "0.1.0"), null, "a bare git hash installed: no verdict");
eq(verdict("0.1.0", "cda52b6"), null, "a bare git hash in the file: no verdict");
eq(verdict(null, "0.1.0"), null, "an unread running version: no verdict");
eq(verdict("0.1.0", null), null, "a file with no version: no verdict");

report("pages/update/semver");
