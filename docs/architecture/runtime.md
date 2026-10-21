# Runtime

What happens from power-on, which FreeRTOS tasks then run, who owns which piece of state, and
the timing constants that shape behaviour. Everything here is read from `src/main.cpp` and the
components it starts.

## Boot sequence

`app_main()` in `src/main.cpp` starts the subsystems in a fixed order. The order is chosen so that
anything that can fail comes after the things the owner needs to see and recover from a failure.
Every start except NVS is "log the error and carry on"; seven of them also record a fault bit.

| # | Call | On failure |
| --- | --- | --- |
| 1 | `comfoair_log_install()` — route ESP-IDF logging into the RAM ring as well as the console | — (first, so boot lines are in `GET /api/log`) |
| 2 | Log project name and version from `esp_app_get_description()` | — |
| 3 | `nvs_init()` — `nvs_flash_init()`; on `ESP_ERR_NVS_NO_FREE_PAGES` or `ESP_ERR_NVS_NEW_VERSION_FOUND` the partition is erased and initialised again | any other error aborts the boot (`ESP_ERROR_CHECK`) |
| 4 | `comfoair_state_reset()` — seed registry defaults, including `schema_version` | — |
| 5 | `board_get()` and a log line with the board name, CAN pins and bitrate, button pin | — |
| 6 | `comfoair_led_init(b)` — indication; the LED shows the `boot` look until the phase monitor runs | logged, continue without indication |
| 7 | `comfoair_button_start(b, comfoair_net_open_setup_window)` — before bus and network, so the reset gesture exists even if they fail | logged: no button reset this boot |
| 8 | Log the compiled-in web UI size | — |
| 9 | `comfoair_can_start(b, on_can_frame)` — before the network, so a slow router never delays reading the unit | fault `CAN`; poll, control and probe are skipped |
| 9a | `comfoair_poll_start()` — hourly PDO request sweep | fault `POLL`; the unit still broadcasts |
| 9b | `comfoair_control_start()`, then the four durations are copied from NVS into the state model | fault `CONTROL`; the unit is read-only |
| 9c | `comfoair_probe_task_init()` — only once the sender started, because it shares its sequence counter | logged; only the probe is lost |
| 10 | `comfoair_net_start(b, on_net_state)` — provisioning task, Wi-Fi, configuration load | fault `NET`, the phase monitor is started, and `app_main` returns: steps 11–15 never run |
| 11 | `comfoair_mqtt_init()` — the inbound queue, before HTTP so a config save can always post a reconfigure | fault `MQTT_QUEUE` |
| 12 | `comfoair_http_start()` on port 80 | fault `HTTP` |
| 12a | `comfoair_ota_runtime_start()` — only if HTTP started; arms the 30 s confirmation of a new image | logged; the image stays on probation |
| 13 | `app_mdns_start()` — `comfoair-XXXX.local` and `_http._tcp` (port 0 = name only, if HTTP failed) | logged inside |
| 14 | `comfoair_mqtt_set_observer(comfoair_http_notify_state_changed)` | — |
| 15 | `comfoair_mqtt_start()` — builds the client if a broker is configured | `ESP_ERR_NOT_FOUND` (no broker) is normal; anything else is fault `MQTT_CLIENT` |
| 16 | `comfoair_phase_monitor_start()` — last, so its first tick sees every start verdict | logged; the device then reports phase `fault` |

The seven fault bits (`CAN`, `POLL`, `CONTROL`, `NET`, `MQTT_QUEUE`, `MQTT_CLIENT`, `HTTP`) are
recorded with `comfoair_phase_monitor_note_fault()`, which works before the monitor exists. They
are sticky for the boot (only ever OR-ed in) and turn the device phase into `fault`, which only
`ota`, `trial` and the two access-point phases outrank (`comfoair_phase_derive()` in
`components/comfoair_phase/comfoair_phase.c`) — visible on the LED and in `GET /api/status` without
a USB cable. See [Indication](/device/indication).

After step 16 `app_main()` returns; from then on the device is the set of tasks below.

## Tasks

| Task name | Created by | Prio | Stack (bytes) | Role |
| --- | --- | --- | --- | --- |
| `can_rx` | `comfoair_can` | 10 | 4096 | Drains the 32-deep receive queue and calls `on_can_frame()` for every frame: probe census, PDO decode into the state model, notify both transports. Must never block. |
| `can_bus` | `comfoair_can` | 9 | 3072 | Bus-off recovery: `twai_node_recover()`, the first attempt at once, then 250 ms doubling to 5 s; the backoff resets when the bus is active again. |
| `comfoair_poll` | `comfoair_poll` | 5 | 3072 | One RTR sweep of the request PDOs (`COMFOAIR_REQUEST_PDOS`) every hour, 100 ms between frames, as node `0x2A`; can be told to sweep now, or to request one PDO after a delay (the away timer, ~1.5 s after an away command). |
| `comfoair_ctrl` | `comfoair_control` | 4 | 3072 | Takes built commands off an 8-deep queue and transmits their frames (200 ms transmit timeout each), honouring per-frame delays (the "Auto" preset's third frame waits 1000 ms). |
| `comfoair_probe` | `comfoair_probe_task` | 3 | 4096 | Runs a probe walk when asked; reads unit identity, settings and active errors 10 s after start, then every 6 h (every 60 s until a read is complete); reads the active errors alone every 60 s in between and writes the two error entities. |
| `comfoair_net` | `comfoair_net` | 5 | 4096 | The provisioning task: owns the radio, feeds Wi-Fi events into the state machine, ticks it every 250 ms, performs its actions (connect, commit, restore), persists flags. |
| `comfoair_dns` | `comfoair_captive` | 3 | 3072 | Captive DNS responder on UDP 53, running only while the access point is up. |
| httpd | `esp_http_server` | 5 (IDF default) | 8192 | Serves every HTTP request, one at a time, with up to 7 open sockets. Also runs the WebSocket send, which the push task queues onto it with `httpd_queue_work()`. |
| `comfoair_ws` | `comfoair_http` | 4 | 4096 | WebSocket push: wakes on a state change or every 15 s, waits at least 250 ms between pushes, sends full state to new clients and deltas or a `ping` otherwise. |
| `comfoair_mqtt` | `comfoair_mqtt` | 4 | 4096 | The publisher: drains inbound commands (8-deep queue), applies a reconfigure, runs the reconnect ladder, publishes availability, discovery and state. Wakes on notification or every 1 s. |
| `mqtt_task` | esp-mqtt | 5 (IDF default) | 6144 (IDF default) | The client's own network loop, present while a broker is configured; it dispatches events to `comfoair_mqtt`'s handler, which only records facts and enqueues commands. |
| `comfoair_phase` | `comfoair_phase_monitor` | 3 | 3072 | 1 Hz: reads the network, CAN and MQTT state, derives the phase, sets the LED, reports bus availability to MQTT, asks for a poll sweep when the bus has been quiet for 60 s. |
| `comfoair_led` | `comfoair_led` | 3 | 3072 | Renders the current look every 25 ms (RGB LED and heartbeat). |
| `comfoair_btn` | `comfoair_button` | 4 | 2560 | Samples the button every 20 ms and executes the gesture on release. |
| `ota_restart` | `comfoair_ota_runtime` | 1 | 4096 | Short-lived: sleeps, then `esp_restart()`, so an HTTP answer goes out first. |
| `comfoair_bye` | `comfoair_mdns` | 5 | 3072 | Short-lived: sends the mDNS goodbye before a deliberate restart. |
| `mdns` | `espressif__mdns` | 1 | 4096 | The mDNS responder (`CONFIG_MDNS_TASK_PRIORITY`, `CONFIG_MDNS_TASK_STACK_SIZE`). |

ESP-IDF's own system tasks (Wi-Fi, TCP/IP, the event loop, `esp_timer`) run beside these.

Three callbacks run outside these tasks: the TWAI receive callback runs in interrupt context and
only copies the frame into the `can_rx` queue; the Wi-Fi/IP event handler runs on the system event
task and only enqueues a fact for `comfoair_net`; the 30 s OTA confirmation runs from an
`esp_timer`.

## Ownership and locking

The concurrency rule throughout is **one owner per piece of mutable state**, with a lock only where
more than one task genuinely has to touch the same data.

| State | Owner / writers | Protection |
| --- | --- | --- |
| Entity values and change marks (`comfoair_state`) | Written by `can_rx` (PDOs), by httpd and `comfoair_mqtt` (optimistic select sets and stored durations), by `app_main` (durations at boot) | `comfoair_lock()`: one recursive FreeRTOS mutex, `components/comfoair_lock/`. Recursive because a renderer holds it across a whole document while calling per-entity getters that take it again. |
| Configuration document, provisioning machine | `comfoair_net` | Two recursive mutexes in `comfoair_net.c`: one around the machine and the document, one around the radio. The radio holder takes the machine lock briefly to copy the station credentials (`apply_sta_config()`), never the reverse; a request asking "is the device provisioned?" takes only the machine lock, so it never waits behind a Wi-Fi scan. |
| Durations and permanent-manual setting | `comfoair_control` | A mutex; readers get a copy. |
| RMI sequence counter | shared by `comfoair_ctrl` and `comfoair_probe` | A statically initialised spinlock; callable before either task starts. |
| Probe walk and results | `comfoair_probe` task | One mutex around the pure probe module; the node census is a separate spinlock so `can_rx` never waits. |
| esp-mqtt client, topic context, retry ladder | `comfoair_mqtt` publisher task only | No lock: no other task creates, destroys or publishes on the client. Other tasks reach it through `comfoair_mqtt_notify_state_changed()`, `comfoair_mqtt_reconfigure()` and `comfoair_mqtt_note_bus()`, which set a flag or an atomic and wake the task. Statistics are atomics. See the MQTT rules below. |
| LED look and overlays | `comfoair_led` render task | Short critical sections; setters copy into a block the render task copies out. |
| Device phase | `comfoair_phase` task | Readers load one word, no lock. |
| RAM log ring | whichever task logs | No lock, by design: concurrent log calls can interleave a line, an accepted exposure that keeps logging non-blocking from every task. |

Two rules keep the bus safe from the network:

- **Nothing on the CAN receive path waits on the network.** `on_can_frame()` feeds the probe's
  census (a spinlock and a non-blocking queue send), applies the PDO under the state lock — the one
  thing it can wait for, while a renderer holds it — and sets two flags; publishing happens on the
  MQTT and WebSocket tasks. A blocking call there would stop the bus reader, overflow the 32-deep
  receive queue and lose PDOs.
- **No request handler touches the bus.** HTTP and MQTT handlers queue commands for
  `comfoair_ctrl`; `comfoair_can_transmit()` waits for the driver, and on an unpowered unit
  nothing is ever acknowledged.

Three rules keep MQTT from deadlocking or losing updates (`components/comfoair_mqtt/`):

- **One task owns the client, and there is no MQTT lock.** Starting, stopping, publishing,
  retracting and reconnecting all run on the `comfoair_mqtt` publisher task. The counters other
  tasks increment are `_Atomic` (`comfoair_mqtt.c`). Why not a mutex: esp-mqtt dispatches its
  events synchronously from inside its own API lock, so a component lock taken in the event handler
  is ordered "API lock, then ours", while a publish under the component lock takes them the other
  way round. That ABBA inversion once hung MQTT together with the single web-server task, and the
  same lock held around `esp_mqtt_client_stop()` could wait for ever on a client task that was
  itself waiting for the lock.
- **The event handler enqueues and records; it never publishes or writes flash.**
  `comfoair_mqtt_on_event()` (`comfoair_mqtt_event.c`) runs on the esp-mqtt task while the publisher
  may be blocked inside `esp_mqtt_client_publish()`. Any buffer the two shared would be mutated under
  the waiting publisher — which is how, before this rule, a state payload once landed retained on
  `<prefix>/status` and greyed out every entity in Home Assistant. The handler sets flags, stores
  atomics and copies inbound commands into the 8-deep queue; every publish and every flash write
  happens on the publisher task.
- **A change mark is cleared only if no newer change arrived.** Each state consumer has a
  generation counter per entity that rises with every mark. The publisher samples it before
  reading the value and clears the mark with `comfoair_state_clear_change_if()` only if it has not
  moved (`publish_and_clear()` in `comfoair_mqtt_publish.c`); the compare and the clear happen in
  one hold of the state lock. An unconditional clear would erase the mark of a PDO applied while
  the publish was in flight — and for the durations, which nothing rebroadcasts, the retained
  value on the broker would stay wrong until the owner changed it again (for the three RMI-read
  selects, until the next settings read, up to 6 h later).

## The RAM log

`comfoair_log` (`components/comfoair_log/include/comfoair_log.h`) keeps the last
`COMFOAIR_LOG_LINES` = 80 lines of up to 159 characters (`COMFOAIR_LOG_LINE_LEN` = 160-byte slots, about 13 KB)
in a ring; when it is full the oldest line goes. `comfoair_log_install()` puts a hook in front of
ESP-IDF's `vprintf`, so every `ESP_LOG*` line reaches both the console and the ring; fragments are
assembled into whole lines at each newline first.
The ring is served by `GET /api/log` as a JSON array, oldest first, and is cleared by the 15 s
button reset. Because the log is readable over HTTP, no component logs a secret — see
[Security](/architecture/security).

## Timing constants that matter

| Constant | Value | Source | Effect |
| --- | --- | --- | --- |
| CAN bitrate | 50 kbit/s | `board_m5stack_nanoc6.c` | Fixed by the ComfoAir Q bus. |
| Poll sweep | 1 h, 100 ms between frames | `COMFOAIR_POLL_DEFAULT_CONFIG()` | Refresh for rarely broadcast PDOs. |
| Command transmit timeout | 200 ms per frame | `comfoair_control.cpp` | Bounds the wait on a unit that does not acknowledge. |
| Bus quiet / probe / silent | 30 s / 60 s / 90 s | `COMFOAIR_BUS_QUIET_MS` in `comfoair_bus_health.h` | 60 s of silence triggers one poll sweep; 90 s marks the bus silent and publishes `offline` on the bus availability topic. |
| Bus-off recovery backoff | first attempt at once, then 250 ms doubling to 5 s | `comfoair_can.c` | Automatic recovery of a bus-off controller. |
| WebSocket push floor / keep-alive | 250 ms / 15 s | `WS_MIN_INTERVAL_MS`, `WS_KEEPALIVE_MS` in `comfoair_http_ws.c` | Coalesces PDO bursts; the keep-alive stays under the SPA's own 35 s silence timeout (`SILENCE_TIMEOUT_MS` in `web/src/api/ws.ts`), after which the client drops the socket. |
| MQTT network timeout | 2 s | `comfoair_mqtt_client.c` | Bounds connect, CONNACK wait and each socket write. |
| MQTT reconnect ladder | 2 s doubling to 60 s (1 s start after a dropped session); 300 s flat for refusals; ±20 % jitter | `comfoair_mqtt_backoff.c` | See [Failure model](/architecture/failure-model). |
| MQTT session "settled" | 60 s | `comfoair_mqtt_backoff_settled()` | Only a session this long resets the attempt counter. |
| No-broker hold-off | 30 s | `COMFOAIR_PHASE_NO_BROKER_MS` | The phase turns `no-broker` only after 30 s disconnected. |
| Provisioning tick | 250 ms | `comfoair_net.c` | Deadlines of the provisioning machine. |
| Setup window | 15 min, extended by activity up to 60 min; 5 min when reopened | `comfoair_prov_config_defaults()` | How long the open access point stays up. |
| Wi-Fi trial / address timeout | 90 s / 30 s | same | New credentials roll back after 90 s without an address. |
| Wi-Fi retry backoff | 1 s doubling to 30 s | same | Station reconnects; never a reboot. |
| Fallback access point | after 30 min of failure | same | See [Failure model](/architecture/failure-model). |
| OTA confirmation | 30 s after the HTTP server started | `COMFOAIR_OTA_CONFIRM_DELAY_MS` | Before this, a restart rolls back to the previous image. |
| OTA restart delay | 1 s | `comfoair_http_ota.c` | The 200 answer reaches the browser before the restart. |
| Failed-login delay | 1 s | `COMFOAIR_AUTH_FAIL_DELAY_MS` | Paid on the single httpd task. |
| Phase monitor / LED render / button poll | 1 s / 25 ms / 20 ms | `comfoair_phase_monitor.c`, `comfoair_led.h`, `comfoair_button.h` | — |
