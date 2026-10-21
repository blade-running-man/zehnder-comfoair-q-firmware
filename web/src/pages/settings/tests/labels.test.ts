// Every field label on the settings page, in every language, gives a distinct element id.
//
// Run: node src/pages/settings/tests/labels.test.ts
//
// TextField and Select derive their element id from the label (label.toLowerCase() with
// whitespace turned into "-"), so the label text is load-bearing: two labels that slug to the
// same id make clicking either <label> focus the first input. The English labels were spelled
// out in full for exactly that reason ("Broker password", "Web interface password", "Wi-Fi
// password"); a translation that shortened two of them to the same word would bring the
// collision back in that one language, with nothing on screen to show it. This pins it for
// every catalog. The list below is every label passed to a TextField, SecretField or Select
// under pages/settings -- a new field's label key must be added here.

import { catalogs, LOCALES } from "../../../i18n/catalogs/index.ts";
import type { MsgKey } from "../../../i18n/catalogs/en.ts";
import { eq, ok, report } from "./harness.ts";

const LABEL_KEYS: readonly MsgKey[] = [
  "settings.broker.host.label",
  "settings.broker.port.label",
  "settings.broker.username.label",
  "settings.broker.password.label",
  "settings.broker.topicPrefix.label",
  "settings.device.name.label",
  "settings.device.password.label",
  "settings.wifi.network.label",
  "settings.wifi.network.ssidLabel",
  "settings.wifi.password.label",
];

// The same expression as components/ui/TextField and components/ui/Select.
const slug = (label: string) => label.toLowerCase().replace(/\s+/g, "-");

for (const { code } of LOCALES) {
  const ids = LABEL_KEYS.map((key) => {
    const msg = catalogs[code][key];
    ok(typeof msg === "string", `${code}: ${key} is a plain string, as a label must be`);
    return slug(typeof msg === "string" ? msg : "");
  });
  eq(new Set(ids).size, ids.length, `${code}: every settings field label gives a distinct id`);
}

report("settings/labels");
