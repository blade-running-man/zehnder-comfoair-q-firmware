// POST /api/ota and the version read around it (docs/device/ota-and-system.md,
// docs/interfaces/rest-api.md).
//
// Same terms as every other call in client.ts: no header, cookie or route curl cannot use --
// `curl --data-binary @firmware.bin -H 'Content-Type: application/octet-stream' .../api/ota` is
// the same request this file makes.

import { ApiError, send } from "./client.ts";

/** The 200 body: the slot the image went into, how many bytes were written and -- from firmware
 *  that reads the app descriptor -- the version the image carries. The device reboots ~1 s after
 *  sending it. `version` is optional because an older firmware's 200 does not have it. */
export interface OtaAccepted {
  ok: true;
  rebooting: boolean;
  partition: string;
  bytes: number;
  version?: string;
}

/** No answer at all -- the connection dropped or was refused. `allSent` says whether every byte
 *  had gone first: then the device may well have installed the image and be rebooting. */
export class OtaLost extends Error {
  readonly allSent: boolean;

  constructor(allSent: boolean) {
    super("The connection to the device was lost.");
    this.allSent = allSent;
  }
}

/**
 * Uploads the raw image. Rejects with ApiError (status + the device's `error` sentence) for a
 * refusal, with OtaLost when nothing answered.
 *
 * XMLHttpRequest, not fetch: fetch has no upload progress, and a 1.3 MB upload over the device's
 * Wi-Fi takes 10-30 s with nothing on screen otherwise. The body is the File itself, so the
 * browser streams it and sets Content-Length -- which the device requires (no length -> 411) to
 * check the slot fit before it erases anything. DO NOT wrap it in FormData: the device reads the
 * body as the image byte for byte, and a multipart envelope would be refused as "not an image".
 */
export function uploadFirmware(
  file: Blob,
  onProgress: (sent: number, total: number) => void,
): Promise<OtaAccepted> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    let sent = 0;
    xhr.open("POST", "/api/ota");
    xhr.setRequestHeader("Content-Type", "application/octet-stream");
    xhr.upload.onprogress = (e) => {
      sent = e.loaded;
      onProgress(e.loaded, e.lengthComputable ? e.total : file.size);
    };
    xhr.onload = () => {
      let body: unknown = null;
      try {
        body = JSON.parse(xhr.responseText);
      } catch {
        // Not JSON; the status line is all there is.
      }
      if (xhr.status === 200) {
        resolve(body as OtaAccepted);
        return;
      }
      const error = (body as { error?: unknown } | null)?.error;
      reject(new ApiError(xhr.status, typeof error === "string" ? error : xhr.statusText));
    };
    // A refused connection and a dropped one look the same here (status 0, no body).
    xhr.onerror = () => reject(new OtaLost(sent >= file.size));
    xhr.onabort = () => reject(new OtaLost(false));
    xhr.send(file);
  });
}

// Per poll: a rebooting device does not refuse the connection, it simply is not there, and a
// browser waits a long time for a SYN nobody answers. Three seconds keeps the 2 s poll clock honest.
const STATUS_TIMEOUT_MS = 3000;

/** sw_version from GET /api/status: the app version the running firmware was built as. Rejects
 *  on any failure, including a body without the field. */
export async function getSwVersion(): Promise<string> {
  const ctl = new AbortController();
  const timer = setTimeout(() => ctl.abort(), STATUS_TIMEOUT_MS);
  try {
    const response = await send("/api/status", { cache: "no-store", signal: ctl.signal });
    const doc = (await response.json()) as { sw_version?: unknown };
    if (typeof doc.sw_version !== "string") throw new ApiError(response.status, "no sw_version");
    return doc.sw_version;
  } finally {
    clearTimeout(timer);
  }
}
