# 05b · Network outages

Wi-Fi failures stretched over time: a changed Wi-Fi key, a router that is gone, a router that
reboots, a router that associates but hands out no address, a move to a new home, and power cuts.
The unit is not needed; a router you administer is. Several items take hours — plan them first in a
day, and use a phone Wi-Fi scanner in **continuous** mode, not a one-off list, whenever an item
says the access point must never appear.

The rule underneath every item: the device keeps retrying (1 s, 2 s, 4 s … capped at 30 s) and
never reboots for a missing network; after **30 minutes** of sustained `network-not-found`,
`wrong-password` or `refused` it raises its open recovery access point for 5 minutes beside the
station, then lowers it again, about every 35 minutes; `no-address` never raises it by itself.
As-built: [Networking](/device/networking#retries-and-failure-classes),
[access-point fallback](/device/networking#access-point-fallback),
[trial and rollback](/device/networking#trial-and-rollback).

`failure`, `failure_reason` (the raw Wi-Fi reason code), `state` and `phase` are read from
`GET /api/status` — over the home network while it works, otherwise from the recovery access point
or the serial console.

## The Wi-Fi key was changed on the router

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-NET-01 | Device online in Home Assistant. Change **only** the router's key (same SSID, band and channel) | Within about 3.5 minutes every entity in HA is unavailable (the broker publishes the will, `<prefix>/status` = `offline`, at 1.5 × the 120 s keepalive), and every state topic still holds its last retained value | HA; `mosquitto_sub -v -t '<prefix>/#'` | router, broker, HA |
| HV-NET-02 | Watch the status for a few minutes | `failure` = `wrong-password` with `failure_reason` 15 or 204 repeating (202 counts as `wrong-password` too, but on its own it is weak evidence about the key); attempts spaced 1, 2, 4, 8, 16, 30 s, never further | serial console (driver lines); `failure_reason` | router, USB |
| HV-NET-03 | Same state | Phase `net-rejected`: RGB LED **blue double pulse** (not red); the status LED keeps its 1 Hz heartbeat | LED; `phase` | router |
| HV-NET-04 | Leave it. Stopwatch from the key change | After 30 minutes the recovery access point `comfoair-XXXX` is on the air for 5 minutes, then off for about 30, and so on. Measure the actual first wait, the up time and the gap, and compare with `fallback_after_ms` (30 min) and `reopen_window_ms` (5 min) | phone Wi-Fi scanner (continuous); `fallback_ap_in_ms` | router, stopwatch, phone |
| HV-NET-05 | Recovery access point on the air. Join it and save the **new, correct** key | Within 90 s the access point is gone, the LED shows the green flash, and the HA entities come back with their retained values **without restarting Home Assistant** | LED; HA | router, phone, HA |
| HV-NET-06 | Repeat from HV-NET-01, but in the recovery window save the new key **with one wrong character** | After 90 s the trial rolls back to the last known-good pair: `net: the network just saved did not work; '<ssid>' put back` (that pair also fails now, because the key changed); the phase is `net-rejected` again; `wifi_ssid` in `GET /api/status` names the network | serial log; `GET /api/status` | router, phone, USB |
| HV-NET-07 | In the next recovery window, save the correct key | The device connects: the rollback did not lock the owner out of the next attempt | `GET /api/status` | router, phone |

## The network is gone for good

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-NET-08 | Device online on `home-old`. Switch the router off for good | `failure` = `network-not-found` (reason 200, then 201 repeating); in HA the entities go unavailable after the broker's keepalive; the ventilation keeps running; the LED shows the blue double pulse, nothing red | `GET /api/status` via USB; HA | router, HA |
| HV-NET-09 | 30 minutes later | Phase `recovery-ap`, RGB LED **blinking blue**, `comfoair-XXXX` in the network list, and the station keeps trying behind it | LED; phone Wi-Fi list | router, phone |
| HV-NET-10 | Join it, open Settings → Connection | The page shows the window's countdown, the failure `network-not-found` and the configured network `home-old` | browser | phone |
| HV-NET-11 | Leave the page open and touch nothing until the window ends | The access point goes off the air and the phase returns to `net-lost`; the phone drops off — expected, not a fault. Any HTTP request in between extends the window, never past 60 minutes from when it opened | phone; `GET /api/status` via USB | phone |
| HV-NET-12 | In the next window, enter `home-new` with a **wrong** key; wait 90 s | The trial fails; because `home-old` was known-good, it is put back: `net: the network just saved did not work; 'home-old' put back`, and the device goes on trying `home-old` | serial log; `GET /api/status` | router, phone, USB |
| HV-NET-13 | In the next window, enter `home-new` with the correct key | The phone loses the access point the moment the station moves to the new router's channel; phase `online`; HA entities come back, discovery is republished, `<prefix>/status` = `online` | HA; `mosquitto_sub` | router, phone, broker, HA |
| HV-NET-14 | Device online on `home-new`. Switch that router off | The cycle starts again with the **30-minute** wait, not with the shorter gap: the first wait and the gap are separate | phone Wi-Fi scanner; `fallback_ap_in_ms` | router, phone |

## The router reboots

False alarms are the main risk here: a router that reboots at night must not put an open access
point on the air.

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-NET-15 | Device online, HA showing all entities, broker log open. Pull the router's power for **4 minutes** | Within seconds: `state` = `retrying`, `failure` = `network-not-found`, phase `net-lost` and the blue double pulse — there is no hold-off on a Wi-Fi failure. The status LED keeps blinking throughout | LED; serial | router, USB |
| HV-NET-16 | Same outage. Note when the will arrives | The broker publishes `<prefix>/status` = `offline` at about T + 180 s (1.5 × the 120 s keepalive). Until then HA shows the old values; confirm by `last_updated` of any entity that nothing moved | broker log; HA | router, broker, HA |
| HV-NET-17 | Power the router back on | Association within 30 s of the router's beacons (the retry cap), address, then `mqtt.connected` = true within a few seconds; all entities return together | `GET /api/status`; HA | router, HA |
| HV-NET-18 | Two devices online. Reboot the router; record the broker's timestamps of each device's first publish | Record the gap. The MQTT retry ladder jitters every wait to 80–120 % by the device id, so two devices do not stay in lockstep across retries | broker log with timestamps | router, broker, second device |
| HV-NET-19 | The router reboots every 10 minutes for two hours (a timer socket; 12 cycles) | `comfoair-XXXX` is **never** on the air — each outage is shorter than 30 minutes. Check with the continuous scanner | phone Wi-Fi scanner | router, phone |
| HV-NET-20 | Straight after HV-NET-19, power-cycle the device | No access point at boot: the "no address" flag was not set by the reboots | phone Wi-Fi scanner | router, phone |
| HV-NET-21 | An outage of 35 minutes: switch the router off and watch | At 30 minutes the access point appears (blue blink) and the station keeps trying; switch the router back on at minute 33 — the device connects by itself without waiting for the window to end | LED; `GET /api/status` | router, phone |

## Associated, but no address

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-NET-22 | Exhaust the DHCP pool (or a MAC filter that admits association but no lease) with the device's network stored. Power on | Within about 35 s `failure` = `no-address` with `failure_reason` 0, and the access point does **not** come up — not after 35 minutes either: a router whose DHCP is still waking must not cause an open access point | `GET /api/status` via USB; phone Wi-Fi scanner | router, phone, USB |
| HV-NET-23 | After at least 30 **uninterrupted** minutes of `no-address`, power-cycle the device | Within seconds `comfoair-XXXX` comes up (the device boots in `fallback-ap`); `GET /api/status` on it shows `provisioned` false and `setup_window_remaining_ms` ≈ 300000; after 5 minutes the network goes. A power cut **before** the 30 minutes have passed restarts the clock, so the next boot raises nothing — the escape costs 30 minutes plus one reboot | phone; `curl -s http://192.168.4.1/api/status` | router, phone |
| HV-NET-24 | During that 5-minute window, admit the device's MAC on the router and touch nothing | The station gets an address and the access point goes down **at once**, not at the end of the window; the device appears in HA | phone; HA | router, phone, HA |
| HV-NET-25 | With the flag set (HV-NET-23), restore DHCP and power-cycle | The device connects; an access point raised at boot goes down as soon as the address arrives; a second power cycle raises none — the address cleared the flag | phone Wi-Fi scanner | router, phone |
| HV-NET-26 | Provision the device into a guest network with client isolation; a phone on the same guest network opens `http://<ip from the router's lease list>` | No connection, while the router shows an active lease and the device reports itself connected. A power cycle repeats it exactly. There is no way out without the button or USB — record it | browser; router UI | router, phone |
| HV-NET-27 | A router that answers DHCP only about 60 s after power-on. Power the router and the device on together | The device gets its address and comes online; a `no-address` verdict set at 30 s is cleared by the address. The 30-second address timeout must not form a loop that drops the station's own DHCP exchange every half minute | `GET /api/status`; serial | router |

## Moving: a different network without a reset

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-NET-28 | Device online for more than a minute (`wifi_known_good` true, `state` `connected`). In Settings → Wi-Fi enter an SSID that is not on the air and any key | The answer arrives **before** the connection drops and shows `state` `trial`; the next request fails | browser | — |
| HV-NET-29 | 90–120 s later, reload the page at the **old** address | It opens. The log has `net: the network just saved did not work; '<old ssid>' put back`; `wifi_ssid` in `GET /api/status` is the old network; no broker field changed | browser; `GET /api/log` | — |
| HV-NET-30 | The router is replaced: same network name, new key. Press Save on the Wi-Fi card **without touching the key field** | After 90 s the device rolls back to a pair that also fails; `failure` = `wrong-password`; about 30 minutes later the recovery access point comes up. The worst path of this scenario, and it must look exactly like this | `GET /api/status`; phone Wi-Fi scanner | router, phone |
| HV-NET-31 | The device is moved, powered off, to a new home where the old network does not exist. Stopwatch from power-on | The first `comfoair-XXXX` appears about 30 minutes after power-on (record the time). After provisioning the new network, the UI password, broker settings and topic prefix are unchanged — a move is not a change of owner | phone Wi-Fi scanner; `GET /api/config` | phone, stopwatch |

## Power cuts

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-NET-32 | Everything provisioned and working. Switch the house's main breaker off for 60 s and on again, stopwatch running | The RGB LED goes white, then blue, and shows **no amber or red in the first 5 minutes** (the start-up grace draws the broker and bus absences in blue); the status LED blinks from the first second; `comfoair-XXXX` never appears | LED; phone Wi-Fi scanner | router, broker, phone |
| HV-NET-33 | Same cut. Open HA as soon as it is up | The card is available with yesterday's numbers; within 5 minutes of power returning the numbers update, and "Visit device" on the card opens the web UI | HA | broker, HA |
| HV-NET-34 | Cut power to the **device only** (router, broker and HA stay up), then restore it | The device publishes to HA within about 15 s, raises no access point, and `mqtt.connected` turns true without any intervention | HA; `GET /api/status` | broker, HA |
