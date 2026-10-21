# 05 · Provisioning and recovery

Getting a device onto a network, the setup window, the short button press that reopens it, and the
names the device can be found by. The failure scenarios matter more than the happy path: they are
how an owner ends up locked out of a device that hangs behind the front panel of a running unit.
None of these items needs the unit. Long network outages are in [05b](./05b-network-outages).

## First setup on the access point

As-built: [Networking](/device/networking), [First setup](/guide/first-setup).

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-PROV-01 | A board with erased NVS, no USB host attached (power only). Apply power | Within 2 s the RGB LED is solid white and the blue status LED starts its 1 Hz heartbeat; within 10 s the RGB LED **blinks blue** and an **open** network `comfoair-XXXX` appears on the phone. On USB the log says `net: provisioning: nothing stored, access point 'comfoair-XXXX'` with the same `XXXX` | LED; phone Wi-Fi list; serial log | phone |
| HV-PROV-02 | Read two boot lines over USB: `net: names: access point 'comfoair-XXXX', id <12 hex>` and `comfoair_mdns: findable at http://comfoair-YYYY.local:80/ as '<name>' (5 TXT pairs, N octets)` | (a) `XXXX` and `YYYY` are the same string — two different producers print them, so a mismatch means one format was changed. (b) The `id` ends **one less** than `XXXX`: the SSID comes from the SoftAP MAC, the id from the STA MAC, and SoftAP = STA + 1 — by design, not a defect. (c) `id` equals `device_id` in `GET /api/status`; the default device name is `ComfoAir Q` plus the last four characters of `id`. (d) Record N; one packet's budget is 200 octets | serial log; `curl -s http://<ip>/api/status` | USB |
| HV-PROV-03 | An iPhone joins `comfoair-XXXX` | No captive-portal sheet opens. From a laptop on the same network, `curl -si http://192.168.4.1/hotspot-detect.html` answers exactly 69 bytes of body with `Cache-Control: no-store` | phone; `curl` | phone, laptop |
| HV-PROV-04 | An Android phone joins | It stays on the network (its connectivity check is answered `204`). Record what the system shows. A `POST /api/provision` from the phone's browser reaches the device (the save shows in the serial log) | phone; serial log | phone |
| HV-PROV-05 | A Windows 11 PC joins | No "sign in to network" flyout; any name typed in the address bar opens the device's page | laptop | Windows PC |
| HV-PROV-06 | On the access point, open Settings | The **Wi-Fi** card works: scan, pick, save. Saving any other setting is refused (`403`): while the open access point is on the air the device counts as unclaimed, and `POST /api/provision` is the only write | browser | phone |
| HV-PROV-07 | Access point on the air, no UI password set: `curl -si http://192.168.4.1/api/log` | `200` and the log ring — reads are open until a password exists, which is why log lines never quote a secret. Check the ring holds no Wi-Fi key or broker password | `curl` | laptop |
| HV-PROV-08 | Same conditions: open `ws://192.168.4.1/ws` from the phone's browser console | The socket **opens** and pushes the state: the WebSocket handshake is answered by the HTTP server itself and is not authenticated (a documented gap, with or without a password). Nothing can be written over it | browser console | phone |
| HV-PROV-09 | Fill the Wi-Fi card correctly and press **Save Wi-Fi and reconnect** | The page shows "Accepted — the device is trying now" **before** the phone loses the network; the RGB LED breathes blue (trial); when the router gives an address the access point goes off the air and the phase becomes `online` | browser; LED | phone, router |

## The setup window

The window is 15 minutes on a never-configured device and 5 minutes every later time; client
activity extends it up to 60 minutes from when it opened. As-built:
[setup window](/device/networking#setup-window), [the setup press](/device/button#the-setup-press).

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-PROV-10 | A fresh board nobody connects to. Wait 16 minutes | The access point is gone after 15 minutes; the log says `net: setup window closed`; the phase is `setup-expired` and the RGB LED is **solid blue**; the radio is off | phone Wi-Fi scanner; serial log; LED | phone |
| HV-PROV-11 | The board from HV-PROV-10. Power-cycle it | The network returns within 10 s and `GET /api/status` (from the access point) shows `setup_window_remaining_ms` ≈ **300000**, not 900000: the closed window was remembered across the power cut | `curl -s http://192.168.4.1/api/status` | phone or laptop |
| HV-PROV-12 | A device with credentials whose router is off (state `retrying`, access point down). Short press (under 1 s) | The access point is on the air within a few seconds; `comfoair_btn: setup window requested by button`; nothing is erased. **A short press, not a 5-second hold** — five seconds is the erasing soft reset | phone Wi-Fi list; serial log | phone, router |
| HV-PROV-13 | **A short press on a working device.** Device online, visible in Home Assistant, MQTT connected. Short press. Then, from the home network, `curl -si -X POST -u :<pw> http://<ip>/api/config -d '{}'` | `comfoair-XXXX` appears within about 2 s and: the station does **not** reconnect (same `ip`, no disconnect in the log), the MQTT session stays (no `offline`/`online` pair in the broker dump), Home Assistant never shows the device unavailable. The `POST` is `403` while the window runs — the device counts as unclaimed, from the LAN too. After 5 minutes the access point goes by itself; `state` stays `connected` and `phase` `online` — **not** `retrying` | `mosquitto_sub -v -t '<prefix>/status'`; `GET /api/status`; HA | phone, broker, HA |
| HV-PROV-14 | The way back when the name does not resolve (any failure in HV-PROV-19..21): short press, join `comfoair-XXXX`, open Settings → Connection. Then return to the home network and type the address shown. Let the window run out and reopen Settings at that address | The Connection card shows the device's current **home** IP address and network name; that address opens the same interface from home; after the window, Wi-Fi, UI password and broker are all still there — the press erased nothing. **Do not hold 5 s for this**: that erases exactly the Wi-Fi and password being looked for | browser | phone |
| HV-PROV-15 | A device with a network (5-minute window). Short press; a phone joins, opens the dashboard and lies screen-up, nothing touched. After 4 minutes read `GET /api/status` from a laptop on the same access point | `setup_window_remaining_ms` is back near **300000**, the access point is on the air and the phone's page is live: an open WebSocket counts as a person reading and extends the window | `curl` | phone, laptop |
| HV-PROV-16 | Continue HV-PROV-15: close the phone's tab (or lock it until the socket drops). Touch nothing for 6 minutes. Read `GET /api/status` from the home network | `comfoair-XXXX` is **not** on the air and `state` = `connected`. If the access point stays up, the device is counting a client that is gone, and the window would end only at the 60-minute ceiling — an open access point for an hour in an empty room | `curl`; phone Wi-Fi scanner | phone |

## Names on the home network

As-built: [names and identity](/device/networking#names-and-identity), [mDNS](/device/networking#mdns).

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-PROV-17 | The device joined the home network. 30 s later | The RGB LED gives a short **green flash every 10 s** (phase `online`); `GET /api/status` gives the address | LED; `curl` | — |
| HV-PROV-18 | Laptop on the same network: `dns-sd -B _http._tcp local` (or `avahi-browse -rt _http._tcp`) | The list shows **`ComfoAir Q XXXX`** — the device name, whose four digits come from the STA MAC (one less than the `.local` name) — not `comfoair-XXXX` | `dns-sd` | laptop |
| HV-PROV-19 | Open `http://comfoair-XXXX.local/` from: a Mac or iPhone; a Windows 10/11 PC **without Bonjour** (Edge); Android (Chrome); Linux with `avahi-daemon` (`avahi-resolve -n comfoair-XXXX.local`, then a browser) | Mac/iPhone: opens. Record the result for each other platform, with the **exact Windows version** and Android version — they are the only source for which versions resolve `.local`. On Linux, a name that `avahi-resolve` answers but the browser does not means `mdns4_minimal` is missing from `/etc/nsswitch.conf` — not a firmware defect | browser | laptop, phone, Windows PC |
| HV-PROV-20 | Router admin page, DHCP client list | The device is listed as `comfoair-XXXX`, not `espressif` | router UI | router |
| HV-PROV-21 | A phone on a guest network with client isolation (device on the main one); a phone on the second node of a mesh | Record. Under isolation the name **not** resolving is the expected result. Either failure is answered by HV-PROV-14 | browser | phone, router |
| HV-PROV-22 | Enter `homeassistant.local` as the broker host and save | Record whether it resolves (the IP stack sends `.local` queries by multicast itself). If it does not, it is a live defect to name separately | `GET /api/status` `mqtt.failure` (`dns` if not); `GET /api/log` | broker |
| HV-PROV-23 | `dns-sd -L "ComfoAir Q XXXX" _http._tcp` (quote the name; take it from HV-PROV-18) | TXT holds exactly `board`, `ver`, `id`, `path`, `api` — five pairs, never a sixth; no SSID, broker host or "password set" flag. `ver` equals the version on the first boot line; `id` equals `device_id` | `dns-sd` | laptop |
| HV-PROV-24 | Device on its own access point (short press), iPhone joined to it: open `http://comfoair-XXXX.local/`. Meanwhile resolve the name from a laptop on the home network | The iPhone opens the page — iOS resolves `.local` only by multicast, so this works only if mDNS answers on the access point too. Two **different** addresses for one name (one per interface) is normal for a multi-homed host, not a defect | browser; `dns-sd -G v4 comfoair-XXXX.local` | phone, laptop |
| HV-PROV-25 | Change the device's DHCP lease on the router (or shorten it and wait) **without** disassociating; resolve the name again | Record whether the name follows the new address. The firmware does not re-announce by itself; if it does not follow, the fix belongs in the mDNS component, not in the network callback | `dns-sd -G v4 comfoair-XXXX.local` | router, laptop |
| HV-PROV-26 | Rename the device (`device_name`) in Settings, no reboot. Browse again; reload `http://comfoair-XXXX.local/` | The browse list shows the **new** name; the `.local` host name is unchanged and the tab used for renaming keeps working | `dns-sd -B`; browser | laptop |

## Damaged settings storage — destructive

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-PROV-27 | Record every setting first (HV-SAFE-05). Erase one sector of the `nvs` partition: `esptool.py --chip esp32c6 erase_region 0x9000 0x1000`. Power on | The device boots — **no panic, no boot loop**. Record which path it took: `comfoair: NVS unusable (<err>); erasing` and a fresh setup access point (only `ESP_ERR_NVS_NO_FREE_PAGES` and `ESP_ERR_NVS_NEW_VERSION_FOUND` take that path), or a normal start with the keys of the lost page gone (`net: stored setting '…' was not usable and has been reset to its default`, or the setup access point if the Wi-Fi pair was lost). Any other `nvs_flash_init()` error stops the boot in `ESP_ERROR_CHECK` — if that is what happens, it is a finding | serial log | USB |
