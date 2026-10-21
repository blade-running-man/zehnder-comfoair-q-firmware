# Mock ComfoAir Q

Local UI development with no device and no reflashing. `npm run dev:mock` starts the SPA (with
HMR) and an in-process simulated ComfoAir Q that serves the firmware's own HTTP contract — `/api/*`,
the `/ws` push socket and the Update page's `POST /api/ota` — from an evolving model.

```sh
npm run dev:mock                         # = vite --mode mock
MOCK_SCENARIO=cooling npm run dev:mock   # start in a preset other than "heating"
```

Open the printed URL. No `.env.local` and no proxy are involved: in mock mode `vite.config.ts`
drops the device proxy and adds `mock/plugin.ts` instead. Plain `npm run dev` still talks to a real
device through `VITE_DEVICE_IP`. Nothing under `mock/` is imported by `src/`, and the plugin is
added only in mode `mock` (and is `apply: "serve"`), so `npm run build` output is unaffected.

## What it serves

The same routes as `components/comfoair_http/`, with its status codes and its error
sentences byte for byte (the SPA translates them by exact match — `src/i18n/firmwareText.ts`):

| Route | Answer |
| --- | --- |
| `GET /api/state` | every registry key, `null` = no reading yet |
| `GET /api/entities`, `GET /api/entities/<key>` | the generated registry; `404 unknown entity` |
| `POST /api/entities/<key>` | `{"value": …}` judged against the registry: `404 No such writable entity.` (read-only or unknown), `422` bad value / bad field, `503` when the bus refuses, else `{"accepted":true}` |
| `POST /api/ops/<name>` | `boost`, `set_bypass`, `set_balance_mode`, `set_ventilation_level`, `set_temperature_profile`, `set_away` — every parameter required |
| `GET /api/config`, `POST /api/config` | secrets read back as `__UNCHANGED__` (stored) or `""`; the sentinel keeps, `""` clears; Wi-Fi keys refused with the route that works; `409 read-only` |
| `POST /api/provision` | three `wifi_psk` states, the sentinel refused; answers before "joining", which completes ~4 s later |
| `GET /api/wifi/scan` | a sweep with duplicates and a hidden network, or `503 scan unavailable` |
| `GET /api/status`, `GET /api/log` | the provisioning/broker document; the log ring |
| `/ws` | `state` on connect, `delta` as the unit changes, `ping` after 15 s of quiet |
| `POST /api/probe`, `GET /api/probe` | the RMI probe: `{}` (or `node`/`another_counter`) starts a 10 s simulated walk — `202`, `409` while one runs, `413` empty body, `422` bad field, `503` on `bus_down`; GET returns the growing report until `done` (`mock/probeData.ts` is the content) |
| `GET /api/system` | the System page's summary: the live Q350's identity (read 10 s after boot, then every 6 h, like the device), the nanoc6 device block, network, bus, commands, MQTT — see the `unitInfo` knob (`mock/system.ts`) |
| `POST /api/ota` | the raw image (`application/octet-stream`). Behind the policy: `403` with no UI password, `401` without the login. Then `comfoair_ota.c`'s pre-flight and sentences: `409` while the last image is on probation (30 s after it boots), `411` no body, `422` under 64 KiB, `413` over the 1984 KiB slot, `422` when byte 0 is not `0xE9`, `422` "This file is firmware for a different device, not the ComfoAir Q controller." when the app descriptor at `0x20` (magic `0xABCD5432`, `project_name` at `0x50`) is missing or names another project (the chip id is not checked). `200 {"ok":true,"rebooting":true,"partition":"ota_1","bytes":N,"version":"<image version>"}`, then every connection is dropped for 5 s and the device comes back running the image's own version (from `0x30`; the mock starts as `0.1.0`) on the other slot (`/api/status` `sw_version`, `/api/system` `firmware`/`partition`) — see the `ota` knob (`mock/ota.ts`) |

The access policy is the firmware's: reads open until a UI password exists (then `401` with
`WWW-Authenticate: Basic realm="comfoair"`; any user name, the password is what counts), commands
and `/api/config` allowed without a password, everything else `403`; an unprovisioned device takes
`POST /api/provision` only. Setting a UI password on the Settings page locks the mock the way it
locks the device.

## The model

Once a second (`mock/evolve.ts`, time accelerated): the outdoor air swings ±2 °C over ten minutes;
the fans follow the ventilation level (Auto picks Low, or Medium when the extract air is humid);
supply/exhaust temperatures follow the exchanger (efficiency falls with flow, the bypass skips it,
an unbalanced pair recovers less); humidity conserves absolute water content, so warmed winter air
reads dry; the bypass in Auto opens for free cooling; the pre-heater holds intake air at −4 °C;
power, avoided heating/cooling and the energy counters follow. Boost, bypass override, balance and
away count down on their PDOs and release their mode at zero.

## Scenarios — switching live

`POST /__mock/scenario` (mock only, not a firmware route; it ignores the password and `offline`, so
it can always bring the device back). `GET /__mock/scenario` shows the knobs and lists the presets.

```sh
M=http://localhost:5173/__mock/scenario   # use the port vite printed
curl -s -X POST -H 'Content-Type: application/json' -d '{"preset":"cooling"}' $M
curl -s -X POST -H 'Content-Type: application/json' -d '{"filterDays":3}' $M
curl -s $M
```

A preset is a reset (scenario, unit, network, UI password), then any knobs in the same body apply on
top. Knobs alone change only themselves.

| Preset | What the UI shows |
| --- | --- |
| `heating` (default) | 2 °C outside, 21.5 °C indoors, bypass closed, ~90 % recovery |
| `cooling` | mild/cooling season: 19 °C outside, 24.5 °C in, bypass opened by Auto, recovery blank |
| `supply_stopped` | −12 °C, no pre-heater, supply fan at 0, constraint "Below: Frost Protection" |
| `filters_soon` / `filters_overdue` | 9 / 0 filter days left |
| `waiting` | no PDO yet: everything `null` but the device's seeded selects, durations, schema version |
| `refusing` | every command `503 the bus is busy` (durations still store — they send no frame) |
| `bus_down` | no readings and `503 commands are not running` |
| `offline` | every HTTP and socket connection dropped: "nothing reached the device" |
| `unprovisioned` | setup access point: only `POST /api/provision`; status shows the window |
| `locked` | UI password `comfoair-mock` |
| `read_only` | `POST /api/config` → `409` read-only |
| `scan_unavailable` | `GET /api/wifi/scan` → `503` |
| `ota_fail` | `POST /api/ota` → `422` "The uploaded image failed verification and was not installed." |
| `ota_rollback` | `POST /api/ota` → `200`, but after the reboot the device runs the OLD version on the old slot |
| `unit_unread` | `GET /api/system`: the unit never answered — `unknown`, every identity field `null` |
| `unit_errors` | `GET /api/system`: two active errors, `21` with text and `99` with `text: null` |
| `probe_silent` | the probe ends `failed`/`no_answer` after 3 s — unless started with `another_counter: 1` |

Knobs: `provisioned` (bool), `password` (string, `""` = none), `season` (`heating`/`cooling`),
`readings` (`live`/`none`), `commands` (`ok`/`busy`/`down`), `offline`, `supplyStopped`,
`configReadOnly` (bools), `filterDays` (whole number), `scan` (`ok`/`unavailable`),
`ota` (`ok`/`fail`/`rollback`), `probe` (`ok`/`silent`),
`unitInfo` (`ok`/`unread`/`no_answer`/`errors`).

OTA needs a UI password, as on the device, so the success path is reached from `locked` (or after
setting a password on the Settings page); `ota_fail` and `ota_rollback` set the same password. Upload a real
`firmware.bin`, or craft a test image of any version (here `0.2.0`) with
`python3 -c "import struct,sys; h=bytearray(0x70); h[0]=0xe9; h[0x20:0x24]=struct.pack('<I',0xABCD5432); h[0x30:0x35]=b'0.2.0'; h[0x50:0x5a]=b'comfoair-q'; sys.stdout.buffer.write(bytes(h)+bytes(1300000))" > fw.bin`.

## Tests

```sh
npm run test:mock        # mock/tests, separate from `npm test` on purpose
npm run typecheck:mock   # the whole mock/ tree, tests included
```
