import { effect, signal } from "@preact/signals";
import { connection } from "../../api/device";
import { getSwVersion } from "../../api/ota";
import styles from "./Watermark.module.css";

// The version the RUNNING firmware reports, never one baked into the SPA. The SPA is embedded in
// the image, so both are the same today -- but a string typed here would be a second copy of the
// version beside the git tag (docs/development/releasing.md), and the device's own answer is
// the one an owner needs when reporting what they run.
//
// Re-read each time the device socket opens, not once per page load: an update reboots the
// device, which drops the socket, and the update page does NOT reload -- so a version read only
// at load would go on naming the old image under the new one.
const version = signal<string | null>(null);

effect(() => {
  if (connection.value !== "open") return;
  getSwVersion().then(
    (v) => (version.value = v),
    () => {}, // the next socket open asks again; a stale answer beats a blank corner
  );
});

export function Watermark() {
  if (version.value === null) return null;
  return (
    <div class={styles.watermark} aria-hidden="true">
      v{version.value}
    </div>
  );
}
