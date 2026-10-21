# 06b · Home Assistant discovery and availability

What Home Assistant ends up showing: the discovery documents, the two availability topics, when the
tree is republished, and what the device empties on the broker when it moves. The most likely
failure class here is **silent**: Home Assistant drops a discovery document its schema rejects, and
the entity simply never appears. So the items count entities rather than glance at the card.

As-built: [Home Assistant discovery](/interfaces/home-assistant-discovery),
[MQTT](/interfaces/mqtt#availability-status-and-bus).

## Discovery

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-HA-01 | Fresh broker, discovery on (the default). Connect the device; count. Then open HA and **count** the entities on the card | `mosquitto_sub -W 3 -t 'homeassistant/#' -v \| wc -l` gives **76**; the log says `mqtt: published discovery for 76 entities`. HA shows one device "ComfoAir Q xxxx", manufacturer Zehnder, **76** entities, 6 of them disabled by default (the six of [HV-CAN-21](./02-can-bus-and-polling)), **no** second card, Data Freshness under Diagnostics. Fewer than 76 means documents were rejected: read HA's log for `Invalid MQTT … configuration` before anything else, and attach the rejected config message | `mosquitto_sub`; `GET /api/log`; HA device page; HA log | broker, HA |
| HV-HA-02 | Take any document from the dump | It carries `"avty":[{"t":"<prefix>/status"},…]` and `"avty_mode":"all"`, and **no** `"avty_t"` — both keys in one document is rejected by HA's schema | `mosquitto_sub -v -t 'homeassistant/#' -W 3` | broker |
| HV-HA-03 | Measure the retained discovery tree | Record the total bytes of the 76 retained documents (expected order: about 35 KB) | `mosquitto_sub -W 3 -t 'homeassistant/+/<id>/#' \| wc -c` | broker |
| HV-HA-21 | Upgrading a Home Assistant that knew the schema-1 table: flash, run the cleanup of [HV-MQTT-43](./06-mqtt), then open the device page and **Developer tools → Statistics** | No entity of a retired key is left on the card. Bypass Override Active is a binary sensor whose entity id ends in `_bypass_override_active`; the old `bypass_override` sensor entity is gone with its config topic, or orphaned — remove it and repoint any automation or dashboard that named it. Statistics lists `exhaust_fan_speed` and `supply_fan_speed` under "units changed" (`RPM` → `rpm`): until that is fixed there, Home Assistant compiles no long-term statistics for the two fan speeds. Fix it (keep the history) and check that hourly statistics resume | HA device page; Developer tools → Statistics | broker, HA |

## Availability

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-HA-04 | Live bus: `mosquitto_sub -v -t '<prefix>/bus'`. Then cut the bus (or the unit) until `bus` goes `offline`, and **without touching the device** restart the broker. Then restore the bus and watch for an hour | One retained `online` while the bus is live. After the broker restart, `<prefix>/bus` arrives again on the new connection (the device does not assume the broker remembers). During the hour, `bus` is published **only** when the verdict changes — one message a second would be 3600 retained writes an hour on someone else's broker | `mosquitto_sub` | unit, broker |
| HV-HA-05 | Configure the broker with the CAN bus **physically disconnected** | The card appears complete. The unit's readings, the fan and the three sensor-ventilation selects are **unavailable** (the bus has never been heard, so `<prefix>/bus` = `offline`; the selects are unknown until the first unit read); the six device-held entities — the four durations, **Schema Version** (showing `2`) and **Data Freshness** (`Connected`) — are available. Schema Version or Data Freshness greyed out means the availability rule lost a branch | HA device card | broker, HA |
| HV-HA-06 | Device connected, bus live: `mosquitto_sub -v -t '<prefix>/data_freshness/state' -W 700`, stopwatch | `ON` at once on connect, then exactly every **300 ± 2 s**; no duplicate a second after the connect message. It is **not retained** (a retained copy would renew `expire_after` on every resubscribe) | `mosquitto_sub`; stopwatch | broker |
| HV-HA-07 | Unplug the **device** (not the broker, not the router) and leave it 20 minutes; watch the card | Within about 1.5 × the 120 s keepalive the broker publishes the will: `<prefix>/status` = `offline` and every entity goes unavailable. Data Freshness becomes unavailable at the latest 15 minutes (± 1) after its last `ON` on its own — the one signal that holds even if the will never fires | HA device card; `mosquitto_sub` | broker, HA |

## The unit's active errors

As-built: [Errors and alarms](/unit/areas/errors-and-alarms),
[The unit read](/protocol/probe#the-unit-read). **DO NOT** provoke an error to test these: open the
unit's front door (error 34; mains behind it), unplug a sensor, change installer settings or send
`82 03 01`. The items observe errors as they come.

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-HA-22 | Discovery on, unit healthy | Within about 70 s of boot HA shows **Unit Error** (problem: "OK") and, under Diagnostics, **Active Errors** = "No errors"; `GET /api/state` has `"unit_error": false, "active_errors": "No errors"`; `/api/system` `unit.errors.ids` = `[]`. With the unit's breaker off both go unavailable after about 90 s (the bus topic) | HA; REST | unit, broker, HA |
| HV-HA-23 | Observe, do not provoke: when the bypass is next overridden for the owner's own reasons (display or `set_bypass`) | If `/api/system` lists the manual-bypass id (89 "Bypass is in manual mode" on firmware above R1.4.0, 81 at R1.4.0 and below): within 60 s Active Errors carries its text and **Unit Error stays OFF** (no problem raised); both clear within 60 s after the override ends. If no id appears, record that a timed override raises no error on this unit's firmware — a fact about the unit, not a failure | `/api/system`; HA; stopwatch | unit, broker, HA |
| HV-HA-24 | Observe at the next real error — the filter pre-warning: when `filter_replacement_remaining_days` reaches the pre-warning period (Q350: 21 days, RMI [28.1.2](/unit/properties#prop-28-1-2), `1C/01/02`) | `/api/system` lists id 79 "Order new filters now, because the remaining filter life time is limited"; Active Errors carries the same text and **Unit Error stays OFF** (a routine reminder). At 0 days id 77 "The filters of the Ventilation Unit must be replaced now" appears and Unit Error turns ON. The display shows the warning. Record ids, texts and the display's wording — the error-text table is verified only here | `/api/system`; HA; display | unit, HA, weeks |

## When discovery is published again

Rule: the tree is owed (1) once per boot, (2) after every accepted `POST /api/config`, (3) after the
broker link dropped while the device **still had an IP address** — the broker may have lost its
retained store. A drop caused by the device's own Wi-Fi does not count. The observable is the line
`mqtt: published discovery for N entities`. As-built:
[When discovery is published](/interfaces/home-assistant-discovery#when-discovery-is-published).

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-HA-08 | Reboot the router (as in [HV-NET-15](./05b-network-outages)); after the device reconnects, look at the log and a dump on the broker | **No** `published discovery` line and no `homeassistant/…/config` in the dump — only birth, `bus`, subscriptions and states (at most about 79 messages and a few KB — against 76 more documents and ~35 KB more on a first connect). If the line does appear, the assumption that the address goes before the broker link is wrong on this hardware — record it | `GET /api/log`; `mosquitto_sub -v -t '#'` | router, broker |
| HV-HA-09 | Then press Save in Settings without changing anything | `published discovery for 76 entities` appears. A different N means the save caught a drop: look for `discovery stopped after M entities; it will be published again` | `GET /api/log` | broker |
| HV-HA-10 | **The most expensive item here, and its failure is invisible.** Leave the router and the device alone. On the broker: stop mosquitto, delete its persistence file (so the retained store is really gone), start it | The device reconnects and logs `published discovery for 76 entities`, and every entity returns in HA. No line means the device reconnected to an empty broker and said nothing: **nothing appears in HA and no error is shown anywhere** | `GET /api/log`; HA | broker, HA |
| HV-HA-11 | Restart mosquitto **with** its persistence file | The line appears as well — about 35 KB sent for nothing. This is the accepted cost (the device cannot tell the two restarts apart, and missing a republish is far worse); record it, change nothing | `GET /api/log` | broker |
| HV-HA-12 | Untick **Publish Home Assistant discovery**, save; tick it again, save | No `published discovery` line while unticked, including across reconnects; the second save gives N = 76. Unticking does **not** remove the device from HA — the retained documents stay. What removes it is a withdraw: changing **or clearing** the broker host while the old broker is connected (HV-HA-16, run with its backup), or clearing the `homeassistant/+/<id>/+/config` topics by hand | `GET /api/log`; HA | broker, HA |
| HV-HA-13 | The link on the card: power-cycle the device, then open the card's "Visit device". Next renew the lease so the address changes. Then leave the device 10 minutes without an address change. Finally switch Wi-Fi off at the router for 3 minutes and back, same address | After the power cycle, on the first pass with an address: `mqtt: device address is <ip>; Home Assistant is owed its card again` followed by `published discovery for 76 entities`, and the link works. After the address change the link leads to the **new** address. In the quiet 10 minutes exactly **one** `published discovery` line in total and no new config document on the broker. During the Wi-Fi gap the link stays on the card and no discovery line appears | `GET /api/log`; `mosquitto_sub -v -t 'homeassistant/#'`; HA | router, broker, HA |

## Cleanup when the device moves

Changing **only the prefix** is a *rekey* (state topics emptied, discovery rewritten in place,
entities keep their ids and history). Changing the **broker host or port**, or clearing the host, is
a *withdraw* (every discovery document emptied first, then the state topics) — it removes the device
from that broker's Home Assistant. Anything else empties nothing. As-built:
[Cleanup and retraction](/interfaces/home-assistant-discovery#cleanup-and-retraction).

::: danger Back up Home Assistant before HV-HA-16
A withdraw deletes the device and its 76 entities from a working Home Assistant on purpose. Entity
ids go with them, and dashboards, automations, scripts, templates and long-term statistics that name
them stop working silently. Pointing the device back recreates the entities, not necessarily under
the same ids and never with their history. Before HV-HA-16: (1) **Settings → System → Backups →
Create backup**, full, and download it off the HA machine; (2) copy `.storage/core.entity_registry`,
`.storage/core.device_registry`, `configuration.yaml`, `automations.yaml` and `scripts.yaml`;
(3) save what will be deleted: `mosquitto_sub -h A --retained-only -W 3 -v -t 'homeassistant/+/<id>/#' > ha-configs-before.txt`
and the same for `-t '<prefix>/#'`; (4) list what refers to the device's entities
(**Developer Tools → Template**, <code v-pre>{{ device_entities('&lt;ha device id&gt;') }}</code>). **No backup — do not run
HV-HA-16 or HV-HA-17.**
:::

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-HA-14 | Prefix `comfoair/<id>`, card alive in HA, dump of `-t '#'` running. Change **only** the prefix to `vent/<id>` and save | Answer immediate. Log `mqtt: broker mqtt://…, prefix 'vent/<id>', discovery on`. The dump shows exactly **80** empty retained messages under the old prefix (`status`, `bus`, 75 state topics and the fan's three leaves) and **none** under `homeassistant/#`. Afterwards `mosquitto_sub --retained-only -W 3 -v -t 'comfoair/<id>/#'` prints exactly one line, `comfoair/<id>/status offline` (the retraction empties it, then the client's deliberate disconnect writes `offline` once more). The new tree has `online`, the documents and the states. In HA the card is **not** duplicated, entity ids are unchanged and a temperature's history is continuous across the change — entities disappearing and reappearing would be a withdraw where a rekey belonged | `mosquitto_sub`; HA history | broker, HA |
| HV-HA-15 | Change only the device name; then press Save with nothing changed | No empty payload in the dump, under either tree — the name lives inside the discovery documents, which are rewritten anyway. The card's name changes; entity ids and history do not. HA may show a brief unavailability while the client rebuilds — the price of one rebuild path | `mosquitto_sub`; HA | broker, HA |
| HV-HA-16 | **Back up first (box above).** Device on broker A (live). Change the host to B. Watch the button, then press it again; stopwatch from the **second** press to `broker mqtt://B…` in the log | The form shows the red warning "Saving removes this device and its 76 entities from Home Assistant on A:1883" and the button changes to **Delete them and save**; the first press sends nothing; editing any field returns the button to "Save settings". After the second press the A dump shows, in order: 76 empty `homeassistant/…/config`, then 80 empty `<prefix>/…`, then `<prefix>/status offline` — config documents strictly before states. A fresh `-t '#'` subscription on A then shows only `<prefix>/status offline`. The device disappears from HA on A and does **not** return when HA on A is restarted; HA on B shows the same 76 entities with the same `unique_id`s and no history from A (expected). Record the time — under a second is typical; up to about 6.5 s plus client teardown is within bounds; tens of seconds means the 2 s retraction budget is not working. The web UI answers throughout and the device does not reboot | `mosquitto_sub` on A and B; `GET /api/log`; HA on A and B | 2 brokers, HA |
| HV-HA-17 | Broker A **off** (`mqtt.connected` false). Change the host to B but do not save yet; then save | Before saving, the form shows "Nothing can be cleaned up on A:1883 — the device is not connected to it", that the card and 76 entities stay in HA, and a ready command `mosquitto_sub -h A -p 1883 -W 3 --retained-only --remove-retained -t '<prefix>/#' -t 'homeassistant/+/<id>/#'` — read the second `-t` and confirm it names the **device id**, not `homeassistant/#` (which would wipe every other device). The button says "Save settings" and works on the first press. No `mqtt: retraction:` line; under a second from save to `broker mqtt://B…`. When A is back its retained tree is untouched — including `<prefix>/status online` from before, which nothing can correct until it is removed by hand | `GET /api/log`; `mosquitto_sub` on A | 2 brokers, HA |
| HV-HA-18 | Broker A in a black hole **after** the session is up (`iptables … -j DROP` on A, so the device still believes it is connected). Change the host to B, confirm (the warning may show the "could not read whether the device is connected" variant), stopwatch from the second press | At most ~4.5 s to the `retraction:` line and ~6.5 s to `broker mqtt://B…`. The line reads `mqtt: retraction: queued N of M under '<prefix>' in T ms; GAVE UP on the budget; outbox not drained` (or `outbox unknown`). **Read the last parts, not N**: at QoS 1 a publish is "queued" even into a dead session, so N = M proves nothing; `T` ≤ 4500 and `GAVE UP on the budget` prove the ceiling. `; the link went before the sweep did` instead of `GAVE UP` means the broker vanished mid-sweep — that run says nothing about the ceiling; repeat it | `GET /api/log`; stopwatch | 2 brokers |
| HV-HA-19 | Two devices on one broker (if available) | Their prefixes differ (they derive from the MAC); a command published to one never reaches the other, even when one prefix starts like the other | `mosquitto_sub`; `GET /api/log` on both | broker, second device |
| HV-HA-20 | A previous firmware for the same unit still running on another board and registered in HA with the same MAC connection | HA merges both onto **one** card (they share the MAC connection), the second set of entities suffixed `_2`. Expected, not a defect; remove the old device from HA once, after acceptance | HA device card | broker, HA, second board |
