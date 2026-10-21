# Board

`components/board/` is the board boundary. It is the only place in the firmware where a GPIO
number is written; every other component receives its pins, polarities and transport from a
`board_t` handed to it at start-up. The firmware ships for one board, the **M5Stack NanoC6**
(ESP32-C6, 4 MB flash), and the boundary exists so that the next board is a new descriptor rather
than a search through the tree for pin numbers.

## What the component contains

| File | Role |
| --- | --- |
| `include/board.h` | the `board_t` and `board_rgb_t` types and `board_get()` |
| `board_m5stack_nanoc6.c` | the one descriptor, `k_nanoc6`, and `board_get()` returning it |
| `include/board_rgb_order.h`, `board_rgb_order.c` | the WS2812 byte-order enumeration and the pure permutation table `board_rgb_positions()` |

`board_get()` returns a pointer to static, constant storage. `app_main` (`src/main.cpp`) calls it
once and passes the pointer down to each consumer it starts. The one other caller is
`comfoair_http_system.c`, which calls `board_get()->name` itself to fill `device.board` in
`GET /api/system`.

### `board_t`

| Field | Meaning |
| --- | --- |
| `name` | board name; reported in `GET /api/system` (`device.board`) and the mDNS TXT record (`board=`) |
| `can_tx`, `can_rx` | TWAI pins |
| `can_bitrate` | CAN bit rate; the ComfoAir Q bus runs at 50 kbit/s |
| `button` | the gesture button, or `GPIO_NUM_NC` for none |
| `button_inverted` | `true` when a pressed button reads low (internal pull-up is enabled); `false` enables a pull-down instead |
| `button_is_download_strap` | the pin is also the ROM download strap; it only adds a warning to the log (`comfoair_button_start()` and the boot line in `app_main`). The gesture logic does not read it |
| `status_led` | the plain heartbeat LED, or `GPIO_NUM_NC` for none |
| `status_led_inverted` | `true` when the LED is wired cathode-to-pin (level 0 lights it) |
| `rgb` | the colour indicator, a `board_rgb_t` (below) |
| `net_transport` | `BOARD_NET_WIFI` or `BOARD_NET_ETH` |

### `board_rgb_t`

| Field | Meaning |
| --- | --- |
| `present` | the board has an addressable RGB LED |
| `data` | the WS2812 data pin |
| `power_gate` | the pin that switches the LED's supply, or `GPIO_NUM_NC` when it is wired to the rail |
| `count` | number of pixels |
| `color_order` | the byte order the chip expects on the wire, one of the six `BOARD_RGB_ORDER_*` permutations |
| `power_gate_settle_ms` | delay after raising the gate before the first frame; 0 means no wait |

`board_rgb_positions(order, &r, &g, &b)` maps a logical colour onto wire positions for a given
order (for GRB: red at position 1, green at 0, blue at 2). It is pure and host-tested in
`test/test_board_rgb_order`; the LED driver uses it to build the `led_strip` component's colour
format. An order outside the enumeration answers the GRB positions.

The colour order is a **board property**: it is a field of the descriptor, and settling it on a
bench is a one-line change in the board file. `board_rgb_order_t` lists **all six** permutations
(`GRB`, `RGB`, `RBG`, `GBR`, `BRG`, `BGR`), not the three common ones. One observation of pure red
only identifies which wire byte the chip reads as red: with the packer set to GRB, a chip whose
true order is BRG also lights red and the green/blue swap goes unseen. Two observations (pure red
and pure green) pin the order, and the answer can be any of the six. Each name is the order **the
chip expects**, first byte first, not the permutation applied to it; the two readings are inverses
of each other. The permutation lives in this pure function, not in the LED driver, because a copy
of it in a diagnostic table once drifted in three of six rows with every test green;
`test/test_board_rgb_order` now checks the table against the function, and
`board_rgb_order.c` static-asserts its table against `BOARD_RGB_ORDER_COUNT`.

## The NanoC6 descriptor

| Signal | Value | Notes |
| --- | --- | --- |
| `name` | `m5stack-nanoc6` | |
| CAN TX | GPIO2 | |
| CAN RX | GPIO1 | |
| CAN bit rate | 50 000 | |
| Button | GPIO9, inverted (active low) | also the ESP32-C6 download strap |
| Status LED | GPIO7, active high (`status_led_inverted = false`) | the descriptor's comment records it as a blue LED, anode on the pin through 22 Ω to ground |
| RGB data | GPIO20 | one WS2812, `count = 1`; the descriptor's comment records the chip's DO pin as unconnected on the schematic |
| RGB power gate | GPIO19 | |
| RGB colour order | `BOARD_RGB_ORDER_GRB` | **not verified on hardware** (marked so in the descriptor); the vendor does not document the colour order — GRB is the common WS2812 order, and the descriptor records it as the one observed to work on this board |
| Power-gate settle | 10 ms | **not verified on hardware** (marked so in the descriptor); an estimate with no documentary source |
| Network transport | `BOARD_NET_WIFI` | |

::: warning GPIO9 is a boot strap
The button sits on the pin the ROM samples at reset. Holding it while power is applied, or while
the chip restarts, puts the chip into the serial bootloader and the firmware never runs. See
[Button](/device/button) for how the gesture and the reset path avoid it.
:::

The two RGB values marked "not verified" are deliberately spelled out in the descriptor rather
than left to the zero initialiser, so that the bench measurement that settles each one is a
one-line edit in this file.

## Who reads the descriptor

| Consumer | Fields it reads |
| --- | --- |
| `src/main.cpp` (`app_main`) | `name`, `can_tx`, `can_rx`, `can_bitrate`, `button`, `button_is_download_strap` (one boot log line) |
| `comfoair_can` (`comfoair_can_start(board, ...)`) | `can_tx`, `can_rx`, `can_bitrate` |
| `comfoair_led` (`comfoair_led_init(board)`) | `status_led`, `status_led_inverted`, `rgb.*`, `name` (log) |
| `comfoair_button` (`comfoair_button_start(board, ...)`) | `button`, `button_inverted`, `button_is_download_strap` |
| `comfoair_net` (`comfoair_net_start(board, ...)`) | `net_transport`, `name` (log) |
| `src/app_mdns.c` (`app_mdns_start(board, ...)`) | `name` |
| `comfoair_http_system.c` (calls `board_get()` itself) | `name` |

Every consumer except `comfoair_http_system.c` takes the board as a parameter rather than calling
`board_get()` itself. None of them names a board or a pin.

## Adding a board

This is what the boundary is for, so it is spelled out.

### What a new board requires

1. A new descriptor file in `components/board/`, `board_<name>.c`, defining its own `board_t` and
   `board_get()`.
2. A selection between descriptors. **None exists today**: `components/board/CMakeLists.txt` lists
   `board_m5stack_nanoc6.c` unconditionally, and the "Kconfig entry" the header comment in
   `board.h` mentions has not been written. A second board needs either a Kconfig choice or a
   per-environment source selection in that `CMakeLists.txt`.
3. A PlatformIO environment in `platformio.ini` with the right `board =` (flash size matters: the
   NanoC6 uses `esp32-c6-devkitm-1` because it is the 4 MB part) and a matching `sdkconfig`.
4. A partition table check. `partitions.csv` assumes 4 MB; see
   [OTA and system](/device/ota-and-system#partition-layout).
5. One binary per board. The image header carries the chip id and the OTA upload refuses an image
   built for another chip, so boards on different chips cannot share a `.bin`.

### What degrades by itself

| Board property | Effect |
| --- | --- |
| No button (`button = GPIO_NUM_NC`) | `comfoair_button_start()` returns `ESP_ERR_NOT_SUPPORTED`, logged; no setup-window press and no factory reset by button |
| Button with the opposite polarity | set `button_inverted = false`; the pull is chosen from it |
| No mono LED (`status_led = GPIO_NUM_NC`) | the sign-of-life flash is mixed into every colour look instead (`comfoair_led_resolve()` with `has_mono = false`) |
| No RGB LED (`rgb.present = false`) | the colour channel is blanked; only the heartbeat runs. Carrying the eleven looks on a mono LED alone is not designed |
| No indicators at all | `comfoair_led_init()` returns `ESP_OK` and starts no task |
| No RGB power gate | the driver skips the gate; a ceiling of 0 would blank the pixel but could not cut its supply. Nothing calls `comfoair_led_policy_set()` today, so the ceiling never reaches 0 (see [Indication](/device/indication#brightness)) |
| Different colour order | one `BOARD_RGB_ORDER_*` value; all six permutations exist |

### What would need real work

- **Ethernet.** `board_net_transport_t` already names `BOARD_NET_ETH`, but `comfoair_net_start()`
  refuses any transport other than Wi-Fi with `ESP_ERR_NOT_SUPPORTED`. On such a board the
  network branch of `app_main` fails and the device reports the `fault` phase. An Ethernet
  transport has to be added inside `comfoair_net`.

### What a new board does not touch

The protocol and state layers (`comfoair_protocol`, `comfoair_state`, `comfoair_registry`), the
command path, MQTT, HTTP, the gesture decider (`comfoair_button.c`), the indication resolver
(`comfoair_led_pattern.c`, `comfoair_led_map.c`), the phase derivation, provisioning and the
configuration store. None of them includes a pin number, and the host test suites that cover them
do not change.
