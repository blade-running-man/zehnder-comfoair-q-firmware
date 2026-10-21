# Testing

Three test layers, none of which needs hardware:

| Layer | Command | What it covers |
| --- | --- | --- |
| Host suites (C/C++, Unity) | `pio test -e native` | protocol, state, policy, JSON, commands, the CAN transport, MQTT/HA decisions, provisioning — 44 suites under `test/` |
| Python | `python3 -m pytest tests/python -q` | the registry generator, versioning, changelog, release assembly, the lwIP socket budget |
| Web | `cd web && npm test` (plus `npm run build`) | the SPA's models, i18n and API helpers |

A green run of all three is not readiness for the real unit. Anything the owner must check on
real hardware goes into a scenario under `docs/hardware-verification/`, grouped by what the device
does, with items keyed `HV-xx-nn` (see [Hardware verification](/hardware-verification/)).

## Test first

The protocol, state, policy, JSON and command layers depend on neither the hardware nor the
framework, so they are tested on the host — and the test is written **before** the
implementation. Expected values come from the reference documents and captured device data, never
from the code under test: a test written from the code it tests proves only that the code equals
itself. Many suites say in their header which document their vectors come from.

**Anything expressible as a pure function lives where a test reaches it.** A device-only file
(`#ifdef ESP_PLATFORM`, a task, a driver call) compiles to nothing on the host, so a decision left
there is a decision no suite checks. The pattern throughout the tree is a pure half beside a thin
device half: the button decider and its release gate (`comfoair_button.c`) beside the poller task,
one phase-monitor tick as arithmetic (`comfoair_phase_collect_pass()`) beside the 1 Hz task, the LED
resolver beside the render task, the WS2812 byte order (`board_rgb_positions()`) beside the driver.
What stays under `#ifdef` is only what cannot be moved, and what it does on hardware is a
[hardware-verification](/hardware-verification/) scenario.

**A rule no test can pin is replaced by structure.** When the only available test is one that
fails after a duplicate has already drifted, the duplicate is removed instead: the MQTT status
topic has exactly one producer (`comfoair_ha_meta_topic()` with `COMFOAIR_HA_META_STATUS`), so
there is no second format for a test to compare against.

## Host suites

```sh
pio test -e native                   # every suite
pio test -e native -f test_wire      # one suite
```

### One directory per suite

PlatformIO links every `.cpp` file in a test directory into **one binary**, so two files with a
`main()` in the same directory collide. A suite is therefore a directory, and a suite that grows
past the 600-line ceiling is split into a new directory, not a new file beside the old one. Several
suites below exist for exactly that reason (`test_wire_status`, `test_wire_epoch`,
`test_ha_availability`, `test_provision_setup_window`, `test_probe_report_live`). Inside one
directory, several `.cpp` files are fine as long as only one has `main()` (`test_phase`,
`test_mqtt_backoff`).

The ceiling is a convention, not a gate: nothing in the build or the tests checks it. Counting
every `.c` and `.cpp` file in the directory, ten suites are over it today: `test_config`,
`test_can_transport` (its fake ESP-IDF sources included), `test_mqtt_backoff`, `test_wire`,
`test_provision` and `test_phase` by the widest margin, then `test_captive`, `test_auth`,
`test_command` and `test_led_pattern`. The split-off suites above exist so that new cases stop
landing in them.

### `[env:native]`

The `native` environment in `platformio.ini`:

- runs every directory matching `test_*` (`test_filter`) with the Unity framework;
- builds with `-std=c++17 -Wall -Wextra -pthread -Wno-missing-field-initializers` and promotes
  **only** `-Werror=switch` to an error. A
  switch over an enum that has just gained a value is how a new phase, failure class or PDO type
  gets silently answered "unknown"; a warning does not stop that. Do not widen it to `-Werror`:
  Unity's macros and the fakes are why the rest of `-Wextra` stays advisory.
- puts the `include/` directories of the components the suites use, and
  `test/test_can_transport/idf`, on the include path with explicit `-I` flags;
- sets `lib_extra_dirs = components`. The components are ESP-IDF components and PlatformIO
  libraries at once, so the host suites compile **the very same sources** the device runs. The
  dependency finder pulls in only what a test includes, so board- and IDF-bound components are
  never built on the host.
- excludes `src/` (`build_src_filter = -<*>`): the firmware entry point cannot build on the host.

### Callbacks, not includes

PlatformIO's library dependency finder does not evaluate the preprocessor. An `#include` of
another component's header pulls that component into the host build even when it sits inside
`#ifdef ESP_PLATFORM`, and `[env:native]` then compiles every `.c` file of it. One cross-component
include from a device-only file therefore drags ESP-IDF headers into every suite that reaches the
component. This was measured, not assumed: including `comfoair_net.h` from the button task turned
`test_button_gesture` red with `esp_event.h file not found`, and the same happened to
`test_mdns_name`.

So a component that must reach a neighbour on the device is handed a function pointer or a struct
of facts by the entry point under `src/`, where the rest of the wiring is visible, instead of
including the neighbour's header: `comfoair_button_start(board, comfoair_net_open_setup_window)`, and the
`comfoair_mdns_config_t` that `app_mdns_start()` fills. The phase monitor is a separate component
(`comfoair_phase_monitor`) rather than a file inside `comfoair_phase` for the same reason: its MQTT
and CAN includes would otherwise land in `test_phase` and `test_bus_health`.

The board environment sets `test_ignore = *`, so no host suite is ever built for the device.

### The fake ESP-IDF, and why fakes are never shared

`test/test_can_transport/idf/` is a fake ESP-IDF (TWAI driver, FreeRTOS on pthreads) for the CAN
transport, the first component that cannot compile without the framework. Its include path is
reachable only from `[env:native]`, so it can never shadow the real driver on the device. The fake
keeps the real driver's blocking contract rather than simplifying it away — a cooperative shim
would test a different loop than the one that ships.

**Fakes are owned by one suite.** Do not promote a fake into a shared directory: once two suites
share it, "it grew a feature for suite B" becomes a way to quietly weaken suite A. The same rule
applies to the fake ventilation units in the probe suites — `test_probe_scan_bounds` builds its own
rule-based fake rather than reusing `test_probe_scan`'s table.

### The suites

#### CAN protocol and the unit

| Suite | Purpose |
| --- | --- |
| `test_pdo_decode` | PDO frame decoding, against the vectors in the PDO/RMI reference |
| `test_airflow_constraints` | the PDO 230 airflow-constraint bitset and its wording |
| `test_rmi_frame` | RMI request construction, byte for byte against the command table |
| `test_rmi_read` | RMI read requests and response classification; proves the read builders cannot emit a command ≥ `0x80` |
| `test_rmi_response` | RMI response reassembly and payload parsing |
| `test_command` | the bytes a command writes to the unit — the suite with the highest stake |
| `test_can_transport` | the CAN transport against the fake ESP-IDF: buffer lifetime, queues, blocking |
| `test_state` | the state model: PDO frames in, entity values out, against `pdo_table.py` |
| `test_state_bits` | the `bit` binary operator: one bit of a PDO drives one row (PDO 337's boost and away timers) |
| `test_state_special` | PDO 230's text in its own buffers: every changed constraint text is marked, so MQTT and the WebSocket follow it |
| `test_state_copy` | `comfoair_state_get_copy()`: a text copied into the caller's buffer under the lock survives every later write |
| `test_state_unit_errors` | `comfoair_state_set_unit_errors()`: the two unit-error rows, written by their generated indices, unknown until the first read |
| `test_api` | the JSON projection of the state that REST, WebSocket and MQTT all serve |

#### Probe and system information

| Suite | Purpose |
| --- | --- |
| `test_probe_scan` | the read-only RMI walk against a table-driven fake unit |
| `test_probe_scan_bounds` | what stops the walk, against a rule-driven fake |
| `test_probe_report` | the probe report: catalogue, decoders, redaction, JSON |
| `test_probe_report_live` | the probe report against the first real report from a ComfoAir Q350 |
| `test_sysinfo` | `GET /api/system`: exact shape, null rules, error texts, buffer discipline |
| `test_unit_errors_summary` | the two unit-error entities' values: which ids raise `unit_error` (never an informational one, never any before the firmware is known) and the one-line text, cut at 255 bytes on a character boundary |
| `test_unit_cadence` | when the probe task reads the unit again: the full read and the errors-only read, wrap-safe |

#### Web surface, configuration and security

| Suite | Purpose |
| --- | --- |
| `test_http_policy` | who may do what, as a pure function — the access policy |
| `test_auth` | parsing an `Authorization` header from untrusted input |
| `test_secrets` | write-only secrets: redaction, the sentinel, keep/clear/store decisions |
| `test_json` | the reader that decodes request bodies |
| `test_wire` | the request and response documents of the provisioning and config API |
| `test_wire_status` | `GET /api/status` and the rules its members obey |
| `test_wire_epoch` | `applied_epoch` in `GET /api/status`, the member a caller acts on |
| `test_config` | the pure half of the stored configuration (the NVS layer itself is device-only): stored names, field validation, and the failure cases — a power cut between writes, edge-case SSIDs, a corrupt record, a renamed key |
| `test_log` | the in-memory ring log: bounded, non-blocking, valid JSON for any text |
| `test_ota` | the pre-flight on an uploaded image, before any flash sector is erased |

#### Provisioning, network and indication

| Suite | Purpose |
| --- | --- |
| `test_provision` | the provisioning state machine, driven through failure scenarios |
| `test_provision_setup_window` | the short button press that opens the setup window without erasing anything |
| `test_captive` | the captive-portal probe answers and the captive DNS server's decisions |
| `test_mdns_name` | the mDNS host name, instance name and TXT records |
| `test_button_gesture` | the button gesture as a pure function of pressed state and time |
| `test_button_poller` | the host-reachable half of the button polling task |
| `test_phase` | the single device phase derived from network, broker and bus facts |
| `test_bus_health` | the bus freshness verdict and its four states |
| `test_led_pattern` | the LED palette and the mapping from phases to looks |
| `test_board_rgb_order` | the WS2812 byte-order table a person reads off a bench |

#### MQTT and Home Assistant

| Suite | Purpose |
| --- | --- |
| `test_ha` | the topic tree and the discovery payloads: which Home Assistant component each kind becomes, and which keys each payload carries or omits |
| `test_ha_availability` | which topics an entity's availability is read from |
| `test_ha_cleanup` | what the device retracts when it stops being what it was, and in which order |
| `test_ha_enabled` | `en: false` on exactly the rows the table disables, over the whole registry |
| `test_ha_freshness` | the freshness entity, which turns silence into `unavailable` in Home Assistant |
| `test_ha_none` | an entity with no value publishes `None` on every state topic, over the whole registry |
| `test_publish_rate` | the MQTT deadband, minimum interval and max hold per row, against a fake clock |
| `test_discovery_gate` | when the full discovery tree is published again |
| `test_retract_budget` | the ceiling on a retraction |
| `test_device_url` | `configuration_url` on the Home Assistant device card |
| `test_mqtt_inbound` | what an arriving MQTT message means |
| `test_mqtt_backoff` | the broker retry ladder, transcribed from its design table |
| `test_mqtt_facts` | the broker facts the esp-mqtt event handler loses, and what the owner reads instead |

## Python tests

```sh
python3 -m pytest tests/python -q
```

`tests/python/conftest.py` provides `_load()`, which the suites use to load modules from `tools/`
by path on a bare interpreter — no PlatformIO, no firmware build. If a test there ever
needs one of those, the thing it tests has moved to the wrong layer.

| File | Purpose |
| --- | --- |
| `test_registry.py` | the generator against the table: every entity reaches the C registry and the TypeScript types exactly once, the contract counts (58 values, 13 controllable entities, 60 polled PDOs), frozen names, option order |
| `test_registry_unit_errors.py` | the 2 unit-error entities: their kinds, the two generated indices, 76 entities in all, the refusal of a table without exactly one row of each form, and no option label spelled `None` |
| `test_registry_publish_rate.py` | the publication-rate fields against the generator, the refusal of values the publisher could not honour, and the per-row values the Q350 census chose |
| `test_registry_q_scope.py` | the base-unit scope (no accessory, Option-Box or raw schedule PDO), the label, kind and metadata corrections, the disabled set, schema 2 |
| `test_entity_names_i18n.py` | the DE/NL/UK name overlay matches the registry, and the refusal actually fires when it does not |
| `test_version.py` | every row of the SemVer-from-tag table, including throwaway git repositories |
| `test_changelog.py` | extracting one release section from `CHANGELOG.md` |
| `test_make_release.py` | the ESP Web Tools manifest and the layout checks that keep an install from erasing NVS |
| `test_socket_budget.py` | httpd's clients + internals + every other socket user + a spare fit `CONFIG_LWIP_MAX_SOCKETS` in `sdkconfig.defaults`, and every source file that opens a socket is counted in `comfoair_http_sockets.h` |

## Web tests

```sh
cd web
npm test           # every *.test.ts under web/src
npm run build      # type-checks the suites (and the app); runs none of them
npm run test:mock  # the dev mock's own suites, under web/mock/tests
npm run typecheck:mock  # tsc --noEmit -p mock/tsconfig.json: type-checks the whole mock/ tree
```

The suites are plain `.ts` files run by Node's built-in type stripping, which is why the web side
needs Node 22.6 or newer: on an older Node every suite dies with a syntax error. `web/scripts/test.mjs`
finds every `*.test.ts` under `web/src` itself — a new suite runs without being listed anywhere —
runs each in its own Node process, and checks the `N/M passed` tally each one prints, so a suite
that crashes or swallows its own failure still counts as failed.

The web gate is **both** `npm test` and `npm run build`. The test run is deliberately not folded
into the build: the firmware build runs `npm run build` every time, and a red web suite must not
look like a broken firmware build. The mock suites are separate again: the mock is a developer
convenience, and a red mock suite must not read as a broken SPA.

`npm run build` does not type-check the mock's suites: `tsc -b` reaches the mock only through what
`vite.config.ts` imports, so `web/mock/tests` is outside it. `npm run typecheck:mock` covers the
whole `web/mock/` tree, tests included, under the same strictness as `tsconfig.node.json`
(`web/mock/tsconfig.json`). Like the mock suites, it is not part of the web gate.

## What the release workflow runs

Before it builds a release, `.github/workflows/release.yml` runs `pio test -e native` and
`python -m pytest tests/python -q`. It does not run the web suites (`npm test`); the firmware
build it runs next does run `npm run build`, so the SPA is type-checked. See
[Releasing](/development/releasing).
