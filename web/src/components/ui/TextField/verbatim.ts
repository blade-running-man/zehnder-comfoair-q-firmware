/**
 * Input attributes for a value the machine reads and the owner only relays.
 *
 * A Wi-Fi key, an SSID, a hostname, an MQTT topic prefix: none of them is prose, and every
 * helpful thing a mobile keyboard does to prose corrupts them silently. The one that costs
 * the most is capitalisation of the first character of a password -- the device answers
 * "authentication failed", which is exactly what it would say if the owner had mistyped, and
 * nothing on either side can show that the typed string and the sent string differ.
 *
 * Kept in a .ts file of its own, not inline in TextField.tsx, so that the values can be
 * pinned by a test: the suite is plain TypeScript run by node, and node's type stripping does
 * not handle JSX (see pages/settings/tests/harness.ts).
 *
 * DO NOT add `autocomplete` here. It is a per-field decision -- "new-password" on the field
 * that sets the web-interface password, nothing at all on the Wi-Fi key so that a password
 * manager may still offer the household one -- and a blanket value would take that decision
 * away from every field at once.
 */
export const VERBATIM_INPUT = {
  // "none", not the legacy "off": Safari maps "off" onto "sentences" and capitalises anyway.
  autocapitalize: "none",
  // Non-standard, and the platform that honours it is the platform that autocorrects.
  autocorrect: "off",
  // The boolean, not the string. Preact assigns the DOM property here, and the string "false"
  // is truthy -- it would turn spell checking ON.
  spellcheck: false,
} as const;
