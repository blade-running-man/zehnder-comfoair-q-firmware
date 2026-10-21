// SemVer 2.0.0 precedence (semver.org §11), for the Update page's "newer / same / older" line.
// Pure, no imports.
//
// The firmware's version is a SemVer string derived from the git tag (0.1.0,
// 0.1.1-dev.3+gabc1234, ...), but older builds report a bare git hash ("cda52b6"). Anything that
// is not strict SemVer gets NO verdict: guessing an order between a hash and a version would warn
// about a downgrade that is not one, or stay silent about one that is.

/** semver.org's own regular expression: no leading zeros in numeric identifiers, no empty ones. */
const SEMVER =
  /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-((?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?(?:\+[0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*)?$/;

export interface SemVer {
  /** MAJOR, MINOR, PATCH as their digit strings: compared by length then lexically, which is
   *  numeric order for any size (no leading zeros), with no Number overflow at 2^53. */
  core: [string, string, string];
  /** The pre-release identifiers; empty for a release. Build metadata is dropped -- it has no
   *  bearing on precedence (§10). */
  pre: string[];
}

export function parseSemVer(text: string): SemVer | null {
  const m = SEMVER.exec(text);
  if (!m) return null;
  return { core: [m[1]!, m[2]!, m[3]!], pre: m[4] === undefined ? [] : m[4].split(".") };
}

const sign = (n: number): -1 | 0 | 1 => (n < 0 ? -1 : n > 0 ? 1 : 0);

function compareDigits(a: string, b: string): -1 | 0 | 1 {
  if (a.length !== b.length) return sign(a.length - b.length);
  return a < b ? -1 : a > b ? 1 : 0;
}

const NUMERIC = /^\d+$/;

/** §11.4: numeric identifiers numerically, alphanumeric ones in ASCII order, and a numeric one is
 *  always lower than an alphanumeric one. */
function compareIdentifier(a: string, b: string): -1 | 0 | 1 {
  const an = NUMERIC.test(a);
  const bn = NUMERIC.test(b);
  if (an && bn) return compareDigits(a, b);
  if (an) return -1;
  if (bn) return 1;
  return a < b ? -1 : a > b ? 1 : 0;
}

export function compareSemVer(a: SemVer, b: SemVer): -1 | 0 | 1 {
  for (let i = 0; i < 3; i++) {
    const c = compareDigits(a.core[i]!, b.core[i]!);
    if (c !== 0) return c;
  }
  // §11.3: a pre-release is lower than the release of the same core (0.1.1-dev.3 < 0.1.1).
  if (a.pre.length === 0 || b.pre.length === 0) return sign(b.pre.length - a.pre.length);
  for (let i = 0; i < Math.min(a.pre.length, b.pre.length); i++) {
    const c = compareIdentifier(a.pre[i]!, b.pre[i]!);
    if (c !== 0) return c;
  }
  // §11.4.4: all shared identifiers equal -- the longer set is higher.
  return sign(a.pre.length - b.pre.length);
}

/** What the picked file is relative to the running firmware. */
export type Verdict = "newer" | "same" | "older";

/** null when either side is unknown or not SemVer: then the page shows both strings and no claim. */
export function verdict(installed: string | null, image: string | null): Verdict | null {
  const a = installed === null ? null : parseSemVer(installed);
  const b = image === null ? null : parseSemVer(image);
  if (a === null || b === null) return null;
  const c = compareSemVer(b, a);
  return c > 0 ? "newer" : c < 0 ? "older" : "same";
}
