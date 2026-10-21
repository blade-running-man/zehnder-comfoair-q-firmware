# 03 · Web, API and access

The web UI served from flash, the REST documents, the WebSocket push, the status document's
diagnostic fields, the access policy, the UI's language and theme switches, and many connections at once. Most items run on the bench; the ones that compare values
need the unit. Run the access items twice where they say so: once on the device's own access point
(unprovisioned) and once on the home network with a UI password set.

## The web UI from flash

As-built: [Web UI](/interfaces/web-ui), [REST API](/interfaces/rest-api#the-get-fallback-and-captive-probes).

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-API-01 | Open `http://<ip>/` (on the setup network `http://192.168.4.1/`) | The dashboard shows **real readings** of the unit, not placeholders; the tab title is "ComfoAir Q"; the connection badge says **Live**; a value not yet reported shows a dash. Change the level at the unit: the page follows by itself, without a reload | browser | unit |
| HV-API-02 | Open `http://<ip>/settings` directly (a deep link); then `curl -sI http://<ip>/app.js \| grep -i content-encoding`; then `curl -sI 'http://<ip>/app.js?v=2' \| head -1` | The deep link is not a 404 — the same shell is served and the SPA routes it; `Content-Encoding: gzip`; the query-string request is `200` and is `app.js`, not the HTML shell | browser; `curl` | — |

## REST documents

As-built: [REST API](/interfaces/rest-api#state-and-entities).

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-API-03 | `curl -s http://<ip>/api/state \| head -c 600`; then the same with `?x=1` | A valid JSON object with numbers for readings and `null` for entities the unit has not sent (`null` means "exists, no reading"). The query string changes nothing | `curl` | unit |
| HV-API-04 | `curl -s http://<ip>/api/entities \| head -c 600` | An array; every element has `key`, `name`, `kind`, `pdo`, `min`, `max`, `writable` (fourteen members in all); an entity without a range has `"min":null`, and `away_duration` has `"max":43200`. A `0` where `null` belongs means REST and the web types disagree | `curl` | — |
| HV-API-05 | `curl -s http://<ip>/api/entities/supply_air_temp_post_heater`; `curl -si http://<ip>/api/entities/no_such_thing` | The first: `key`, `name`, `unit`, `value`. The second: `404 {"error":"unknown entity"}`. A known entity without a reading is `200` with `"value":null` — a typo and a silent bus are different answers | `curl` | — |
| HV-API-06 | `curl -s http://<ip>/api/state \| grep -o '"airflow_constraints":"[^"]*"'` | A string, not `null`: `No restrictions` or a list of reasons such as `Below: Frost Protection` | `curl` | unit |
| HV-API-07 | `curl -s http://<ip>/api/log` | A JSON array of the last 80 log lines, the same lines the serial monitor printed — the only way to read the log without USB | `curl` | — |
| HV-API-08 | With a broker user and password and a UI password set, fetch `/api/state`, `/api/entities`, `/api/status`, `/api/log` and `/api/config` and search each for the actual broker password, broker user name, Wi-Fi key and UI password | None of the four values appears anywhere — not as a value, not as a length. `/api/config` shows `"__UNCHANGED__"` for each stored secret | `curl -s -u :<pw> http://<ip>/api/<doc> \| grep -c '<the secret>'` → 0 for each | broker |
| HV-API-21 | With the broker connected: `curl -s http://<ip>/api/state`, then `mosquitto_sub -W 3 --retained-only -v -t '<prefix>/#'` | For every key `GET /api/state` reports as `null`, the retained `<prefix>/<key>/state` is `None`; with `"fan": null` all three fan leaves are `None`. A key that is `null` in REST and a number on the broker is a defect | `curl`; `mosquitto_sub` | broker |

## The status document

As-built: [REST API](/interfaces/rest-api#get-api-status), [MQTT](/interfaces/mqtt#reconnect-and-backoff).

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-API-09 | On the home network: `curl -s http://<ip>/api/status \| python3 -m json.tool \| grep rssi`; then carry the device to the edge of coverage and repeat | A number between −30 and −90, never `null` while associated; at the edge it drops by at least 10 dB. `null` on an associated device means the radio query failed — the field would be useless exactly where it is needed | `curl` | — |
| HV-API-10 | Read `uptime_ms` twice exactly 60 s apart (stopwatch); also once within 10 s of a reboot | It grows by 60 000 ± 2000 (not 60, not 0); right after boot it is in the thousands, matching the stopwatch from power-on to within 2 s. With the setup window open, `setup_window_remaining_ms` falls by the same amount — one clock feeds both | `curl -s http://<ip>/api/status` | stopwatch |
| HV-API-11 | No broker configured (a fresh device): read the `mqtt` block | The block is present with `"configured":false`, `"connected":false`, `"link":"off"`. A missing block, or `link` not `off`, is a defect | `curl` | — |
| HV-API-12 | Broker configured and up: read the block twice | `"link":"online"`, `"failure":"none"`, `"retry_in_ms":null`, `attempts` unchanged between the reads | `curl` | broker |
| HV-API-13 | Open Settings with the browser's network tab open; compare the `/api/status` response body with `curl` | Byte for byte the same document — the page renders the device's answer, not a second rendering of its own | browser dev tools | — |

## Access policy

As-built: [REST API](/interfaces/rest-api#authentication-and-access), [Security](/architecture/security).

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-API-14 | On the setup access point (device unprovisioned, no UI password): `curl -si -X POST http://192.168.4.1/api/ota`; `-X POST …/api/config`; `-X DELETE …/api/state`; `curl -si …/api/state` | `403 {"error":"forbidden"}`, `403`, `403`, and `200` for the read. Reading is open; on the open access point the only write accepted is `POST /api/provision` — proximity to the antenna authorises nothing. **If any of the writes answers 200, stop and report it** | `curl` | phone or laptop on the AP |
| HV-API-15 | On the home network with a UI password set: `curl -si -X POST http://<ip>/api/config -d '{}'` and `curl -si http://<ip>/api/status`, both without credentials; then `curl -si -u :wrong http://<ip>/api/status` | `401` with `WWW-Authenticate: Basic realm="comfoair"` and `{"error":"unauthorized"}` for both; the wrong password is also `401`, answered after about one second | `curl` | — |

## WebSocket push

As-built: [WebSocket](/interfaces/websocket).

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-API-16 | In the browser console on the device's page: `const ws = new WebSocket('ws://' + location.host + '/ws'); ws.onmessage = e => { const m = JSON.parse(e.data); console.log(Date.now(), m.type, e.data.length); }`. Watch a minute; change the level at the unit | The first frame is one `state` frame with the whole model; after it only `delta` frames with just the changed keys; an unchanged value produces no frame; a `ping` every 15 s when nothing changes; frames never closer than 250 ms, even during a burst of PDOs. Record how many frames came in a minute | browser console | unit |
| HV-API-17 | Close and reopen the tab five times in a row | Push keeps working each time: the seven sockets are freed. Push that dies after a few reloads means dead sockets are not released | browser | — |

## Language and theme

Both choices live in the browser, not on the device: `localStorage` keys `comfoair.lang` (`en`,
`de`, `nl`, `uk`) and `theme` (`dark`, `light`). Neither is guessed — no `navigator.language`, no
`prefers-color-scheme` — so an unset value is English and dark. `localStorage` is per origin, so
`http://<ip>/` and the `.local` name keep separate choices. As-built: [Web UI](/interfaces/web-ui#languages),
`web/src/i18n/index.ts`, `web/src/components/Nav/Nav.tsx`.

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-API-18 | In a fresh private window open `http://<ip>/`. Pick each of `DE`, `NL`, `UA`, `EN` in the navigation bar's language switcher; after each, reload the page. After the `UA` pick and again after the final `EN`, run in the console: `localStorage.getItem('comfoair.lang')`, `document.documentElement.lang` | The page is English at first. Each pick re-labels the navigation, the page and the entity names at once, without a reload, and survives the reload. Both console values read `uk` after `UA` (the code, not the label) and `en` after `EN`. Log lines and Home Assistant stay English — the translation is web-only | browser; dev-tools console | — |
| HV-API-19 | In a fresh private window press the theme button at the right end of the navigation bar (☀ in dark, ☽ in light); reload; then `localStorage.getItem('theme')` | The page opens dark whatever the OS setting is. The button flips the page between dark and light at once, the choice survives the reload, and the console shows `light` or `dark` to match | browser; dev-tools console | — |

## Many connections at once

As-built: [WebSocket](/interfaces/websocket#limits).

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-API-20 | With the broker connected (`curl -s http://<ip>/api/status` shows `"connected":true` in the `mqtt` block) and Home Assistant's integration polling the device — or, without it, two `while true; do curl -s http://<ip>/api/state > /dev/null; done` loops in two terminals — open the device's page in six browser tabs one after another, then reload each tab once. Finally `curl -s http://<ip>/api/log \| grep -c 'error in accept'` | Every tab loads, its connection badge says **Live**, and keeps following the unit; the reloads load too. A tab may briefly lose an idle socket to the least-recently-used purge and reconnect on its own, but no page load hangs or fails. The `grep` count is `0`: not one `httpd_accept_conn: error in accept (23)` line in the log (with the old ten-socket table this line filled most of the 80-line ring) | browser; `curl`; `GET /api/log` | broker, HA or laptop |
