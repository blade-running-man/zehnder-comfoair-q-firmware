// `npm test`: every web suite, one node process apiece.
//
// The suites are plain .ts files run by node's own type stripping (node 24, no flag), and
// src/pages/settings/tests/harness.ts is all they need; this file only finds them and adds up
// what they print. Every `*.test.ts` under src/ is a suite, so a new one is run without being
// listed anywhere -- a list here would be a second place to forget it.
//
// `npm run build` type-checks the suites and executes none of them, so the web gate is BOTH
// commands. DO NOT fold this into the build: the firmware build runs `npm run build` on every
// board build, and a red web suite would then look like a broken firmware build.

import { readdirSync, statSync } from "node:fs";
import { join } from "node:path";
import { spawnSync } from "node:child_process";

function suites(dir) {
  const found = [];
  for (const name of readdirSync(dir).sort()) {
    const path = join(dir, name);
    if (statSync(path).isDirectory()) found.push(...suites(path));
    else if (name.endsWith(".test.ts")) found.push(path);
  }
  return found;
}

let failedSuites = 0;
let checks = 0;
let passed = 0;
const all = suites("src");
for (const file of all) {
  const run = spawnSync(process.execPath, [file], { encoding: "utf8" });
  process.stdout.write(run.stdout);
  process.stderr.write(run.stderr);
  // Each suite ends with harness.ts's `<suite>: N/M passed`; a suite that died before printing
  // it counts as failed with no checks, which is what a crash is.
  const tally = /: (\d+)\/(\d+) passed\s*$/.exec(run.stdout);
  if (tally) {
    passed += Number(tally[1]);
    checks += Number(tally[2]);
  }
  // THE TALLY IS CHECKED, not only the exit status. report() throws on a failed check, so the
  // two normally agree -- but a suite that swallows its own failure, or prints the tally and
  // exits 0 some other way, would otherwise be counted as a pass while saying it failed. The
  // status alone is the one thing a broken suite can most easily get right.
  const shortfall = tally !== null && tally[1] !== tally[2];
  if (run.status !== 0 || !tally || shortfall) {
    failedSuites++;
    const said = tally ? `, ${tally[1]}/${tally[2]} passed` : ", no tally";
    console.log(`FAILED: ${file} (exit ${run.status}${said})`);
  }
}
console.log(`web suites: ${all.length - failedSuites}/${all.length} suites, ${passed}/${checks} checks passed`);
process.exit(failedSuites > 0 ? 1 : 0);
