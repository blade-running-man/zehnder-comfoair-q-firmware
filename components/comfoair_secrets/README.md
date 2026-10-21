# comfoair_secrets — the write-only secret scheme: sentinel, redaction, the keep/clear/store decision, constant-time compare

> This is documentation, not code — re-read the source before relying on a detail.

The settings page loads the whole configuration and submits the whole configuration back. A
stored password must never leave the device, and yet an untouched password field has to
round-trip without wiping what is stored. This component is the server half of that scheme. A set
secret reads back as a published sentinel string. A submitted value is turned into one of four
actions. Password checks go through a constant-time comparison. The client half is
`web/src/api/secrets.ts` (`CONFIG_UNCHANGED`) together with
`web/src/pages/settings/sections/SecretField.tsx`. The two halves only work as a pair.

It is pure C. No ESP-IDF, no allocation, no I/O. It is one of the bottom layers: it requires
nothing.

## Responsibility

Owns:
- The sentinel string `COMFOAIR_SECRET_SENTINEL` and what it means in each direction.
- The word-based rule for which key names carry a secret (`comfoair_secret_key`).
- The outbound redaction rule: set → sentinel, unset → `""`, never the value.
- The inbound decision: absent / sentinel / `""` / anything else → KEEP / CLEAR / STORE / REJECT.
- The constant-time equality used for password and digest checks.

Does NOT do:
- Store, load or hash anything. NVS, the PBKDF2 UI-password record and the projection of the
  config document belong to `comfoair_config`. That component calls `redact`, `decide` and
  `equals`.
- Parse request bodies. `comfoair_wire` (`comfoair_wire_parse_provision`) and `comfoair_config`
  (the patch apply) read the JSON and hand the submitted C string, or NULL, to `decide`.
- Validate a value it was told to STORE (length, charset, Wi-Fi PSK rules). That is
  `comfoair_config` (`comfoair_config_check_psk` and friends).
- Carry out a decision. It returns an enum. The caller copies, clears or refuses, and maps REJECT
  to its own error code (`comfoair_wire` uses `COMFOAIR_CONFIG_ERR_PSK`).
- Redact the Wi-Fi PSK. The stored PSK is a length-prefixed byte array with no terminator, so
  `comfoair_config_project()` applies the same rule by `psk_len` and does not call `redact`.
- Authenticate HTTP sessions. That is `comfoair_auth`, which applies the same
  "empty never matches" rule one layer up.

## Public API

Header: `include/comfoair_secrets.h`. Every function is pure and reentrant except for the probe
counter (see Concurrency). Any task may call them. None returns `esp_err_t`.

| Symbol | Contract |
| --- | --- |
| `COMFOAIR_SECRET_SENTINEL` | `"__UNCHANGED__"`. This is what a set secret reads back as. It must equal `CONFIG_UNCHANGED` in `web/src/api/secrets.ts`. It is a published string, so it must never become a stored credential (see `decide`). |
| `bool comfoair_secret_key(const char *key)` | True if `key` contains any of `password`, `passwd`, `psk`, `secret`, `token` or `key`, ignoring ASCII case. NULL → false. It is a substring match, not a list of exact names. |
| `const char *comfoair_secret_redact(const char *stored)` | Returns `COMFOAIR_SECRET_SENTINEL` when `stored` is non-NULL and non-empty, otherwise `""`. Reads at most one byte of `stored`. It always returns a static literal and never a pointer into `stored`, so the result is safe to keep after the stored buffer changes. |
| `comfoair_secret_action_t` | `KEEP`: untouched, or nothing to change. `CLEAR`: emptied on purpose. `STORE`: store the submitted value. `REJECT`: change nothing and report an error. |
| `comfoair_secret_action_t comfoair_secret_decide(bool has_stored, const char *submitted)` | `submitted == NULL` (key absent) → KEEP. Sentinel with something stored → KEEP. Sentinel with nothing stored → **REJECT**. `""` with something stored → CLEAR. `""` with nothing stored → KEEP. Anything else → STORE. The caller must handle all four values. REJECT must refuse the whole request, not just skip the field. |
| `bool comfoair_secret_equals(const char *stored, const char *candidate)` | Constant-time equality. NULL or empty `stored` → false, and so does NULL `candidate`. So `("", "")` is **false**. It reads every byte of the longer string and folds the length difference into the result instead of returning early. |
| `size_t comfoair_secret_compared_bytes(void)` | How many bytes the last `equals` call read (0 when it bailed out on empty or NULL). It is a test probe and not for production use. |

## Implementation

- `comfoair_secrets.c`: the whole implementation, under 90 lines. `contains_ci()` is a naive
  O(n·m) case-insensitive substring search. It folds case with `| 0x20`, which is correct for
  ASCII letters. It also maps a few punctuation pairs onto each other (`@`/`` ` ``, `[`/`{`),
  but the words in the list are all letters, so that does no harm.
- `include/comfoair_secrets.h`: the contract and the rationale for each rule.
- `CMakeLists.txt`: `idf_component_register(SRCS comfoair_secrets.c INCLUDE_DIRS include)`, with
  no REQUIRES or PRIV_REQUIRES. On the host, `[env:native]` reaches it through
  `lib_extra_dirs = components` and `-I components/comfoair_secrets/include`.

How a secret flows:
1. **Out (GET).** `comfoair_config_project()` sets `mqtt_password` and `ui_password` from
   `comfoair_secret_redact()`. It sets `psk` with the same rule, checked by length.
2. **In (POST/PUT).** `comfoair_config` (broker and UI password in the patch apply) and
   `comfoair_wire_parse_provision()` (the Wi-Fi PSK, called by `comfoair_net` under its machine
   lock because KEEP has to read the stored key) both go through `comfoair_secret_decide()`.
   There is one vocabulary and one place that knows what the sentinel means.
3. **Check (login).** `comfoair_config_check_ui_password_against()` compares the stored digest hex
   with the freshly computed one using `comfoair_secret_equals()`.

Invariants:
- No function returns or copies a stored value. `redact` returns literals only.
- The sentinel is never STOREd. If the sentinel arrives while nothing is stored, the result is
  REJECT, not STORE.
- An empty or absent stored secret matches no candidate in `equals`.

Concurrency: `s_compared` is a plain file-static `size_t`, written on every `equals` call with no
lock or atomic. Concurrent callers race on it. That only corrupts the diagnostic count, never the
comparison result. Do not read it anywhere except a single-threaded test.

DO NOT:
- Swap `comfoair_secret_key`'s word list for a list of exact key names. A future key such as
  `"MQTT Password"` or `"API Token"` would then be published in full until someone remembered to
  extend the list.
- Add a word to the list without checking every existing config field name. The
  `test_config` cross-check asserts that each field's `secret` flag agrees with this function,
  and a broad word would flip a harmless field to secret.
- Let `decide` STORE the sentinel when nothing is stored (the "obvious" simplification of treating
  the sentinel as an ordinary value). It is a public string, so it would become a working
  password on every unconfigured device. For the Wi-Fi PSK, a stored but unusable key also means
  the device raises no setup access point.
- Make `redact` return `""` for a set secret, or have GET leave the key out. The page echoes back
  everything it loaded, so an untouched field would come back as `""`, be read as CLEAR, and
  saving an unrelated setting would wipe the credentials.
- Replace `equals` with `strcmp`/`memcmp`, or add an early return on a length mismatch. An early
  exit tells an attacker how much of a guess was right.
- Let `equals("", "")` return true. That would turn "no password set" into "the password is the
  empty string".
- Remove `comfoair_secret_compared_bytes()` as dead code. It is the only way the host suite can
  pin the constant-time property, because a test cannot measure the clock reliably.

## Dependencies

Requires: nothing (only `<string.h>`, `<stdbool.h>` and `<stddef.h>`).

Depended on by:
- `comfoair_config`: public `REQUIRES comfoair_secrets`, because `comfoair_config.h` includes
  this header. It uses `redact`, `decide`, `equals` and the sentinel.
- `comfoair_wire`: public `REQUIRES`, because `comfoair_wire.h` includes this header. It uses
  `decide` for `wifi_psk`.
- It reaches `comfoair_net` and `comfoair_http` only through those two components.
- `comfoair_json` deliberately does **not** depend on it.

`comfoair_secret_key()` has no production call site in `components/` or `src/`. It is referenced
in comments in `comfoair_config`, `comfoair_wire` and `comfoair_provision` as the rule for what
must never be logged or sent. It is enforced by the `test_config` cross-check against
`comfoair_config_field_info_t.secret`.

## Tests

- `test/test_secrets/test_secrets.cpp` (14 cases) is the suite for this component. It covers key
  matching (positive and negative), redaction of set, empty and NULL values, all six
  `decide` branches including REJECT of the sentinel when nothing is stored, `equals`
  correct/wrong/prefix/suffix, empty and NULL stored values, and the constant-time probe (7 bytes
  read for both a 7-byte and a 1-byte guess against `"hunter2"`). Run it with
  `pio test -e native -f test_secrets`.
- Indirect coverage:
  - `test/test_config` asserts that every config field's `secret` flag equals
    `comfoair_secret_key(name)`, projects secrets as the sentinel, and refuses the sentinel when
    nothing is stored.
  - `test/test_wire` covers the sentinel round trip for `wifi_psk`.
  - `test/test_auth` pins the empty-password rule one layer up.
  - `web/src/pages/settings/tests/model.test.ts` covers the client half.

Not covered:
- Real timing. The probe counts bytes read, not cycles. The data-dependent `i < len ? … : 0`
  selects inside the loop are not checked for branchlessness on the RISC-V target.
- That `COMFOAIR_SECRET_SENTINEL` and the TS `CONFIG_UNCHANGED` are equal. No test compares the
  two files. A drift would only show on the device.

## Notes

- **Substring matching errs on the side of over-redaction, on purpose.** If a non-secret field is
  over-redacted, it just shows as "unchanged" and the user retypes it. If a credential is
  under-redacted, it gets published. `"key"` is in the list, so any field named `*key*` is
  treated as secret. `"psk"` is there because that is Wi-Fi's word for it, and no other config
  name contains those three letters.
- **Why a sentinel and not an omitted key.** Absent and empty must stay different. Absent (NULL)
  means "nothing was said" → KEEP. `""` means "the user emptied it" → CLEAR. The sentinel means
  "the page echoed back what it was shown" → KEEP.
- **`equals` leaks the length of the longer string.** The loop runs `max(len(stored),
  len(candidate))` times, so the time reveals the longer of the two lengths, but not where the
  first difference is. The one production caller compares two fixed-length digest hex strings,
  so nothing is leaked there.
- **The sentinel is 13 printable bytes**, so it is a valid WPA2 PSK as far as length and charset
  go. That is why REJECT cannot be left to downstream validation: nothing downstream would catch
  it.
