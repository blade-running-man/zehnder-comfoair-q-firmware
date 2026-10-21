# web_assets — the SPA built by the firmware build and compiled into the binary

> This is documentation, not code — re-read the source before relying on a detail.

The device serves its own web UI (Preact + Vite SPA in `web/`) without a file system. This
component makes the SPA part of the firmware: its `CMakeLists.txt` runs the web build while CMake
configures, embeds the three gzipped output files with `EMBED_FILES`, and `web_assets.c` exposes
them as a small table of `{path, content type, bytes, length}` that the HTTP server walks. The
result is one artefact: an OTA image can never carry a new firmware serving an old UI, and nobody
has to remember to rebuild the front end.

## Responsibility

Owns:

- Running `npm ci` (only when `web/node_modules` is missing) and `npm run build` in `web/` at CMake
  configure time, failing the configure with `FATAL_ERROR` if npm is absent or either step fails.
- Re-running `npm run build` on every firmware build (custom target `web_spa`, a dependency of the
  component library), so an edit under `web/src` reaches the next binary.
- Embedding `web/dist/index.html.gz`, `app.js.gz`, `style.css.gz` and mapping them to the URL paths
  `/`, `/app.js`, `/style.css` with their content types.

Does NOT do:

- Serve anything, set headers, route, or apply the access policy — `comfoair_http`
  (`asset_get()` in `components/comfoair_http/comfoair_http_assets.c`) does, including the SPA-shell
  fallback for client-side routes and the JSON 404 for unknown `/api/` paths.
- Compress anything — Vite does it at build time (`vite-plugin-compression2`, `deleteOriginalAssets`
  in `web/vite.config.ts`); the bytes here are already gzip.
- Decide the output file names — `web/vite.config.ts` does (`entryFileNames: "app.js"`,
  `assetFileNames: "[name][extname]"`, `cssCodeSplit: false`, IIFE format).
- Captive-portal probe answers — `comfoair_captive`, consulted by `comfoair_http` before it falls
  through to the assets.

## Public API

Header: `include/web_assets.h` (C, wrapped in `extern "C"` because `src/main.cpp` is C++).

| Symbol | Contract |
| --- | --- |
| `web_asset_t` | `{ const char *path; const char *content_type; const unsigned char *start; size_t len; }`. All pointers refer to static storage (string literals and the embedded flash image); the caller owns nothing and frees nothing. `start`/`len` are **gzip bytes**, not the plain file — the server must send `Content-Encoding: gzip`. Not NUL-terminated. |
| `const web_asset_t *web_assets(void)` | Returns a pointer to a static 4-entry array: three assets, then a terminator whose `path` is `NULL`. Iterate until `a->path == NULL`. Cannot fail, returns no `esp_err_t`, never returns `NULL`. Any task context. |

The table, in order (`web_assets.c`):

| Index | `path` | `content_type` | Source file |
| --- | --- | --- | --- |
| 0 | `/` | `text/html` | `web/dist/index.html.gz` |
| 1 | `/app.js` | `application/javascript` | `web/dist/app.js.gz` |
| 2 | `/style.css` | `text/css` | `web/dist/style.css.gz` |
| 3 | `NULL` | — | terminator |

Index 0 being the shell is part of the contract: `comfoair_http` takes `web_assets()` (element 0)
as the page to return for any non-API GET that matches no asset.

## Implementation

- `CMakeLists.txt` — the configure-time web build, `idf_component_register(SRCS web_assets.c
  INCLUDE_DIRS include EMBED_FILES ...)`, and the `web_spa` custom target (`ALL`) that the
  component library depends on.
- `web_assets.c` — the six `_binary_<name>_gz_start/_end` linker symbols (declared with `asm()`
  names, since `EMBED_FILES` derives them from the file name with dots turned into underscores)
  and the function that fills the static table.
- `include/web_assets.h` — the type and the one function.

Flow: CMake configure → `npm ci` if needed → `npm run build` (`tsc -b && vite build`, output
emptied and rewritten in `web/dist`) → `EMBED_FILES` generates assembler stubs for the three files →
on every `pio run`, `web_spa` rebuilds `web/dist` before the component library, and the generated
`.S` rules depend on the files, so changed bytes are re-embedded. On a fresh configure the SPA is
therefore built twice (once at configure, once by `web_spa`); that is the price of the files having
to exist at configure time.

The table is filled on every call rather than with a static initializer: `end - start` of two
distinct linker symbols is not a constant expression in C, so it cannot initialise a static. Every
call writes the same values into the same array.

DO NOT:

- Rename or add a file in `web/dist` on the web side alone. The names are hard-wired three times:
  Vite's output names, the `EMBED_FILES` list, and the `asm("_binary_...")` symbols plus paths in
  `web_assets.c`. A renamed output fails the configure (missing embed file); an extra output (for
  example an image larger than Vite's `assetsInlineLimit: 4096`, or a second JS chunk) is silently
  not embedded, and a request for it gets the HTML shell with 200 from `comfoair_http`'s fallback
  rather than a 404, which is hard to spot.
- Move the web build into an ordinary build rule only. `EMBED_FILES` is resolved by
  `idf_component_register` while CMake configures, so the files must already exist then; a
  build-time-only rule fails a clean configure.
- Reorder the table so that `/` is not index 0 — the SPA-shell fallback in `comfoair_http` reads
  element 0 directly.
- Replace `EMBED_FILES` with a LittleFS/SPIFFS partition. The partition table has no file system
  (2 x 1984 KiB OTA slots), and a separate UI image could drift from the firmware after an OTA.
- Drop the `FATAL_ERROR` on missing npm in favour of embedding a stale `web/dist`; a build without
  Node must fail loudly, not ship whatever was last left on disk.

Concurrency: no lock. Callers (`app_main` once at boot, the httpd task per request) may run
`web_assets()` concurrently; the writes are identical, so a reader never observes a value other than
the final one in practice, but it is formally an unsynchronised write. If the table ever gains
per-call variation, make it a one-time initialisation instead.

## Dependencies

- Requires: no IDF components (`idf_component_register` lists no `REQUIRES`/`PRIV_REQUIRES`);
  build-time it requires `npm`/Node and the `web/` project (`web/package.json`, `web/vite.config.ts`).
- Depended on by: `src/` (`src/CMakeLists.txt` REQUIRES `web_assets`; `src/main.cpp` sums the
  lengths and logs `web UI compiled in: <N> B` at boot) and `comfoair_http`
  (`components/comfoair_http/CMakeLists.txt`; `asset_get()` serves the table, reached via the
  `"/*"` GET route through `comfoair_http_captive_probe_get()`).
- CI: `.github/workflows/release.yml` sets up Node because the board build runs the web build here.

## Tests

No host suite exercises this component: nothing under `test/` includes `web_assets.h`, and it
depends on linker symbols that only exist in the board build. What is covered elsewhere is the SPA
itself (`cd web && npm test`) and, on hardware, HV-BOOT-01 in
`docs/hardware-verification/01-boot-and-resilience.md` (the boot log's `web UI compiled in: N B`
must equal the summed sizes of the three `.gz` files from the same build). Not covered by any automated check: that
the three embedded names match Vite's actual output, that `/` stays at index 0, and the content
types. A clean `pio run -e m5stack-nanoc6` is the only gate — a missing file fails the configure.

## Notes

- Why gzip at build time and `Content-Encoding: gzip` always: it keeps flash use small and the
  server free of a compressor. Every browser the UI targets accepts gzip, so no `Accept-Encoding`
  negotiation is done; a client that refused gzip would get undecodable bytes.
- Why only three paths: Vite is configured for a single IIFE bundle (`app.js`), one CSS file and
  inlined small assets, so the UI is exactly three files and the table can be a fixed array.
  `/index.html` is not an entry; a request for it falls through to the shell fallback in
  `comfoair_http` and gets the same page.
- The comment in `CMakeLists.txt` quotes the UI as "~15 KB gzipped"; the files on disk in
  `web/dist` are about 122 KB in total (`app.js.gz` dominates). The real figure is logged at boot by
  `src/main.cpp`.
