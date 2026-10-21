# Releasing

A release is a `v*` git tag. The tag is the only source of the version; `CHANGELOG.md` supplies
the release text; `tools/make_release.py` turns one board build into the installable site; and
`.github/workflows/release.yml` does all of it on a tag push.

::: warning The workflow has not run yet
The release workflow is written but dormant: this repository has no git remote, so no tag push
can trigger it. What it builds is reproducible locally, and that is where it has been checked (see
[Building a release locally](#building-a-release-locally)).
:::

## The version

`tools/version.py` derives a [SemVer 2.0.0](https://semver.org/spec/v2.0.0.html) version from
`git describe --tags --long --dirty --match 'v[0-9]*'`, and from nothing else. There is no
`version.txt`, no version typed into CMake and none in `package.json`: every hand-written copy
would be a second value that drifts from the first.

| Tree | Version |
| --- | --- |
| exactly on `v0.1.0`, clean | `0.1.0` |
| N commits after `v0.1.0` | `0.1.1-dev.N+g<sha7>` — the next patch, as a pre-release |
| the same, dirty tree | `0.1.1-dev.N+g<sha7>.dirty` |
| on the tag but dirty | `0.1.1-dev.0+g<sha7>.dirty` — never the release number |
| N commits after `v0.2.0-rc.1` | `0.2.0-rc.1.dev.N+g<sha7>` — still below `0.2.0` |
| no `v*` tag at all | `0.0.0-dev.N+g<sha7>` |
| no git (a source archive) | `0.0.0+unknown` |

Only tags of the form `v` followed by a digit are versions; any other tag is ignored. A tag that
is not valid SemVer (for example `v0.01.0`) is refused, and so is a version longer than 31
characters — `esp_app_desc_t.version` holds 31, and ESP-IDF would truncate silently.

```sh
python3 tools/version.py                   # print this checkout's version
python3 tools/version.py --check-tag v0.1.0  # fail unless the checkout IS that tag
```

### How it reaches the image

`CMakeLists.txt` includes `tools/version.cmake`, which calls `tools/version.py --cmake` and sets
`PROJECT_VER` **before** `project()` — the point at which ESP-IDF takes the given version instead
of running its own `git describe`. ESP-IDF's default would not be SemVer. The same call registers
the git files whose change must re-run CMake (HEAD, the branch ref, the tags, packed refs).

PlatformIO decides on its own whether to re-run CMake and does not look at git, so the
`tools/pio_version.py` pre-build hook compares the current version with the one stamped in the
build directory and forces a re-configure on a difference. That also catches a tree turning dirty
or clean, which no git file records. The version the running firmware reports in `/api/system` is
therefore exactly the string `tools/version.py` printed.

## When to bump what

The tag is chosen by a person; `tools/version.py` only derives everything between tags from it.
The rule follows [SemVer 2.0.0](https://semver.org/spec/v2.0.0.html), with the device's contract
as the public API:

| Bump | When |
| --- | --- |
| **MAJOR** | something another system relies on breaks: a REST path, method or response shape; an MQTT topic or payload; an entity key or `SCHEMA_VERSION` (`tools/pdo_table.py`); the NVS layout (keys, namespaces); a partition-table change that needs a USB flash; a feature removed |
| **MINOR** | something is added: an entity, an endpoint, a response field, a feature, newly accepted values |
| **PATCH** | a fix that leaves the contract as it was; a translation; documentation that ships in the image |

While MAJOR is 0 SemVer treats the contract as unstable, but the rule above applies already, so
that reaching 1.0.0 changes nothing about how versions are chosen. `1.0.0` is tagged when the
acceptance criteria in `AGENTS.md` hold on real hardware.

## The changelog

`CHANGELOG.md` follows [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/):

- `## [Unreleased]` on top, collecting entries under `### Added`, `### Changed`, `### Fixed`,
  `### Security` and so on;
- one `## [X.Y.Z] - YYYY-MM-DD` section per release;
- `[X.Y.Z]: <url>` link reference definitions at the end.

At release time, move the Unreleased entries into a new `## [X.Y.Z] - YYYY-MM-DD` section. That
section becomes the text of the GitHub release, and a release without one fails before anything
is built.

```sh
python3 tools/changelog.py --section 0.1.0   # or v0.1.0, or Unreleased
```

`tools/changelog.py` prints the section body without its heading. It stops at the next section,
at a top-level heading, or at the link references, and `0.1` does not match `[0.1.0]`. A missing
or empty section is an error.

## Assembling `dist/`

After `pio run -e m5stack-nanoc6`, `tools/make_release.py` builds `dist/`, which is both the
GitHub Pages site and the set of release assets:

| File | What it is |
| --- | --- |
| `comfoair-q-<version>-bootloader.bin` | bootloader |
| `comfoair-q-<version>-partitions.bin` | partition table |
| `comfoair-q-<version>-otadata.bin` | initial OTA data |
| `comfoair-q-<version>-firmware.bin` | the application |
| `comfoair-q-<version>-ota.bin` | the same application, named for upload through the web UI |
| `manifest.json` | the ESP Web Tools manifest |
| `index.html` | the browser installer page, from `installer/index.html` |

Everything in the manifest is **read from the build**, not typed: the part offsets from
`flasher_args.json`, cross-checked against the partition table actually in `partitions.bin`; the
chip family from the app image header; the version from `esp_app_desc_t` inside `firmware.bin`.
The installer page's `@VERSION@` and `@OTA_BIN@` tokens are filled in, and a page that lost a
token or grew an unknown one is refused.

Two deliberate choices guard the owner's settings:

- **Four parts, never the merged factory image.** A merged image pads every gap with `0xFF`, so
  writing it erases NVS — Wi-Fi, web password, broker — on every install, updates included.
  `check_layout()` refuses any part set that overlaps NVS.
- **`new_install_prompt_erase: true`.** The firmware does not speak Improv, so every install looks
  like a new one to ESP Web Tools. With this flag it asks, unticked by default; left false it would
  erase the device unasked. `new_install_improv_wait_time` is `0` for the same reason.

A version that is not a release (a `-dev` pre-release, or `+g<sha>`/`.dirty` build metadata) is
refused: a published image must be rebuildable from its tag. `--allow-unreleased` (alias
`--allow-dirty`) overrides that for local testing; the version's own suffix then marks the build
on the page and in the manifest. Other options: `--build-dir` (default
`.pio/build/m5stack-nanoc6`), `--installer` (default `installer/`), `--out` (default `dist/`).

### Building a release locally

```sh
pio run -e m5stack-nanoc6
python3 tools/make_release.py --allow-unreleased   # drop the flag on a clean tagged tree
python3 -m http.server -d dist 8000                 # then open http://localhost:8000 in Chrome
```

ESP Web Tools needs Web Serial, so the install button works in Chromium-based browsers.

## The release workflow

`.github/workflows/release.yml` runs on a pushed `v*` tag, and on `workflow_dispatch`. Only a tag
ref reaches a release or the Pages site; a dispatch from a branch builds and keeps the artefact for
inspection. It is triggered by a tag and not by pushes to `main` because a person clicking
Install must never receive an unreviewed commit.

**`build`** (read-only permissions):

1. Check out with full history (`fetch-depth: 0`) — a shallow clone has no tags and would stamp
   `0.0.0-dev`.
2. Set up Python 3.12.
3. On a tag: `tools/version.py --check-tag` proves the checkout is the tag, and
   `tools/changelog.py --section` extracts the release notes — ten seconds instead of a failed
   publish after a ten-minute build.
4. Set up Node 24 (the firmware build runs `npm ci` and `npm run build` in `web/` itself) and
   restore the PlatformIO cache (`~/.platformio`) keyed on `platformio.ini`.
5. Install PlatformIO 6.1.19, run `pio test -e native`, install pytest and run
   `python -m pytest tests/python -q`.
6. `pio run -e m5stack-nanoc6`.
7. `tools/make_release.py` (with `--allow-unreleased` when not on a tag).
8. Read the version back from `dist/manifest.json`; on a tag it must equal the tag, or the page
   would offer one thing and install another. The OTA image size is reported against the
   2 031 616-byte app slot in the job summary.
9. Upload `dist/` and, on a tag, the release notes as artefacts.

The workflow runs neither the web suites (`npm test`) nor the documentation-site build.

**`publish`** (tags only, `contents: write`): download the artefacts, upload `dist/` as the Pages
artefact, create the GitHub release from the notes (or update its notes on a re-run), and attach
every file in `dist/` with `--clobber`.

**`deploy`** (`pages: write`, `id-token: write`): publish to GitHub Pages. Pages must be enabled
with "GitHub Actions" as the source; otherwise this job fails with a 404 from `configure-pages`.

Runs are queued, never cancelled: a half-deployed site or a half-uploaded release is worse than a
wait.

## Checklist

1. Move the Unreleased entries in `CHANGELOG.md` into `## [X.Y.Z] - YYYY-MM-DD` and commit.
2. Make sure the tree is clean and the gates are green (`pio test -e native`,
   `python3 -m pytest tests/python -q`, `cd web && npm test && npm run build`).
3. Tag `vX.Y.Z` on that commit and push the tag — with the owner's permission.
