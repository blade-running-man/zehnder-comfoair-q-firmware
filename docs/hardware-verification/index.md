# Hardware verification

A successful build is not readiness. The host suites prove the pure decisions — which frame a
command becomes, which phase a set of inputs gives, which topics a cleanup empties — but they do not
see a radio, a CAN transceiver, a broker, a Home Assistant install or a wall clock. Everything on
these pages is what only the real device on the real unit can show, and **none of it has been
walked yet**.

The scenarios are grouped by what the device does, not by when the code was written. Each one is
written against the firmware as it is today; where the as-built behaviour differs from what was
once expected, the item states the current behaviour.

## Status

| Scenario | Items | Status |
| --- | --- | --- |
| [00 · Setup and safety](./00-setup-and-safety) | 6 | not yet run |
| [01 · Boot and resilience](./01-boot-and-resilience) | 5 | not yet run |
| [02 · CAN bus and polling](./02-can-bus-and-polling) | 21 | not yet run |
| [03 · Web, API and access](./03-web-api-and-access) | 20 | not yet run |
| [04 · Commands](./04-commands) | 25 | not yet run |
| [05 · Provisioning and recovery](./05-provisioning-and-recovery) | 27 | not yet run |
| [05b · Network outages](./05b-network-outages) | 34 | not yet run |
| [06 · MQTT](./06-mqtt) | 43 | not yet run |
| [06b · Home Assistant discovery and availability](./06b-home-assistant) | 21 | not yet run |
| [07 · Indication and button](./07-indication-and-button) | 26 | not yet run |
| [08 · OTA and system](./08-ota-and-system) | 46 | not yet run |
| [09 · RMI probe](./09-rmi-probe) | 24 | not yet run |

The probe was run once outside these scenarios; that walk is recorded in the
[probe findings](/reference/rmi-probe-findings-q350). It answers the first question of
[09](./09-rmi-probe) for the owner's Q350 but does not tick any item.

## The item format

Every item has a stable ID, `HV-<AREA>-<nn>`. Code comments cite items by this ID, so an ID is
never renumbered or reused; an item that becomes obsolete is struck out in place with the reason.

| Area | Scenario |
| --- | --- |
| `SAFE` | setup and safety |
| `BOOT` | boot and resilience |
| `CAN` | CAN bus, polling, bus silence |
| `API` | web UI, REST, WebSocket, access policy |
| `CMD` | commands to the unit |
| `PROV` | provisioning, setup window, names, mDNS |
| `NET` | Wi-Fi and router outages |
| `MQTT` | broker connection, commands over MQTT, the reconnect ladder |
| `HA` | discovery, availability, cleanup on the broker |
| `LED` | RGB LED and heartbeat |
| `BTN` | the button and the two-level reset |
| `OTA` | firmware upload, rollback, installer |
| `PROBE` | the RMI probe |

Each table row is one item:

| Column | Content |
| --- | --- |
| **ID** | the stable ID |
| **Step** | what to do (Given / When) |
| **Expected** | what must be seen — an observation, not an opinion |
| **Observe** | where to look: an exact log line, a `curl`, a topic, a Home Assistant view |
| **Needs** | the equipment, see below |

Each section names the as-built page that describes the behaviour under test. In the commands,
`<ip>` is the device's address, `<id>` the twelve hex digits of `device_id`, `<prefix>` the
topic prefix (default `comfoair/<id>`), `XXXX` the four hex digits of the access-point name, and
`:<pw>` the UI password as Basic credentials (`curl -u :<pw>`), needed once a password is set.

## Recording a run

Keep a run log beside the device. For every item write **passed**, **failed** or **not run, and
why**, and the **actual number** where the item asks for one — half the items exist for the number,
not the tick. For a failure attach the serial log, `GET /api/log`, `GET /api/status` and, for MQTT
items, the `mosquitto_sub` dump at that moment. Update the status table above when a scenario has
been walked.

## Equipment

| Needs | What | Used by |
| --- | --- | --- |
| `USB` | M5Stack NanoC6 on a USB-C cable, serial monitor at 115200 (`~/.platformio/penv/bin/pio device monitor`) | everything |
| `unit` | the ComfoAir Q powered, CAN connected (GPIO2 TX / GPIO1 RX at 50 kbit/s through the CAN transceiver base — the bare C6 has no transceiver), its own display in view, its breaker reachable | 02, 04, 09, parts of 06 |
| `phone` | two phones of **different platforms** (iPhone and Android): they behave differently on an open access point and with `.local` names | 05 |
| `laptop` | macOS or Linux with `curl`, `dns-sd` or `avahi-browse`/`avahi-resolve`, `tcpdump`; optionally a Windows 10/11 PC **without Bonjour** | 03, 05 |
| `router` | admin access: DHCP lease list, DHCP off / pool exhausted, MAC filter, PSK change, a guest network with client isolation, a timer socket for repeated reboots | 05b |
| `broker` | mosquitto with `mosquitto_sub`/`mosquitto_pub`; a second test broker for the move-broker items | 06, 06b |
| `HA` | Home Assistant with the MQTT integration, and a **full backup** before 06b's withdraw items | 06, 06b |
| `stopwatch` | a stopwatch and a phone Wi-Fi scanner in **continuous** scan mode | 05b, 07 |
| `analyser` | a CAN analyser — optional; items marked with it are otherwise checked indirectly or recorded as not run | 02, 04 |
| `scope` / `meter` | an oscilloscope or logic analyser, a multimeter — optional | 07 |

## Order

Walk the scenarios in file order, with two exceptions that protect the rest of the run:

1. **[00](./00-setup-and-safety) first.** It holds the rules that keep the device and the unit
   intact.
2. **Destructive items last.** The two-level reset (the destructive section of
   [07](./07-indication-and-button)), the broker withdraw items in [06b](./06b-home-assistant)
   (they delete the device from a working Home Assistant — take a backup first), the NVS-corruption
   item in [05](./05-provisioning-and-recovery) and the permanent temperature profile in
   [04](./04-commands) change state that the other items rely on.

[05b](./05b-network-outages) is long by the calendar — several items take hours; plan them first in
a day. Items that need the unit are only in 02, 04, 09 and a few in 06; everything else can be done
on the bench with the board on USB.
