# ComfoAir Q firmware — reference for coding agents

Own ESP-IDF firmware for the Zehnder ComfoAir Q.
C / ESP-IDF 5.5.5 (carried by the pinned platform) / PlatformIO, one universal binary, everything configured at
runtime. Three control surfaces over one model: MQTT, HTTP REST and a web UI of
its own. Phases 1–7 have landed (skeleton → CAN → core → REST → provisioning →
MQTT/HA → commands). Phase 8 is OTA and release, 9 is tests/CI/docs, 10 is
acceptance on the real unit. **Nothing has been accepted on hardware yet.**

This file is tool-agnostic: it is the one instruction file for any coding agent.
Tool-specific configuration (subagent definitions, hooks, permissions) lives in
that tool's own directory and must not restate the rules below.

## v1 scope

v1 ships on **one board, the M5Stack NanoC6** (ESP32-C6, 4 MB). In v1: CAN, PDO
parsing and RMI commands in full for the ComfoAir Q350/450/600 base unit (all 58
values (60 polled PDOs), the unit's active errors (2 entities read over RMI), 13
controllable entities; accessory and Option-Box PDOs are not published); the six parameterised operations; MQTT with HA discovery as
**the main control path**; HTTP REST covering exactly what MQTT covers;
provisioning (AP, captive portal, NVS, setup window, two-level reset); a
**minimal** web UI (setup, state, log, `.bin` upload); OTA by `.bin` upload;
host tests, CI, an ESP Web Tools page.

Deferred past v1, listed so it does not creep in: the three other boards (the
architecture must admit them — the board boundary exists from day one), full
ventilation control from the web UI, a network OTA path, TLS to the broker, and
the fate of `zehnder-comfoair-q-ha`.

## Architecture

**One model, three transports.** The state + command model is the single source,
generated from `pdo_table.py`; HTTP, MQTT and the WebSocket push channel are
transports over it, and the SPA is a client of that API and nothing more.

**Everything is configured at runtime.** Wi-Fi, broker, the discovery flag and
the device name are entered in the web UI and live in NVS. No secret in the
repository and none in the binary, which is what lets one published `.bin` suit
everyone.

**No manual build steps.** The SPA build and the registry regeneration are part
of the firmware build; "don't forget to rebuild the front end" is not allowed.

## Read first

- `docs/` — the as-built reference site (architecture, protocol, interfaces,
  device, development, guide, unit). It describes what the firmware does today
  and is the authority for it. Start at `docs/architecture/overview.md`; the
  board boundary is in `docs/device/board.md`, security in
  `docs/architecture/security.md`, the API in `docs/interfaces/`.
- `components/<name>/README.md` — **read it before working in that component.**
  It is the per-component contract and implementation doc. Nothing loads it
  automatically, so open it yourself. It is documentation, not code: re-read the
  source before relying on a detail.

## Commands

```sh
pio run -e m5stack-nanoc6           # firmware; runs `npm run build` in web/ as part of it
pio test -e native                  # host test suites — the gate for every protocol change
pio test -e native -f test_wire     # one suite
pio run -e m5stack-nanoc6 -t upload # flashing is the owner's job, ask first
cd web && npm run dev               # SPA dev server, proxies /api and /ws to the device
python3 tools/generate_registry.py  # regenerate registry + types from tools/pdo_table.py
                                    # (also runs as a pre-build hook via tools/regenerate.py)
```

`pio` may not be on `PATH`; it lives at `~/.platformio/penv/bin/pio`.

## How work is done here

**Delegate, do not accumulate.** The main thread is for decisions and for
holding the plan. Anything that means reading many files — locating code,
verifying a claim — goes to a sub-task that returns a brief. Three roles are used:

| Role | For |
| --- | --- |
| scout | read-only reconnaissance; returns citations, never file dumps |
| component author | implements ONE component or file, test-first |
| reviewer | adversarial check against the acceptance criteria and the rules below |

**Split the work before starting it.** An implementation plan becomes N
independent tasks, each naming its files and its test suite. Tasks that share no
state are run **in parallel**. Tasks that do share state are sequential — say so
in the plan rather than discovering it in a conflict.

**One task, one context.** A sub-task gets the seam it needs (the public header,
its test suite, the docs page) and nothing else. It reports under 200 words:
paths changed, verification output, and what it deliberately left alone.

**Verification is output, not assertion.** "Tests pass" without the pasted
output is a failure. `pio test -e native` and a clean
`pio run -e m5stack-nanoc6` before any claim of doneness. A phase is never
declared done on the strength of a successful build.

**Review findings are checked against the source,** not accepted on trust. At
the end of a piece of work, list what was accepted, what was rejected, and why.

## File budget

**350 lines is the ceiling for a hand-written source file**, 600 for a Unity
suite (a suite is one binary per directory, so it splits by directory).
Generated files are exempt.

**Standing exemption (owner's decision): `tools/pdo_table.py`.** It is the one entity list
every surface is generated from; cutting it into several files is the very thing "one entity
list, ever" forbids, and its size is rows of data, not logic. Let it grow past 350; do NOT
split it. Its dataclasses and helpers stay small — new logic goes in `generate_registry.py`.

Split along a seam that already exists: one responsibility per file, the public
header unchanged. A component is a directory, so `comfoair_config.c` and
`comfoair_config_nvs.c` beside each other is the shape to reach for, not one
file holding both. Many files are over the ceiling today (the largest:
`comfoair_json.cpp`, `comfoair_config_nvs.c`, `comfoair_provision.c`); split them when you
next work inside them, not in a drive-by rewrite. Nothing enforces the ceiling
automatically. Recount with `wc -l` before acting on it.

## Acceptance criteria

The project counts as done only when all of these hold. Code comments cite them
by number.

1. **One binary** from the releases page installs through ESP Web Tools and is
   configured in the browser — no build, no YAML, nothing entered at compile time.
2. **Home Assistant sees the device through MQTT discovery** and controls it,
   with no integration installed.
3. **REST covers exactly what MQTT covers.** No operation available in only one
   transport.
4. **The web UI uses no privileged endpoint** — everything it does is reachable
   from outside through the same API. This holds for the minimal v1 UI too: the
   rule is not deferred together with the scope.
5. **The M5Stack NanoC6 builds and runs.** The three other boards are not a v1
   criterion, but the board ↔ everything-else boundary exists and is
   demonstrated: what a fourth board would require, and what it would not touch.
6. Host tests run without hardware, CI is green, binary size is tracked (inside
   the C6's 4 MB, with room for the OTA partition).
7. **No Arduino and no Arduino library.** No `#include <Arduino.h>`, directly or
   transitively through a third-party component. `grep -ri` over sources and
   build files (the whole tree except `docs/`, `web/node_modules/` and `NOTICE`)
   finds none of `FastLED`, `NeoMatrix`, `ArduinoHA`, `PubSubClient`,
   `ESPAsyncWebServer`. `platformio.ini` has no `framework = arduino`. `matrix`
   is deliberately not on the list: `strategy: matrix:` in CI is legitimate.
8. **The licence is settled.** No file in the tree carries a licence that
   contradicts the chosen one.

## Rules that are load-bearing

- **No Arduino.** Not the core, not its libraries, not transitively. Criterion 7
  makes the absence checkable; `platformio.ini` must never gain
  `framework = arduino`.
- **One entity list, ever.** Everything — REST, MQTT topics, HA discovery and
  `web/src/api/entities.ts` — is generated from `tools/pdo_table.py`. A second
  hand-written list is forbidden outright. `registry_generated.h` and `entities.ts` are outputs: fix the
  generator, never the file. **The device config types (`DeviceConfig`,
  `ConfigPatch`, …) are HAND-WRITTEN in `web/src/api/client.ts`** and describe
  the device config, not entities (`tools/generate_registry.py` says so beside
  `TS_OUT`); never point the generator at that file.
- **The web UI has no privileged endpoint.** If a screen would be easier with a
  path of its own, the API is wrong (criterion 4).
- **No GPIO number outside `components/board/`.** That is the whole point of
  criterion 5.
- **Secrets are write-only.** A stored password is never returned by any
  projection of the API; reads get a sentinel. This is what unblocks a single
  universal binary.
- **Nothing reboots because a peer is absent.** No "broker unreachable for N
  minutes, restart".
- **The platform pin is load-bearing.** `platformio.ini` pins
  `pioarduino/platform-espressif32#55.03.311` because a floating URL once moved
  the toolchain under code that had been verified against a different tree.
  Bump it deliberately, re-check what the firmware leans on in IDF
  (the TWAI driver, WebSocket support in `esp_http_server`, esp-mqtt's last will,
  the partition table, `EMBED_FILES`), and say so.
- **Nothing is committed or pushed without the owner's permission.**

## Tests

The protocol, state, policy, JSON and command layers depend on neither the
hardware nor the framework, so they are tested natively on the host — and the
test is written **before** the implementation. Host tests cover at least: PDO
parsing against the vectors in `docs/reference/comfoair-pdo-rmi.md`, RMI frame
building and parsing, `airflow_constraints` at the bounds, the generator against
`pdo_table.py` (every entity has exactly one place in the API, the MQTT topics,
discovery and `entities.ts`), and REST/MQTT contract tests (criterion 3).

One directory per suite under `test/`: PlatformIO links every `.cpp` in a
directory into one binary, so two `main()`s in one directory collide.

The components are ESP-IDF components and PlatformIO libraries at once, so the
host tests compile the very same sources the device runs. `test/test_can_transport/idf/`
is a fake ESP-IDF reachable only from `[env:native]`. **DO NOT** promote a fake
to a shared directory: once two suites share one, "it grew a feature for suite B"
becomes a way to quietly weaken suite A.

Anything the owner must check on real hardware goes into a scenario under
`docs/hardware-verification/` (grouped by what the device does, items keyed
`HV-<AREA>-<nn>`) — a build is not readiness.

## Documentation

Every non-trivial function carries a comment saying **why**, with the rationale
inline or citing the source line or `docs/` page that proves it, and saying `DO NOT` where an obvious
improvement breaks something. Comments that restate the code are noise —
delete them.

Every public header documents its contract: ownership, thread and task context,
what happens on failure, and which `esp_err_t` values a caller must handle.

A decision a reader could reasonably second-guess in six months is recorded
where it lives, in English and self-contained: in a comment beside the code, or
in the owning `docs/` page under "Why not …". State the rejected alternative and
the evidence (`file:line` or a measured result) and what test or hardware
scenario pins it. When a decision is reversed, rewrite to the current truth and
keep one line on why the earlier option failed. There is no separate history
file.

## Layout

| Path | Role |
| --- | --- |
| `src/main.cpp` | entry point |
| `components/board/` | board descriptor; the only place pins are named |
| `components/comfoair_*/` | one component per responsibility; a component is a directory |
| `components/web_assets/` | SPA embedded via `EMBED_FILES`, built at CMake configure time |
| `web/` | Preact + Vite + TypeScript SPA |
| `test/` | host suites, one directory each |
| `tools/` | registry generator and the entity table `pdo_table.py` |
| `partitions.csv` | 2 × 1984 KiB OTA, no filesystem — `ota_0` must stay at `0x10000` |
