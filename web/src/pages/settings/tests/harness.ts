// A test harness of forty lines, because there is nowhere else to put these tests.
//
// The project's suite runs with `pio test -e native`, which builds C and Unity; it cannot
// reach TypeScript. The obvious answer -- vitest -- means editing web/package.json and
// pulling a dependency tree, and this page's task is confined to web/src. So the tests are
// plain .ts files run by node's own type stripping (node 24 does this with no flag), and
// this file is the only thing they need that a test runner would have given them.
//
// Two constraints shape it, and both are the tsconfig's, not taste:
//
//   * NOTHING is imported. `types: ["vite/client"]` (tsconfig.app.json) leaves @types/node
//     out of the program, so `import ... from "node:test"` does not typecheck even though it
//     runs. `console` and `throw` are in lib.dom and need no types at all.
//   * Relative imports in a test carry the `.ts` extension, which is why
//     `allowImportingTsExtensions` is on. Node resolves the specifier literally; Vite is
//     happy either way. The modules under test therefore import only TYPES from anything
//     that reaches CSS or the DOM -- a type-only import is erased before node sees it.
//
// DO NOT make this file assert deep equality by reference-walking. JSON.stringify is enough
// for the plain data these tests carry and its failure message is readable, which matters
// more here than covering cases this suite does not have.
//
// The suites are run by `npm test` (scripts/test.mjs), which finds every `*.test.ts` under src/
// and runs each in its own node process, so a new suite is run without being listed anywhere.
// The runner reads the `N/M passed` line report() prints as well as the exit status: DO NOT
// change that line's shape without changing the runner's regex with it.
//
// Nothing invokes it automatically: the firmware build runs `npm run build`
// (components/web_assets/CMakeLists.txt, twice -- once at configure time, once as the
// `web_spa` target rerun on every `pio run`), which typechecks these files and executes none
// of them, so running `npm test` before a commit is still a human's job.

let checks = 0;
let failures = 0;

// Template-interpolated rather than returned straight from JSON.stringify: that returns
// undefined for an undefined input, and "undefined" printed is more use in a failure message
// than an empty one.
function show(v: unknown): string {
  return `${JSON.stringify(v)}`;
}

/** Passes when `actual` and `expected` serialise the same. `why` is the claim being pinned. */
export function eq(actual: unknown, expected: unknown, why: string): void {
  checks++;
  if (JSON.stringify(actual) === JSON.stringify(expected)) return;
  failures++;
  console.log(`  FAIL ${why}\n       expected ${show(expected)}\n       actual   ${show(actual)}`);
}

export function ok(cond: boolean, why: string): void {
  checks++;
  if (cond) return;
  failures++;
  console.log(`  FAIL ${why}`);
}

/**
 * Prints the tally and THROWS when anything failed.
 *
 * The throw is the exit code: an uncaught rejection makes node exit non-zero, which is what
 * makes `node a.test.ts && node b.test.ts` stop at the first broken suite. Returning a
 * boolean would leave a red suite reporting success to a shell.
 */
export function report(suite: string): void {
  console.log(`${suite}: ${checks - failures}/${checks} passed`);
  if (failures > 0) throw new Error(`${suite}: ${failures} failed`);
}
