# comfoair_mqtt_backoff — the host-buildable MQTT deciders: retry ladder, status words, discovery gate, device URL, retract ceiling

> This is documentation, not code — re-read the source before relying on a detail.

`comfoair_mqtt` is esp-mqtt, FreeRTOS and a client handle, so `[env:native]` never compiles it, and
any condition written inside it is checked by no test. This component holds the *decisions* the
MQTT client depends on as pure C with **no `REQUIRES` at all**, so host suites can drive them:
how long to wait before the next broker attempt and what to call the reason (the retry ladder),
the words `/api/status` prints for the broker block, when the retained HA discovery tree must be
republished, when the device-card `configuration_url` must be rewritten, and the time ceiling on
a discovery retraction sweep. The impure side (clock, IP address, client handle, atomics) stays
in `comfoair_mqtt` and passes plain numbers in.

## Responsibility

Owns:
- Failure classification of an esp-mqtt error/CONNACK into seven classes (`comfoair_mqtt_classify`).
- The backoff table and its deterministic ±20 % jitter (`comfoair_mqtt_backoff_ms`), the
  "settled session" reset rule, the per-device jitter seed.
- The publisher-task retry state machine (`comfoair_mqtt_ladder_pass` and friends): when to arm a
  rung, when to ask for a reconnect, when to count an attempt, when to stand down.
- The packed 32-bit "failure word" layout the event handler stores in one atomic.
- The single spelling of the `mqtt.link`, `mqtt.failure` and `mqtt.subscriptions` words, and the
  `last_transport_errno` normalisation.
- The three discovery-republish rules (`comfoair_discovery_gate`).
- The "URL moved" rule for `configuration_url` (`comfoair_device_url_refresh`).
- The retraction loop condition (`comfoair_retract_continue`).

Does NOT do:
- Talk to esp-mqtt, read a clock, read the IP, call `esp_mqtt_client_reconnect()`, or store
  anything across tasks — `comfoair_mqtt` (`comfoair_mqtt_retry.c`, `comfoair_mqtt_event.c`,
  `comfoair_mqtt_discovery.c`, `comfoair_mqtt_client.c`, `comfoair_mqtt_retract.c`) does.
- Render JSON — `comfoair_wire` (`comfoair_wire_render_status.c`) calls the `*_name()` functions.
- Decide the provisioning phase / `BROKER_REJECTED` latch — `comfoair_phase`
  (`comfoair_phase_connack_is_refusal()`); this component only has to agree with it.
- Produce a SUBACK verdict: nothing records one today, so `subscriptions` always reads `none`.
- Reboot, give up, or return a terminal state. The only effect it can request is a `bool reconnect`
  and a millisecond count.

## Public API

Five headers in `include/`.

### `comfoair_mqtt_backoff.h` — classes, table, words

| Symbol | Contract |
| --- | --- |
| `comfoair_mqtt_fail_t` | `NONE, DNS, TCP, UNAVAILABLE, PROTOCOL, REJECTED, DROPPED, COUNT`. A class is a decision about *how long to wait* and *what to tell the owner*, not a copy of the CONNACK enum. |
| `comfoair_mqtt_err_kind_t` | `NONE, TRANSPORT, REFUSED` — the three shapes esp-mqtt errors actually take. |
| `comfoair_mqtt_link_t` | `OFF, CONNECTING, ONLINE, BACKOFF, REJECTED`. "attempt in flight" is `CONNECTING` + `retry_in_ms = null`, not a separate `pending`. |
| `comfoair_mqtt_subs_t` | `NONE, PENDING, GRANTED, REFUSED` (no producer yet). |
| `comfoair_mqtt_signal_t` | Input to classify/link_of: `kind, connack, name_unresolved, was_connected, configured, connected, retry_scheduled`. No time, no handle. |
| `comfoair_mqtt_classify(sig)` | Total; NULL → `NONE`. `was_connected` → `DROPPED` first, outranking the error fields. TRANSPORT → `DNS`/`TCP`. REFUSED: 0 → `NONE`, 3 → `UNAVAILABLE`, 1/2 → `PROTOCOL`, 4/5/anything else → `REJECTED`. Any task, no state. |
| `comfoair_mqtt_backoff_ms(why, attempt, seed)` | Delay in ms. Never 0 for any input; unknown `why` gets the fast row. `attempt` saturates at `COMFOAIR_MQTT_ATTEMPT_MAX` *before* hashing. |
| `comfoair_mqtt_backoff_settled(connected_ms)` | `connected_ms >= 60000`. Evaluated every pass while connected (level-triggered). |
| `comfoair_mqtt_link_of(sig)` | NULL or `!configured` → `OFF`; `connected` → `ONLINE`; PROTOCOL/REJECTED → `REJECTED`; else `BACKOFF` if `retry_scheduled`, else `CONNECTING`. |
| `comfoair_mqtt_transport_errno(int)` | Positive passes through, everything else → 0. `esp_transport_get_errno()` returns -1 when it has no handle; a cast would print 4294967295. |
| `comfoair_mqtt_fail_name` / `_link_name` / `_subs_name` | Static string literals, never NULL; out-of-range → `"unknown"`. `REJECTED` spells `"bad-credentials"`, `PROTOCOL` spells `"protocol"`. |
| `COMFOAIR_MQTT_ATTEMPT_MAX` = `16u` | Shared by the table (stops climbing) and the ladder's counter (stops counting). Spelled once so the two cannot drift. |

The table (base, then `× (80 + FNV(seed, attempt) mod 41) / 100`):

```
DNS, TCP, UNAVAILABLE   2000 4000 8000 16000 32000 60000 ...  ceiling 60 s
DROPPED                 1000 2000 4000  8000 16000 32000 60000 ...
PROTOCOL, REJECTED      300000 flat
```

Why these numbers (constants in `comfoair_mqtt_backoff.c`):
- **2 s first rung**, not sub-second: brokers (EMQX, cloud) ban IPs for rapid failures, a house of
  devices wakes together after a power cut, and one esp-mqtt attempt already costs ~20 s because
  `network.timeout_ms` is spent twice per attempt.
- **1 s for DROPPED**: "accepted us before" is a different fact from "never got through".
- **60 s ceiling**, not 30 s: restarts of HA/Mosquitto (5–20 s) resolve on rungs 2–4, before the
  ceiling matters; for long outages 60 s halves the SYN rate.
- **300 s flat for refusals**: a broker bans by IP for repeated bad logins — and with it every
  device behind the same NAT. CONNACK 3 is *not* a refusal (it is what Mosquitto answers while
  restarting), so it climbs the fast row.
- **One jitter rule for every row**, the flat rung included; `why` is not hashed so PROTOCOL and
  REJECTED wait exactly the same.

### `comfoair_mqtt_ladder.h` — the publisher-task state machine

| Symbol | Contract |
| --- | --- |
| `comfoair_mqtt_ladder_t` | `retry_scheduled, retry_at_ms, session_timed, connected_since_ms, address_lost, attempt`. One instance, owned by the publisher task only; zero-init is the correct start. |
| `comfoair_mqtt_ladder_in_t` | `now_ms, client, configured, connected, has_address, seed` plus the failure fields `kind, connack, name_unresolved, was_connected, failed`. |
| `comfoair_mqtt_ladder_out_t` | `reconnect, link, failure, attempts, retry_known, retry_in_ms`. Every field written every pass (when all pointers are non-NULL). `retry_known == false` renders as `null`, not 0. |
| `comfoair_mqtt_ladder_pass(st, in, out)` | One pass. Any NULL → returns doing nothing. Caller calls `esp_mqtt_client_reconnect()` iff `out->reconnect`. |
| `comfoair_mqtt_ladder_not_yet(st, now)` | Call when `esp_mqtt_client_reconnect()` failed (client not yet in WAIT_RECONNECT): re-arms the rung due now, no attempt counted. |
| `comfoair_mqtt_ladder_reset(st)` | Owner saved settings: attempts → 0, rung unarmed. Called after the client rebuild, on the publisher task. Leaves `address_lost` alone. |
| `COMFOAIR_MQTT_FAILWORD_*` | Bits 0–7 CONNACK, 8 unresolved, 9 was_connected, 10 FAILED, 11–12 kind. One relaxed load yields four fields that agree; two atomics could pair an old kind with a new connack. |
| `comfoair_mqtt_failword_pack(...)` | Always sets FAILED. Used by the event handler. |
| `comfoair_mqtt_failword_unpack(word, in)` | Writes only the five failure fields of `in`; NULL-safe; any 32-bit value is valid input. |
| `comfoair_mqtt_backoff_seed(device_id)` | 32-bit FNV-1a of the device id (MAC / client id). NULL and `""` → FNV offset basis. |

### `comfoair_discovery_gate.h`

| Symbol | Contract |
| --- | --- |
| `comfoair_discovery_gate_t` | `{ bool published; bool link; }`, zero-init correct. `link` is the previous pass's view of the connection, needed to see an edge. |
| `comfoair_discovery_event_t` | `PASS`, `SENT` (full sweep done), `REBUILT` (`comfoair_ha_ctx_t` rewritten). |
| `comfoair_discovery_gate(gate, ev, connected, has_address)` | By value in and out. REBUILT → both false. SENT → `published = true`, `link` untouched. PASS → if `link && !connected && has_address` clear `published`; then `link = connected`. |
| `comfoair_discovery_due(gate)` | Inline `!published` — the only spelling of the negation. |

### `comfoair_device_url.h`

| Symbol | Contract |
| --- | --- |
| `COMFOAIR_DEVICE_URL_MAX` = 24 | Exactly `sizeof "http://255.255.255.255/"`. IPv4 only. |
| `comfoair_device_url_refresh(url, size, ip)` | Builds `http://<ip>/` on the stack; returns true and copies **only** if it differs from `url`. NULL/zero buffer, NULL/empty `ip`, or truncation → false and `url` untouched. |

### `comfoair_retract_budget.h`

| Symbol | Contract |
| --- | --- |
| `comfoair_retract_continue(index, total, elapsed_ticks, budget_ticks)` | `index < total && elapsed_ticks < budget_ticks`. Meant to be the `for` condition itself. Caller passes the unsigned tick difference, so it is wrap-safe. |

## Implementation

- `comfoair_mqtt_backoff.c` — the table constants, `jitter_pct()` (FNV-1a over seed + saturated
  attempt), `rung_ms()`, classify, link_of, the name functions, transport_errno.
- `comfoair_mqtt_ladder.c` — `signal_of()` (the single builder of a full `comfoair_mqtt_signal_t`),
  the pass, not_yet, reset, failword pack/unpack (with a `_Static_assert` that the kind field is
  wide enough), the seed.
- `comfoair_discovery_gate.c`, `comfoair_device_url.c`, `comfoair_retract_budget.c` — one rule each.

Ladder pass, in order of precedence:
1. **No client object** → nothing armed, session untimed.
2. **Connected** → time the session; once it has lasted 60 s, `attempt = 0`; nothing armed.
3. **Disconnected, no address** → the ladder *stands*: nothing armed, nothing counted,
   `address_lost = true`.
4. **Disconnected, nothing failed yet and nothing armed** → the client is dialling on its own
   (fresh build or reconfigure); do nothing.
5. **Otherwise** → if the address just came back, arm a rung due *now* without counting; if no rung
   is armed, arm `now + backoff_ms(class, attempt, seed)` and increment `attempt` (saturating);
   if the armed rung is due (signed difference ≥ 0), disarm and set `reconnect`.

Then `link`/`failure` are computed from one `signal_of(in, st->retry_scheduled)` — the value after
the update.

Invariants: no clock (time is a `uint32_t` argument; comparisons are `int32_t` differences, correct
across the ~49.7-day wrap and at `now == 0`); no globals, no allocation, no locks — concurrency is
"one owner": the ladder state belongs to the publisher task, and cross-task facts arrive as the
packed failure word.

DO NOT:
- Seed the jitter with `esp_random()`. The MAC seed gives each device its own ladder (anti-storm)
  and the *same* ladder every boot (HV-MQTT-16 reproduces; tests pin exact milliseconds).
- Fold `why` into the hash — PROTOCOL and REJECTED must wait identically.
- Replace `rung_ms()`'s doubling loop with `first << attempt`. Equal for every reachable input
  today, but the shift is UB at 32 and wraps to a sub-second retry once `ATTEMPT_MAX` exceeds 20.
- Hash the caller's raw `attempt`: saturating before the hash is what makes the cap observable.
- Move `was_connected` below the error switch in `comfoair_mqtt_classify()`: `last_connack` is a
  latch, and a dropped live session would be read as a password refusal (5-minute rung).
- Reset the ladder on connect instead of on a settled 60 s session: a broker that accepts and drops
  at once would get endless 1 s reconnects, each republishing all state.
- Rename `address_lost` to `had_address`: the zero value would grant a free immediate attempt at
  every boot (first interval 0 s instead of 2 s).
- Clear the ladder from `comfoair_mqtt_stop_client()`: two of its callers run on `app_main` before
  the publisher task exists — a second writer.
- Key "is the client dialling on its own" on `kind != NONE` instead of `failed`: a timed-out connect
  dispatches only DISCONNECTED with no error, and the ladder would never dial again.
- Build a `comfoair_mqtt_signal_t` anywhere but `signal_of()`: a half-filled one makes `link_of`
  answer `off` on a live broker.
- Reorder the discovery gate's `gate.link = connected` above the comparison (the rule would never
  fire), or drop any of its three terms.
- Change `comfoair_device_url_refresh` to "url is empty": it would freeze the card on the first
  lease and never follow a router handing out a new address. Blanking on address loss costs two
  discovery sweeps per Wi-Fi blip.
- Turn `comfoair_retract_continue` back into a predicate checked inside the loop body: an `if` can
  be deleted with every host case still green; a loop condition cannot.

## Dependencies

- Requires: nothing (`CMakeLists.txt` has no `REQUIRES`; only `<stdbool.h>`, `<stdint.h>`,
  `<stddef.h>`, `<stdio.h>`, `<string.h>`, `<assert.h>`). The emptiness is deliberate — it is what
  keeps the component host-buildable.
- Depended on by: `comfoair_mqtt` (`REQUIRES comfoair_mqtt_backoff`; callers in
  `comfoair_mqtt_retry.c`, `comfoair_mqtt_event.c`, `comfoair_mqtt_discovery.c`,
  `comfoair_mqtt_client.c`, `comfoair_mqtt_retract.c`) and `comfoair_wire` (`REQUIRES`; its
  `comfoair_wire_mqtt_t` carries the three enums and `comfoair_wire_render_status.c` renders them
  via the name functions). `comfoair_http_read.c` casts `comfoair_mqtt_stats_t`'s `uint8_t` fields
  to the enums (reached through `comfoair_wire.h`). `platformio.ini`'s native env adds
  `-I components/comfoair_mqtt_backoff/include`.

## Tests

Host suites (`pio test -e native -f <suite>`):
- `test/test_mqtt_backoff/` — `test_mqtt_backoff.cpp` (table, jitter, saturation, classes, names,
  agreement with `comfoair_phase_connack_is_refusal()`), `test_mqtt_ladder.cpp` (state machine:
  address return, settled reset, reconfigure, wrap, not_yet, never-stuck), `test_mqtt_wiring.cpp`
  (seed, failword round trip, lockstep), shared `ladder_harness.h`. `test_mqtt_backoff.cpp` is at
  the 600-line suite ceiling.
- `test/test_mqtt_facts/` — `comfoair_mqtt_transport_errno`.
- `test/test_discovery_gate/` — the three gate rules.
- `test/test_device_url/` — `comfoair_device_url_refresh`, including the buffer size.
- `test/test_retract_budget/` — `comfoair_retract_continue`, including tick wrap.
- `test/test_wire_status/` — exercises the `*_name()` spellings through the status renderer.

Not covered by any host suite: the call sites in `comfoair_mqtt` (whether the right fields are
unpacked, whether `not_yet` is called on `ESP_FAIL`, whether `reset` follows a rebuild) and the
real esp-mqtt behaviour (DNS signal on plain TCP is unverified). Those are hardware scenarios
under `docs/hardware-verification/` (e.g. HV-MQTT-16, HV-MQTT-24, HV-HA-08..12).

## Notes

- **Why one component for five lodgers.** PlatformIO's dependency finder works per component, so a
  component either builds on the host or it does not. The discovery gate, device URL and retract
  budget share no code with the ladder; they live here only because this is the MQTT-adjacent
  directory that builds on the host, and they need nothing either.
- **Why the link enum is here, not in `comfoair_wire`.** The words are needed by the log line and by
  `/api/status`; spelling them in two places diverges on the first edit.
- **The CONNACK code space has three readers** — `comfoair_wire_connack_name()`,
  `comfoair_phase_connack_is_refusal()`, and `comfoair_mqtt_classify()`. PROTOCOL ∪ REJECTED must
  equal the phase's "refusal" set; the test, not a build dependency, holds them together.
- **Unknown CONNACK codes classify as REJECTED**: the cheapest wrong answer is five minutes of
  waiting, not an IP ban.
- **Discovery gate is a bool, not a content fingerprint.** `comfoair_ha_ctx_t` is filled only in
  `comfoair_mqtt_start_client()`, so "rebuilt" already equals "content changed"; and a fingerprint
  would miss a broker *host* change (not part of the context) — a new empty broker would never get
  discovery. Ambiguous cases fall toward republishing: ~35 KB is a cost, an empty HA is a fault.
- **No `homeassistant/status` subscription**: the "drop while we still had an address" edge stands
  in for it, avoiding a fourth subscription pattern that a strict ACL might refuse.
- **`comfoair_device_url_refresh` is called on every publisher wake** (from
  `comfoair_mqtt_retry.c`), so a false answer on "nothing moved" is load-bearing: a true costs the whole
  retained discovery tree, one config document per entity (~35 KB as rendered on the host).
