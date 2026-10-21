# Smart-home standards

The firmware speaks MQTT (with Home Assistant discovery), HTTP REST, a WebSocket push channel and
mDNS. This page answers the question an owner without Home Assistant asks — *will this work for
me?* — and records why the other smart-home standards are not implemented, with the numbers behind
each verdict.

Every figure below is marked **measured** (read from this repository or a build of it) or
**estimated** (not measured in this configuration; carried as an order of magnitude). No verdict
rests on an estimate alone.

## The budget every option is measured against

| Resource | Figure | Kind | Source |
| --- | --- | --- | --- |
| Board | M5Stack NanoC6: ESP32-C6FH4, 4 MB flash, no PSRAM documented | measured (vendor sheet) | [ESP32 nodes](/reference/esp32-node-hardware) |
| Application slot | 1984 KiB (`0x1F0000`), two slots for OTA | measured | `partitions.csv` |
| Application image | 1 341 564 bytes (≈ 1310 KiB, 66 % of the slot), leaving ≈ 674 KiB — **an earlier build, not the current image** | measured once, undated (it predates the repository's current history), PlatformIO's `Flash:` line for `m5stack-nanoc6`; the image has grown since with the web UI and features, so re-measure with `pio run -e m5stack-nanoc6` before relying on it | build output |
| NVS partition | 16 KiB (`0x4000`) | measured | `partitions.csv` |
| Spare flash above the slots | 64 KiB | measured | `partitions.csv` |
| Radios enabled | Wi-Fi; IEEE 802.15.4 enabled in Kconfig with no stack above it (`CONFIG_OPENTHREAD_ENABLED` unset, no Zigbee component); Bluetooth off (`CONFIG_BT_ENABLED` unset); software coexistence on | measured | `sdkconfig.m5stack-nanoc6` |

**NVS cannot grow.** It is 16 KiB so that `ota_0` starts at `0x10000`: PlatformIO assembles the
merged image with the application at `0x10000` regardless of the table, and a larger NVS pushes the
first app partition to `0x20000`, where `merge-bin` aborts with an overlap. That merged image is
what ESP Web Tools installs, so enlarging NVS breaks the owner's install path, not just a build
(the comment in `partitions.csv`).

## Verdicts

| Standard | What it would give the owner | Cost here | Verdict |
| --- | --- | --- | --- |
| **Plain MQTT, no discovery** | Every entity as a scalar topic for Node-RED, openHAB, ioBroker, Domoticz, Telegraf, Grafana | none — already the transport | **Provided.** See [below](#without-home-assistant) |
| **Home Assistant MQTT discovery** | The device appears by itself as one card | none on the device; about 35 KB retained on the broker (rendered on the host; HV-HA-03 measures it on a broker) | **Provided**, behind the `ha_discovery` switch |
| **mDNS / DNS-SD** | `comfoair-XXXX.local` and an `_http._tcp` service instead of hunting for the IP | the `espressif/mdns` managed component | **Provided** (`components/comfoair_mdns`) |
| **Matter over Wi-Fi** | Apple Home, Google Home, Alexa, SmartThings without a bridge | `esp-matter` +600…900 KiB flash (**estimated**), ≈ 100 KiB heap (**estimated**), BLE, a per-device attestation certificate, CSA membership | **No on this board** — [why](#matter) |
| **Matter over Thread** | The same, plus a low-power mesh | all of the above, plus OpenThread and a border router at the owner's | **No** — strictly more expensive than Matter over Wi-Fi for the same gain |
| **Zigbee (ZCL)** | Works with any Zigbee hub, without Wi-Fi or a broker | `esp-zigbee-sdk` (flash **not measured**), own partitions, and the setup access point removed | **Not in this firmware** — [why](#zigbee) |
| **Apple HomeKit (HAP)** | Apple Home and Siri directly | SRP, Ed25519/Curve25519, ChaCha20-Poly1305, pairing; hundreds of KB (**estimated**); MFi for commercial use; no ventilation profile in HAP | **No.** Matter has replaced HAP as the route for new devices, and the Home Assistant bridge gives the same result |
| **Amazon Alexa, Google Home (native)** | Voice control without Home Assistant | a cloud skill or action, account linking and hosting for ever; neither has a local device protocol | **Never.** Native support is a hosting budget, not firmware, and the project has no cloud |
| **SSDP / UPnP** | An icon in Windows' network view | a few KB | **No.** Cheap and useless here: no target client discovers devices this way, and mDNS covers the same need |
| **KNX (KNXnet/IP)** | The unit on a KNX installation without a gateway | tunnelling or routing on UDP 3671, a group-address model, KNX Association membership and an ETS catalogue entry | **No.** A KNX installation already has an IP gateway, and MQTT-to-KNX bridges exist in software |
| **Modbus TCP** | Building management systems, Loxone, industrial controllers | a register map and a server on 502/tcp; open specification, no fees, no certification | **Candidate for a later release** — only with the register map generated from `tools/pdo_table.py`, never as a second hand-written entity list |
| **Modbus RTU** | The same, for panels without Ethernet | an RS-485 transceiver the board does not have | **No.** Missing hardware. The unit itself has no Modbus or RS-485 terminals either ([ComfoAir Q hardware](/reference/comfoair-q-hardware)) |

Alexa and Google Home are "never" for a reason that is not cost: neither talks to a device
locally. A cloud skill talks to Alexa and then reaches the device, so "native Alexa support" is a
line in a hosting budget, not a line in the firmware.

## Matter {#matter}

Four facts carry the verdict; three are structural or measured, and none depends on the estimate.

1. **Flash (estimated increment, measured base).** `esp-matter` on the C6, on top of the Wi-Fi,
   lwIP and mbedTLS already paid for, is estimated at +600…900 KiB. Against the measured image of
   ≈ 1310 KiB that is ≈ 1910 KiB at the low end — 96 % of the 1984 KiB slot, with no room left for
   a growing web UI — and does not fit at the high end. The conclusion holds at either bound.
2. **NVS (measured, and fixed).** Matter keeps fabric tables, per-fabric certificates, ACLs,
   bindings and group keys in persistent storage. 16 KiB is far too little (the exact minimum is
   not measured), and NVS cannot grow without breaking the ESP Web Tools image (see above).
3. **One public binary (structural).** Every Matter device needs its own Device Attestation
   Certificate, chained to a CSA-authorised authority and stored in a factory partition whose
   content is unique per device. That is incompatible with one public `.bin` that every owner
   installs from the browser. A Vendor ID is issued only to CSA members, and the test VIDs are
   forbidden in production.
4. **Commissioning collides with the setup access point (structural).** Matter commissions over
   BLE (off in this build, and undocumented by the board vendor) or on the network — which here
   would mean provisioning Wi-Fi through the captive portal and then commissioning a second time.
   Matter also assumes no open configuration access point after commissioning, while this
   firmware's fallback access point returns as a duty cycle by design.

Whether uncertified controllers would accept a test-certificate device is not verified and carries
no weight in the verdict. Matter is reconsidered only on a board with 8 MB of flash. (The 8 MB board
in the [ESP32 nodes](/reference/esp32-node-hardware) reference, the AtomS3, has no 802.15.4 radio, and
this firmware has no build for it: `platformio.ini` defines only `m5stack-nanoc6` and the host
`native` environment. The NanoC6 has the radio and 4 MB. The radio path is open; the application
slot is not.)

## Zigbee {#zigbee}

The silicon is not the obstacle: Wi-Fi, including an access point, can run beside Zigbee on the
C6, and this build already enables the coexistence arbiter (`CONFIG_ESP_COEX_SW_COEXIST_ENABLE`).
The reason Zigbee is not in this firmware is architectural:

1. **The access point is the whole onboarding.** The firmware has no other way to receive Wi-Fi
   credentials than its open `comfoair-XXXX` access point with the captive portal. Removing it
   removes `comfoair_provision`, `comfoair_captive` and the entry point of `comfoair_net`, and with
   them any way to reach the web UI.
2. **Zigbee beside Wi-Fi keeps the costs of both.** Zigbee's main gain is that the secret problem
   disappears — no Wi-Fi credentials, no broker password, no open access point. Keeping Wi-Fi for
   the web UI keeps all of that and adds a second radio stack sharing the air.
3. **Flash is unmeasured and has to fit beside ≈ 1310 KiB**, and Zigbee needs its own partitions
   (`zb_storage`, `zb_fct`) where 64 KiB are spare.

Zigbee is a different product: `zehnder-comfoair-q-zigbee` carries the protocol layer
(`comfoair_protocol`, `comfoair_registry`, `tools/pdo_table.py`) across unchanged, which is the
isolation this firmware already keeps for other boards.

## Without Home Assistant {#without-home-assistant}

An owner without Home Assistant gets a working device, not a kit. Nothing below needs new code.

- **MQTT as a contract of its own.** Every entity publishes a bare scalar on
  `<prefix>/<key>/state` — a number as a number, a boolean as `ON`/`OFF`, text as the bare label —
  so `mosquitto_sub -t 'comfoair/#' -v` is a readable dump and Node-RED, openHAB, ioBroker,
  Domoticz, Telegraf or Grafana read any topic without a parser. A missing reading is not published
  at all, so a graph does not get a false gap. Commands go to `<prefix>/<key>/command`, the fan
  leaves and `<prefix>/ops/<name>/set`. See [MQTT](/interfaces/mqtt).
- **What MQTT alone does not carry:** units, device classes, icons, display names, option lists,
  ranges and the grouping into one device live only in discovery documents, and there is no topic
  listing the keys. All of it is served by `GET /api/entities` (see [REST API](/interfaces/rest-api)),
  generated from the same `tools/pdo_table.py`.
- **REST and the WebSocket** give the same values and the same commands without a broker, and
  mDNS makes the device findable as `comfoair-XXXX.local`.
- **Apple Home, Google Home or Alexa** are reached through a bridge rather than natively: Home
  Assistant (or another hub that reads MQTT) carries the certification, keeps one integration
  instead of four, and maps the unit's rich model — Auto/Away/Low/Medium/High/Boost with durations
  — onto each ecosystem's poorer one once, in its own configuration, instead of fixing a lossy
  mapping into the firmware for ever. An owner who will never run any hub is a different audience,
  and the answer for them is a different product, not Matter here.

Two topics are deliberately not provided:

- **`<prefix>/entities`** (a self-describing index). The entity document is tens of kilobytes as
  the host renders it, many times the 1536-byte MQTT buffer, so the renderer would return nothing; providing it means either a new static buffer of
  that size or a chunked renderer. `GET /api/entities` already serves the same content.
- **`<prefix>/diagnostics`.** REST and MQTT must cover the same *operations*, and `GET /api/status`
  is a projection, not an operation. A continuously changing retained topic would also need a rate
  rule nothing here could justify.
