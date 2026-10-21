// web/mock/wsframe.ts
// The minimum of RFC 6455 the mock needs, hand-rolled so the mock adds no `ws` dependency: the
// handshake's accept key, unmasked server-to-client text frames, and enough of the client's
// frames to answer a close. The channel is push-only, so data frames from the client are dropped
// unread -- as the firmware's comfoair_http_ws_get() drains and drops them.
import { createHash } from "node:crypto";

const WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

export function acceptKey(clientKey: string): string {
  return createHash("sha1").update(clientKey + WS_GUID).digest("base64");
}

/** One FIN text frame. Servers must not mask (RFC 6455 5.1). */
export function encodeText(text: string): Buffer {
  const payload = Buffer.from(text, "utf8");
  const len = payload.length;
  let header: Buffer;
  if (len < 126) {
    header = Buffer.from([0x81, len]);
  } else if (len < 65536) {
    header = Buffer.alloc(4);
    header[0] = 0x81; header[1] = 126; header.writeUInt16BE(len, 2);
  } else {
    header = Buffer.alloc(10);
    header[0] = 0x81; header[1] = 127; header.writeBigUInt64BE(BigInt(len), 2);
  }
  return Buffer.concat([header, payload]);
}

/** A close frame with no status, the polite answer to the client's own close. */
export function closeFrame(): Buffer {
  return Buffer.from([0x88, 0x00]);
}

/** True when a chunk from the client starts a close frame (opcode 0x8). */
export function isClose(chunk: Buffer): boolean {
  return chunk.length > 0 && (chunk[0] & 0x0f) === 0x8;
}
