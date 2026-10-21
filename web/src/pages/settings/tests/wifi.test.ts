// The network list, and what the owner is told before the device leaves the network.
//
// Run: node src/pages/settings/tests/wifi.test.ts
//
// The scan list exists because of one support request: "the network is right there on my
// phone and it will not find it". A router publishes ONE name on both bands, the C6 has only
// 2.4 GHz, and the owner concludes the device is broken. Offering the device's own scan is
// therefore an invariant rather than a choice (docs/guide/first-setup.md).

import type { ScanResult } from "../../../api/client.ts";
import { CONFIG_UNCHANGED } from "../../../api/secrets.ts";
import {
  MANUAL_SSID_OPTION,
  applyScan,
  chooseNetwork,
  describeWifiKey,
  listedSsid,
  mergeScan,
  provisionNotice,
  provisionRequest,
  pskForSsid,
  reconcileChoice,
  seedWifi,
  signalWords,
  type WifiForm,
} from "../wifi.ts";
import { eq, ok, report } from "./harness.ts";

const net = (ssid: string, rssi: number, secure = true): ScanResult => ({ ssid, rssi, secure });

// The three boxes of the Wi-Fi card, in the order WifiForm declares them -- eq() compares
// serialised JSON, so a helper that fixes the key order is what keeps these assertions about
// values rather than about property insertion.
const form = (ssid: string, manual: boolean, psk: string): WifiForm => ({ ssid, manual, psk });

// --- merging what the radio saw ----------------------------------------------------------

{
  // One SSID, two BSSIDs -- a mesh node in the hall and the router in the cellar, or simply
  // the same access point answering twice in one sweep. The list must show ONE row, at the
  // strongest of the two, or the owner picks between two identical-looking lines.
  const list = mergeScan([net("Kitchen", -80), net("Attic", -50), net("Kitchen", -42)], "");
  eq(list.map((n) => n.ssid), ["Kitchen", "Attic"], "strongest first, and each name once");
  eq(list[0].rssi, -42, "a duplicated SSID keeps its STRONGEST reading, not the last one seen");
}

{
  const list = mergeScan([net("Open", -60, false), net("Open", -70, true)], "");
  eq(list.length, 1, "duplicates collapse regardless of their flags");
  eq(list[0].secure, false,
     "the kept row is the strongest one, flags and all -- inventing a merged flag would put a "
     + "password box on a network that has no password");
}

{
  // 802.11 SSIDs are octet strings; the standard has no notion of case folding. Two houses
  // on one street really can run "Home" and "home", and merging them would offer a name that
  // is not on the air.
  const list = mergeScan([net("Home", -50), net("home", -60)], "");
  eq(list.length, 2, "SSIDs are compared byte for byte, never case-insensitively");
}

{
  // A hidden network answers a scan with an empty SSID. It cannot be offered as a name --
  // that is precisely what the manual entry box is for.
  const list = mergeScan([net("", -40), net("Kitchen", -60)], "");
  eq(list.map((n) => n.ssid), ["Kitchen"], "hidden networks are dropped from the list");
}

{
  // THE failure this guards: a <select> whose value is not among its options renders as the
  // FIRST option. The device is configured for a network the scan did not see -- the router
  // is off, or it is simply out of range at this moment -- and the control silently shows the
  // neighbour's network instead. One press of Save and the device is provisioned to it.
  const list = mergeScan([net("Neighbour", -40)], "Kitchen");
  eq(list.map((n) => n.ssid), ["Kitchen", "Neighbour"],
     "the configured network is always in the list, and first, even when nothing saw it");
  eq(list[0].rssi, null, "with no reading, because there was none -- not a fabricated 0");
  eq(list[0].configured, true, "and marked, so the UI can say why it is there");
  eq(list[0].secure, true,
     "assumed to need a key: showing a password box for an open network wastes a line, "
     + "hiding it for a secured one strands the owner");
}

{
  const list = mergeScan([net("Kitchen", -55), net("Neighbour", -40)], "Kitchen");
  eq(list.map((n) => n.ssid), ["Kitchen", "Neighbour"],
     "a configured network the scan DID see appears once, still first");
  eq(list[0].rssi, -55, "keeping the reading the scan gave it");
  eq(list.filter((n) => n.configured).length, 1, "and marked exactly once");
}

eq(mergeScan([], ""), [], "no scan and no configured network is an empty list, not a row of ''");

// --- the password field follows the network -------------------------------------------------

{
  // The sentinel means "the key you already hold". That is only true while the SSID is the
  // one it was stored for: carrying it onto a different network tells the device to join
  // "Neighbour" with the key for "Kitchen", which fails in a way that looks like a wrong
  // password typed by the owner.
  eq(pskForSsid("Kitchen", "Kitchen", CONFIG_UNCHANGED), CONFIG_UNCHANGED,
     "re-provisioning the SAME network may keep the stored key");
  eq(pskForSsid("Neighbour", "Kitchen", CONFIG_UNCHANGED), "",
     "choosing a DIFFERENT network clears it: the stored key belongs to the old one");
  eq(pskForSsid("Kitchen", "", CONFIG_UNCHANGED), "",
     "a device with no configured network has no key to keep");

  // Only the STORED key is tied to the stored network. A key the owner has typed belongs to
  // whatever they are typing it for -- and this runs on every keystroke of the manual SSID
  // box, so wiping typed characters here would make it impossible to type the password first.
  eq(pskForSsid("Neighbour", "Kitchen", "typed by hand"), "typed by hand",
     "a typed key survives the network changing under it");
  eq(pskForSsid("Neighbour", "Kitchen", ""), "",
     "and an empty field stays empty rather than being refilled");
}

// --- what the owner reads before pressing Save -----------------------------------------------

{
  const first = provisionNotice("Kitchen", "", false);
  ok(first.lines.length >= 3, "the notice is more than one line");
  ok(first.lines.every((l) => l.trim().length > 0), "no blank lines");
  ok(first.lines.some((l) => l.includes("Kitchen")), "it names the network being joined");
  eq(first.hasFallback, false, "a device with no network has nothing to fall back to");

  const again = provisionNotice("Attic", "Kitchen", true);
  eq(again.hasFallback, true, "a device that already has one does");
  ok(again.lines.some((l) => l.includes("Kitchen")),
     "and the notice names it, because 'it will come back on its own' is the sentence that "
     + "stops the owner from resetting a device that is only busy");
  ok(again.lines[again.lines.length - 1] !== first.lines[first.lines.length - 1],
     "the two cases do not end with the same promise");
}

{
  // THE case this parameter exists for. The owner mistyped the key once: NVS holds an SSID,
  // no address was ever obtained, and comfoair_provision.c sets has_known_good ONLY where an
  // address arrives (comfoair_provision.c:304-311) and rolls back ONLY where it is set
  // (comfoair_provision.c:442-451). A stored SSID is not evidence of a working pair, and the
  // promise "the device puts it back by itself, nothing needs to be reset" told to an owner
  // whose device is instead sitting in RETRYING is the one sentence that makes them wait for
  // a return that is not coming.
  const stored = provisionNotice("Attic", "Kitchn", false);
  eq(stored.hasFallback, false,
     "a stored SSID that never produced an address is NOT a fallback");
  ok(!stored.lines.some((l) => l.includes("Kitchn")),
     "and the notice must not name it, because naming it is promising a return to it");
  ok(!stored.lines.some((l) => l.toLowerCase().includes("nothing needs to be reset")),
     "nor tell the owner to sit still: on this device the way back is the setup network, not "
     + "a rollback that will not run");

  eq(provisionNotice("Attic", "", true).hasFallback, false,
     "and a known-good flag with no SSID to name is not one either -- the sentence has to be "
     + "able to say WHICH network it goes back to");
}

{
  // The setup network is not permanent. It closes 15 minutes after it came up (extended by
  // activity, capped at 60) and comes back for a few minutes on a restart
  // (docs/device/networking.md, "Setup window"); comfoair_provision.c:553-558 keeps the AP
  // up only while `window_running`. "You can come back to this page and try again" with no
  // clock on it is an instruction that expires.
  const fresh = provisionNotice("Kitchen", "", false);
  ok(fresh.lines.some((l) => l.includes("15 minutes")),
     "the no-fallback branch carries the window rather than promising a page that will be gone");
  ok(fresh.lines.some((l) => l.toLowerCase().includes("restart")
                          || l.toLowerCase().includes("power")),
     "and names the one thing that brings the setup network back after it closes");
}

// --- wording for a number nobody reads ---------------------------------------------------------

{
  // Pinned AT each threshold and one dBm past it, because the thresholds are the whole
  // function: a version that returns four non-empty strings in the wrong order passes any
  // test that only checks that they are non-empty and different, and it tells the owner that
  // the network to avoid is the strong one.
  eq(signalWords(null), "not seen in this scan",
     "no reading at all is its own answer -- mergeScan gives the configured network rssi null "
     + "precisely so this row can say 'the device cannot hear it right now'");

  eq(signalWords(-30), "strong", "same room");
  eq(signalWords(-55), "strong", "-55 belongs to the stronger side of its boundary");
  eq(signalWords(-56), "good", "and one dBm past it does not");
  eq(signalWords(-67), "good", "-67 is the usual line for real-time traffic, and is still good");
  eq(signalWords(-68), "weak", "past it, weak");
  eq(signalWords(-75), "weak", "-75 is the last of weak");
  eq(signalWords(-76), "very weak", "and below that, very weak");
  eq(signalWords(-90), "very weak", "barely there");

  // The property the four strings have to have, stated separately from their spelling: rewording
  // is allowed, reordering is not.
  const stronger = [-30, -55, -56, -67, -68, -75, -76, -90].map(signalWords);
  const rank = ["strong", "good", "weak", "very weak"];
  ok(stronger.every((w, i) => i === 0 || rank.indexOf(stronger[i - 1]) <= rank.indexOf(w)),
     "the words only ever get weaker as the number does, so the list never recommends the "
     + "network the owner should avoid");
}

// --- the row that is not a network -------------------------------------------------------

{
  // The network list needs one row meaning "none of these, let me type it", and that row
  // needs a value no network can have. "Other" cannot be it: somebody's router broadcasts
  // "Other". Length is the only property that rules every real SSID out at once.
  ok(MANUAL_SSID_OPTION.length > 32,
     "longer than the 32 octets 802.11 allows an SSID (COMFOAIR_CONFIG_SSID_MAX), so no "
     + "network can ever carry this value");
  ok(/^[\x20-\x7e]+$/.test(MANUAL_SSID_OPTION),
     "and printable ASCII, so its length in characters IS its length in octets -- and so it "
     + "survives being put in a DOM attribute, which a NUL sentinel would not reliably do");
  eq(mergeScan([net(MANUAL_SSID_OPTION, -40)], "").length, 1,
     "nothing in mergeScan treats it specially: it is the LIST's sentinel, not the scan's");
}

// --- what the key box says, which is NOT what a config secret says ----------------------------

{
  // describeSecret() in model.ts speaks for the configuration document, where "" means CLEAR
  // THE STORED SECRET. On this form "" means something else entirely -- join a network that
  // has no key -- and the box is emptied by the page itself whenever the chosen network
  // changes. Reusing that wording here would tell an owner who has just picked a different
  // network that saving is about to delete something.
  eq(describeWifiKey(CONFIG_UNCHANGED, "Kitchen").includes("Kitchen"), true,
     "the stored key is described by the network it belongs to");
  ok(describeWifiKey("", "Kitchen").toLowerCase().includes("open"),
     "an empty box means an open network, not a deletion");
  ok(!describeWifiKey("", "Kitchen").toLowerCase().includes("delete"),
     "and says nothing about deleting anything");
  ok(describeWifiKey("typed", "Kitchen").length > 0, "a typed key gets a sentence too");
  ok([CONFIG_UNCHANGED, "", "typed"].every((v) => !describeWifiKey(v, "K").includes(CONFIG_UNCHANGED)),
     "and none of them shows the sentinel");
}

// --- the control must never display a network the form is not holding --------------------------

{
  // Same failure as the missing configured network, arriving from the other side: a <select>
  // whose value matches no option displays the FIRST one. That happens on a fresh device the
  // moment the first scan lands -- ssid is still "" -- and it happens again when a name typed
  // by hand is not in a list that has just been refreshed.
  const options = mergeScan([net("Attic", -50), net("Kitchen", -42)], "");

  eq(listedSsid(options, ""), "",
     "\"nothing chosen\" is itself displayable -- WifiSection carries a leading row for it -- so "
     + "the list does not have to invent a choice to stay honest");
  eq(listedSsid(options, "Attic"), "Attic", "a listed choice is kept");
  eq(listedSsid(options, "Cellar"), "", "an unlisted one cannot be displayed, so nothing is");
  eq(listedSsid([], "Cellar"), "", "with nothing to fall back to, nothing is chosen");

  eq(reconcileChoice(options, ""), { ssid: "", manual: false },
     "a scan the owner ran to LOOK does not choose for them: preselecting the strongest row "
     + "leaves them one press from provisioning the device to the loudest neighbour");
  eq(reconcileChoice(options, "Kitchen"), { ssid: "Kitchen", manual: false },
     "an existing choice that is in the list survives the scan");
  eq(reconcileChoice(options, "Cellar"), { ssid: "Cellar", manual: true },
     "a typed name the scan did not find keeps the name and keeps the typing box -- the list "
     + "cannot show it, and silently retyping it into another network is the failure");
  eq(reconcileChoice([], ""), { ssid: "", manual: true },
     "a scan that found nothing leaves the only control that works");
}

// --- the values that outlive the render that read them ----------------------------------------
//
// Every function below exists because the page calls it from a continuation AFTER an await, and
// the page's own state is not what it was when the request went out. They take the form as it is
// NOW and return the form as it should be; SettingsPage feeds them from a functional setState
// updater and from a ref written the instant GET answers, which is what makes "now" true.

{
  eq(chooseNetwork(form("Kitchen", false, CONFIG_UNCHANGED), "Neighbour", "Kitchen"),
     form("Neighbour", false, ""),
     "picking a different network drops the stored key with it: it belongs to the old SSID");
  eq(chooseNetwork(form("Kitchen", false, CONFIG_UNCHANGED), "Kitchen", "Kitchen"),
     form("Kitchen", false, CONFIG_UNCHANGED),
     "and re-picking the same one keeps it");
  eq(chooseNetwork(form("Kit", true, "typed by hand"), "Kitc", "Kitchen"),
     form("Kitc", true, "typed by hand"),
     "a typed key survives every keystroke of the manual SSID box, which is what makes it "
     + "possible to fill the two boxes in either order");
}

{
  // Finding 1, as a value rather than as a closure. The sweep takes seconds and the radio
  // leaves this page's channel while it runs (client.ts, scanNetworks); the owner types a
  // hidden network's name into the manual box in the meantime. The scan comes back with the
  // neighbour's network in it and must not take the typed name away.
  eq(applyScan(form("HiddenNet", true, "the key for it"), [net("Neighbour", -40)], ""),
     form("HiddenNet", true, "the key for it"),
     "a name typed while the sweep was running survives the sweep landing");

  // The other ordering: Scan pressed before GET /api/config answered, so the configured SSID
  // and its stored key arrive DURING the sweep. Reconciling against the empty form the click
  // captured drops both.
  eq(applyScan(form("Kitchen", false, CONFIG_UNCHANGED), [net("Neighbour", -40)], "Kitchen"),
     form("Kitchen", false, CONFIG_UNCHANGED),
     "the configured network and its stored key survive a scan that did not hear it -- "
     + "mergeScan puts it in the list, so nothing has to be replaced to keep the list honest");

  eq(applyScan(form("", false, ""), [net("Neighbour", -40)], ""), form("", false, ""),
     "an untouched form after a scan is still untouched: the owner chooses");
  eq(applyScan(form("", false, ""), [], ""), form("", true, ""),
     "and a scan that found nothing leaves the typing box, the only control that works");

  eq(applyScan(form("Attic", false, CONFIG_UNCHANGED), [net("Attic", -50)], "Kitchen"),
     form("Attic", false, ""),
     "a scan that confirms a network OTHER than the configured one still drops the stored key: "
     + "the key belongs to the network it was stored for");
}

{
  // Finding 2. load()'s seeding guard has to read the boxes as they are when GET answers, not
  // as they were when the page mounted -- on the mount path those are always "" and "", so a
  // guard on the captured values is unconditionally true and overwrites whatever was typed
  // while the request was in flight.
  eq(seedWifi(form("", false, ""), { wifi_ssid: "Kitchen", wifi_psk: CONFIG_UNCHANGED }),
     form("Kitchen", false, CONFIG_UNCHANGED),
     "empty boxes take what the device answered");
  eq(seedWifi(form("Attic", false, ""), { wifi_ssid: "Kitchen", wifi_psk: CONFIG_UNCHANGED }),
     form("Attic", false, ""),
     "a network name typed while GET was in flight is NOT replaced by the stored one");
  eq(seedWifi(form("", false, "typed"), { wifi_ssid: "Kitchen", wifi_psk: CONFIG_UNCHANGED }),
     form("", false, "typed"),
     "and neither is a key: the box the owner is holding wins over the document");
  eq(seedWifi(form("", true, ""), { wifi_ssid: "", wifi_psk: "" }),
     form("", true, ""),
     "a fresh device seeds nothing into either box and leaves the manual flag alone");
}

{
  // Finding 4. The page really does hold the sentinel on this route -- pskForSsid preserves it
  // for the configured network on purpose -- and "__UNCHANGED__" is a perfectly storable
  // 13-byte string: it passes comfoair_config_check_psk (>= PSK_MIN 8, no NUL, not length 64
  // so no hex check). A handler that stores the field verbatim writes it as the household key,
  // credentials then exist so no access point comes up, and the device disappears
  // (comfoair_provision.c:443-446). Absence is the one value no handler can store.
  eq(provisionRequest(form("Kitchen", false, CONFIG_UNCHANGED)), { wifi_ssid: "Kitchen" },
     "keeping the stored key OMITS wifi_psk, which comfoair_config_patch_t already defines as "
     + "\"leave the stored value alone\" (comfoair_config.h:365-368)");
  ok(!JSON.stringify(provisionRequest(form("Kitchen", false, CONFIG_UNCHANGED)))
       .includes(CONFIG_UNCHANGED),
     "so the sentinel never reaches the wire on the one route that writes the household key");

  eq(provisionRequest(form("Kitchen", false, "hunter2")),
     { wifi_ssid: "Kitchen", wifi_psk: "hunter2" },
     "a typed key is sent as typed");
  eq(provisionRequest(form("Guest", false, "")), { wifi_ssid: "Guest", wifi_psk: "" },
     "and an empty key is SENT, empty: an open network is a network, and omitting the key here "
     + "would mean 'keep' instead (comfoair_config_check.c:40-43)");
}

report("wifi");
