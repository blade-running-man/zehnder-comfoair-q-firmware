# comfoair_mdns — the `comfoair-XXXX.local` name and one `_http._tcp` service

> This is documentation, not code — re-read the source before relying on a detail.

Without this component the owner reaches the web UI by an IP address read off the router, while
the sticker on the case and the setup Wi-Fi SSID both say `comfoair-8ae5`. `comfoair_mdns` makes
the same string resolvable as `comfoair-8ae5.local`, advertises the web server as a DNS-SD
`_http._tcp` service under the owner's chosen device name, and sends a TTL-0 goodbye before a
reset-to-defaults reboot. It is split into a pure half (names, TXT, byte arithmetic — host-tested)
and a thin driver over `espressif/mdns` that exists only on the device.

## Responsibility

Owns:
- The host name format: `"comfoair-"` + four lower-case hex characters, always 13 characters.
- The DNS-SD instance name (the owner's device name, falling back to the host name), truncated at
  63 octets on a UTF-8 boundary.
- The closed TXT allowlist: `board`, `ver`, `id`, `path=/`, `api=/api` — in that order.
- Starting the responder once, renaming the instance, and the non-blocking goodbye.

Does NOT do:
- Choose which MAC the four hex characters come from — `comfoair_net` (`comfoair_net_short_id()`,
  SoftAP MAC) is the single producer; this component receives the string.
- Gather its own inputs — `src/app_mdns.c` (`app_mdns_start()`) reads `comfoair_net`, `board`
  and `esp_app_get_description()` and fills `comfoair_mdns_config_t`.
- Track network state or re-announce on address changes — `espressif/mdns` subscribes to
  `WIFI_EVENT`/`IP_EVENT`/`ETH_EVENT` itself and manages its per-interface PCBs.
- Resolve name conflicts — neither it nor `espressif/mdns` does (see Notes).
- Decide when to reboot — `comfoair_button_reset.c` waits `COMFOAIR_MDNS_GOODBYE_MS` and calls
  `esp_restart()`.
- Validate or persist the device name — `comfoair_config` (sanitize/default) and `comfoair_http`.

## Public API

Header: `include/comfoair_mdns.h`. Includes only `esp_err.h` and the C standard headers, so a
consumer needs nothing extra in its `REQUIRES`.

| Symbol | Contract |
| --- | --- |
| `comfoair_mdns_hostname(short_id, out[14])` | Pure. Always writes a legal 13-char DNS label. Upper-case A–F folded to lower; any other non-hex char → `'0'`; short/NULL input padded with `'0'`. `out == NULL` → no-op. |
| `comfoair_mdns_instance(device_name, hostname, out[64])` | Pure. Copies `device_name`, or `hostname` if it is NULL/empty. Truncates at 63 octets without splitting a UTF-8 sequence. Both NULL → `""`. |
| `comfoair_mdns_txt(facts, out, cap)` | Pure. Fills caller-owned array with up to 5 pairs; returns count. A pair whose value is NULL/empty or whose `key=value` exceeds 255 octets is DROPPED (never truncated); the rest still go out. Honours `cap`. Pointers in `out` borrow from `facts` and static literals. |
| `comfoair_mdns_txt_bytes(items, n)` | Pure. Wire size: per item `1 + strlen(key) + 1 + strlen(value)`. Exists so the 200-octet budget is measured in a test. |
| `comfoair_mdns_copy_field(out, cap, src)` | Pure. Bounded copy that reads at most `cap - 1` octets of `src` (no `strncpy`/`%s`), always terminates, returns octets written; NULL `src` → `""`. |
| `comfoair_mdns_start(config)` | Device only. Call ONCE from `app_main`, after `comfoair_net_start()` and `comfoair_http_start()`. Strings are borrowed for the call only (copied into statics). Returns `ESP_ERR_INVALID_ARG` (NULL config), the `mdns_init()` or `mdns_hostname_set()` error, else `ESP_OK` — including when the instance name or service add was refused (logged at WARN). Caller must LOG AND CONTINUE, never abort. |
| `comfoair_mdns_rename(device_name)` | Device only. Moves the service instance name only; host name never changes. No-op before a successful start, when no service was registered, or when the name is unchanged. Returns nothing; failure is a WARN log. Called from the httpd task after `POST /api/config` has answered. |
| `comfoair_mdns_goodbye()` | Device only. Returns immediately; spawns task `comfoair_bye` (3072 B, priority 5) that calls `mdns_free()` (TTL-0 announce). No-op if the responder never started; a second call is a no-op. |
| `COMFOAIR_MDNS_GOODBYE_MS` = 150 | How long the reset caller waits for the goodbye before restarting regardless. |

Types and constants:
- `COMFOAIR_MDNS_HOST_MAX` = 13 — fixed length is part of what makes the name sticker-printable.
- `COMFOAIR_MDNS_INSTANCE_MAX` = 63 — RFC 6763 §4.1.1, octets not characters.
- `COMFOAIR_MDNS_TXT_MAX` = 5 — the closed key list.
- `COMFOAIR_MDNS_ITEM_MAX` = 255 — RFC 6763 §6.1, one length-prefixed string per item.
- `COMFOAIR_MDNS_TXT_BUDGET` = 200 — RFC 6763 §6.2 one-packet budget; NOT enforced at runtime
  (the only variable-length input is a compile-time board name), measured by the host suite.
- `comfoair_mdns_txt_t {key, value}` — mirrors `mdns_txt_item_t`; declared locally so the header
  compiles on the host. `_Static_assert`s in `comfoair_mdns.c` keep the layouts equal (the driver
  casts the array).
- `comfoair_mdns_facts_t {board, version, device_id}` — the ONLY inputs that can reach TXT. Exactly
  three non-secret pointers; the test pins `sizeof == 3 pointers`.
- `comfoair_mdns_config_t {short_id, device_name, http_port, facts}` — `http_port == 0` means
  the web server did not start: host name only, no service.

## Implementation

- `comfoair_mdns_name.c` — the pure half. No ESP-IDF header may be included (it is compiled into
  host suites). Holds `PREFIX "comfoair-"`, the TXT keys/constant values, `hex_or_zero()`,
  `utf8_prefix()` (walks back off `10xxxxxx` continuation octets), `pair_ok()`, `put()`.
- `comfoair_mdns.c` — the driver, entirely inside `#ifdef ESP_PLATFORM`. Static buffers
  `s_host[14]`, `s_instance[64]`, `s_board[64]`, `s_version[33]`, `s_device_id[64]`; flags
  `s_up` (mdns_init succeeded) and `s_service_up` (service registered).
- `CMakeLists.txt` — `SRCS` both files, `PRIV_REQUIRES espressif__mdns`, no comfoair component.

Start flow: compute host and instance → `mdns_init()` (fail → return) → `mdns_hostname_set()`
(fail → ERROR, return error, `s_up` stays true) → `mdns_instance_name_set()` (fail → WARN) → if
`http_port == 0` return `ESP_OK` → copy facts with `copy_field` (version into a 33-byte buffer, so
a 32-byte unterminated `esp_app_desc_t.version` is read exactly) → build TXT →
`mdns_service_add(instance, "_http", "_tcp", port, txt, n)` (fail → WARN, `ESP_OK`) → INFO log
naming `http://<host>.local:<port>/`, instance, pair count and octets (used by HV-PROV-02/19).

Concurrency: no lock. `start` runs once in `app_main`; `rename` runs on the httpd task; `goodbye`
on the button task just before a reboot. The flags are plain `bool`s; `goodbye` clears both
before spawning the task so a repeat call cannot start two `mdns_free()`s.

DO NOT:
- Include `comfoair_net.h` (or any comfoair header) in either file. PlatformIO's dependency finder
  ignores `#ifdef ESP_PLATFORM`, follows the include and tries to build `comfoair_net.c` natively —
  `pio test -e native -f test_mdns_name` failed on `esp_event.h` when this was tried. Take the fact
  as a `comfoair_mdns_config_t` field instead.
- Remove the `#ifdef ESP_PLATFORM` wrapper from `comfoair_mdns.c` — every `.c` here is compiled
  by any host suite reaching this header, including the button suites via `comfoair_button_reset.c`.
- Use `ESP_ERROR_CHECK` anywhere — nothing reboots because mDNS failed.
- Call `mdns_free()` synchronously in `goodbye` — it has two unbounded waits (`portMAX_DELAY` on the
  service lock, a `vTaskDelay(10)` spin in `service_task_stop()`), and the caller is one statement
  from `esp_restart()` with NVS already erased.
- Hang start/stop off `comfoair_net_cb_t` — it fires on state changes only; a DHCP address change
  without a disconnect produces no callback and the record would go stale.
- Truncate a TXT value — a half version string looks like an answer. Drop the pair.
- Add a field to `comfoair_mdns_facts_t` or a TXT key without asking "would I print it on the
  case?" — TXT is multicast to 224.0.0.251 on every interface, the open setup AP included.
- Share `PREFIX` with `comfoair_net` as a macro — the dependency is one-way; the format is
  transcribed (here, in `comfoair_net_start.c`'s `build_ap_ssid()`, and in the test).
- Hard-code the HTTP port — it comes from the caller (`http.port` in `src/main.cpp`).

## Dependencies

- Requires: `espressif__mdns` (PRIV; managed name, fetched by `src/idf_component.yml`, pinned
  `espressif/mdns: "1.12.0"`; the short name `mdns` does not resolve). FreeRTOS and `esp_log` come
  via the IDF.
- Kconfig it relies on (`sdkconfig.defaults`): `CONFIG_MDNS_MAX_SERVICES=2` (one used, one spare
  for a future `_comfoair._tcp`), `CONFIG_MDNS_ENABLE_BROWSE=n`, `CONFIG_MDNS_ENABLE_CONSOLE_CLI=n`.
- Depended on by: `src/app_mdns.c` (`comfoair_mdns_start`, called from `src/main.cpp` with
  `http_err == ESP_OK ? http.port : 0`), `components/comfoair_http/comfoair_http_config.c`
  (`comfoair_mdns_rename` after the config save), `components/comfoair_button/comfoair_button_reset.c`
  (`comfoair_mdns_goodbye` + `COMFOAIR_MDNS_GOODBYE_MS` before `esp_restart()`).

## Tests

- `test/test_mdns_name/test_mdns_name.cpp` — host suite over the pure half: host name format, DNS
  label legality, determinism, instance fallback, 32-byte names, the default-name case, UTF-8-safe
  truncation (Cyrillic, emoji), `copy_field` bounds on an unterminated 32-byte field, the exact TXT
  key allowlist and order, a forbidden-key list (ssid/broker/pass/prov/…), `sizeof(facts) == 3`
  pointers, the 200-octet budget, the 255-octet item limit, a `-dirty` version passing unchanged.
- `test/test_button_gesture` and `test/test_button_poller` compile this component transitively
  (through `comfoair_button_reset.c`) but test nothing in it.
- NOT covered on the host: anything in `comfoair_mdns.c` — start/rename/goodbye, probing, the
  goodbye packet, re-announcement, the AP-interface record. Which MAC feeds `short_id` and that the
  SSID format still matches `PREFIX` are hardware checks (HV-PROV-02, HV-PROV-18 in
  `docs/hardware-verification/05-provisioning-and-recovery.md`).

## Notes

- **Host name and instance name are deliberately different strings.** The host name is a key
  derived from the SoftAP MAC (same as the SSID and sticker) and never moves, so bookmarks and the
  open tab survive a rename. The instance name is the owner's free-form label; it cannot become a
  host name because spaces/Cyrillic/emoji have no lossless DNS label. On a booted device the config
  layer defaults the name to `ComfoAir Q <last four of the STA MAC>`, so the bench unit shows
  `ComfoAir Q 8ae4` beside `comfoair-8ae5` — the pair kept in step is the one a person types. The
  empty-name fallback is defensive and unreachable from a booted device.
- **A name conflict means silence.** `espressif/mdns` 1.12.0 has no conflict resolution: a lost
  probe only slows retries; the PCB never announces and no suffix is added. No auto-rename is added
  here because the result would match neither the sticker nor the SSID; the owner's way out of the
  1-in-65536 collision is renaming one device.
- **`http_port == 0` advertises the name but no service**, because a name that resolves to a
  refused connection reads as a broken device.
- **`id` is the STA MAC**, which is also the MQTT client id — a second client using it would knock
  the device off the broker. Accepted because the MAC is derivable from the AP's beacons anyway.
- **Only one service.** `_comfoair._tcp` is not registered: Home Assistant arrives over MQTT
  discovery, and a second TXT would be a second list that eventually disagrees.
- `device_name` is taken from the redacted `comfoair_net_config_snapshot()` projection, never from
  `comfoair_net_broker()` (which carries the broker password) — enforced by the caller, not here.
