# comfoair_captive — the captive portal: probe-path answers and a resolve-everything DNS responder

> This is documentation, not code — re-read the source before relying on a detail.

When a phone joins the device's open setup access point it does two things this component
answers. It fetches an OS-specific connectivity-probe URL, and it resolves names. The probe
table tells every OS "this network is fine", in the exact bytes that OS compares against, so the
OS does **not** open its cut-down captive sign-in browser (no WebSocket, closed by the OS
mid-typing). The DNS responder answers every A lookup with the access point's own address, so
whatever the owner types into a real browser lands on the setup page. The two halves are one
decision: the probe table suppresses the OS sign-in window, and DNS is then the only way the
owner finds the setup page.

## Responsibility

Owns:
- The constant table of probe paths and their byte-exact answers (`comfoair_captive_probe.c`).
- The pure DNS reply builder: one datagram in, one datagram (or nothing) out (`comfoair_captive_dns.c`).
- The UDP socket on port 53 and its one FreeRTOS task (`comfoair_captive_dns_server.c`).

Does NOT do:
- Register any HTTP handler or see `esp_http_server`. `comfoair_http`'s
  `comfoair_http_captive_probe_get()` (the last `/*` GET route) looks the path up and sends the row,
  falling through to `asset_get()` for non-probe paths.
- Strip the query string, fragment, absolute-form authority or trailing slash from a path:
  `comfoair_http_gate.c`'s `comfoair_http_request_path()` does, before the lookup.
- Decide when the access point is on the air or what its address is. `comfoair_net`'s
  `comfoair_net_captive_dns_follow_ap()` starts/stops the responder and passes the AP netif's
  address.
- Configure DHCP to advertise the device as the resolver (see Notes; it is an sdkconfig default).
- Fix Android's HTTPS probe (would need a TLS listener on 443 with a google.com certificate).

## Public API

| Symbol | Contract |
| --- | --- |
| `comfoair_captive_answer(path)` (`comfoair_captive.h`) | Pure. Returns a pointer into the static table for an exact, whole-string match, else `NULL` (also for `NULL` path). Any task; no ownership transfer, pointer valid forever. Caller must serve the row *ahead of* the access policy (see Notes). |
| `comfoair_captive_table(&count)` | Pure. Returns the whole table; writes the row count when `count != NULL`. `NULL` count is tolerated, not an error. |
| `comfoair_captive_answer_t` | `path`, `status` (ready for `httpd_resp_set_status`, always 2xx), `content_type` (NULL when no body), `body` (NULL for the 204s), `body_len` (byte-exact), `cache_control` (`"no-store"` on every row), `asked_by` (OS name, for a log line). |
| `comfoair_captive_dns_reply(query, query_len, address[4], out, out_cap)` (`comfoair_captive_dns.h`) | Pure, re-entrant. Returns bytes to send, or `0` meaning "send nothing". `address` is four octets in wire order, owned by the caller. `out` may alias `query`; `out_cap` must then be the *buffer size*, not `query_len`. Never reads past `query_len`; never writes a partial reply. |
| `COMFOAIR_CAPTIVE_DNS_PORT` | `53`. |
| `COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE` | `512` — classic UDP DNS (RFC 1035 4.2.1). It is the socket loop's buffer size, **not** a check on the query: the reply builder never compares `query_len` to it. The largest reply this code can build is 12 + 259 + 16 = 287 bytes, so it always fits. |
| `comfoair_captive_dns_start(address[4])` (`comfoair_captive_dns_server.h`, device only) | Opens a UDP socket bound to `address`:53 and starts task `comfoair_dns`. Returns `ESP_OK`, `ESP_ERR_INVALID_STATE` (already running, or a previous task never stopped), `ESP_ERR_INVALID_ARG` (NULL address), `ESP_FAIL` (socket / `SO_RCVTIMEO` / bind failed), `ESP_ERR_NO_MEM` (task create failed). Never aborts or reboots; the caller logs and carries on. |
| `comfoair_captive_dns_stop()` | Signals the task and waits up to 1.5 s for it to exit. Safe when not running. Returns nothing; if the task did not exit it leaves the handle set so the next `start()` refuses with `ESP_ERR_INVALID_STATE`. |

The probe table (seven rows): `/generate_204`, `/gen_204` (Android, `204`, no body);
`/hotspot-detect.html` (iOS/macOS, 69 bytes with trailing newline); `/ncsi.txt` (14 bytes, no
newline) and `/connecttest.txt` (22 bytes, no newline) for Windows; `/success.txt` (8 bytes, with
newline) and `/canonical.html` (90 bytes) for Firefox. Matching is by path only — the Host header
is ignored, because every Android vendor mirror uses the same path.

## Implementation

- `comfoair_captive_probe.c` — the `TABLE[]`, its literals, `answer()` (linear `strcmp`) and `table()`.
- `comfoair_captive_dns.c` — `question_len()` (bounded name walk) and `comfoair_captive_dns_reply()`.
- `comfoair_captive_dns_server.c` — static `s_rx`/`s_tx` buffers, `dns_task()`, start/stop. The
  whole file is inside `#ifdef ESP_PLATFORM`, so it compiles to nothing on the host.

DNS reply rules (`comfoair_captive_dns_reply`): refuse (return 0) when shorter than the 12-byte
header, QR already set, opcode not QUERY, QDCOUNT != 1, the name has any non-label byte (top bits
non-zero — compression pointers and reserved types), the name exceeds 255 wire bytes or has no
terminator inside the datagram, type/class do not fit, or the reply would not fit `out_cap`.
Otherwise: echo the id, set QR|AA plus the client's RD, RA, RCODE 0; copy the question back byte
for byte (`memmove`); for `A`/`IN` append one answer (name pointer `0xC00C`, TTL 0, the four
octets); for any other type/class (AAAA above all) answer NOERROR with zero records. Authority and
additional sections are dropped, including an EDNS OPT.

Server lifecycle: `start()` copies the address, creates the socket, sets a 1 s `SO_RCVTIMEO`
(`struct timeval`), binds to the AP address, then creates the task (stack 3072, priority 3). The
task loops on `recvfrom` while `s_running`; a timeout just re-checks the flag; any other error
backs off 1 s and is logged once per outage plus one "recovered after N" line. A non-zero reply is
sent with an unchecked `sendto`. On exit the task closes its own socket and clears `s_alive`.
`stop()` clears `s_running` and polls `s_alive` every 20 ms for at most `RECV_TIMEOUT_MS + 500`.

Concurrency: no mutex. `s_running`/`s_alive` are `volatile bool` flags between the caller and the
one task; the static buffers are owned by that task alone. `start()`/`stop()` are not mutually
safe from different tasks — the single caller is `comfoair_net`'s `step()`.

DO NOT:
- Redirect a probe (3xx) or answer it with the setup page — that is exactly the OS sign-in window
  and the Android "no internet, use mobile data" outcome this component exists to prevent.
- Retype or reformat a probe body (trailing newlines included). They are byte-compared by the OS;
  `body_len` is `sizeof - 1` so literal and length cannot drift.
- Turn probe matching into a prefix match or add a second path stripper.
- Follow a compression pointer in a question — `0xC00C` is a two-byte decompression loop on a
  single-core device reachable from the pavement.
- Reply to a packet with QR set — two responders (or one spoofed source port 53) would ping-pong.
- Normalise the case of the echoed question — resolvers randomise case and drop mismatches.
- Raise the TTL above 0 — the phone would carry "everything is 192.168.4.x" onto the owner's
  real network.
- Add a `query_len > 512` check in the reply builder — the buffer belongs to the caller.
- Bind the socket to `INADDR_ANY` — in AP_STA mode it would answer every lookup on the owner's
  home network too.
- Stop the task by `close()`-ing its socket from `stop()` — closing a descriptor another task is
  blocked in is an lwip-configuration-dependent race; the receive timeout is the stop mechanism.
- Log the queried name at any level — `/api/log` is readable by anyone on the open AP.
- Remove `REQUIRES lwip` from `CMakeLists.txt` — lwip is not an IDF common requirement, and
  `lwip/sockets.h` stops resolving without it.
- Add `comfoair_http` (or `esp_http_server`) as a dependency — the component would then be able to
  register a handler and open a second door around the access policy.

## Dependencies

- Requires: `lwip` (public `REQUIRES`); uses `esp_log` and FreeRTOS from the IDF common set.
- Used by: `comfoair_http` (`comfoair_captive.h` — the probe table, in
  `comfoair_http_captive_probe_get()`), `comfoair_net` (`comfoair_captive_dns_server.h` — start/stop
  following the access point). `comfoair_net` deliberately never touches the probe table.
- Host build: `platformio.ini` adds `-I components/comfoair_captive/include`; the component is a
  PlatformIO library too, so the host build compiles all three `.c` files (hence the
  `ESP_PLATFORM` guard on the server).

## Tests

- `test/test_captive/test_captive.cpp` — 33 cases over both pure halves. Probe table: each OS's
  status, body bytes and length, no 3xx, no body built from device state, `no-store` on every
  row, the row count pinned at seven, whole-path matching, query stripping left to the server,
  `NULL` count tolerated. DNS: A answer with the caller's address, byte-exact case-preserving echo,
  TTL 0, NOERROR/no-records for other types and classes, EDNS queries, and every refusal listed
  above. Every DNS case runs through `guarded_reply()`, which places the datagram flush against a
  `PROT_NONE` page (mmap/mprotect), so any read past `query_len` faults instead of passing.
  Also pinned: in-place reply (`out == query`) and the 512-is-not-a-check behaviour.
- NOT covered: `comfoair_captive_dns_server.c` (socket, task, stop/timeout, error backoff) has no
  host suite and only runs on the device. The HTTP handler that serves the table and the DHCP
  resolver advertisement are also untested on the host.

## Notes

- **Served ahead of the access policy, on purpose.** On the fallback AP of a device with a UI
  password, an unauthenticated GET gets 401 from the policy; 401 is not 204, so the OS would
  declare a captive portal. This is safe only because every row is a compile-time constant —
  nothing formatted at send time, so no name, address or reading can leak around the policy.
- **Two of three, not three.** Android 9+ also probes `https://www.google.com/generate_204`. DNS
  points it at the device, nothing listens on 443, and HTTP-success/HTTPS-failure is Android's
  PARTIAL_CONNECTIVITY: the owner sees "limited connectivity" and must tap through. The cost of
  the design overall: no window opens by itself; the owner must open a browser.
- **Windows NCSI DNS half is declined.** Windows also expects `dns.msftncsi.com` to resolve to
  `131.107.255.255`; special-casing a name in the pavement-reachable parser was judged not worth
  it for the least likely client. If ever added, it belongs beside the qtype test in
  `comfoair_captive_dns_reply()`, with the malformed-name tests pointed at it first.
- **Omitted probe paths** (`/redirect`, `/library/test/success.html`, Kindle and other vendor
  lists) are listed in a comment in `comfoair_captive_probe.c` so they read as considered. Adding a
  row requires editing the count test — the table is a list of things the firmware lies to.
- **DHCP must hand out the AP address as DNS.** It does today only because
  `CONFIG_LWIP_DHCPS_ADD_DNS` is on in `sdkconfig.m5stack-nanoc6` and the DHCP server then offers
  its own address. Turning that option off silently kills the portal; nothing in this component
  would notice.
- **Answering non-A types with NOERROR rather than silence** spares the client a resolver timeout
  (phones ask AAAA first). Dropping the EDNS OPT is harmless: the client falls back to 512-byte
  UDP, more than this code ever produces.
- **Task priority 3** sits below CAN receive (10) and the push channel (4), so a query flood
  cannot delay draining the TWAI receive queue — the one place this firmware loses data silently.
- **Static buffers, not stack or heap**: they cannot fail to exist when a packet arrives, and the
  3 KB stack only has to hold lwip's frames.
