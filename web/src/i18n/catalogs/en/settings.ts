// Settings page (pages/settings/**): Settings.tsx, every sections/*.tsx, and the display-string
// helpers in wifi.ts, model.ts and errors.ts. EN values are moved VERBATIM from those files --
// the settings/tests/* suites pin several of them byte-for-byte (under node the locale is
// English), so do NOT reword an existing key without checking that suite first. Sentinel values
// (MANUAL_SSID_OPTION, CONFIG_UNCHANGED) are code, not catalog entries.
//
// The firmware error sentences firmwareText.ts's DETAIL_TEMPLATES match belong here too, as
// `settings.detail.*` keys (the block at the end).
//
// THIS object defines the namespace's key set. Every key added here must be added to
// ../de/settings.ts, ../nl/settings.ts and ../uk/settings.ts too: those are typed
// `Record<keyof typeof settings, Message>` against this object, so tsc fails the build in each of
// them until it is. Prefix every key "settings." -- en.ts spreads all namespaces into one object,
// and a key two namespaces share would silently overwrite one of them (catalogs.test.ts fails
// on that). A count-dependent message is a plural object ({ one, other } in English); the
// other languages supply their own categories, which catalogs.test.ts checks.
import type { Message } from "../../format.ts";

export const settings = {
  // --- Settings.tsx (page chrome) -----------------------------------------------------------
  "settings.title": "Settings",
  "settings.readOnly.headline": "These settings were written by a newer firmware",
  "settings.readOnly.detail":
    "This build does not understand the stored configuration's layout, so it refuses "
    + "to rewrite it rather than damage it. Reinstall the firmware this device had, "
    + "or clear its settings with the button, before changing anything here.",
  "settings.load.hint":
    "The broker and device settings cannot be shown until they can be read. The Wi-Fi "
    + "form above does not depend on them.",
  "settings.tryAgain": "Try again",
  "settings.loading": "Reading the device's configuration…",
  "settings.save": "Save settings",
  "settings.saved": "Settings saved",
  "settings.saveBar.hint": "Saves the broker and device settings together. It does not touch the network.",
  // The headline of a failure this page invented (a form value patchFromForm() refused), not
  // the device's answer.
  "settings.notSent": "Not sent — nothing left this page",

  // --- BrokerSection ------------------------------------------------------------------------
  "settings.broker.title": "MQTT broker",
  "settings.broker.subtitle": "Where the device publishes, and where Home Assistant finds it.",
  "settings.broker.host.label": "Broker host",
  "settings.broker.host.placeholder": "192.168.1.10 or homeassistant.local",
  "settings.broker.port.label": "Broker port",
  "settings.broker.port.placeholder": "1883",
  "settings.broker.username.label": "Broker username",
  "settings.broker.password.label": "Broker password",
  "settings.broker.password.warning":
    "This connection is not encrypted in this version, so this password is visible to "
    + "anything on your network. Do not reuse a password you use anywhere else.",
  "settings.broker.topicPrefix.label": "Topic prefix",
  "settings.broker.discovery.label": "Publish Home Assistant discovery",

  // --- DeviceSection --------------------------------------------------------------------------
  "settings.device.title": "Device",
  "settings.device.subtitle": "What it is called, how it behaves, and who may change it.",
  "settings.device.name.label": "Device name",
  "settings.device.name.placeholder": "Ventilation",
  "settings.device.name.hint":
    "A display name only. The device's identity — its broker client id and its Home "
    + "Assistant device id — is derived from its MAC address, so renaming it here cannot "
    + "produce a second device in Home Assistant.",
  "settings.device.password.label": "Web interface password",
  "settings.device.password.warnTitle": "There is no \"forgot password\" for this device.",
  // Split around an <em>, so the emphasis stays a real element (JSX puts these back together).
  "settings.device.password.warn2a": "Holding the button ",
  "settings.device.password.warn2em": "while",
  "settings.device.password.warn2b":
    " power comes up does something else entirely — it puts the chip into its firmware "
    + "loader, which looks exactly like a device that has died. Power it up first, wait, then "
    + "hold.",
  "settings.device.password.noneSet":
    "No password is set. Anyone who can reach this device on the network can change "
    + "these settings — which is a reasonable choice for a device on a home network, and "
    + "it is the default deliberately.",

  // --- WifiSection + wifi.ts -------------------------------------------------------------------
  "settings.wifi.title": "Wi-Fi",
  "settings.wifi.subtitle": "The list is what this device can hear, which is not what your phone shows.",
  "settings.wifi.network.label": "Network",
  "settings.wifi.network.choose": "— choose a network —",
  "settings.wifi.network.currentlyConfigured": "currently configured",
  "settings.wifi.network.open": "open",
  "settings.wifi.network.manualOption": "Other / hidden network — type the name…",
  "settings.wifi.network.ssidLabel": "Network name (SSID)",
  "settings.wifi.network.ssidPlaceholder": "exactly as the router broadcasts it",
  "settings.wifi.scan.again": "Scan again",
  "settings.wifi.scan.start": "Scan for networks",
  "settings.wifi.chooseFromList": "Choose from the list",
  "settings.wifi.typeInstead": "Type the name instead",
  "settings.wifi.scan.hint":
    "A name that is on your phone and not in this list is the usual reason a device "
    + "\"cannot find\" a network that is plainly there: most routers publish one name on both "
    + "bands, and this device has no 5 GHz radio. Type it in if that happens — and hidden "
    + "networks never appear in any scan, so they can only be typed.",
  "settings.wifi.password.label": "Wi-Fi password",
  // WifiSection.tsx's fallback name for describeWifiKey() when nothing is configured yet.
  "settings.wifi.password.fallbackNetworkName": "the stored network",
  // WifiSection.tsx's fallback name for provisionNotice()'s {target} when no SSID is chosen yet.
  "settings.wifi.notice.newNetworkFallback": "the new network",
  "settings.wifi.password.hintUnencrypted":
    "If you are on the device's own setup network, that network is open: this key crosses "
    + "it unencrypted, once. Nothing in a browser page served over plain HTTP can prevent "
    + "that.",
  "settings.wifi.network.openHint": "{ssid} is open — leave the password empty.",
  "settings.wifi.network.securedWarning":
    "{ssid} is secured and the password box is empty. The device will associate and be "
    + "turned away.",
  "settings.wifi.beforeYouPress": "Before you press this",
  "settings.wifi.save": "Save Wi-Fi and reconnect",
  "settings.wifi.accepted": "Accepted — the device is trying now",

  // wifi.ts's signalWords() -- a word for a dBm reading (settings/tests/wifi.test.ts pins each
  // exactly, and the thresholds stay in code).
  "settings.wifi.signal.notSeen": "not seen in this scan",
  "settings.wifi.signal.strong": "strong",
  "settings.wifi.signal.good": "good",
  "settings.wifi.signal.weak": "weak",
  "settings.wifi.signal.veryWeak": "very weak",

  // wifi.ts's describeWifiKey() -- the sentence under the Wi-Fi key box.
  "settings.wifi.key.stored":
    "The key already stored for {ssid} will be used again. It is never sent back to "
    + "this page, so the box is empty; type to replace it.",
  "settings.wifi.key.empty":
    "Empty: the device will try to join without a key. Correct for an open network, "
    + "and nothing else.",
  "settings.wifi.key.typed": "This key will be sent to the device as typed.",

  // wifi.ts's provisionNotice() -- the four sentences read before Save is pressed.
  "settings.wifi.notice.line1":
    "The device replies to this form before it starts connecting, so a confirmation here "
    + "means the credentials were accepted — not that {target} took them.",
  "settings.wifi.notice.line2":
    "While it tries, the setup network you are on now and {target} share a single radio. "
    + "This page will most likely stop responding within a few seconds — that is the normal "
    + "outcome here, not a failure.",
  "settings.wifi.notice.line3":
    "Afterwards the device is on {target}, with whatever address your router hands it. "
    + "Your router's list of connected clients is where to look for it.",
  "settings.wifi.notice.fallback":
    "If the key is wrong, the device puts {previous} back by itself and returns there — "
    + "it is the last network that actually gave this device an address, which is the only "
    + "thing that makes a pair worth going back to. Nothing needs to be reset.",
  "settings.wifi.notice.noFallback":
    "This device has no network it has ever reached, so there is nothing for it to go "
    + "back to. If the key is wrong it keeps trying, and its own setup network stays on "
    + "the air for about 15 minutes from when it appeared. After that the setup network "
    + "is gone until the device is restarted, which brings it back for a few minutes.",

  // --- model.ts ---------------------------------------------------------------------------------
  // describeSecret()'s five states (SecretField's note/placeholder pair). The two placeholders
  // are pinned byte-for-byte by settings/tests/model.test.ts.
  "settings.secret.stored.text":
    "Stored on the device. It is never sent back, so the box is empty; type to replace it.",
  "settings.secret.stored.placeholder": "unchanged — type to replace",
  "settings.secret.willClear.text": "Emptied. Saving will delete the stored value.",
  "settings.secret.willClear.placeholder": "empty — saving deletes the stored value",
  "settings.secret.willReplace.text": "Saving will replace the stored value.",
  "settings.secret.notSet.text": "Not set.",
  "settings.secret.notSet.placeholder": "not set",
  "settings.secret.willSet.text": "Saving will store this for the first time.",
  // patchFromForm()'s one client-side validation.
  "settings.error.portRange": "Broker port must be a whole number between 1 and 65535.",

  // --- errors.ts (explainError headlines; detail is always the device's own sentence) --------
  "settings.error.notImplemented.headline": "The device has this route and no code behind it yet",
  "settings.error.noHandler.headline": "This firmware build does not serve that endpoint",
  "settings.error.passwordNeeded.headline": "The device wants its web-interface password",
  "settings.error.writeRefused.headline": "The device refused this write",
  "settings.error.badRequest.headline": "The device would not accept this",
  "settings.error.fault.headline": "The device reported a fault",
  "settings.error.browserGaveNoReason": "The browser gave no reason.",
  "settings.error.disconnectExpected.headline": "No answer — which is what success looks like here",
  "settings.error.disconnectExpected.detail":
    "The device replies before it retunes its radio, so a request that dies mid-flight "
    + "usually means it is already trying. ({browserSaid})",
  "settings.error.offline.headline": "No answer from the device",
  "settings.error.offline.detail": "Nothing reached the device, so it has said nothing about this. ({browserSaid})",

  // --- settings.detail.* -- the firmware's own error sentences ------------------------------
  // Each value is the firmware's sentence BYTE FOR BYTE: an English reader must see exactly
  // what the device sent (errors.test.ts pins the passthrough), and firmwareText.test.ts
  // checks every template matches its own English value.
  // comfoair_config_strerror() (components/comfoair_config/comfoair_config_check.c)
  "settings.detail.ssid": "the network name must be 1 to 32 characters",
  "settings.detail.psk":
    "the network password must be 8 to 63 characters, or exactly 64 hexadecimal digits, "
    + "or empty for an open network",
  "settings.detail.wifiPair":
    "the network name and its password are stored as one record: send both, and re-enter "
    + "the password whenever the name changes",
  "settings.detail.host":
    "the broker address must be a host name or an IP address, with no scheme and no port",
  "settings.detail.port": "the broker port must be between 1 and 65535",
  "settings.detail.user": "the broker user name is longer than this device can store",
  "settings.detail.brokerPassword": "the broker password is longer than this device can store",
  "settings.detail.prefix":
    "the topic prefix must be 1 to 64 characters, without spaces, + or #, and without an "
    + "empty level",
  "settings.detail.name":
    "the device name must be 1 to 32 characters and contain no control characters",
  "settings.detail.uiPassword":
    "the web password must be 8 to 128 characters and contain no control characters",
  "settings.detail.noHash":
    "this device could not secure the password just now; nothing was changed",
  "settings.detail.readOnly":
    "these settings were written by a newer firmware and cannot be changed until the "
    + "device is reset",
  "settings.detail.noDocument": "the request carried no settings, so nothing was saved",
  "settings.detail.refused": "the settings were refused",
  // comfoair_wire_strerror() (components/comfoair_wire/comfoair_wire.c)
  "settings.detail.badBody": "The request body is not a JSON object this device can read.",
  "settings.detail.badField":
    "A field is missing, of the wrong type, or longer than this device can store.",
  "settings.detail.wrongRoute": "The network is set through POST /api/provision, not here.",
  "settings.detail.notApplied": "The request could not be applied.",
  // literal error bodies in components/comfoair_http/
  "settings.detail.unauthorized": "unauthorized",
  "settings.detail.forbidden": "forbidden",
  "settings.detail.notImplemented": "not implemented yet",
  "settings.detail.noEndpoint": "no such endpoint",
  "settings.detail.bodyTooLarge": "body too large",
  "settings.detail.truncatedBody": "truncated body",
  "settings.detail.scanUnavailable": "scan unavailable",
  "settings.detail.scanTooLarge": "scan does not fit",
  "settings.detail.busy": "busy",
  "settings.detail.statusTooLarge": "status does not fit",
  "settings.detail.configTooLarge": "config does not fit",
  "settings.detail.emptyBody": "The device answered {1} with an empty body.",
  "settings.detail.unreadableBody": "The device answered {1} with a body this page could not read.",

  // --- broker/device notes, the reset-button ladder, the Connection card (status.ts) ---
  "settings.broker.topicPrefix.hint":
    "Changing the prefix moves every state topic. The entities in Home Assistant follow "
    + "and keep their history — what describes them to Home Assistant does not carry the "
    + "prefix, so it is rewritten rather than replaced, and you will not see a second set. "
    + "If the device is connected to the broker when you save, it empties the topics at the "
    + "old prefix on the way past; if it is not, they stay there as retained messages that "
    + "nothing reads any more.",
  "settings.broker.discovery.note":
    "Off: the device stops publishing what describes it to Home Assistant, but does not "
    + "take back what it published before. The device and its entities stay there and go on "
    + "updating — clearing them means removing those retained messages at the broker by "
    + "hand.",
  "settings.device.permanentManual.label": "Keep the ventilation level",
  "settings.device.permanentManual.note":
    "On: a level chosen here or in Home Assistant sticks. Off: the unit returns to AUTO "
    + "within two hours, and its humidity and temperature controls stay active — manual "
    + "mode switches them off.",
  "settings.device.button.p1":
    "The way back is the button on the device, and how long you hold it is the whole of "
    + "it. Press it {after} and watch the light: it turns white the moment the press is "
    + "noticed, goes back to its usual colour between one and five seconds, turns amber at "
    + "five and red at fifteen, growing brighter as the next step comes up.",
  "settings.device.button.p1em": "after the device has started up",
  "settings.device.button.p2":
    "{short} and it erases nothing: it puts its own {ap} network on the air for a few "
    + "minutes. Useful when the device has lost the household Wi-Fi — but this password "
    + "still applies there. {gap} and nothing happens at all. That gap is deliberate: a "
    + "knock or a sleeve cannot erase anything.",
  "settings.device.button.p2short": "Let go before one second",
  "settings.device.button.p2gap": "Let go between one and five seconds",
  "settings.device.button.p3":
    "{soft}, while the light is amber, and it clears the Wi-Fi credentials and this "
    + "password together — and keeps the broker settings, the topic prefix, the device name "
    + "and the durations. It flashes three times, restarts, and comes back with that same "
    + "network on the air and this page open to anyone.",
  "settings.device.button.p3strong": "Let go after five seconds",
  "settings.device.button.p4":
    "{hard}, into the red, and it erases everything instead, broker settings included: "
    + "that step is for a device leaving the house. It does not tidy up after itself on the "
    + "broker, so Home Assistant keeps the device and its entities — point the device at a "
    + "different broker first, while it can still reach this one, if you want them gone. "
    + "Past twenty-five seconds it decides the button is stuck and does nothing at all; let "
    + "go and start again.",
  "settings.device.button.p4strong": "Keep holding past fifteen seconds",
  "settings.status.title": "Connection",
  "settings.status.noKnownGood":
    "This network has never produced an address, so there is nothing to roll back to if "
    + "the next one fails.",
  "settings.status.failure.notFound":
    "That network was not found. Check the name, and that it is a 2.4 GHz network — this "
    + "device has no 5 GHz radio.",
  "settings.status.failure.wrongPassword":
    "The network refused the key. Retype it; the stored one is never shown back.",
  "settings.status.failure.refused":
    "The router answered and turned the device away. Its access point may be full, or it "
    + "may be an enterprise network this form cannot express.",
  "settings.status.failure.noAddress":
    "The device joined the network and was never given an address. That is the router's "
    + "DHCP, not the password.",
  "settings.status.failure.unknown":
    "The device reported a failure this page does not have wording for: {failure} (reason "
    + "{reason}).",
  "settings.status.where.connected": "On {ssid}, at {ip}.",
  "settings.status.where.setupAp":
    "Serving its own setup network. Anyone in range can reach it, so writing is refused "
    + "until it joins a network.",
  "settings.status.where.trying": "Trying to join {ssid}.",
  "settings.status.where.none": "No network is configured.",
} satisfies Record<string, Message>;
