# CAN bus

The ventilation unit talks ComfoNet, which is classic CAN with 29-bit identifiers at 50 kbit/s.
The firmware drives it through the ESP32-C6's on-chip TWAI controller, and the component that
does so, `components/comfoair_can`, is deliberately thin: it moves frames and keeps statistics.
Every rule about what an identifier or a payload *means* lives in the host-tested headers of
`components/comfoair_protocol` (see [PDO and RMI](/protocol/pdo-rmi)).

## Controller configuration

The controller is brought up once at boot by `comfoair_can_start()`, with everything
board-specific taken from the board descriptor (`board_get()` in `components/board/`).

| Setting | Value | Where it comes from |
| --- | --- | --- |
| Bit rate | 50 000 bit/s | `board->can_bitrate`; the NanoC6 descriptor sets `50000` |
| TX / RX pins | board descriptor | `board->can_tx`, `board->can_rx` (NanoC6: TX GPIO 2, RX GPIO 1, in `board_m5stack_nanoc6.c`) |
| Bit timing | bit rate only | the driver chooses the segments and sample point |
| Acceptance filter | none | every frame on the bus reaches the receive path |
| Transmit identifiers | always extended (29-bit) | `.ide = 1` in `comfoair_can_transmit()` |
| Driver TX queue | 8 frames | `TX_QUEUE_DEPTH` |
| Retransmissions per frame | 3 | `.fail_retry_cnt = 3`; never `-1` (endless retry against an unpowered unit) |
| RX software queue | 32 frames | `RX_QUEUE_LEN` |

The pins are never named outside `components/board/`; another board would supply its own
descriptor and the transport would not change.

### Driver

The transport uses ESP-IDF's node-based TWAI driver, the `esp_driver_twai` component
(`esp_twai.h`, `esp_twai_onchip.h`; `REQUIRES board esp_driver_twai` in
`components/comfoair_can/CMakeLists.txt`). It is the driver every TWAI example in the pinned
ESP-IDF (5.5.5) is written against, and it exposes the pieces this transport is built on:
per-node callbacks for receive, transmit-done, errors and state changes, and an explicit
`twai_node_recover()`. The legacy `driver/twai.h` API is still shipped and not marked deprecated in
that release, so it remains a fallback if the new driver ever has to be abandoned; nothing in the
firmware uses it.

If the controller does not come up, `app_main` logs `CAN did not start: … -- continuing without
the bus`, records a CAN fault for the indication and `/api/status`, and carries on: the network,
the web UI and MQTT still start so the fault can be seen. The PDO request cycle and the command
sender are only started when the bus did come up, and the RMI probe only when the command sender
started as well (`src/main.cpp`).

## Node identity

The firmware is one node on the bus and always speaks as **node `0x2A`**
(`COMFOAIR_POLL_DEFAULT_CONFIG().local_node_id` and `COMFOAIR_CONTROL_LOCAL_NODE`, which must
agree). The ventilation unit is **node `0x01`** (`COMFOAIR_CONTROL_UNIT_NODE`), and commands are
addressed to it and nothing else — the destination is a constant on purpose, not a setting.

The firmware does **not** transmit a node-presence heartbeat. Its only transmitters are the PDO
request cycle, the command sender and the RMI probe task (owner-started walks and the periodic unit
identity read). On the owner's Q350 the unit answered PDO requests and RMI reads from `0x2A`
without one (see the [probe findings](/reference/rmi-probe-findings-q350)).

## Identifier classes

The receive callback sorts every frame by its identifier. The tests behind each rule live with
the protocol headers.

| Class | Identifier test | What the firmware does with it |
| --- | --- | --- |
| PDO broadcast | `(id & 0x18003FC0) == 0x00000040`, extended, not RTR | decoded into the state model |
| PDO request | same layout, sent as a remote-transmission request | sent by the [polling](/protocol/polling) cycle; another node's RTR is ignored |
| RMI | `(id & 0x1FF80000) == 0x1F000000` | the probe queues responses addressed to `0x2A` while a walk or the unit identity read is running; every RMI frame is also logged at DEBUG |
| Heartbeat | `(id & ~0x3F) == 0x10000000` | recorded in the node census, then logged at DEBUG as `other id=…` like anything else |
| Anything else | — | logged at DEBUG as `other id=…` |

A frame that is not extended, or is a remote-transmission request, is dropped before any of this:
an RTR from another node carries an identifier identical to a PDO broadcast, and without the
guard it would overwrite entity values with nothing (`on_can_frame()` in `src/main.cpp`).

## Receive path

1. **Interrupt** (`comfoair_can_on_rx_done`): copies the frame into the 32-deep RX queue and
   returns. An RTR is stored with length 0 whatever its DLC says, and a DLC above 8 is clamped to 8.
   If the queue is full — or the interrupt fired with nothing to collect — `rx_dropped` is
   incremented; no frame disappears without being counted.
2. **Task `can_rx`** (priority 10, 4 KiB stack): takes frames off the queue and calls the
   consumer's callback with the identifier, the extended and RTR flags, the data and the length.
   The data is valid only during the call.
3. **Consumer** (`on_can_frame()` in `src/main.cpp`): feeds every extended, non-RTR frame to the
   probe's node census, then applies PDOs to the state model and notifies HTTP and MQTT. Nothing on this
   path blocks: a stall here would overflow the RX queue and lose PDOs.

## Transmit path

`comfoair_can_transmit(id, data, len, rtr, timeout_ms)` sends one extended frame of 0–8 bytes.

- The frame is copied into one of **9 transmit slots** (8 queued in the driver plus one in the
  shift register). The driver keeps a *pointer* to the frame and reads it later from the TX-done
  interrupt, so the slot stays owned by the transport until that interrupt releases it, however
  long the caller waited.
- When all 9 slots are busy the call returns `ESP_ERR_NO_MEM` at once.
- Otherwise it waits up to `timeout_ms` for the driver to finish and returns `ESP_ERR_TIMEOUT`
  if it did not. On a bus whose unit is unpowered nothing is acknowledged, so a timeout is the
  ordinary failure, not an exotic one.
- An RTR is sent with no data and DLC 0.

| Caller | Frames | Timeout per frame |
| --- | --- | --- |
| PDO request cycle (`comfoair_poll`) | RTR, no data | 100 ms |
| Command sender (`comfoair_control`) | RMI chunks | 200 ms |
| RMI probe (`comfoair_probe_task`) | RMI chunks | 200 ms |

None of the callers retries a failed send on its own; the driver's three retransmissions are the
only repetition at this level. (The probe re-asks a request once when no *reply* arrives, which is
a different thing — see [Probe](/protocol/probe#what-a-walk-asks).)

## Errors and bus-off

The controller's error interrupt increments `bus_errors`. State changes are forwarded from the
interrupt to a second task, **`can_bus`** (priority 9), because recovery cannot run in an
interrupt:

- When the controller goes **bus-off**, the task calls `twai_node_recover()` and counts it in
  `bus_recoveries`. The first attempt is immediate; if the bus keeps collapsing, the next attempts
  wait 250 ms, then double up to a cap of 5000 ms.
- When the controller **leaves** bus-off, the back-off is reset to zero and `bus recovered` is
  logged.
- It never gives up. A unit that is switched on tomorrow must still be found, and nothing reboots
  because the bus is dead.

Error-warning and error-passive transitions are not acted on; they are visible in the statistics.

## Statistics

`comfoair_can_get_stats()` feeds the `bus` block of `GET /api/system`. The four transport counters
(`rx_frames`, `rx_dropped`, `bus_errors`, `bus_recoveries`) are zeroed by `comfoair_can_start()`, so
they describe the current session on the bus; the node census is not, and covers everything since
boot.

| Field | Meaning |
| --- | --- |
| `started` | the controller is running |
| `state` | `stopped`, `active`, `warning`, `passive` or `off` — `stopped` is checked first, because the driver also reports a stopped node as bus-off |
| `rx_frames` | frames delivered to the RX queue |
| `rx_dropped` | frames lost because the queue was full, or interrupts with nothing to collect |
| `bus_errors` | error interrupts |
| `bus_recoveries` | bus-off recoveries requested; a number that keeps climbing points to wiring |
| `tx_error_count`, `rx_error_count` | the controller's own error counters |
| `nodes_seen` | every node id heard since boot (from the probe's census, see [Probe](/protocol/probe)) |

Whether the *unit* is still talking is a separate verdict built on `rx_frames`; it is described
under [Polling](/protocol/polling#when-the-unit-stops-answering).

## What is logged

| Level | Line | When |
| --- | --- | --- |
| INFO | `up on tx=… rx=… at 50000 bps` | the controller started |
| ERROR | `twai_new_node_onchip: …` | the driver refused the configuration |
| WARN | `bus off -- recovery requested` | a recovery was started |
| INFO | `bus recovered` | the controller left bus-off |
| DEBUG | `recover: …` | a recovery request was refused (usually the node had already left bus-off) |
| ERROR | `a task did not retire in time` | `comfoair_can_stop()` could not stop a task within 1 s |
| DEBUG | `PDO … node … len … unclaimed` | a PDO no entity uses (from `src/main.cpp`) |
| DEBUG | `RMI id=… len=… response` / `request` | every RMI frame received |

The log tag is `can` for the transport and `comfoair` for the receive callback; the ring buffer is
served by `GET /api/log`.
