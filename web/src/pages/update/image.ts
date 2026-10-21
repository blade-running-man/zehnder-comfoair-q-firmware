// The app descriptor inside a picked firmware.bin: which firmware the file is, and which version.
// Pure, no imports, so the page's pre-flight, its suite and the mock's POST /api/ota all read it
// the same way.
//
// esp_app_desc_t sits right after the image header (24 B) and the first segment header (8 B), so
// at a fixed offset in every app image this toolchain builds -- the same offsets
// comfoair_ota_check_app_desc() reads (components/comfoair_ota/include/comfoair_ota.h), verified
// on this firmware's own firmware.bin.

export const APP_DESC_OFFSET = 0x20;
/** ESP_APP_DESC_MAGIC_WORD, little-endian on the flash like every word of the image. */
export const APP_DESC_MAGIC = 0xabcd5432;
export const VERSION_OFFSET = 0x30;
export const PROJECT_OFFSET = 0x50;
/** version[32] and project_name[32]: NUL-terminated when shorter. */
export const FIELD_BYTES = 32;
/** COMFOAIR_OTA_PREFLIGHT_BYTES: through the end of project_name. */
export const PREFLIGHT_BYTES = 0x70;
/** esp_app_get_description()->project_name of this firmware. The device refuses any other name:
 *  its owner runs two ESP32-C6 firmwares (this one and an OpenTherm thermostat), and the other
 *  one's image passes the chip check. */
export const PROJECT_NAME = "comfoair-q";

export interface AppDesc {
  version: string;
  project: string;
}

/** A char[32] field up to its NUL, or all 32 bytes when it has none. Byte for byte (Latin-1):
 *  the toolchain writes ASCII here, and an odd byte must not throw on the way to a refusal. */
function field(head: Uint8Array, offset: number): string {
  let out = "";
  for (let i = offset; i < offset + FIELD_BYTES; i++) {
    const b = head[i] ?? 0;
    if (b === 0) break;
    out += String.fromCharCode(b);
  }
  return out;
}

/** The descriptor, or null when the prefix is too short or the magic is not there (a foreign or a
 *  pre-descriptor image, or not an app image at all). */
export function parseAppDesc(head: Uint8Array): AppDesc | null {
  if (head.length < PREFLIGHT_BYTES) return null;
  const magic = new DataView(head.buffer, head.byteOffset, head.byteLength).getUint32(APP_DESC_OFFSET, true);
  if (magic !== APP_DESC_MAGIC) return null;
  return { version: field(head, VERSION_OFFSET), project: field(head, PROJECT_OFFSET) };
}

/** What the device requires before it erases anything: the magic, and this firmware's name. */
export function isThisFirmware(desc: AppDesc | null): desc is AppDesc {
  return desc !== null && desc.project === PROJECT_NAME;
}
