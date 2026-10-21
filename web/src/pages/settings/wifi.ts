// The network list and the warning that goes with changing it.
//
// Pure, and pinned by tests/wifi.test.ts. The single most valuable thing in this file is
// mergeScan(): every rule in it is a way the owner ends up provisioning the device to a
// network they did not choose.
//
// Relative imports below carry the .ts extension, unlike the .tsx files in this page.
// That is what lets node resolve this module when the suite runs it directly
// (tests/harness.ts explains why the suite is run that way); Vite is indifferent, and
// `allowImportingTsExtensions` is already on in tsconfig.app.json.

import type { ProvisionRequest, ScanResult } from "../../api/client.ts";
import { CONFIG_UNCHANGED } from "../../api/secrets.ts";
import { t } from "../../i18n/index.ts";

export interface NetworkOption {
  ssid: string;
  /** dBm, or null when this row is not from the scan -- see mergeScan(). */
  rssi: number | null;
  secure: boolean;
  /** True for the SSID the device is currently configured for. */
  configured: boolean;
}

/**
 * The three boxes of the Wi-Fi card, as ONE value.
 *
 * Together and not as three useState atoms, because every rule on this card is a rule about a
 * COMBINATION -- which key belongs to which network, which control the name is being typed
 * into -- and three atoms can only be updated one at a time from a render that has already
 * been left behind. The functions below take this whole value and return a whole new one, so
 * SettingsPage can apply them from inside a functional setState updater, which is the only
 * place in a hook component where "the value as it is now" is available after an await.
 */
export interface WifiForm {
  /** The SSID the form holds: picked from the list, typed by hand, or "" for no choice yet. */
  ssid: string;
  /** True while the typing box is showing instead of the list. */
  manual: boolean;
  /** The key box: typed characters, "" for none, or the sentinel for the one already stored. */
  psk: string;
}

export interface ProvisionNotice {
  /**
   * True when the device has a network it will put back BY ITSELF if this one fails.
   *
   * Not "an SSID is stored". comfoair_provision.c rolls back only where `has_known_good` is set
   * (comfoair_provision.c:442-451) and sets it only where a pair has actually produced an
   * address (comfoair_provision.c:304-311). An SSID stored with a mistyped key has never
   * produced one, so there is nothing behind it to go back to.
   */
  hasFallback: boolean;
  lines: string[];
}

/**
 * The value of the "none of these -- let me type it" row in the network list.
 *
 * It has to be a value no network can ever have, and length is the only property that rules
 * every real SSID out at once: 802.11 caps an SSID at 32 octets (COMFOAIR_CONFIG_SSID_MAX,
 * components/comfoair_config/include/comfoair_config.h). "Other" would not do -- somebody's
 * router really does broadcast "Other", and picking it would silently mean "type it in".
 *
 * Printable ASCII rather than a NUL sentinel, because this string is put into a DOM attribute
 * and read back out of one, and a NUL survives that by nobody's promise.
 */
export const MANUAL_SSID_OPTION = "please type the network name in yourself";

/**
 * Turns one scan into the list the owner picks from.
 *
 * Four rules, each from a distinct way of getting this wrong:
 *
 *  1. ONE ROW PER NAME, at its strongest reading. A sweep returns a record per BSSID, so a
 *     mesh or a repeater puts the same name on screen three times and the owner has to choose
 *     between identical lines.
 *  2. BYTE-FOR-BYTE names. An SSID is an octet string and 802.11 has no case folding; "Home"
 *     and "home" are two networks, and merging them offers a name that is not on the air.
 *  3. NO HIDDEN NETWORKS. A hidden network beacons an empty SSID -- there is no name to
 *     offer, which is exactly what the manual entry box is for.
 *  4. THE CONFIGURED NETWORK IS ALWAYS THERE, and first. This is the one that bites: a
 *     <select> whose value is not among its options displays the FIRST option instead, so a
 *     device configured for a network the scan did not see this second would show the
 *     neighbour's network as if it were the current setting -- and one press of Save would
 *     make that true.
 */
export function mergeScan(scan: ScanResult[], configuredSsid: string): NetworkOption[] {
  const strongest = new Map<string, NetworkOption>();

  for (const seen of scan) {
    if (seen == null || typeof seen.ssid !== "string" || seen.ssid === "") continue;
    const prev = strongest.get(seen.ssid);
    if (prev !== undefined && prev.rssi !== null && prev.rssi >= seen.rssi) continue;
    strongest.set(seen.ssid, {
      ssid: seen.ssid,
      rssi: seen.rssi,
      // `!== false` and not a plain read: a firmware that omits the key entirely must land on
      // "needs a password". Showing a password box for an open network costs one ignored
      // field; hiding it for a secured one leaves the owner unable to join their own network.
      secure: seen.secure !== false,
      configured: seen.ssid === configuredSsid,
    });
  }

  // Stable by construction: Array.prototype.sort has been required to be stable since ES2019,
  // so networks at equal strength keep the order the radio reported them in rather than
  // shuffling between scans.
  const list = [...strongest.values()].sort((a, b) => (b.rssi ?? 0) - (a.rssi ?? 0));

  if (configuredSsid !== "") {
    const at = list.findIndex((n) => n.ssid === configuredSsid);
    if (at >= 0) {
      const [current] = list.splice(at, 1);
      list.unshift(current);
    } else {
      // rssi null, not 0 and not -100: there was no reading. A fabricated number would sort
      // and read as a measurement, and "the device cannot see it right now" is the one thing
      // this row exists to say.
      list.unshift({ ssid: configuredSsid, rssi: null, secure: true, configured: true });
    }
  }

  return list;
}

/**
 * What the password box should hold once the chosen network has changed.
 *
 * The sentinel means "the key you already hold", and that is only true of the network it was
 * stored against. Carried onto a different SSID it tells the device to join the neighbour's
 * network with this one's key -- which comes back as an authentication failure, identical to
 * the owner mistyping, on a device that has by then already left the old network.
 *
 * Only the STORED key is dropped. A key the owner has typed belongs to whatever they are
 * typing it for, and this runs on every keystroke of the manual SSID box: wiping typed
 * characters here would make it impossible to fill the two fields in the other order.
 */
export function pskForSsid(selected: string, configured: string, currentPsk: string): string {
  if (currentPsk !== CONFIG_UNCHANGED) return currentPsk;
  return selected !== "" && selected === configured ? currentPsk : "";
}

/**
 * A network chosen, from the list or one keystroke at a time in the typing box.
 *
 * `manual` is untouched: which control the owner is using is their decision, not a consequence
 * of what they typed into it.
 */
export function chooseNetwork(form: WifiForm, ssid: string, configured: string): WifiForm {
  return { ssid, manual: form.manual, psk: pskForSsid(ssid, configured, form.psk) };
}

/**
 * The form once a scan has come back, computed from the form AS IT IS rather than as it was.
 *
 * The sweep is thirteen channels and takes seconds, and on this device the radio leaves the
 * channel this page is served on while it runs (scanNetworks(), api/client.ts). Everything the
 * owner does in those seconds -- typing a hidden network's name, or simply having GET
 * /api/config answer -- happens after the values the click captured stopped being true. Both
 * orderings end the same way if the stale ones are used: the typed name is replaced by the
 * loudest network the sweep heard, or the stored key is dropped because `configured` was still
 * "" when the click happened.
 *
 * DO NOT call this with values read from an enclosing render. It exists to be called from a
 * setState updater with the state the updater is handed, and from a `configured` that was
 * written the instant GET answered; passing a render's captured copies puts the bug back with
 * no visible change to this file.
 */
export function applyScan(form: WifiForm, scan: ScanResult[], configured: string): WifiForm {
  const choice = reconcileChoice(mergeScan(scan, configured), form.ssid);
  return {
    ssid: choice.ssid,
    manual: choice.manual,
    psk: pskForSsid(choice.ssid, configured, form.psk),
  };
}

/**
 * The form once GET /api/config has answered.
 *
 * Seeded ONLY while both boxes are still empty, and "empty" is the whole rule rather than an
 * approximation of "untouched": an empty box holds nothing the owner can lose, so filling it
 * costs nothing, and anything else on screen is something they put there. That includes a name
 * typed while this very request was in flight -- WifiSection is rendered before the load
 * resolves, deliberately (Settings.tsx), so the seconds between are seconds the owner spends
 * typing.
 *
 * `manual` is left alone in both branches. Whether the list or the box is showing is a
 * decision about controls, and a document arriving is not a reason to change controls under
 * somebody's fingers.
 */
export function seedWifi(form: WifiForm, cfg: { wifi_ssid: string; wifi_psk: string }): WifiForm {
  if (form.ssid !== "" || form.psk !== "") return form;
  return { ssid: cfg.wifi_ssid, manual: form.manual, psk: cfg.wifi_psk };
}

/**
 * The body of POST /api/provision.
 *
 * The one thing this does is make sure the SENTINEL never leaves the page on this route. The
 * form holds it whenever the owner is re-provisioning the network the stored key belongs to --
 * pskForSsid() keeps it there on purpose -- and "__UNCHANGED__" is not an obviously invalid
 * key: it is 13 printable bytes, so it passes comfoair_config_check_psk (>= PSK_MIN, no NUL,
 * not length 64 and therefore not hex-checked) and stores like any other passphrase. A handler
 * that writes the field through then leaves the device holding credentials that cannot
 * associate, and credentials existing is exactly what stops an access point from coming up:
 * the device disappears, which comfoair_provision.c:443-446 names as the likeliest catastrophe
 * in this project.
 *
 * So "keep the stored key" is sent as the ABSENCE of the key, which is already this firmware's
 * word for it -- comfoair_config_patch_t: "A NULL string means the key was ABSENT... absent
 * leaves the stored value alone, empty clears it" (comfoair_config.h:365-368). One decoding
 * rule for the network on both routes, and no string on the wire that anything could store.
 *
 * DO NOT collapse the empty case into the absent one. An open network is a network and
 * comfoair_config_check_psk returns OK for length zero for exactly that reason
 * (comfoair_config_check.c:40-43); "" and absent are the two answers this route needs to tell
 * apart.
 */
export function provisionRequest(form: WifiForm): ProvisionRequest {
  if (form.psk === CONFIG_UNCHANGED) return { wifi_ssid: form.ssid };
  return { wifi_ssid: form.ssid, wifi_psk: form.psk };
}

/**
 * The sentences shown BEFORE the Wi-Fi form is submitted, not after.
 *
 * After is too late: this page is served over the device's own access point, and the reply to
 * POST /api/provision is the last thing the browser will get from it. An owner who has not
 * been told that reads the silence as a device they have just broken.
 *
 * DO NOT add "then open http://comfoair.local" here. THE REASON HAS CHANGED AND THE BAN HAS NOT:
 * the firmware does register an mDNS name now -- components/comfoair_mdns, and it is
 * `comfoair-XXXX.local`, never the bare `comfoair.local` this sentence used to forbid -- but a
 * name is not a guarantee. Android browsers frequently do not resolve `.local` at all, a guest
 * VLAN with client isolation never will, and Windows without Bonjour is a coin toss. Naming it
 * here, alone, at the exact moment
 * the owner is least able to tell a wrong instruction from a broken device, is the same failure
 * as before with a different cause.
 *
 * What works instead is BOTH, side by side, always: the name as a first try and the
 * device's address underneath it. THE OBSTACLE THIS COMMENT USED TO NAME IS GONE -- the SPA does
 * call GET /api/status (getStatus(), api/client.ts, on a clock in useProvisionStatus.ts) and the
 * address is already on this page, in StatusSection's "On <network>, at <address>".
 *
 * It still does not belong in THIS function, for a reason nothing fetched can fix: these sentences
 * are written BEFORE the join, and the address the device will have on the network it is about to
 * try does not exist yet. The only one in hand is the address on the network being LEFT, which is
 * the most misleading thing this text could name. The name-plus-address block belongs beside the status,
 * where an address is a fact rather than a guess; what stays banned here is the name ALONE.
 *
 * `knownGood` is comfoair_prov_has_known_good() (comfoair_provision.h:299), and it is a
 * separate argument from `previous` because the two are NOT the same fact. A stored SSID says
 * somebody typed something once; the flag says a pair actually produced an address, and that
 * is the only condition under which the rollback in this device runs.
 */
export function provisionNotice(
  target: string,
  previous: string,
  knownGood: boolean,
): ProvisionNotice {
  // Both, and the conjunction is not belt-and-braces: `knownGood` is what makes the rollback
  // real, and `previous` is what lets the sentence NAME where the device goes back to. A
  // promise that cannot name its destination is not one the owner can act on.
  const hasFallback = knownGood && previous !== "";
  return {
    hasFallback,
    lines: [
      t("settings.wifi.notice.line1", { target }),

      // One radio, one channel: the access point follows the station when the station tunes
      // to the target network's channel. The access point stays up for the whole setup window
      // precisely so the answer can get out first (docs/architecture/failure-model.md).
      t("settings.wifi.notice.line2", { target }),

      t("settings.wifi.notice.line3", { target }),

      hasFallback
        // The trial has something to roll back TO: a pair that never produces an address is
        // dropped and the last known-good pair restored (comfoair_provision.c:442-451). Saying
        // so is what stops the owner from resetting a device that is merely busy.
        ? t("settings.wifi.notice.fallback", { previous })
        // No known-good pair, whether or not something is stored. A first provisioning has
        // nothing to restore and comfoair_provision.c:452-456 simply goes on retrying, so the
        // way back is the setup network -- and that is on a clock. The window closes about
        // 15 minutes after the access point appeared, extended by activity and capped at an
        // hour (docs/device/networking.md), and comfoair_provision.c:553-558 keeps the AP up
        // only while the window is running. "Come back to this page" without the clock on it
        // is an instruction that quietly expires.
        : t("settings.wifi.notice.noFallback"),
    ],
  };
}

/**
 * A word for a number the owner should not have to interpret.
 *
 * The thresholds are the ordinary Wi-Fi planning figures -- -67 dBm is the usual line for
 * "good enough for real-time traffic". NOTHING on the device depends on them: this is wording,
 * and a network at -80 is offered exactly like any other, because a weak network the owner
 * knows is theirs is still the right answer.
 */
export function signalWords(rssi: number | null): string {
  if (rssi === null) return t("settings.wifi.signal.notSeen");
  if (rssi >= -55) return t("settings.wifi.signal.strong");
  if (rssi >= -67) return t("settings.wifi.signal.good");
  if (rssi >= -75) return t("settings.wifi.signal.weak");
  return t("settings.wifi.signal.veryWeak");
}

/**
 * The sentence under the Wi-Fi key box.
 *
 * Deliberately NOT describeSecret() from model.ts, although both describe an empty password
 * box. They mean opposite things:
 *
 *   config document | "" means CLEAR the stored secret
 *   this form       | "" means the network HAS no key, which half the guest networks in the
 *                     world do not (comfoair_config_check_psk returns OK for length 0 for
 *                     exactly that reason -- comfoair_config_check.c:40-43)
 *
 * And the box here is emptied by the page itself, whenever the chosen network changes
 * (pskForSsid). Borrowing the document's wording would announce a deletion to an owner who
 * has done nothing but pick a different network from a list.
 */
export function describeWifiKey(psk: string, ssid: string): string {
  if (psk === CONFIG_UNCHANGED)
    return t("settings.wifi.key.stored", { ssid });
  if (psk === "") return t("settings.wifi.key.empty");
  return t("settings.wifi.key.typed");
}

/**
 * The SSID a network list can actually display, or "" for the row that means "none of them".
 *
 * A <select> whose value matches none of its options shows the FIRST option instead -- so a
 * form holding a hand-typed name, next to a list that has just arrived, would show the owner a
 * network they did not choose. This is the same failure mergeScan() guards from the other
 * direction, and it has the same consequence: one press of Save and the wrong choice is true.
 *
 * The answer used to be "fall back to the strongest", and that fixed the control by making the
 * choice: an owner who pressed Scan merely to LOOK ended up one press from provisioning the
 * device to the loudest network in the building. WifiSection carries a leading row whose value
 * is "" instead, so "nothing chosen" is a thing the control can show; mergeScan() guarantees
 * "" is never a real SSID (a hidden network's empty beacon is dropped), so that row can never
 * collide with one. The button is disabled while the form holds "".
 *
 * DO NOT drop that row from the <select>. Without it this function hands the control a value
 * it cannot display, which is the failure the whole file is arranged around.
 */
export function listedSsid(options: NetworkOption[], ssid: string): string {
  return options.some((n) => n.ssid === ssid) ? ssid : "";
}

/**
 * What the form should hold once a fresh scan has arrived.
 *
 * Three cases, and two are worth writing down. A name the owner typed that the scan did not
 * find is NOT dropped in favour of something that was found: it is kept, and the form stays in
 * its typing box -- a hidden network is invisible to every scan by definition, so "the scan did
 * not see it" is not evidence of a mistake. And a form with no choice in it still has no choice
 * afterwards: a scan is something the owner ran to see what is there, and answering it by
 * selecting a network for them is answering a question they did not ask.
 */
export function reconcileChoice(
  options: NetworkOption[],
  ssid: string,
): { ssid: string; manual: boolean } {
  // manual only when there is no list at all: with nothing to pick from, the typing box is the
  // only control that can name a network, and burying it behind a row in an empty dropdown
  // would hide the one thing that works.
  if (ssid === "") return { ssid: "", manual: options.length === 0 };
  if (options.some((n) => n.ssid === ssid)) return { ssid, manual: false };
  return { ssid, manual: true };
}
