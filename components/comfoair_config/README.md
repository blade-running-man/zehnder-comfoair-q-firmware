# comfoair_config — the persisted device configuration: validation, projection, NVS storage and the two-tier reset

> This is documentation, not code — re-read the source before relying on a detail.

Everything that has to survive a power cut lives here: the Wi-Fi pair, the broker (host, port,
user, password), the topic prefix, the HA-discovery flag, the device name and the web-UI
password record. It also names every NVS namespace and key the firmware uses for those, plus
the keys other components store into its namespaces (durations, provisioning flags). The
problem it solves is that a bad value stored once outlives every reboot: a device that panics
on its own configuration, or that ends up with no network, no access point and no way in,
cannot be recovered over the air. So values are validated on the way in, re-validated on the
way out of flash, and secrets never leave through any projection.

The component has two halves. The pure half (`comfoair_config.c`, `_check.c`, `_password.c`,
`_apply.c`; no ESP-IDF header) holds every decision; `comfoair_config_nvs.c` is the device-only
flash layer, wrapped in `#ifdef ESP_PLATFORM`, that opens namespaces, moves bytes and asks the
pure half.

## Responsibility

Owns:
- The in-memory document `comfoair_config_t`, its defaults, and the all-or-nothing patch apply.
- Per-field validators and the owner-facing error sentences (`comfoair_config_strerror`).
- The client projection `comfoair_config_public_t` (secrets → sentinel or `""`).
- The UI-password record format `1$<iter>$<salt hex>$<digest hex>` and its constant-time check.
- The single "is this Wi-Fi record usable" predicate (`comfoair_config_wifi_usable`).
- Sanitising a document read from flash, and reporting what was replaced as a bitmask.
- Schema versioning and the read-only mode on a newer stored schema (OTA rollback).
- Every NVS namespace/key name, the legacy Wi-Fi migration, the known-good Wi-Fi slot, the two
  provisioning flags, the namespace-based two-tier reset, and the device PBKDF2 + salt.

Does NOT do:
- Hold the live document or lock it — `comfoair_net` owns `g_net_config` (guarded by its
  `g_net_machine` mutex) and calls load/apply/save/project.
- Parse JSON request bodies into `comfoair_config_patch_t` — `comfoair_json` / `comfoair_wire`.
- Decide *when* to commit/restore known-good credentials or set the prov flags —
  `comfoair_provision` decides, `comfoair_net` executes.
- Sessions, login throttling or cookies — `comfoair_auth` (it calls
  `comfoair_config_check_ui_password_against` with `comfoair_config_device_kdf()`).
- Detect the reset gesture — `comfoair_button` (it calls `comfoair_config_nvs_reset`).
- Read or write the four durations / permanent-manual setting — `comfoair_control` does, using
  the key names defined here, in `cfg_app`. They are registry entities, not part of the
  configuration document, and do not appear in `GET /api/config`.
- Define the sentinel or the secret-word redaction — `comfoair_secrets`.

## Public API

Pure half (`include/comfoair_config.h`), callable from any task, no allocation, no logging:

| Symbol | Contract |
| --- | --- |
| `comfoair_config_field(f)` | Row of the field table (`name`, NVS `key`, namespace, `secret`). NULL for an out-of-range id or an unfilled row — callers must handle NULL. |
| `comfoair_config_ns_name(ns)` | Namespace string, NULL out of range. |
| `comfoair_config_reset_erases(tier, ns)` | HARD → every namespace; SOFT → `cfg_wifi`, `cfg_owner`, legacy `comfoair`. Ask this; never list namespaces at the call site. |
| `comfoair_config_check_*()` | One validator per field, returns `comfoair_config_err_t`. NULL is treated as `""`. SSID/PSK take an explicit length (not C strings). `check_port` takes `uint32_t` so 70000 is refused, not wrapped to 4464. |
| `comfoair_config_wifi_usable(w)` | `check_ssid && check_psk` over the record. The one predicate both "has credentials" and `sanitize` use. |
| `comfoair_config_wifi_from_legacy(out, ssid, psk)` | Builds a record from the old two-string pair; `out` always zeroed first. **false = do not store, do not erase the source.** NULL/empty psk = open network. |
| `comfoair_config_strerror(err)` | Owner sentence; never echoes a value (strings reach the public log ring). Never NULL. |
| `comfoair_config_defaults(cfg, device_id)` | Zeroes and fills defaults: port 1883, discovery on, prefix `comfoair/<mac12>`, name `ComfoAir Q <last 4 of mac>`. Malformed id → `comfoair` / `ComfoAir Q`. |
| `comfoair_config_apply(cfg, patch, hash)` | All-or-nothing: validates every field, then writes. NULL field = absent (keep), `""` = clear. Returns `ERR_NO_DOCUMENT` for NULL cfg/patch, `ERR_READ_ONLY` when `cfg->read_only`, `ERR_WIFI_PAIR` for half a pair or sentinel PSK with a changed SSID, `ERR_NO_HASH` if a UI password is set without a usable hash ctx or the KDF fails. Does not persist — caller then calls `comfoair_config_nvs_save`. |
| `comfoair_config_project(cfg, out)` | The only exit from `comfoair_config_t`. Every pointer is strlen-safe; secrets are `COMFOAIR_SECRET_SENTINEL` or `""`. Pointers borrow from `cfg`. Safe on an unsanitised document. |
| `comfoair_config_password_set(cfg)` | Derived: `ui_pw_hash[0] != '\0'`. Also used by the HTTP access policy. |
| `comfoair_config_hash_password(pw, ctx, out, cap)` | Renders the record; writes nothing on failure (rendered into a local first). Refuses iterations above `_KDF_ITERATIONS_MAX`. |
| `comfoair_config_check_ui_password_against(record, cand, kdf)` | Constant-time digest compare via `comfoair_secret_equals`. Empty candidate or unparsable record → false. Costs one PBKDF2 (~100 ms) — call at login only. |
| `comfoair_config_sanitize(cfg, device_id)` | Repairs a document read from flash; returns `COMFOAIR_CONFIG_REPAIRED(field)` bits. Never logs. |
| `comfoair_config_schema_check(stored)` | `== 1` CURRENT, `< 1` (incl. 0 = fresh) MIGRATE, `> 1` FUTURE. |

Device half (`include/comfoair_config_nvs.h`, `ESP_PLATFORM` only, flash I/O, call from a task
that may block — in practice `comfoair_net`'s and `comfoair_button`'s):

| Symbol | Contract |
| --- | --- |
| `comfoair_config_nvs_load(cfg, id, repairs)` | Always leaves `cfg` bootable (defaults first). Returns ESP_OK or the open error of `cfg_app`; **a failure is not fatal** and must not be ESP_ERROR_CHECKed. `repairs` (nullable) = sanitize bits OR values present but too long for this build. |
| `comfoair_config_nvs_save(cfg)` | Writes wifi blob, `cfg_app`, `cfg_owner`, and the schema number; attempts every field, returns the first error. `ESP_ERR_INVALID_STATE` when `read_only`. |
| `comfoair_config_nvs_save_wifi(w)` | Only the `sta` blob (POST /api/provision path). Not gated on `read_only`. |
| `comfoair_config_nvs_commit_known_good()` | Copies `sta` → `sta_good`. `ESP_ERR_NVS_NOT_FOUND` if nothing stored. |
| `comfoair_config_nvs_restore_known_good(out)` | Copies `sta_good` → `sta`; `out` (nullable) gets the pair. `ESP_ERR_NVS_NOT_FOUND` on first provisioning (a state, not a fault). |
| `comfoair_config_nvs_has_credentials()` / `_has_known_good()` | Blob present, right size, and `comfoair_config_wifi_usable`. |
| `comfoair_config_nvs_load_prov_flags(wc, na)` | Never fails; absent/unreadable → false (the safe answer). Neither pointer may be NULL. |
| `comfoair_config_nvs_save_prov_flags(wc, na)` | Both u8 keys, one commit. Call on a transition only (flash wear). Not gated on `read_only`. |
| `comfoair_config_nvs_reset(tier)` | `nvs_erase_all` per namespace the tier names; continues past failures, returns the first error. |
| `comfoair_config_device_kdf()` | PBKDF2-HMAC-SHA256 via `mbedtls_pkcs5_pbkdf2_hmac_ext`. |
| `comfoair_config_device_hash_ctx(ctx, salt)` | Fills `salt` (caller-owned, must outlive `apply`) with `esp_fill_random`, iterations 10000. |

Key constants and why:
- Names (`cfg_wifi`, `cfg_owner`, `cfg_app`, legacy `comfoair` with `wifi_ssid`/`wifi_psk`;
  keys `schema`, `sta`, `sta_good`, `win_closed`, `no_addr`, `dur_boost`, `dur_bypass`,
  `dur_balance`, `dur_away`, `manual_perm`; field keys equal field names except Wi-Fi → `sta`
  and `ui_password` → `ui_pw_hash`) are a contract: renaming one makes an OTA forget data.
  `COMFOAIR_CONFIG_NVS_NAME_MAX 15` — NVS refuses (not truncates) a 16-char key.
- `SCHEMA_VERSION 1` — bump only when a stored value's meaning/layout changes.
- `SSID_MAX 32`, `PSK_MAX 64`, `PSK_MIN 8` — 802.11 / WPA2 limits; both extremes are legal and
  stored byte for byte. 64 must be hex (wpa_supplicant picks hex-vs-passphrase by length alone
  and fails silently on bad hex).
- `HOST_MAX 253` (RFC 1035), `USER_MAX 128`, `BROKER_PASS_MAX 128`, `PREFIX_MAX 64`,
  `NAME_MAX 32` (bytes, so UTF-8 names are shorter in characters).
- `UI_PASS_MIN 8`, `UI_PASS_MAX 128` — no lockout exists, so the search space carries the
  defence; DO NOT lower the minimum to allow PINs.
- `SALT_LEN 16`, `DIGEST_LEN 32`, `HASH_MAX 127` (record is 105 chars today),
  `KDF_ITERATIONS 10000` (~100 ms on a C6), `KDF_ITERATIONS_MAX` = 100×. The max is shared by
  writer and parser so nothing written is unreadable, and it bounds the cost a corrupted digit
  string could impose per login.
- `DEVICE_ID_LEN 12` — full STA MAC as hex; identity is the MAC, never the name.

## Implementation

Files:
- `include/comfoair_config.h` — pure contract: names, sizes, types, validators, apply, sanitize.
- `include/comfoair_config_nvs.h` — device-only flash contract.
- `comfoair_config.c` — field table `FIELDS[]`, `NS_NAMES[]`, the reset tiers, `terminated()`,
  defaults, the projection, sanitize and the schema check: everything that reads the whole document or the
  table, and the one place a key name is written down.
- `comfoair_config_check.c` — one checker per field, `wifi_usable`, `wifi_from_legacy`,
  `strerror`: what a single value must look like, with no document in sight, so the rules
  `apply`, `sanitize` and `comfoair_wire` all ask live in one file.
- `comfoair_config_password.c` — the UI password record: hex codec, `comfoair_config_parse_record()`,
  `hash_password`, `check_ui_password_against`. Its own file because it is a format with a
  reader and a writer that must agree, and nothing else here encodes or decodes hex.
- `comfoair_config_apply.c` — `comfoair_config_apply()` and its bounded `copy_str()`: the one
  write path, all-or-nothing, long enough to be a file on its own.
- `comfoair_config_internal.h` — private, beside the sources: `is_hex()` as `static inline`,
  `password_record_t`, and `comfoair_config_parse_record()` (shared by `_password.c` and
  `sanitize`). A name leaving one file is prefixed `comfoair_config_`; `terminated()` has one
  user and stays `static` in `comfoair_config.c`.
- `comfoair_config_nvs.c` — whole file under `#ifdef ESP_PLATFORM`; load/save, migration,
  known-good, prov flags, reset, device KDF.
- `CMakeLists.txt` — lists every source in `SRCS` (no globbing on the device);
  `REQUIRES comfoair_secrets nvs_flash mbedtls esp_hw_support` (public,
  because the headers include `comfoair_secrets.h` and `esp_err.h`; `esp_hw_support` named
  explicitly for `esp_random.h`).

Load flow (`comfoair_config_nvs_load`): defaults → open `cfg_app` READONLY and read `schema`
(missing = 0) → `read_only = (FUTURE)` → read app strings/u16/bool field by field → read
`ui_pw_hash` from `cfg_owner` → read the `sta` blob → if no blob and MIGRATE, migrate legacy
Wi-Fi → `sanitize` → write `schema = 1` only if MIGRATE, not read-only, and `cfg_app` opened OK
or was absent.

Apply flow: NULL checks → read-only gate → Wi-Fi pair decision (KEEP when PSK is the sentinel
and SSID unchanged; CLEAR when both `""`; SET otherwise, both validated) → validate every other
present field, `comfoair_secret_decide` for both passwords, derive the new UI record into a
local → only then commit. The "nothing can fail" section after validation is where writes live.

Internal helpers worth knowing: `terminated()` (memchr for NUL before any strlen on flash data),
`or_empty()`, bounded `copy_str()`, `comfoair_config_parse_record()` (hand-parsed, no sscanf/strtok),
`load_str()` (probe length first; too long = dropped and reported, never truncated),
`load_bool()` (u8 normalised), `load_wifi()` (blob size must equal `sizeof(comfoair_wifi_t)` —
the size is the version), `erase_namespace()` (READONLY probe first), `key_of()` (NVS addressed
by field enum, so repair bits and keys cannot drift).

Invariants:
- No `ESP_ERROR_CHECK` on anything from flash; every NVS failure returns to a caller that boots
  on defaults.
- The Wi-Fi pair is one blob under one key; `apply` refuses half a pair.
- `comfoair_wifi_t` arrays are not NUL-terminated; always use `ssid_len`/`psk_len`.
- `comfoair_config_t` contains the broker password in plaintext — never log it or pass it to a
  renderer; only `comfoair_config_project()` leaves it.
- Namespaces opened READONLY wherever only reading/probing, because READWRITE creates them.
- No locking in this component; the owner of the document (`comfoair_net`) serialises access.

DO NOT:
- Split `comfoair_wifi_t` into two NVS keys or two patch-apply steps.
- `strncpy(dst, src, sizeof dst - 1)` into SSID/PSK — truncates legal 32/64-byte values.
- Remove `terminated()` because tests pass without it (ASan shows strlen overruns on a 0xff
  document; the length checks only catch it after the overread).
- Replace `comfoair_config_wifi_usable` with a length test in `has_credentials` or `sanitize` —
  two predicates produce "credentials present" with an empty record: no network, no AP.
- Keep an unparsable UI password record in `sanitize` "for security" — it locks the owner out
  of a device whose button may be behind a front panel; clearing leaves it visibly open.
- Add a stored "password is set" flag — it can be written without the record.
- Gate `save_wifi`, `commit/restore_known_good` or `save_prov_flags` on `read_only` — they are
  the way back on a rolled-back device.
- Use `comfoair_config_nvs_reset(SOFT)` to clear the legacy namespace during migration — it
  would also erase the new pair and the UI password.
- Erase the legacy source when `wifi_from_legacy` refuses it, or before the new key is saved.
- Treat any legacy PSK read error other than `ESP_ERR_NVS_NOT_FOUND` as "open network".
- Erase by key list instead of `nvs_erase_all` per namespace.
- Read a stored bool directly as `_Bool` or write the document as one blob.
- Run the password check per request; verify at login and carry a session.
- Reuse the "salt may come from a weak RNG" reasoning for keys or tokens.

## Dependencies

Requires: `comfoair_secrets` (sentinel, `comfoair_secret_redact`, `comfoair_secret_decide`,
`comfoair_secret_equals`), `nvs_flash`, `mbedtls` (PKCS5), `esp_hw_support` (`esp_random`).

Depended on by (REQUIRES/PRIV_REQUIRES or include): `comfoair_net` (main owner: load, apply,
save, project, provisioning credentials and flags), `comfoair_wire` (validators, `strerror`,
`apply` in its contract, `has_credentials`), `comfoair_auth` (UI password check + device KDF),
`comfoair_button` (two-tier reset), `comfoair_control` (duration/manual key names in `cfg_app`),
`comfoair_http` (`comfoair_http_gate.c`, `comfoair_http_read.c`, `comfoair_http_config.c`,
`comfoair_http_system.c`: types and `DEVICE_ID_LEN`),
`comfoair_mqtt` (size constants), `src/app_mdns.c` (projection, `DEVICE_ID_LEN`).

## Tests

- `test/test_config` — the component's own suite: field table and name/key pins (including
  key length ≤ 15 and secret flag agreeing with `comfoair_secret_key`), namespaces and reset
  tiers, every validator at its bounds, `wifi_usable`/`wifi_from_legacy`, defaults, projection,
  password hashing/parsing/iteration caps, `apply` (all-or-nothing, pair rules, sentinel,
  read-only, no-hash), `sanitize`, schema check, `strerror`.
- `test/test_wire` — `defaults`/`project`/`apply` through the wire layer.
- `test/test_mdns_name` — `check_name` and `sanitize`'s default name.
- `test/test_auth` — compile-time pin of `comfoair_config_check_ui_password_against`'s signature.
- `test/test_ha` — uses `COMFOAIR_CONFIG_NAME_MAX` only.

Not covered on the host: all of `comfoair_config_nvs.c` (compiled out off-device by design —
a fake NVS would only test the fake): load ordering, migration, schema write, known-good copy,
prov flags, reset, and the mbedtls KDF. Hardware verification is the only check for those.

## Notes

- **Absent vs empty.** In a patch, NULL means "leave stored value alone" and `""` means "clear".
  Secrets submitted as the sentinel mean "keep". Conflating absent and empty once wiped broker
  credentials on an unrelated save, which is why `comfoair_secret_decide` exists.
- **An empty-field patch succeeds; a NULL patch does not.** The settings page submits the whole
  document every time, so "nothing changed" is a valid save; a body that failed to parse must not
  read back as "accepted".
- **Two storage policies, one disclosure policy.** The broker password is stored in plaintext
  because MQTT needs it; the UI password is a PBKDF2 record. Neither is ever returned.
- **Too-long stored values are reported, not hidden.** `load_str` drops a value larger than this
  build's field and sets its repair bit, because the default that replaces it passes
  `sanitize` and the loss would otherwise be invisible.
- **Newer schema → read-only, not erase, not refuse to boot.** An OTA rollback keeps
  ventilating with its network and broker; settings writes are refused until a hard reset.
- **Fresh store reads schema 0 → MIGRATE**, so "fresh" is not a third code path; migrating
  nothing finds nothing.
- **Default name uses the STA MAC, the AP SSID/mDNS name use the SoftAP MAC**; the last hex
  digit differs by one. Deliberate: the name is already stored on devices and in HA's device
  registry, and changing its source would rename them.
- **Weak-RNG salt is acceptable** because a salt only needs to be unique, and the first password
  may be set on the AP before Wi-Fi seeds `esp_random`.
- **Hostname validation allows `_`, `:`, `[`, `]`** (real LAN names and IPv6 literals) but not
  `/`, so `mqtt://host` is refused with a readable error instead of never resolving.
- **Topic prefix** refuses empty, leading `$`, leading/trailing/double `/`, `+`, `#`, spaces and
  control characters — each produces a topic tree that is legal-looking but wrong or that gets
  the client disconnected.
