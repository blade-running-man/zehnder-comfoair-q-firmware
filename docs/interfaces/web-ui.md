# Web UI

The device serves its own single-page web UI from flash. It is a client of the public API and
nothing more: everything it shows comes from the [REST API](/interfaces/rest-api) and the
[WebSocket](/interfaces/websocket), and everything it does is a request any other client can make
with `curl`.

## Stack and build

| | |
| --- | --- |
| Framework | Preact 11, `preact-router` 4, `@preact/signals` 2 |
| Language | TypeScript |
| Bundler | Vite, output as one IIFE script (`app.js`) and one stylesheet (`style.css`) |
| Source | `web/src/` |
| Output | `web/dist/index.html.gz`, `app.js.gz`, `style.css.gz` — gzipped at build time |
| Embedding | `components/web_assets`, via `EMBED_FILES` |

The SPA build is part of the firmware build. `components/web_assets/CMakeLists.txt` runs `npm ci`
when `web/node_modules` is missing and `npm run build` at CMake configure time, then embeds the
three gzipped files; a custom target rebuilds them on every firmware build. A firmware build
therefore needs Node (22.6 or newer, per `web/package.json`). See
[Building](/development/building).

On the device the three files are served at `/`, `/app.js` and `/style.css` with
`Content-Encoding: gzip`. Any other non-API GET returns the shell (`/`), so the client-side routes
below survive a reload. Nothing is compressed at runtime.

## Pages

Routes are declared in `web/src/main.tsx`; the navigation bar is `web/src/components/Nav`, its
English labels in `web/src/i18n/catalogs/en/nav.ts`.

| Route | Nav label | Page | What it uses |
| --- | --- | --- | --- |
| `/` | Home | `pages/dashboard` — the default screen: a scene of the unit, airflow, temperatures, filter wear and the everyday controls | `/ws`, `GET /api/state`, `POST /api/entities/{key}`, `GET /api/status` (the broker alert) |
| `/diagnostics` | Diagnostics | `pages/state` — every entity the unit reports, grouped and driven by the registry, plus the broker status | `/ws`, `GET /api/entities`, `GET /api/config`, `GET /api/status`, `POST /api/entities/{key}` |
| `/capabilities` | Unit capabilities | `pages/probe` — start the read-only RMI walk and read its report, polled every 2 s while running | `POST /api/probe`, `GET /api/probe` |
| `/system` | System | `pages/system` — unit identity, device, Wi-Fi, CAN, command queue and MQTT counters, polled every 5 s while visible | `GET /api/system` |
| `/settings` | Settings | `pages/settings` — Wi-Fi, broker, device name, discovery flag, UI password, permanent manual mode | `GET`/`POST /api/config`, `POST /api/provision`, `GET /api/wifi/scan`, `GET /api/status` |
| `/log` | Log | `pages/log` — the device's 80-line log ring, fetched on demand, not live | `GET /api/log` |
| `/update` | Update | `pages/update` — pick `firmware.bin`, check it in the browser, upload it, wait for the device to return | `GET /api/config` (is a password set), `POST /api/ota`, `GET /api/status` |

Any unknown route falls back to the dashboard.

The UI opens **one** WebSocket for the life of the page (`connectDevice()` in
`web/src/api/device.ts`, called once from `main.tsx`), not one per screen, because the device has
seven sockets in total. When the socket opens it also fetches
`GET /api/state`, so the page is filled even if the device's own full frame was not sent to it.
Readings stay on screen across a reconnect; a connection badge says whether they are live.

Every page carries a faint watermark in the bottom-right corner: the running firmware's version,
`sw_version` from `GET /api/status` (`web/src/components/Watermark`). It is the device's own
answer, not a string built into the SPA, so it cannot disagree with the git tag the image was
built from (see [Releasing](/development/releasing#the-version)). It is read again every time the
socket opens, because an update reboots the device without reloading the page.

The settings page keeps two forms with two buttons on purpose: the Wi-Fi form posts to
`/api/provision` and takes the device off the network the page is being served over, while the
other settings post to `/api/config`. Secret fields show the sentinel `__UNCHANGED__` as "set".
The settings form sends the broker and UI passwords back as the sentinel unless the owner types a
new value; the Wi-Fi form leaves `wifi_psk` out instead (`web/src/pages/settings/model.ts`,
`wifi.ts`; see [write-only secrets](/interfaces/rest-api#write-only-secrets-and-the-read-sentinel)).

## No privileged endpoint

The UI has no route, header, cookie or token that the public API does not offer to everyone else.
`web/src/api/client.ts` holds the REST calls, and its header says so: "there is no header, cookie
or route here that curl cannot use". The other device calls sit beside it in `web/src/api/`
(`ws.ts` for the socket, `ota.ts`, `probe.ts`, `system.ts`) and use the same public routes. The
dev server proxies exactly two prefixes, `/api` and `/ws` (`proxyRoutes` in `web/vite.config.ts`).

When a UI password is set, every unauthenticated request — including the HTML shell — answers
`401` with `WWW-Authenticate: Basic`, the browser shows its own login prompt, and from then on
attaches the credentials to every same-origin request. There is no login page and no session. Two
paths are exempt: the captive-portal probe paths answer their fixed strings without a check, and
the WebSocket upgrade on `/ws` is answered by `esp_http_server` itself, so the push stream is open
to anyone who can reach the device (see [Security](/architecture/security)).

Writes go over REST only. The WebSocket is push-only, so the UI cannot do anything over it that a
REST client cannot. The UI controls the unit through entity writes; it has a client for the six
operations (`runOperation()` in `web/src/api/client.ts`) but no page calls it today — the
operations are used from Home Assistant and other clients.

## Languages

The UI is available in English, German, Dutch and Ukrainian (`web/src/i18n`, catalogs under
`catalogs/en`, `de`, `nl`, `uk`), switched from the navigation bar. The choice is stored in
`localStorage` under `comfoair.lang`; there is no auto-detection, and an unset or unknown value is
English.

Entity display names in the four languages come from `web/src/i18n/entityNames.generated.ts`,
generated by `tools/generate_registry.py` from `tools/pdo_table.py` plus
`tools/entity_names_i18n.py`. English is the canonical name and the one Home Assistant receives
through discovery; the translations are a web-only overlay. The firmware itself — log lines, error
messages, discovery — stays English.

## Development

```sh
cd web
npm install
npm run dev          # Vite dev server, proxies /api and /ws to a real device
npm run dev:mock     # the whole device API served in-process, no device needed
npm run build        # tsc -b && vite build -> web/dist
npm test             # the web test suites (scripts/test.mjs)
```

`npm run dev` proxies `/api` and `/ws` (with WebSocket upgrade) to `http://$VITE_DEVICE_IP`. Set
the device's address in `web/.env.local`:

```sh
VITE_DEVICE_IP=192.168.1.42
```

Without it the proxy targets a hard-coded default address in `web/vite.config.ts`, which is almost
certainly not your device. `npm run dev:mock` replaces the proxy with the mock in `web/mock/`, which
answers the same routes; it is never part of a production build.

The entity types the UI uses (`web/src/api/entities.ts`) are generated from the same registry as the
firmware and must not be edited by hand; see [Entity registry](/development/entity-registry).
The configuration and status shapes in `client.ts` and `status.ts` are hand-written: they describe
the device's settings, not entities.
