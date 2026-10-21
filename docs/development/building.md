# Building

The firmware is an ESP-IDF project built with PlatformIO. One environment produces the one
universal binary; the web UI and the entity registry are built as part of it, so there is no
manual step to remember.

## Toolchain

| Tool | Version | Where it is decided |
| --- | --- | --- |
| PlatformIO Core | 6.1.19 — the version the release workflow installs (`pip install platformio==6.1.19`) | `.github/workflows/release.yml` |
| Platform | `pioarduino/platform-espressif32`, pinned to `#55.03.311` | `platformio.ini` |
| ESP-IDF | 5.5.5 — the `framework-espidf` package (`3.50505`) that the pinned platform installs | the platform pin |
| Node.js | 22.6 or newer (the release workflow uses 24) | `web/package.json` `engines` |
| Python | 3 — runs the registry generator and the version script during the build | `tools/` |

`platformio.ini` has two environments: `[env:m5stack-nanoc6]` (the board, `framework = espidf`)
and `[env:native]` (the host tests, `platform = native`). There is no Arduino environment, and
there must never be one (see [Conventions](/development/conventions)).

### Why the platform is pinned

`platform = https://github.com/pioarduino/platform-espressif32.git#55.03.311` is a deliberate
pin. A bare git URL floats: it clones whatever is on the default branch at the moment of the
first build. That once moved the project onto a newer ESP-IDF than the one its design had been
verified against, with no change in this repository at all. The ESP-IDF facts the firmware
relies on were checked against the 5.5.4 tree; the pinned platform carries 5.5.5, and that is
recorded next to the pin.

Bump the pin only on purpose: re-run the ESP-IDF checks against the new tree and say so in the
change. The release workflow keys its PlatformIO cache on `platformio.ini`, so a bumped pin
fetches a fresh toolchain rather than restoring the old one.

The pioarduino platform is used rather than `platformio/platform-espressif32`; the reason
recorded at the top of `platformio.ini` is that `platformio/platform-espressif32` 6.x carries
neither a current ESP-IDF nor the ESP32-C6 board catalogue.

### Managed components are pinned exactly

The two components fetched from the Espressif registry are pinned to exact versions in
`src/idf_component.yml`: `espressif/mdns` `1.12.0` and `espressif/led_strip` `3.0.3`. A caret range
such as `^3.0.0` is the floating-platform failure with a smaller radius: the next machine to resolve
it gets a component nobody here has built. `dependencies.lock` is committed (and deliberately not in
`.gitignore`); on top of the versions it records each component's `component_hash`, so the bytes a
later machine fetches are the bytes this tree was built and tested on. `managed_components/` is not
committed; it is refetched from the lock.

The manifest lives in `src/`, not the project root: under PlatformIO `src/` is the project's main
component, and the component manager reads only a component's manifest. A root-level
`idf_component.yml` builds green, fetches nothing and reports no error.

Bump either version deliberately, from what the lock actually resolved, and say so in the change.

## Build the firmware

```sh
pio run -e m5stack-nanoc6
```

`pio` is often not on `PATH`; PlatformIO installs it at `~/.platformio/penv/bin/pio`.

The environment targets the M5Stack NanoC6 as `board = esp32-c6-devkitm-1` — the 4 MB part. The
`devkitc-1` definition is the 8 MB part and would silently mis-size the partition table.
`partitions.csv` gives two 1984 KiB OTA slots and no filesystem; `ota_0` must stay at `0x10000`.

::: warning Judge the build by its exit code
`pio run … | tail` returns the exit code of `tail`, and a failed build looks successful. Write
the output to a file and check `$?`.
:::

### What runs as part of `pio run`

Two PlatformIO pre-build hooks (`extra_scripts` in `platformio.ini`) and one CMake step:

1. **`tools/regenerate.py`** runs `tools/generate_registry.py`, which rewrites the C registry
   header, the TypeScript entity types and the entity-name translations from
   `tools/pdo_table.py`. A stale registry would not fail to build — it would build and ship
   entities that disagree with the table — so it is regenerated on every build. See
   [Entity registry](/development/entity-registry).
2. **`tools/pio_version.py`** compares the version `tools/version.py` computes now with the one
   CMake last configured with, and forces a re-configure when they differ. PlatformIO's own
   re-configure check does not look at git, so without this hook a new commit or tag could leave
   the previous version stamped in the image. See [Releasing](/development/releasing).
3. **The SPA build.** `components/web_assets/CMakeLists.txt` runs `npm ci` in `web/` (only when
   `web/node_modules` is missing) and then `npm run build`, at CMake configure time, and adds a
   custom target that rebuilds it on every build. `EMBED_FILES` is resolved while CMake
   configures, so `web/dist/index.html.gz`, `app.js.gz` and `style.css.gz` must exist by then.
   The UI and the firmware are therefore one artefact: an OTA update can never leave a new
   binary serving an old UI.

Because of step 3, building the firmware needs `npm` on the `PATH`; the configure step stops with
an error if it is missing.

::: tip Parallel builds
Every firmware build writes `web/dist` in the repository, and `PLATFORMIO_BUILD_DIR` does not move
it. Two builds running at once (for example two working copies driven by separate agents) should
each use their own git worktree, and their own `PLATFORMIO_BUILD_DIR`.
:::

### `sdkconfig.defaults` and the generated `sdkconfig.m5stack-nanoc6`

`sdkconfig.defaults` is the committed source of every Kconfig choice. The first build writes the
full configuration to `sdkconfig.m5stack-nanoc6` in the project root; that file is
`.gitignore`d and is regenerated on later builds. Which one wins for a given option depends on how
the generated file holds it:

- **A value the generator wrote as a default** carries a `# default:` line above it. When
  `sdkconfig.defaults` later sets that option, the defaults file wins on the next build. A
  checkout built before `CONFIG_LWIP_MAX_SOCKETS=13` landed holds the old `10` that way, and it
  moves to `13` by itself. Measured on this toolchain: a copy of an old file with
  `# default:` / `CONFIG_LWIP_MAX_SOCKETS=10` came out as `13`, and the build passed.
- **A value without that marker** was set by hand (for example through `menuconfig`). It
  **overrides** `sdkconfig.defaults`. For the socket table, the `_Static_assert` beside
  `cfg.max_open_sockets` in `components/comfoair_http/comfoair_http.c` then stops the build:
  `static assertion failed: "lwIP's socket table cannot hold every socket user"`. That was
  measured too.

The fix in either case is to delete the generated file, so it is regenerated from the defaults
(it holds nothing the repository needs):

```sh
rm sdkconfig.m5stack-nanoc6 && ~/.platformio/penv/bin/pio run -e m5stack-nanoc6
```

## The web UI on its own

```sh
cd web
npm run dev        # dev server; proxies /api and /ws to a real device
npm run dev:mock   # dev server against the built-in mock device, no hardware needed
npm run build      # type-check (tsc -b) and production build into web/dist
```

The dev server's proxy target is `VITE_DEVICE_IP`, read through Vite's `loadEnv` (put it in
`web/.env.local`); unset, it falls back to `192.168.50.92` (`web/vite.config.ts`). Only `/api` and
`/ws` are proxied (`/ws` with the WebSocket upgrade): the dev server serves the UI itself, and the
UI talks to the device only through its public API. In `dev:mock` mode no proxy is set up; the
mock plugin in `web/mock/` answers those paths in-process.

`npm run build` is the real type-check of the SPA; it also type-checks the web test suites but
does not run them (see [Testing](/development/testing)).

## Flashing

```sh
pio run -e m5stack-nanoc6 -t upload
```

Flashing a device is the owner's decision — ask before running it. End users do not build at all:
they install a release from the browser through ESP Web Tools (see
[Installation](/guide/installation)), and later updates go over the web UI (see
[Updating](/guide/updating)).

## This documentation site

The site is VitePress, rooted at `docs/` (config: `docs/.vitepress/config.mts`).

```sh
cd docs
npm ci
npm run docs:dev      # local preview with hot reload
npm run docs:build    # static build into docs/.vitepress/dist
npm run docs:preview  # serve the static build
```

Three environment variables change where the build goes; all are optional:

| Variable | Effect |
| --- | --- |
| `DOCS_BASE` | the site's base path, for publishing under a sub-path such as `/<repo>/` on GitHub Pages; default `/` |
| `DOCS_OUT_DIR` | output directory instead of `docs/.vitepress/dist` |
| `DOCS_CACHE_DIR` | Vite cache directory instead of the default under `docs/.vitepress/` |

The last two exist so that several builds can run side by side without sharing state.

Files under `docs/` that are not authored site pages, such as the source material in
`docs/manuals/`, are left out by `srcExclude` in the config. Dead links between site pages fail the
build; `ignoreDeadLinks` lets through only links that genuinely point outside the built site, such
as links into the excluded trees and relative links that leave `docs/`.

The [Entities](/reference/entities) page is generated during the site build from the firmware's
own generated entity list, so `docs:build` reads `web/src/api/entities.ts` from the repository.
