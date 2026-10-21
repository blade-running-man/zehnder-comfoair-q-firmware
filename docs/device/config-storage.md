# Configuration storage

Everything configurable lives in NVS and is entered at runtime; nothing is set at compile time and
no secret is in the binary. Two components own it:

| Component | Role |
| --- | --- |
| `comfoair_config` | `comfoair_config.c`, `comfoair_config_check.c`, `comfoair_config_password.c` and `comfoair_config_apply.c` are **pure**: names, validation, defaults, the all-or-nothing patch, the password record format, repair of what came back from flash, schema decisions, which namespaces a reset erases. Host suite `test/test_config`. `comfoair_config_nvs.c` is the device half that opens namespaces and moves bytes |
| `comfoair_secrets` | **pure**: the write-only sentinel, the "is this key a secret" rule (no firmware caller), the constant-time compare |

The loaded document is owned by `comfoair_net` (`g_net_config`), which is the only component that
hands it out: projected (secrets replaced) through `comfoair_net_config_snapshot()`, and as
plaintext broker settings through `comfoair_net_broker()` to the MQTT client only. The configuration page an owner sees is described in
[Configuration](/guide/configuration).

## NVS namespaces and keys

The NVS partition is 16 KiB at `0x9000`. Names are part of the stored contract: renaming one would
make an updated device forget its settings, so `test_config` pins every literal. Keys and
namespaces are at most 15 characters (NVS refuses a 16th).

| Namespace | Key | Type | Contents | Written by |
| --- | --- | --- | --- | --- |
| `cfg_wifi` | `sta` | blob (`comfoair_wifi_t`) | the Wi-Fi SSID and key as **one** record | provisioning; legacy migration; also rewritten unchanged by every `POST /api/config` save (`comfoair_config_nvs_save()`) |
| `cfg_wifi` | `sta_good` | blob | the last pair that produced an address (rollback target) | provisioning |
| `cfg_wifi` | `win_closed` | u8 | a setup window has expired at some point | provisioning |
| `cfg_wifi` | `no_addr` | u8 | last run joined a network and never got an address | provisioning |
| `cfg_owner` | `ui_pw_hash` | string | the one-way UI password record | `POST /api/config` |
| `cfg_app` | `schema` | u32 | stored layout version | config load/save |
| `cfg_app` | `mqtt_host` | string | broker host | `POST /api/config` |
| `cfg_app` | `mqtt_port` | u16 | broker port | `POST /api/config` |
| `cfg_app` | `mqtt_user` | string | broker user | `POST /api/config` |
| `cfg_app` | `mqtt_password` | string | broker password, **plaintext** | `POST /api/config` |
| `cfg_app` | `topic_prefix` | string | MQTT topic prefix | `POST /api/config` |
| `cfg_app` | `ha_discovery` | u8 | publish Home Assistant discovery | `POST /api/config` |
| `cfg_app` | `device_name` | string | display name | `POST /api/config` |
| `cfg_app` | `dur_boost`, `dur_bypass`, `dur_balance`, `dur_away` | u16 | operation durations in minutes | `comfoair_control` (an entity command over `POST /api/entities/...` or MQTT) |
| `cfg_app` | `manual_perm` | u8 | a level change also pins manual mode | `comfoair_control` (`POST /api/config`) |
| `comfoair` | `wifi_ssid`, `wifi_psk` | string | legacy Wi-Fi keys from an earlier build; read once for migration | nothing |

The four durations and `manual_perm` are owned by `comfoair_control`, which stores them here; they
are not fields of the stored configuration record (`comfoair_config_t`). They reach the outside two
different ways:

- **The four durations are registry entities**, written through `/api/entities/...` or MQTT like
  every other entity; their bounds come from the entity registry (see
  [Entities](/reference/entities)). Until a value is stored, `comfoair_control` uses
  `comfoair_command_durations_defaults()` (boost, bypass and balance 60 minutes, away 0), the same
  numbers the registry lists as defaults. `comfoair_control` loads these keys only when the command
  sender starts (that is, only when CAN came up), and `app_main` then copies them into the state
  model.
- **`manual_perm` is not an entity.** It appears in the configuration document as the
  `permanent_manual` field: `GET /api/config` renders it (fetched from `comfoair_control`, not
  projected from the stored record), and `POST /api/config` accepts it and hands it to
  `comfoair_control`, which persists it under this key. A `POST` without the field leaves it
  unchanged. Until it is stored the value is `true`. On a device whose command sender did not start
  (CAN failed), `comfoair_control_set_permanent_manual()` returns `ESP_ERR_INVALID_STATE`, the
  handler ignores that code, and the field is neither applied nor stored although the save answers
  `{"saved":true}`.

**Why the Wi-Fi pair is one blob.** NVS has no atomicity across keys. A power cut between writing
an SSID and writing its key would leave a new network with an old password: a typo nobody made.
`comfoair_wifi_t` holds `ssid[32]`, `psk[64]` and explicit lengths with no terminator, so a legal
32-byte SSID and a 64-hex-digit key both survive byte for byte.

## The configuration document

`POST /api/config` carries these fields (Wi-Fi is refused here and goes through
`POST /api/provision`, see [Networking](/device/networking#provisioning-api)):

| Field | Default | Accepted |
| --- | --- | --- |
| `mqtt_host` | `""` (no broker) | empty, or up to 253 characters of letters, digits, `. - _ : [ ]`; no `/`, so a URL is refused |
| `mqtt_port` | 1883 | 1–65535 (taken as a 32-bit number so 70000 is refused, not wrapped) |
| `mqtt_user` | `""` | up to 128 bytes |
| `mqtt_password` | `""` | up to 128 bytes; write-only |
| `topic_prefix` | `comfoair/<device id>` | 1–64 characters; not starting with `$`; no leading or trailing `/`, no `//`, no `+`, `#`, space or control characters |
| `ha_discovery` | `true` | boolean |
| `device_name` | `ComfoAir Q` and the last four id characters | 1–32 bytes, no control characters |
| `ui_password` | not set | 8–128 characters, no control characters; write-only |

Stored Wi-Fi (via provisioning): SSID 1–32 bytes with no NUL; key empty (open network), 8–63
characters, or exactly 64 hexadecimal digits.

The device id is the full STA MAC in lower-case hex (12 characters). It seeds the default prefix
and name, so two devices in one house never share a topic tree, and renaming a device never re-keys
anything.

**All or nothing.** `comfoair_config_apply()` validates every field in the patch before changing
any, so a document with a good host and a bad port changes neither. An absent key leaves the
stored value alone. An empty string clears `mqtt_host`, `mqtt_user` and the two secrets; for
`topic_prefix` and `device_name` it is refused. If the validated document cannot be written to NVS
the change still takes effect in RAM, the save is answered as successful, and the failure is only
logged (`comfoair_net_config_apply()`). Errors come back as `comfoair_config_err_t` with an
owner-readable sentence that never contains a value.

## Secrets

Three values are secret: the Wi-Fi key, the broker password and the UI password.
The field table in `comfoair_config.c` marks them (`secret = true`). `comfoair_secret_key()` treats
any key whose name contains `password`, `passwd`, `psk`, `secret`, `token` or `key`
(case-insensitive) as secret; no running code calls it, and `test_config` uses it to check that the
field table agrees.

**Write-only on the wire.** `comfoair_config_project()` is the only way the document leaves the
store. A set secret reads back as the sentinel `__UNCHANGED__`, an unset one as `""`. On the way in
(`comfoair_secret_decide()`):

| Submitted | Something stored | Nothing stored |
| --- | --- | --- |
| key absent | keep | keep |
| `__UNCHANGED__` | keep | rejected |
| `""` | clear | keep |
| a value | store | store |

So the settings page can submit the whole document back without wiping a stored password, and
never receives it. `ui_password_set` in the projection is derived from the record being non-empty,
never stored separately, so a half-finished write cannot leave the device demanding a password
that does not exist.

**Broker password: plaintext in NVS.** MQTT authenticates with the plaintext, so a hash would only
make the broker unreachable. It leaves the store only through `comfoair_net_broker()`, to the MQTT
client, and must never reach a log (the log is served by `GET /api/log`).

**UI password: one-way record.** Stored as `1$<iterations>$<salt hex>$<digest hex>`:
PBKDF2-HMAC-SHA256 (`mbedtls_pkcs5_pbkdf2_hmac_ext`) with a fresh 16-byte salt from
`esp_fill_random()` per stored password and a 32-byte digest, 10 000 iterations. The iteration count
is in the record, so it can be raised later; a record claiming more than 1 000 000 is rejected so a
corrupted count cannot make a login take minutes. Verification compares the hex digests in
constant time. There is no lockout: each failed authentication is answered after a fixed 1 s delay
(`COMFOAIR_AUTH_FAIL_DELAY_MS`, applied in `comfoair_http_allowed()`), and the 8-character minimum
is the remaining defence against guessing. The record never leaves `comfoair_net`
(`comfoair_net_check_password()`).

## Loading, repair and versioning

`comfoair_config_nvs_load()` runs once, inside `comfoair_net_start()`:

1. Fill the document with **defaults first**, so every failure path still yields a bootable
   document.
2. Read `cfg_app` (including `schema`), `cfg_owner` and `cfg_wifi`. A namespace that cannot be
   opened leaves the defaults standing; only an unreadable `cfg_app` (other than "not found") is
   logged and returned as an error, which `comfoair_net_start()` logs and continues past. Nothing in
   this path is `ESP_ERROR_CHECK`ed, because a panic over stored data is a reboot loop with no way
   out.
3. **Migrate** if the stored schema is older than `COMFOAIR_CONFIG_SCHEMA_VERSION` (currently 1; a
   fresh device reads 0): when `sta` is absent, the legacy `comfoair/wifi_ssid` and
   `comfoair/wifi_psk` are validated, written as one `sta` blob, and the legacy namespace is
   erased. An invalid legacy pair is neither stored nor erased.
4. **Repair** (`comfoair_config_sanitize()`): every field goes back through the same validators.
   Anything unusable is replaced by its default; an unusable Wi-Fi record is cleared, which sends
   the device to its access point; a UI password record that does not parse is cleared, leaving
   the device unlocked rather than sealed. A string too long for this build's field is dropped and
   counted as lost. Repaired and lost fields are reported by name in the log; values are never
   logged.
5. On a migration, write the schema number, unless reading `cfg_app` failed or the document is
   read-only. Every `comfoair_config_nvs_save()` also writes it.

**A newer schema than this build** (what an OTA rollback onto older firmware looks like) is
neither erased nor fatal. The document is read with what this build understands and marked
`read_only`: `POST /api/config` and `comfoair_config_nvs_save()` refuse, while the Wi-Fi paths
(provisioning, known-good commit and restore, the two boot flags) still work, because they are the
way back. A hard reset clears the condition.

The NVS partition itself is erased and re-initialised in `app_main` if it reports no free pages or
a newer NVS format; this costs settings, never function.

## Factory reset

Resets are by namespace with `nvs_erase_all()`, so they also remove keys an older firmware wrote
that this build does not know. `comfoair_config_nvs_reset(tier)` attempts every namespace even if
one fails and returns the first error.

| Tier | Erases | Keeps |
| --- | --- | --- |
| soft (`COMFOAIR_CONFIG_RESET_SOFT`) | `cfg_wifi`, `cfg_owner`, `comfoair` | `cfg_app`: broker, prefix, discovery, name, schema, durations, `manual_perm` |
| hard (`COMFOAIR_CONFIG_RESET_HARD`) | every namespace in the enumeration | nothing |

The legacy namespace is on the soft side because it holds a Wi-Fi pair; leaving it would let the
migration restore the network the owner asked to forget. Both tiers are triggered only by the
button; there is no HTTP or MQTT route that erases configuration. The gesture, the confirmation
and the restart are on the [Button](/device/button#the-two-level-reset) page.
