# comfoair_ota — the pure pre-flight on an uploaded firmware image, and the answer for every refusal

> This is documentation, not code — re-read the source before relying on a detail.

`POST /api/ota` receives a `.bin` and writes it into the spare OTA slot. The first call that
touches flash (`esp_ota_begin()`) starts erasing that slot, so a wrong file that gets that far
costs the spare image. Owners make predictable mistakes: they pick the `.elf` beside
`firmware.bin`, a gzip, a stray text file, an image built for another chip, or the build of
their *other* ESP32-C6 firmware (the OpenTherm thermostat), which passes the chip check. This
component holds the cheap checks that tell those apart from a real image of this firmware, using
only the first bytes of the body, plus the HTTP status line and the owner-facing sentence for
every way an upload can fail. It is pure C (no IDF, no flash, no HTTP, no `esp_err_t`), so every
refusal and its wording is host-tested.

## Responsibility

Owns:

- The length check before the body is read: Content-Length present, at least
  `COMFOAIR_OTA_MIN_IMAGE_BYTES`, at most the update slot.
- The image-header check on the first 24 bytes: magic `0xE9`, `chip_id` equal to the running chip.
- The app-descriptor check on the first `0x70` bytes: descriptor magic and `project_name` equal to
  the running firmware's, plus extracting the uploaded image's version string.
- The fault enum `comfoair_ota_fault_t` and, for each fault, the HTTP status line and the English
  `{"error": ...}` sentence the web UI shows verbatim.

Does NOT do:

- Read the request, call `esp_ota_*`, map `esp_err_t` onto faults, or answer HTTP — that is
  `comfoair_http` (`comfoair_http_ota.c`, and `comfoair_http_ota_preflight.c`, which supplies the
  running chip id and project name and chooses the order of the checks).
- Verify the image. `esp_ota_end()` runs IDF's full verification (segments, checksum, appended
  SHA-256) and that is what decides; a failure there surfaces as `COMFOAIR_OTA_VERIFY_FAILED`.
- Check a signature. Secure boot is off; anyone who can POST `/api/ota` can run code. Access is
  `comfoair_policy`'s job: it refuses `/api/ota` outright (fails closed) on a device with no
  password, and requires authentication otherwise. These checks guard against the *wrong* file,
  not a hostile one.
- Probation, confirmation and the delayed restart into a new image — `comfoair_ota_runtime`.
- The browser-side courtesy pre-flight — `web/src/pages/update/model.ts` and `image.ts` mirror these
  constants so a wrong file is refused before upload; the device check remains the gate.

## Public API

Header: `include/comfoair_ota.h`. All functions are pure, reentrant, callable from any task, keep no
state and allocate nothing. None returns `esp_err_t`; the caller handles a `comfoair_ota_fault_t`.

| Symbol | Contract |
| --- | --- |
| `comfoair_ota_check_length(has_length, content_length, slot_bytes)` | Before reading the body. `!has_length` → `NO_LENGTH`; `< MIN_IMAGE_BYTES` → `TOO_SMALL`; `> slot_bytes` → `TOO_LARGE` (exactly the slot size is OK); else `OK`. |
| `comfoair_ota_check_header(head, len, expected_chip_id)` | On the first `COMFOAIR_OTA_HEADER_BYTES` bytes, before `esp_ota_begin()`. NULL or `len < 24` → `NOT_AN_IMAGE`; byte 0 `!= 0xE9` → `NOT_AN_IMAGE`; LE u16 at bytes 12–13 `!= expected_chip_id` → `WRONG_CHIP`; else `OK`. The magic is judged before the chip. |
| `comfoair_ota_check_app_desc(head, len, expected_project, version_out, version_cap)` | On the first `COMFOAIR_OTA_PREFLIGHT_BYTES` bytes. Every refusal is `WRONG_PROJECT`: NULL `head`/`expected_project`, `len < 0x70`, descriptor magic wrong, `project_name` with no NUL in its 32 bytes, or a name `!= expected_project`. `version_out` (may be NULL; ignored when `version_cap == 0`) is always emptied first, then filled with the image's version (NUL-terminated, at most `version_cap - 1` chars, at most 32) once the descriptor magic matches — even when the project then turns out foreign, so the refusal log names what was uploaded. |
| `comfoair_ota_status_line(fault)` | Static status line, never NULL. `OK` → `"200 OK"`; `NO_LENGTH` → 411; `TOO_SMALL`/`NOT_AN_IMAGE`/`WRONG_CHIP`/`WRONG_PROJECT`/`VERIFY_FAILED` → `"422 Unprocessable Entity"`; `TOO_LARGE` → `"413 Content Too Large"`; `TRUNCATED` → 400; `BUSY`/`UNCONFIRMED` → `"409 Conflict"`; `FLASH_ERROR`/`NO_SLOT` and any out-of-enum value → 500. |
| `comfoair_ota_message(fault)` | Static English sentence for the owner, never NULL. `OK` → `""`; out-of-enum → a generic "unknown reason" sentence. Sent unformatted (the handler JSON-escapes it). |

Faults (`comfoair_ota_fault_t`, `OK = 0`): `NO_LENGTH`, `TOO_SMALL`, `TOO_LARGE`, `NOT_AN_IMAGE`,
`WRONG_CHIP`, `TRUNCATED`, `VERIFY_FAILED`, `BUSY`, `FLASH_ERROR`, `NO_SLOT`, `UNCONFIRMED`,
`WRONG_PROJECT`. Only `NO_LENGTH`..`WRONG_CHIP` and `WRONG_PROJECT` come from this component's
checks; the rest are raised by `comfoair_http_ota.c` from IDF results or handler state and exist
here only so every answer and its wording live in one host-tested table.

Constants and why:

| Constant | Value | Why |
| --- | --- | --- |
| `COMFOAIR_OTA_HEADER_BYTES` | 24 | the whole `esp_image_header_t`; the header check needs nothing past it |
| `COMFOAIR_OTA_IMAGE_MAGIC` | `0xE9` | ESP image magic at byte 0; a gzip starts `1F 8B`, an ELF `7F 'E' 'L' 'F'` |
| `COMFOAIR_OTA_MIN_IMAGE_BYTES` | 64 KiB | far below the real image (~1.3 MB) but above any stray text file, so such a file never erases a slot |
| `COMFOAIR_OTA_APP_DESC_OFFSET` | `0x20` | `esp_app_desc_t` follows the 24-byte image header and the 8-byte first segment header |
| `COMFOAIR_OTA_PREFLIGHT_BYTES` | `0x70` | through the end of `project_name` (`0x50 + 32`); a `_Static_assert` in `comfoair_ota.c` pins it |
| `COMFOAIR_OTA_APP_DESC_MAGIC` | `0xABCD5432` | `ESP_APP_DESC_MAGIC_WORD`, little-endian in flash |

Inside the descriptor, `version[32]` sits at `0x30` and `project_name[32]` at `0x50`
(`APP_DESC_VERSION_AT` / `APP_DESC_PROJECT_AT`, private to `comfoair_ota.c`). These offsets were
checked against this firmware's own `firmware.bin`; the bytes are pinned in the test.

## Implementation

- `include/comfoair_ota.h` — the contract above, the fault enum, the layout constants.
- `comfoair_ota.c` — the three checks and the two answer tables (168 lines).
- `CMakeLists.txt` — `SRCS comfoair_ota.c`, `INCLUDE_DIRS include`, no REQUIRES/PRIV_REQUIRES.

Flow as the HTTP handler uses it (order lives in `comfoair_http`): probation (`UNCONFIRMED`) →
slot lookup (`NO_SLOT`) → `check_length` → buffer the first `0x70` bytes → `check_header` →
`check_app_desc` → only then `esp_ota_begin()`. Every refusal up to that point leaves flash
untouched. `comfoair_http_ota.c` statically asserts that `PREFLIGHT_BYTES` fits its 4096-byte
first chunk and is below `MIN_IMAGE_BYTES`, so a body that passed the length check always has the
pre-flight bytes.

Invariants and details:

- Multi-byte fields (`chip_id`, descriptor magic) are assembled byte by byte, so host endianness
  and alignment do not matter and the host test is meaningful.
- The uploaded bytes are untrusted: the version copy is bounded by the 32-byte field *and* the
  caller's capacity, and stops at the first NUL.
- `project_name` must contain a NUL within its 32 bytes before it is compared with `strcmp`.
- Both `switch`es name every enumerator and have no `default`; a fall-through return after each
  switch handles out-of-enum values.

DO NOT:

- Make the slot check `>=`. An image exactly the slot size fits: IDF's partition write refuses
  only `size > partition->size - offset`, so the slot's last byte is writable.
- Compare `project_name` with `strncmp(.., 32)`. That accepts 32 non-NUL bytes equal to the first
  32 of a longer expected name; a name with no NUL is not one IDF wrote and is refused.
- Add a `default:` to either switch. `[env:native]` builds with `-Wall -Werror=switch`, so a new
  fault without a status line or message fails the host build; a `default` would swallow it.
- Run `check_app_desc` before `check_header` (in the caller). It reads a gzip's bytes as a
  descriptor and calls every non-image `WRONG_PROJECT`, sending the owner after the wrong mistake;
  the header check gives the actionable "pick firmware.bin" answer.
- Hard-code the expected project or chip in a caller. `comfoair_http_ota_preflight.c` takes
  `esp_app_get_description()->project_name` (cannot drift from `project()` in the top-level
  `CMakeLists.txt`, currently `comfoair-q`) and `CONFIG_IDF_FIRMWARE_CHIP_ID` (the same constant the
  bootloader compares against; `0x000D` on the C6).
- Reword the `WRONG_PROJECT` sentence alone. The web Update page shows the identical sentence for
  a file it refuses locally, and `web/src/i18n/firmwareText.ts` maps several of these sentences
  byte for byte onto translation keys (`update.detail.*`). Change both sides together.

## Dependencies

- Requires: nothing (only `<stdbool.h>`, `<stddef.h>`, `<stdint.h>`, `<string.h>`).
- Depended on by: `comfoair_http` (`REQUIRES ... comfoair_ota ...`; used in `comfoair_http_ota.c`,
  `comfoair_http_ota_preflight.c`, and its `comfoair_http_internal.h` includes the header).
- Mirrored (not linked) by the SPA: `web/src/pages/update/model.ts` (min size, slot size, magic,
  check order), `web/src/pages/update/image.ts` (descriptor offsets, magic, `PROJECT_NAME`).

## Tests

- `test/test_ota/test_ota.cpp` (host, `pio test -e native -f test_ota`; the native env reaches
  the component through `lib_extra_dirs = components` and `-I components/comfoair_ota/include`).
  Covers: length boundaries (no length, `MIN - 1` / `MIN`, `SLOT` / `SLOT + 1` with a 1984 KiB
  slot); a real C6 header captured from `firmware.bin`, wrong chip (C3, high byte), wrong magic,
  ELF and gzip starts, short / NULL input; the app descriptor against a real 0x70-byte head of this
  firmware and of the OpenTherm thermostat's build, wrong magic, unterminated / prefix-matching
  names, short input, version truncation to small capacities and an untouched `cap == 0` buffer;
  every fault has a non-200 status line and a message, the exact fault → status map, the
  `firmware.bin` hint, the `WRONG_PROJECT` sentence word for word, and an out-of-enum fault.
- Web side (not this component, but pinned to its wording): `web/src/pages/update/tests/model.test.ts`,
  `image.test.ts`, and `web/src/i18n/tests/firmwareText.test.ts`, which holds several
  `comfoair_ota_message()` sentences verbatim.
- Not covered on the host: the handler glue that maps `esp_err_t` onto `TRUNCATED`, `BUSY`,
  `FLASH_ERROR`, `VERIFY_FAILED`, `NO_SLOT`, `UNCONFIRMED` and the check order in
  `comfoair_http_ota_preflight.c` — those IDF-bound files are not host-built; they are covered by
  the owner's hardware scenarios in `docs/hardware-verification/08-ota-and-system.md`.

## Notes

- **Why a pure component at all.** The pre-flight is the only thing standing between a wrong file
  and an erased spare slot, and the wording is the owner's only diagnosis. Keeping both out of the
  IDF-bound handler lets the exact boundaries and sentences be pinned on the host.
- **Why the faults the checks never raise are still here.** One table for every answer `POST
  /api/ota` gives means one place where status codes and sentences are reviewed and tested, and the
  `-Werror=switch` guarantee covers them all.
- **Why 413 is spelled "Content Too Large".** IDF's own HTTP server uses that RFC 9110 name for
  its 413; the device answers with one spelling everywhere.
- **Why `BUSY` and `UNCONFIRMED` are 409, not 5xx.** Both mean "not now": the same upload succeeds
  later, after the other upload finishes or the running image is confirmed.
- **Why `TOO_SMALL` and the wrong-file faults are 422.** The request is well-formed; the file in it
  is the wrong one.
- **Why the version is copied before the project verdict.** A refused upload's log line should say
  which build was uploaded, which is how the owner recognises "that was the thermostat's image".
