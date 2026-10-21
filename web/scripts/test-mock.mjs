// `npm run test:mock`: the dev mock's own suites (web/mock/tests), one node process apiece.
//
// SEPARATE FROM `npm test` ON PURPOSE. scripts/test.mjs is the web gate the firmware work leans on
// and it scans src/ only; the mock is a developer convenience, and a red mock suite must never read
// as a broken SPA -- nor may a slow one stall that gate. Same tally contract as scripts/test.mjs:
// every suite ends with harness.ts's "<suite>: N/M passed", and the TALLY is checked, not only the
// exit status, so a suite that swallows its own failure is still counted as failed.
import { readdirSync, statSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..", "mock", "tests");

function suites(dir) {
  const found = [];
  for (const name of readdirSync(dir).sort()) {
    const path = join(dir, name);
    if (statSync(path).isDirectory()) found.push(...suites(path));
    else if (name.endsWith(".test.ts")) found.push(path);
  }
  return found;
}

const all = suites(root);
let failedSuites = 0;
let checks = 0;
let passed = 0;
for (const file of all) {
  const run = spawnSync(process.execPath, [file], { encoding: "utf8" });
  process.stdout.write(run.stdout);
  process.stderr.write(run.stderr);
  const tally = /: (\d+)\/(\d+) passed\s*$/.exec(run.stdout);
  if (tally) {
    passed += Number(tally[1]);
    checks += Number(tally[2]);
  }
  const shortfall = tally !== null && tally[1] !== tally[2];
  if (run.status !== 0 || !tally || shortfall) {
    failedSuites++;
    const said = tally ? `, ${tally[1]}/${tally[2]} passed` : ", no tally";
    console.log(`FAILED: ${file} (exit ${run.status}${said})`);
  }
}
// An empty directory is a failure, not a pass: "0/0" would hide a deleted or misplaced tests dir.
if (all.length === 0) failedSuites++;
console.log(`mock suites: ${all.length - Math.min(failedSuites, all.length)}/${all.length} suites, ${passed}/${checks} checks passed`);
process.exit(failedSuites > 0 ? 1 : 0);
