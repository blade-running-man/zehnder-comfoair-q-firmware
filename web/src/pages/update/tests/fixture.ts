// The first 0x70 bytes of a REAL firmware.bin of this firmware (m5stack-nanoc6, built as
// "cda52b6" before the SemVer versioning): `xxd -p -l 112 .pio/build/m5stack-nanoc6/firmware.bin`.
// A real prefix rather than a hand-built one, so an offset that is wrong in image.ts is caught
// against what the toolchain actually writes. Shared by this directory's suites and the mock's.

const HEX =
  "e9050220a61a8040ee0000000d00000000ffff000000000120000f42ec84" +
  "05003254cdab000000000000000000000000636461353262360000000000" +
  "0000000000000000000000000000000000000000636f6d666f6169722d71" +
  "00000000000000000000000000000000000000000000";

export const REAL_PREFIX: Uint8Array = Uint8Array.from(HEX.match(/../g)!, (h) => parseInt(h, 16));
export const REAL_VERSION = "cda52b6";

/** The real prefix with its version and/or project name rewritten, or byte 0 replaced. */
export function prefix(opts: { version?: string; project?: string; first?: number } = {}): Uint8Array {
  const b = REAL_PREFIX.slice();
  const put = (offset: number, text: string): void => {
    b.fill(0, offset, offset + 32);
    for (let i = 0; i < text.length && i < 32; i++) b[offset + i] = text.charCodeAt(i);
  };
  if (opts.version !== undefined) put(0x30, opts.version);
  if (opts.project !== undefined) put(0x50, opts.project);
  if (opts.first !== undefined) b[0] = opts.first;
  return b;
}
