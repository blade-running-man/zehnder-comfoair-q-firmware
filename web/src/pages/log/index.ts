// Explicit extension, not "./Log": this directory also has "log.ts" (the pure logic module,
// tests/log.test.ts imports it as such), and on a case-insensitive filesystem (the default on
// macOS) TypeScript's bundler resolution tries "./Log" + ".ts" before ".tsx" and silently
// matches the wrong file -- `tsc -b` then reports both a missing LogPage export and a
// same-file-different-casing conflict (TS2305 / TS1261). Spelling out ".tsx" pins the exact
// on-disk name so resolution cannot walk into "log.ts" instead.
export { LogPage } from "./Log.tsx";
