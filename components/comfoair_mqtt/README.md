# comfoair_mqtt — the broker connection: one publisher task that owns the esp-mqtt client

> This is documentation, not code — re-read the source before relying on a detail.

The impure half of MQTT / Home Assistant. It owns the esp-mqtt client, a FreeRTOS publisher task,
a bounded inbound command queue, the retry clock and the network. Everything that can be decided
without a broker lives next door and is host-tested: which topic and payload (`comfoair_ha`), what
an arriving command means (`comfoair_mqtt_inbound`), the retry ladder, the discovery gate, the
retraction budget and the device URL (`comfoair_mqtt_backoff`). This component carries out those
decisions. Its central job is to make sure nothing about the broker can stall the rest of the
device: the CAN path, the web server and the ventilation keep working with the broker gone, wrong
or slow.

## Responsibility

Owns:
- The esp-mqtt client's whole lifetime (build, start, stop, destroy). After the first start this
  happens on the publisher task only.
- Publishing: the birth message and the will (`<prefix>/status`), the bus verdict
  (`<prefix>/bus`), the retained per-entity states, the fan's three retained leaves, and the
  data-freshness heartbeat, which is the only publication sent on a clock. A state with no reading
  (REST's `null`) is published as Home Assistant's literal `None`, never as nothing -- the fan's
  three leaves included before PDO 65 -- so a retained reading cannot outlive the value it was.
- Discovery config documents, once per identity (gated), and the three command subscriptions,
  sent again on every connection.
- Receiving commands: the event handler copies them into a queue, and the publisher task acts on
  what `comfoair_mqtt_decide()` returns (submit, store a duration, reject, ignore).
- Retracting the old identity (WITHDRAW / REKEY) when the saved settings move the broker or the
  prefix.
- The retry ladder's device half: the clock, the address read, and the one
  `esp_mqtt_client_reconnect()` call per pass.
- The `comfoair_mqtt_stats_t` counters behind the `mqtt` block of `GET /api/status`.

Does NOT do:
- Topic or payload formatting, discovery JSON, cleanup topic lists: `comfoair_ha`.
- Parsing or deciding a command, and the retained-command refusal: `comfoair_mqtt_inbound`.
- Backoff rows, failure classes, link/fail words, jitter seed, the discovery-gate conditions, the
  retraction ceiling comparison, the URL refresh rule: `comfoair_mqtt_backoff`.
- Sending anything on CAN. Commands go into `comfoair_control`'s queue. This component does not
  REQUIRE `comfoair_can`, and that is deliberate.
- Rendering the status JSON (`comfoair_wire`) or serving it (`comfoair_http`).
- Deciding bus health. It receives a bool from `comfoair_phase_monitor`, which computes it with
  `comfoair_bus_health_available()`.
- Storing the broker settings: `comfoair_net` / `comfoair_config`.

## Public API (`include/comfoair_mqtt.h`)

| Symbol | Contract |
| --- | --- |
| `esp_err_t comfoair_mqtt_init(void)` | app_main, once, **before `comfoair_http_start()`**. Creates only the inbound queue (`INBOX_LEN` 8). Calling it again is a no-op that returns `ESP_OK`. Returns `ESP_ERR_NO_MEM` if the queue cannot be allocated. |
| `esp_err_t comfoair_mqtt_start(void)` | app_main, after init. Calls init itself, builds the client synchronously, **then** creates the publisher task (`"comfoair_mqtt"`, 4096-byte stack, priority 4), and creates the task even when no broker is configured. `ESP_ERR_NOT_FOUND` means no broker host is stored. That is the shipping state, not a fault. `ESP_ERR_NO_MEM` means the task did not start: the client is stopped, `g_inbox` is set to NULL and then the queue is freed, and after that the component does nothing. Any other error comes from building the client. |
| `esp_err_t comfoair_mqtt_reconfigure(void)` | httpd task, after `POST /api/config`. Non-blocking: sets `s_reconfigure` and wakes the task. `ESP_OK` means the request was recorded, not that the client restarted. `ESP_ERR_INVALID_STATE` means init never succeeded. A NULL task is not an error: `comfoair_mqtt_start()` reads the saved settings anyway. |
| `void comfoair_mqtt_notify_state_changed(void)` | Any task, including the CAN receive path. Sets `g_dirty` and calls `xTaskNotifyGive`. Never publishes. |
| `void comfoair_mqtt_note_bus(bool available)` | Any task. Safe before init and with no client. Stores an atomic and wakes the publisher only when the value changes. `comfoair_phase_monitor`'s 1 Hz tick is the only caller. |
| `comfoair_mqtt_set_observer(fn)` | Registers a callback run (on the publisher task) when an MQTT command changes the state model. `src/main.cpp` wires it to `comfoair_http_notify_state_changed`. It is a callback because `comfoair_http` already depends on this component, so a direct call back would be a dependency cycle. |
| `void comfoair_mqtt_get_stats(comfoair_mqtt_stats_t *out)` | Any task, no locks. Zeroes `*out` first, then copies the relaxed atomics, a best-effort snapshot of `connected` and `configured`, and the ladder's five fields. NULL `out` is ignored. |

`comfoair_mqtt_stats_t` notes:
- `configured` means a broker host is stored, not that a client was built. The two differ exactly
  when `esp_mqtt_client_init/start` fails. Do not derive it from `g_client`.
- `last_connack` is the MQTT 3.1.1 CONNACK refusal code (4 = bad credentials, 5 = not
  authorized). It is latched until a successful CONNECTED and cleared in `stop_client()`.
- `disconnects` moves on every `MQTT_EVENT_DISCONNECTED`, failed dials included. It is read
  against `reconnects`: if both climb while `connected` flickers, a second device is probably
  using the same client id.
- `last_transport_errno` is the socket errno from `MQTT_EVENT_ERROR`/TCP transport, run through
  `comfoair_mqtt_transport_errno()` so that -1 does not print as 4294967295. A 0 after a timed-out
  connect is expected: that failure produces only DISCONNECTED.
- `link` and `failure` are `comfoair_mqtt_link_t` and `comfoair_mqtt_fail_t` values stored as
  `uint8_t`. The words are spelled only by `comfoair_mqtt_link_name/fail_name`.
- `retry_in_ms` is `COMFOAIR_PROV_NEVER` (rendered as null) when no attempt is scheduled. That is
  different from 0.
- `applied_epoch` moves only **after** the publisher has rebuilt the client and run one ladder
  pass. A browser polls until it changes and `link` is no longer `connecting`.
- `subscriptions` has no producer yet and always reads 0 (NONE).

## Implementation

Files (they share `comfoair_mqtt_internal.h`, which is NOT in `include/`):
- `comfoair_mqtt.c`: the spine. `mqtt_task`, the counters, the observer, the public entry points.
- `comfoair_mqtt_command.c`: `comfoair_mqtt_handle_inbound()` and its `reject()`/`accepted()`, the
  acting half of an inbound command. It left `comfoair_mqtt.c` for the 350-line ceiling; the
  deciding half is `comfoair_mqtt_inbound`, and no behaviour moved with the cut.
- `comfoair_mqtt_publish.c`: `comfoair_mqtt_publish_one()` (the only call to
  `esp_mqtt_client_publish` apart from the offline message in `stop_client`), changed-state
  passes, the full republish, freshness, the rate slots, and the shared `g_topic[192]` and
  `g_payload[1536]`.
- `comfoair_mqtt_bus.c`: `<prefix>/bus` -- `comfoair_mqtt_note_bus()` and
  `comfoair_mqtt_publish_bus()`.
- `comfoair_mqtt_client.c`: `start_client()`, `stop_client()`, `pending_cleanup()`, the static
  config and identity buffers.
- `comfoair_mqtt_event.c`: `comfoair_mqtt_on_event()`. It is the only code here that runs on the
  esp-mqtt task.
- `comfoair_mqtt_retry.c`: `retry_pass()`, `retry_applied()`, `retry_stats()`,
  `note_address()`.
- `comfoair_mqtt_retract.c`: `comfoair_mqtt_retract()`. It publishes zero-length retained messages
  under a 2000 ms total budget (`RETRACT_BUDGET_MS`), then drains the outbox in 100 ms slices.
- `comfoair_mqtt_discovery.c`: the discovery gate's three hooks, `publish_discovery()`,
  `subscribe_all()`.

The publisher loop (`mqtt_task`) wakes on a notification or after `TICK_MS` 1000. On each pass:
1. Drain the inbox, so each message is handled against the `g_ctx` it arrived under.
2. If a reconfigure was requested: run `pending_cleanup()`, then `retract()` while the old client
   is still connected, then `stop_client()`. Drain the inbox **again** (the old client may have
   queued one more message), then `start_client()`, then `retry_applied()`. `retry_applied()` runs
   unconditionally, even when the start returned `NOT_FOUND` or an error.
3. `retry_pass()`. It runs before the `!g_connected` early-out, because that early-out skips
   exactly the passes where a reconnect is needed.
4. If connected: on `g_republish` call `republish_everything()` (birth, bus, subscribe, discovery,
   every state); otherwise call `publish_freshness()`, then `publish_changed()` if `g_dirty` is set.

Invariants:
- **One task owns the client.** Every function declared in the internal header runs on the
  publisher task. `start_client()` and `stop_client()` are also called from app_main, but only
  before that task exists. That is why `g_ctx`, the buffers, `s_ladder` and `s_gate` are plain
  statics with no lock.
- **The event handler only sets flags, stores atomics and copies bytes into the queue**
  (`xQueueSend` with a 0 timeout). esp-mqtt dispatches events synchronously inside its own API
  lock while the publisher may be blocked in `publish`, so a buffer the two shared would be
  overwritten under the waiting publish. That actually happened: a state payload was published,
  retained, to `<prefix>/status`, and HA marked the device unavailable.
- Counters are `_Atomic` with relaxed ordering. A mutex would recreate an ABBA deadlock with
  esp-mqtt's API lock, and that deadlock once hung the web server.
- `publish_entity()` answers one of five results, in this order (the fan keeps the worst of its
  three leaves, so a dropped link outranks a render that did not fit): `PUBLISH_SENT`,
  `PUBLISH_SAME` (the broker already holds this value), `PUBLISH_HELD` (a rate-limited change),
  `PUBLISH_NOTHING` (a render or topic that does not fit -- a defect; no reading renders `None`),
  `PUBLISH_FAILED` (the client refused it).
- A change mark is cleared only on SENT or SAME, and only if the state generation is unchanged
  (`publish_and_clear()` samples the generation **before** reading the value). `PUBLISH_FAILED`
  and `PUBLISH_HELD` set `g_dirty` again (a held change is re-asked every `TICK_MS`, sending
  nothing until the rule lets it go). `PUBLISH_NOTHING` does not, or every pass would sweep every
  entity forever. Only SENT and FAILED count toward `PUBLISH_PER_PASS`.
- **The value is read with `comfoair_state_get_copy()`** into the publisher's own `s_text`, under
  the state lock: the payload is rendered after the lock is released, and the model's rendered
  texts (PDO 230, the active errors) are double-buffered, which protects a held pointer across one
  later write only.
- Every burst is paced by messages actually sent (`PUBLISH_PER_PASS` 8, then a 20 ms delay).
- `<prefix>/bus` goes out before any state, both in a changed pass and on reconnect. It is
  remembered as published only when the publish succeeds.
- The password lives in the static `s_broker` only while it is being read and is wiped with
  `explicit_bzero` at once. esp-mqtt `strdup`s its config.

**Publication rate.** Each entity state passes `comfoair_publish_rate_decide()` (the
`comfoair_publish_rate` component, host-tested in `test/test_publish_rate`) against a slot per
entity holding what THIS connection last put on the broker: the rows `tools/pdo_table.py` gives a
`publish_deadband` / `publish_min_interval_s` (fan speeds and flows, avoided heating/cooling, the
temperatures) are held while the change is jitter, and a held difference goes out within
`COMFOAIR_PUBLISH_MAX_HOLD_S` (300 s). Everything else -- enums, binaries, selects, the switch, the
fan -- is never held. The deadband is compared on the model's number, which is already the
published value (raw over the divider). A slot is recorded only after `publish_one()` succeeded,
and `republish_everything()` zeroes every slot on each new connection, so the connect sweep is a
full republication and no slot outlives a message a dropped link lost. MQTT only: REST and the
WebSocket are not rate-limited (`comfoair_publish_rate.h` says why).

Constants: `BUFFER_SIZE` 1536. The worst-case fan discovery payload (64-character prefix plus a
32-character non-ASCII name, every character escaped to six bytes) is 1040 bytes and would not
fit in 1024. The same value sizes esp-mqtt's in/out buffers. `QOS_STATE` 0: states are retained,
so a missed one is redelivered on subscribe anyway. `QOS_META` 1: status, bus, will, discovery,
subscriptions and erasures, because retention does not make these idempotent.
`cfg.network.timeout_ms` 2000. A QoS 1 publish writes the socket synchronously, so the 10 s
default would stall the task; 1 s is too short for mDNS `.local` broker names.
`disable_auto_reconnect = true`, because the ladder in `comfoair_mqtt_retry.c` owns the timing.
The client id is the full MAC device id.

DO NOT:
- Call esp-mqtt, format into `g_topic`/`g_payload`, or call any internal-header function from
  httpd, the CAN path or the event handler. Post a flag and wake the publisher instead.
- Put a mutex around the counters or the client.
- Move the `g_last_connack`, `g_last_transport_errno`, `g_last_failure`, `g_connected` and
  `g_republish` clears in `stop_client()` above `esp_mqtt_client_stop()`. Before stop returns, the
  esp-mqtt task can still write them.
- Reset `s_ladder` from `stop_client()`. Two of its callers run on app_main, and that would make a
  second writer.
- Put `subscribe_all()` under the discovery gate. The session is clean, so the broker forgets the
  subscriptions on every disconnect.
- Replace `comfoair_mqtt_discovery_rebuilt()` in `start_client()` with a hash of the six `g_ctx`
  fields. The broker address is not one of them, so a move to a new broker with the same prefix
  would match the hash and never get discovery.
- Retain the freshness state (`state_retained()`). HA restarts `expire_after` on every delivery,
  so a retained `ON` would keep a dead device looking fresh. Do not un-retain the others either.
- Also set `network.reconnect_timeout_ms`, or turn auto-reconnect back on. The two timers would
  race.
- Add a timed publication besides freshness, or any "broker absent, reboot" logic.
- Reword the log line `published discovery for %u entities`. Hardware scenarios HV-HA-08..12 grep
  for it.

## The files and the one-task rule

Moved here from `comfoair_mqtt_internal.h`, verbatim, so the header carries the contract and
this file carries the map.

```text
// The contract between the nine files of comfoair_mqtt, and it is deliberately NOT in include/:
// nothing outside this component may learn that the component has an inside. comfoair_mqtt.h is
// the whole of what the rest of the firmware is allowed to see, and it did not change when this
// header appeared.
//
// THE SHAPE OF THIS COMPONENT IS A COUNTER-REVIEW FINDING. The first version published from the
// esp-mqtt event handler and wrote flash there too. Both are wrong for the same reason and the
// reason is not obvious: esp-mqtt dispatches its events SYNCHRONOUSLY from inside its own API lock
// (mqtt_client.c:1661-1838), so the handler runs while the publisher task is blocked in
// esp_mqtt_client_publish -- and any buffer the two share is mutated under the one that is
// waiting. That produced a real, likely failure: the state payload of whatever was being published
// landed on <prefix>/status, retained, which is not "online", so Home Assistant marked the device
// and every entity unavailable until the next reconnect.
//
// So: the event handler now sets flags and copies bytes into a queue. Nothing else. Every publish,
// every flash write and every buffer lives on the publisher task.
//
// NINE FILES, ONE TASK -- and comfoair_mqtt_event.c is the exception that proves it. The split is
// by responsibility and changes nothing about that rule:
//
//  * comfoair_mqtt.c         -- the spine: the publisher task, the observer, the statistics, the
//                               public entry points
//  * comfoair_mqtt_command.c -- what an arriving command's decision makes happen. It left
//                               comfoair_mqtt.c for the 350-line ceiling; no behaviour moved with
//                               it.
//  * comfoair_mqtt_publish.c -- what goes out on the wire on EVERY connection, and the two big
//                               formatting buffers
//  * comfoair_mqtt_bus.c     -- <prefix>/bus, the CAN verdict as the second availability topic.
//                               It left comfoair_mqtt_publish.c for the 350-line ceiling; no
//                               behaviour moved with it.
//  * comfoair_mqtt_client.c  -- the client's life: start, stop and the configuration buffers it
//                               is built from
//  * comfoair_mqtt_event.c   -- the esp-mqtt event handler, which is the one file here that does
//                               NOT run on the publisher task, and the only reason the rule above
//                               needs stating at all
//  * comfoair_mqtt_retry.c   -- the retry ladder on the device side: the clock, the address, the
//                               one call to esp_mqtt_client_reconnect() and the four values
//                               /api/status reads. It was the fourth file rather than more lines
//                               in the third for the reason the third exists at all -- the
//                               350-line ceiling (AGENTS.md) -- and the DECISIONS inside it are
//                               comfoair_mqtt_backoff's, where a host test can reach them.
//  * comfoair_mqtt_retract.c -- what the device empties when it stops being the thing it was. It
//                               left comfoair_mqtt_publish.c for that same ceiling, along the
//                               seam comfoair_ha already draws on its own side: the
//                               publisher sends what the device IS and the retraction unsays it.
//                               No behaviour moved with it.
//  * comfoair_mqtt_discovery.c -- the config documents and the three subscriptions: what makes
//                               Home Assistant know this device exists. It left
//                               comfoair_mqtt_publish.c for the ceiling too, and it is the one
//                               file here whose contents do NOT go out on every
//                               connection -- it goes out once per identity, with the three
//                               conditions themselves in comfoair_discovery_gate.h where a host
//                               test can call them.
//
// EVERYTHING DECLARED HERE IS SHARED BY AT LEAST TWO OF THEM, and that is the point of the g_
// prefix: in this codebase s_ means "private to one file", so a variable several files can reach
// must not wear it. Anything still called s_ inside one of the nine is private to that file and
// is absent from here. DO NOT add an extern for a variable only one file touches -- the prefix is
// the only thing telling a reader how far a write can be felt.
//
// ONE LOG TAG, "mqtt", defined separately in each of them. The tag is user-visible -- the log
// ring is served over GET /api/log -- and an owner reading it is looking at one broker connection,
// not at this repository's file boundaries. Nine tags would make an internal seam visible in the
// owner's log for no gain, and would break every grep in docs/hardware-verification/.
```

## Dependencies

`REQUIRES mqtt comfoair_ha comfoair_mqtt_inbound comfoair_mqtt_backoff comfoair_net
comfoair_control comfoair_command comfoair_wire comfoair_state comfoair_publish_rate
esp_app_format` (`CMakeLists.txt`).
`comfoair_net_broker()` is the one unredacted config projection, because MQTT authenticates with
the plaintext password. `COMFOAIR_PROV_NEVER` comes from `comfoair_provision.h` through
`comfoair_wire`'s interface.

Depended on by: `comfoair_http` (REQUIRES; `comfoair_mqtt_reconfigure`, `get_stats`,
`notify_state_changed`), `comfoair_phase_monitor` (PRIV_REQUIRES; `get_stats`, `note_bus`) and
`src/main.cpp` (`init`, `set_observer`, `start`, `notify_state_changed`).

## Tests

`[env:native]` does not build this component (`build_src_filter = -<*>`, and esp-mqtt has no
fake). The decisions are tested through the pure neighbours: `test/test_mqtt_inbound`,
`test/test_mqtt_backoff` (ladder plus `test_mqtt_wiring.cpp`, the event-handler/ladder protocol),
`test/test_mqtt_facts`, `test/test_discovery_gate`, `test/test_retract_budget`,
`test/test_device_url`, `test/test_ha`, `test/test_ha_none`, `test/test_ha_cleanup`,
`test/test_ha_freshness`, `test/test_publish_rate` (the rate rule) and `test/test_state_copy`
(the text copied under the lock).

One suite reads this component's source directly: `test/test_ha_availability` opens
`comfoair_mqtt_publish.c` and `comfoair_mqtt_bus.c` (the counts run over both), strips comments, and counts token sequences
(`return entity->kind != COMFOAIR_KIND_FRESHNESS;`, `QOS_STATE, state_retained(entity))`,
exactly one `QOS_STATE, true)`, two `QOS_META, true)`, no `..., false`). Do not write those
sequences anywhere else in those files, and keep them as they are.

Not covered by any host suite: the task loop and its ordering, `comfoair_mqtt_handle_inbound()`'s
effects, the event handler's writes (deleting `bump(&g_disconnects)` leaves every suite green),
client build/teardown, the retraction's outbox drain, and pacing. These are checked on hardware in
`docs/hardware-verification/06-mqtt.md` and `06b-home-assistant.md` (e.g. HV-MQTT-16/18/23/24,
HV-HA-08..12/18).

## Notes

- **Two inbox drains around a reconfigure.** The first drain parses commands against the old
  prefix before the rebuild. After the rebuild they would resolve to nothing and be dropped
  silently. The cost: a command from the outgoing broker can still be applied after the owner has
  switched brokers.
- **`comfoair_mqtt_handle_inbound` ordering.** For a SUBMIT, the command is submitted first and
  only then is the optimistic state published (`comfoair_state_set_option`). For a STORE, the
  duration is stored first, the result is checked, and only then is `comfoair_state_set_number`
  called.
  `ESP_ERR_INVALID_STATE` ("commands are not running") is the same condition the HTTP twin answers
  503 for. Reject logs the reason, never the payload, because the log is served by `GET /api/log`.
- **Cleanup choice** (`comfoair_mqtt_pending_cleanup`, in the client file because only that file
  knows both identities):
  - host or port moved → WITHDRAW, otherwise the old broker keeps a phantom device;
  - only the prefix moved → REKEY, so the configs are rewritten in place and the entities keep
    their history;
  - only the name moved, or nothing changed → nothing, because a blind wipe would also empty the
    config documents, and Home Assistant removes an entity whose config is emptied.
  It returns false when not connected: no waiting and no retries for an absent broker.
- **Explicit `offline` in `stop_client()`.** A clean DISCONNECT does not trigger the will. Without
  it, a retained `online` would outlive a cleared broker setting.
- **`g_url` has two writers** (`start_client` and `note_address`), both on the publisher task. At
  boot the client is built before Wi-Fi has an address, so the URL is filled in later and discovery
  is re-armed when it moves. The address is read only while disconnected, because the IP statics
  are written by the Wi-Fi task without synchronisation.
- **One log tag, `"mqtt"`, in every file.** The tag is visible to the owner through `GET /api/log`,
  and the hardware-verification greps depend on it.
- **Discovery is skipped** when `g_discovery` is off. The gate is marked SENT only after a complete
  sweep with the link up and no refused publish. A document that does not fit is logged and does
  not hold the gate open.
- **Fragmented or oversized inbound messages are rejected, never applied in part.** That covers
  `total_data_len != data_len`, a payload of `COMFOAIR_MQTT_PAYLOAD_MAX` 512 or more, and a topic
  of 192 or more. A full inbox also rejects.
