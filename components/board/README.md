# board — the board descriptor: the only place in the firmware where a GPIO number is named

> This is documentation, not code — re-read the source before relying on a detail.

Every peripheral the firmware drives (the CAN transceiver, the reset button, the heartbeat LED,
the WS2812 colour pixel) sits on a pin that differs from board to board, and some carry other
board-specific facts (button polarity, whether the button is also the download strap, the WS2812
byte order, the network transport). This component collects all of them into one constant
`board_t` so that every other component takes "the board" as a value and names no pin. Adding a
board is meant to be a new descriptor file here, never a grep for pin numbers through the tree
(AGENTS.md acceptance criterion 5). v1 ships exactly one descriptor: the M5Stack NanoC6
(ESP32-C6, 4 MB).

## Responsibility

Owns:
- The `board_t` / `board_rgb_t` descriptor types and the one NanoC6 instance (`k_nanoc6`).
- `board_get()` — the accessor to that static descriptor.
- The WS2812 byte-order vocabulary (`board_rgb_order_t`, all six permutations) and the pure
  permutation table `board_rgb_positions()`, so the order is a board property with one copy.

Does NOT do:
- Configure or drive any pin. GPIO setup is done by the consumers: `comfoair_can` (TWAI),
  `comfoair_button` (input + pull chosen from `button_inverted`), `comfoair_led` (status LED,
  RMT/led_strip, power gate sequencing), `comfoair_net` (transport check).
- Translate the order into the `led_strip` library's struct — `comfoair_led_driver.c`
  (`format_of()`) does that from `board_rgb_positions()`.
- Select between boards at build or run time. There is no selection mechanism (see Notes).
- Persist or accept anything at runtime; the descriptor is compile-time constant.

## Public API

| Symbol | Contract |
| --- | --- |
| `const board_t *board_get(void)` (`include/board.h`) | Any task, any time, no init needed, never fails, never NULL. Returns a pointer to `static const` storage that lives for the whole program — callers may keep and pass it around (consumers that take a `const board_t *` document it as BORROWED, which is safe precisely because of this). No `esp_err_t`. |
| `void board_rgb_positions(order, *r_pos, *g_pos, *b_pos)` (`include/board_rgb_order.h`) | Pure, total, reentrant. Writes the wire position (0..2) of each LOGICAL channel under `order`; any output pointer may be NULL and is skipped. An `order` outside the enumeration answers as `BOARD_RGB_ORDER_GRB` (the descriptor's default hypothesis) rather than leaving outputs uninitialised. |
| `board_t` | `name` (string literal, `"m5stack-nanoc6"`; surfaces in `GET /api/system` `device.board` and the mDNS TXT `board=`), `can_tx`/`can_rx`/`can_bitrate`, `button`/`button_inverted`/`button_is_download_strap`, `status_led`/`status_led_inverted`, `rgb` (`board_rgb_t`), `net_transport`. |
| `board_rgb_t` | `present`, `data`, `power_gate` (`GPIO_NUM_NC` = LED wired straight to the rail), `count`, `color_order`, `power_gate_settle_ms` (0 = no wait). |
| `board_net_transport_t` | `BOARD_NET_WIFI`, `BOARD_NET_ETH`. Only Wi-Fi is implemented; `comfoair_net_start()` returns `ESP_ERR_NOT_SUPPORTED` for anything else. |
| `board_rgb_order_t` | `GRB, RGB, RBG, GBR, BRG, BGR`, then `BOARD_RGB_ORDER_COUNT` (a count, not an order). Each name is the order **the chip expects on the wire, first byte first** — not the permutation applied to it. |

`GPIO_NUM_NC` is the uniform "there is none" sentinel for `button`, `status_led` and
`rgb.power_gate`; consumers degrade on it (no button → `ESP_ERR_NOT_SUPPORTED` from
`comfoair_button_start()`, no status LED → the heartbeat is mixed into the colour looks, etc.).

NanoC6 values (`board_m5stack_nanoc6.c`) and why:

| Field | Value | Why |
| --- | --- | --- |
| `can_tx` / `can_rx` | `GPIO_NUM_2` / `GPIO_NUM_1` | vendor documentation |
| `can_bitrate` | `50000` | the ComfoAir Q (ComfoNet) bus runs at 50 kbps; not negotiable. Carried in the descriptor, not a literal in the driver, so a board that disagrees can say so |
| `button` | `GPIO_NUM_9`, `button_inverted = true`, `button_is_download_strap = true` | GPIO9 is the C6 download strap: holding it through power-on enters the bootloader, so the reset gesture is a hold AFTER boot only |
| `status_led` | `GPIO_NUM_7`, `status_led_inverted = false` | blue LED1, anode on G7, 22 R to GND → HIGH lights it |
| `rgb.data` / `rgb.power_gate` | `GPIO_NUM_20` / `GPIO_NUM_19` | WS2812 data and its supply gate |
| `rgb.count` | `1` | the schematic leaves the WS2812's DO unconnected, so a chain is physically impossible |
| `rgb.color_order` | `BOARD_RGB_ORDER_GRB` | **not verified on hardware**; undocumented by the vendor, GRB is the common WS2812 order and has been seen working |
| `rgb.power_gate_settle_ms` | `10` | **not verified on hardware**; the vendor only says to drive G19 high, with no timing figure |
| `net_transport` | `BOARD_NET_WIFI` | |

## Implementation

- `include/board.h` — the descriptor types and `board_get()`. Includes `board_rgb_order.h` and
  `driver/gpio.h` (for `gpio_num_t`); wraps declarations in `extern "C"` because `src/main.cpp`
  is C++ and would otherwise look for mangled names.
- `include/board_rgb_order.h` — the order enum and `board_rgb_positions()`. Includes only
  `<stdint.h>`.
- `board_m5stack_nanoc6.c` — the single `static const board_t k_nanoc6` and the one-line
  `board_get()` returning it.
- `board_rgb_order.c` — `k_positions[BOARD_RGB_ORDER_COUNT][3]`, rows indexed by enumerator, each
  row `{r_pos, g_pos, b_pos}`; `board_rgb_positions()` bounds-checks and copies one row.
- `CMakeLists.txt` — both sources unconditionally, `INCLUDE_DIRS include`,
  `REQUIRES esp_driver_gpio` (public, because `board.h` exposes `gpio_num_t`).

There is no lifecycle, no state, no locking: everything is `const` data and a pure function.

Reading a `k_positions` row: the enumerator name read left to right is the wire sequence; the row
holds where each LOGICAL channel lands. GRB = wire green, red, blue → red at 1, green at 0, blue
at 2 → `{1, 0, 2}`. Reading a row as "the wire order" gives the inverse permutation, which happens
to be right for the self-inverse orders (GRB, RGB, RBG, BGR) and wrong for GBR and BRG.

DO NOT:
- Name a GPIO number anywhere outside this directory, or have a consumer call `board_get()` and
  hard-code a pin "just this once". Consumers take `const board_t *` as a parameter
  (`comfoair_can_start`, `comfoair_led_init`, `comfoair_button_start`, `comfoair_net_start`,
  `app_mdns_start`); only `comfoair_http_system.c` calls `board_get()` itself, and only for `name`.
- Fold `board_rgb_order_t` into `board.h`. `board.h` drags in `driver/gpio.h`; anything that only
  needs to name an order (the pure LED resolver, `test/test_board_rgb_order`) would then need
  `esp_driver_gpio` in its REQUIRES and could not build on the host.
- Move the permutation back into `comfoair_led_driver.c` as a `switch`. It is here so a host
  suite can reach it; as a driver-local switch a wrong row compiled, satisfied `led_strip`, and
  passed every host case.
- Trim the enum to three orders. One bench observation (pure red) identifies only the position
  that carries R; a second (pure green) can land on any of the six, so all six must be nameable.
- Leave `color_order` / `power_gate_settle_ms` to the zero initialiser. They are spelled out so
  the hardware scenarios that settle them (HV-LED-01, HV-LED-02 in
  `docs/hardware-verification/07-indication-and-button.md`) each become a one-line edit here.
- "Correct" a NanoC6 value against the vendor product page without reading
  `docs/reference/esp32-node-hardware.md` — it records where the page and the hardware disagree.

## Dependencies

- Requires: `esp_driver_gpio` (public; header-only use of `gpio_num_t`). Nothing else.
- Depended on by (CMake `REQUIRES`/`PRIV_REQUIRES board`): `src/` (`main.cpp`, `app_mdns.c/.h`),
  `comfoair_can` (public — `comfoair_can.h` names `board_t`), `comfoair_net` (public),
  `comfoair_led` (private, but `comfoair_led_driver.h` names `board_t`, so `src/` lists `board`
  itself), `comfoair_button` (private, same arrangement for `comfoair_button_task.h`),
  `comfoair_http` (`comfoair_http_system.c`). `comfoair_mdns` and `comfoair_sysinfo` deliberately
  do NOT depend on it: they receive the board name as a plain `const char *`.

## Tests

- `test/test_board_rgb_order/test_board_rgb_order.cpp` — compiles `board_rgb_order.c` alone on the
  host. Checks the HV-LED-01 diagnostic table (transcribed from the bench scenario, not from the
  source, so the two check each other) against what a GRB packer really puts on the wire; that the
  table covers each order once; that configuring the true order round-trips every colour; that
  each row is a permutation; and the out-of-range → GRB fallback plus NULL outputs.
- `test/test_can_transport/test_can_transport.cpp` — links the real `board_m5stack_nanoc6.c`
  against the suite-local fake `test/test_can_transport/idf/driver/gpio.h` and asserts that
  `comfoair_can_start(board_get(), ...)` passes the descriptor's `can_tx`, `can_rx` and
  `can_bitrate` (and that the bitrate is 50000) into the TWAI config.
- Not covered on the host: every other NanoC6 value (button, LEDs, power gate, transport) — these
  are facts about hardware and are settled only by the scenarios under
  `docs/hardware-verification/`. The colour order and gate settle time are explicitly unverified.

## Notes

- **Why `net_transport` exists with only Wi-Fi implemented.** One of the boards the architecture
  must admit (Olimex ESP32-EVB) is Ethernet; naming the transport in the descriptor means the
  network layer refuses an unsupported board loudly (`ESP_ERR_NOT_SUPPORTED`) instead of assuming
  Wi-Fi.
- **Why polarity is a field, not a driver constant.** Both the button and the status LED carry an
  `_inverted` flag. A driver that hard-coded "1 means lit" would, on the first cathode-to-pin
  board, show the heartbeat lit 900 ms of every second — which reads as a fault, not as life.
- **Why the gate settle is a board field.** Only a board with a gated pixel needs a wait; 0 is the
  legitimate "no wait" for a board without one, and the value has no documentary source, so it
  must be replaceable in one place when measured.
- **No board selection exists.** `board.h` says adding a board is "a header plus a Kconfig entry",
  but no Kconfig is present and `CMakeLists.txt` compiles `board_m5stack_nanoc6.c`
  unconditionally. A second descriptor also defines `board_get()`, so a Kconfig choice or a
  per-environment source selection in `CMakeLists.txt` is a prerequisite, along with its own
  PlatformIO env and a partition-table check (`partitions.csv` assumes 4 MB).
- **The `_Static_assert` in `board_rgb_order.c` cannot fire as written:** the table is declared
  with dimension `BOARD_RGB_ORDER_COUNT`, so its row count always equals the count. A new
  enumerator without a row would get a zero-filled `{0,0,0}` row; what actually catches that is
  `test_every_order_is_a_permutation_of_the_three_positions` in the host suite.
- Full board reference (consumer-by-field table, what degrades on a new board):
  `docs/device/board.md`.
