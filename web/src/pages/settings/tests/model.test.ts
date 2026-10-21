// The settings document, on the way in and on the way back out.
//
// Run: node src/pages/settings/tests/model.test.ts
//
// Every case here is the client half of the write-only secret scheme
// (components/comfoair_secrets/include/comfoair_secrets.h). The scheme only works if the
// page submits the sentinel for a field nobody touched, so that is what most of this file
// pins -- the failure it prevents is one save of an unrelated setting wiping the broker
// credentials, which is the failure the sentinel was invented for
// (comfoair_secrets.h:10-12, api/secrets.ts:5-9).

import { CONFIG_UNCHANGED } from "../../../api/secrets.ts";
import type { DeviceConfig } from "../../../api/client.ts";
import { describeSecret, formFromConfig, patchFromForm } from "../model.ts";
import { eq, ok, report } from "./harness.ts";

// What GET /api/config answers on a device that has been set up: both secrets redacted to
// the sentinel, all three derived flags present.
const CONFIGURED: DeviceConfig = {
  wifi_ssid: "Kitchen 2G",
  wifi_psk: CONFIG_UNCHANGED,
  mqtt_host: "192.168.50.10",
  mqtt_port: 1883,
  mqtt_user: "comfoair",
  mqtt_password: CONFIG_UNCHANGED,
  topic_prefix: "comfoair/a4c1385f2b90",
  ha_discovery: true,
  device_name: "ComfoAir Q",
  ui_password: CONFIG_UNCHANGED,
  ui_password_set: true,
  read_only: false,
  // A pair that has reached this network at least once, which is what makes the provisioning
  // rollback real -- see api/client.ts, DeviceConfig.wifi_known_good.
  wifi_known_good: true,
  permanent_manual: true,
};

// And on a device out of the box: every secret "" -- which is NOT the sentinel, and the
// difference is the entire point (comfoair_secrets.h:36-38, comfoair_secret_redact).
const FRESH: DeviceConfig = {
  ...CONFIGURED,
  wifi_ssid: "",
  wifi_psk: "",
  mqtt_host: "",
  mqtt_user: "",
  mqtt_password: "",
  ui_password: "",
  ui_password_set: false,
  wifi_known_good: false,
};

// --- loading -------------------------------------------------------------------------------

{
  const form = formFromConfig(CONFIGURED);
  eq(form.mqtt_password, CONFIG_UNCHANGED,
     "a stored broker password loads as the sentinel, so an untouched field round-trips");
  eq(form.ui_password, CONFIG_UNCHANGED,
     "a stored UI password loads as the sentinel");
  eq(form.mqtt_port, "1883",
     "the port loads as text: it is bound to an <input>, and a number there would make the "
     + "field un-clearable");
  eq(form.ha_discovery, true, "booleans stay booleans");
}

{
  const form = formFromConfig(FRESH);
  eq(form.mqtt_password, "",
     "nothing stored loads as empty, not as the sentinel -- otherwise a device with no broker "
     + "password would report one");
  eq(form.ui_password, "", "same for the UI password");
}

// --- submitting ----------------------------------------------------------------------------

{
  // The whole document, submitted with nothing touched. This is what pressing Save after
  // changing only the device name does to the passwords, and it must do nothing to them.
  const result = patchFromForm(formFromConfig(CONFIGURED));
  ok(result.ok, "an untouched form is submittable");
  if (result.ok) {
    eq(result.patch.mqtt_password, CONFIG_UNCHANGED,
       "an untouched broker password submits the sentinel, which the device reads as KEEP "
       + "(comfoair_secret_decide)");
    eq(result.patch.ui_password, CONFIG_UNCHANGED, "and so does the UI password");
    eq(result.patch.mqtt_port, 1883,
       "the port submits as a JSON number: comfoair_config_check_port takes a number and a "
       + "quoted string is a different type to a parser");
    eq(Object.keys(result.patch).sort(),
       ["device_name", "ha_discovery", "mqtt_host", "mqtt_password", "mqtt_port", "mqtt_user",
        "permanent_manual", "topic_prefix", "ui_password"],
       "the patch carries exactly these keys: no wifi_ssid/wifi_psk, because Wi-Fi goes to "
       + "/api/provision and only there; no ui_password_set/read_only/wifi_known_good, because "
       + "those are derived projections the device computes and must never be told");
  }
}

{
  const form = formFromConfig(CONFIGURED);
  form.mqtt_password = "hunter2";
  const result = patchFromForm(form);
  ok(result.ok && result.patch.mqtt_password === "hunter2",
     "a typed password submits what was typed");
}

{
  const form = formFromConfig(CONFIGURED);
  form.mqtt_password = "";
  const result = patchFromForm(form);
  ok(result.ok && result.patch.mqtt_password === "",
     "an emptied password submits \"\", which the device reads as CLEAR -- deliberately "
     + "distinct from the sentinel");
}

// --- the port, which is the one field this page refuses on its own ----------------------------

for (const bad of ["", "   ", "abc", "0", "65536", "70000", "18.5", "-5", "1883x", "0x75b"]) {
  const form = formFromConfig(CONFIGURED);
  form.mqtt_port = bad;
  const result = patchFromForm(form);
  ok(!result.ok, `port ${JSON.stringify(bad)} is refused before a request is made`);
}

{
  const form = formFromConfig(CONFIGURED);
  form.mqtt_port = "  8883  ";
  const result = patchFromForm(form);
  ok(result.ok && result.patch.mqtt_port === 8883,
     "a pasted port keeps its whitespace out of the JSON rather than being refused");
}

// --- saying which of "stored" and "not set" is on screen ---------------------------------------
//
// SecretField renders both as an empty box, so the placeholder is the only thing that tells
// them apart -- and once the user types, neither word describes the box any more. These five
// are every state a secret field can be in.

{
  eq(describeSecret(CONFIG_UNCHANGED, CONFIG_UNCHANGED).state, "stored",
     "loaded as the sentinel and untouched: there is a secret and it stays");
  eq(describeSecret(CONFIG_UNCHANGED, "").state, "will-clear",
     "loaded as the sentinel and emptied: saving removes the stored secret");
  eq(describeSecret(CONFIG_UNCHANGED, "abc").state, "will-replace",
     "loaded as the sentinel and typed into: saving replaces it");
  eq(describeSecret("", "").state, "not-set",
     "nothing loaded and nothing typed: there is no secret");
  eq(describeSecret("", "abc").state, "will-set",
     "nothing loaded and something typed: saving sets the first one");

  const states = [
    describeSecret(CONFIG_UNCHANGED, CONFIG_UNCHANGED),
    describeSecret(CONFIG_UNCHANGED, ""),
    describeSecret(CONFIG_UNCHANGED, "abc"),
    describeSecret("", ""),
    describeSecret("", "abc"),
  ];
  ok(states.every((s) => s.text.length > 0 && !s.text.includes(CONFIG_UNCHANGED)),
     "every note is a sentence, and none of them shows the sentinel to the owner: it is an "
     + "implementation detail of the transport, not a word anyone should have to read");
}

// --- and the placeholder, which is the OTHER half of the same statement -------------------------
//
// SecretField's own comment calls the placeholder "the only thing that tells the two
// empty-looking states apart". It therefore cannot be derived from the box alone: an emptied
// stored secret and a secret that was never set are both "", and they mean opposite things.
// Deriving both sentences here is what keeps them from drifting apart -- they drifted once, and
// the box read "not set" directly above a note reading "saving will delete the stored value".

{
  eq(describeSecret(CONFIG_UNCHANGED, CONFIG_UNCHANGED).placeholder, "unchanged — type to replace",
     "a stored secret says so, because an empty box would otherwise read as 'there is none'");
  eq(describeSecret("", "").placeholder, "not set",
     "and nothing stored says that instead");

  const clearing = describeSecret(CONFIG_UNCHANGED, "");
  eq(clearing.state, "will-clear", "an emptied stored secret is about to be deleted");
  ok(clearing.placeholder !== "not set",
     "so the box must NOT read 'not set': it is not describing what is stored, it is standing "
     + "over a stored value that saving is about to remove");
  ok(clearing.placeholder.toLowerCase().includes("delete")
     || clearing.placeholder.toLowerCase().includes("remove"),
     "and it agrees with the note under it rather than contradicting it");

  ok([describeSecret(CONFIG_UNCHANGED, "abc"), describeSecret("", "abc")]
       .every((s) => s.placeholder === ""),
     "a box with something typed in it never shows a placeholder, so there is nothing there to "
     + "be wrong -- and an empty string cannot be read as a claim");

  const all = [
    describeSecret(CONFIG_UNCHANGED, CONFIG_UNCHANGED),
    describeSecret(CONFIG_UNCHANGED, ""),
    describeSecret(CONFIG_UNCHANGED, "abc"),
    describeSecret("", ""),
    describeSecret("", "abc"),
  ];
  ok(all.every((s) => !s.placeholder.includes(CONFIG_UNCHANGED)),
     "and none of the five shows the sentinel");
}

// The manual-mode setting round-trips like every other toggle. CONFIGURED is the fixture this
// file already defines at the top -- a device that has been set up -- so the toggle is added to
// it rather than to a new object, and a field added to DeviceConfig later fails to compile here
// until it is answered for.
{
  const off: DeviceConfig = { ...CONFIGURED, permanent_manual: false };
  eq(formFromConfig(off).permanent_manual, false, "the form carries the stored value");
  const rOff = patchFromForm(formFromConfig(off));
  ok(rOff.ok && rOff.patch.permanent_manual === false,
     "and an untouched OFF form submits false -- a patchFromForm that hard-coded true here "
     + "would re-pin the owner's deliberate OFF on every save, and nothing else in this suite "
     + "would catch it");
  const r = patchFromForm({ ...formFromConfig(off), permanent_manual: true });
  ok(r.ok, "a form with the toggle set still validates");
  if (r.ok) eq(r.patch.permanent_manual, true, "and the patch carries it");
}

report("model");
