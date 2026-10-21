// The board boundary (AGENTS.md criterion 5).
//
// Nothing outside this component names a GPIO number. v1 ships one board, but
// three more are waiting -- including the Olimex ESP32-EVB, which is Ethernet,
// which is why the network transport is named here rather than assumed to be
// Wi-Fi. Adding a board is a header plus a Kconfig entry; it must never be a
// grep for pin numbers through the tree.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "board_rgb_order.h"
#include "driver/gpio.h"

#ifdef __cplusplus
// main.cpp is C++ (the protocol headers are), these components are C. Without this
// the linker looks for mangled names and fails with "undefined reference".
extern "C" {
#endif

typedef enum {
    BOARD_NET_WIFI,
    BOARD_NET_ETH,
} board_net_transport_t;

typedef struct {
    bool       present;
    gpio_num_t data;
    gpio_num_t power_gate;   // GPIO_NUM_NC when the LED is wired straight to rail
    uint8_t    count;

    // The two RGB numbers NOTHING IN THE REPOSITORY DOCUMENTS: byte order and gate settling time.
    // They live here, and only here, so that the bench run which finally measures them (hardware
    // scenarios HV-LED-01 and HV-LED-02, docs/hardware-verification/07-indication-and-button.md)
    // is a one-line edit in one board file -- not a hunt through a driver.
    board_rgb_order_t color_order;
    // How long the gate needs before the first frame may be sent. 10 ms has no documentary
    // source: the vendor only says to drive the gate pin high, with no timing figure
    // (docs/reference/esp32-node-hardware.md §12). Zero is legitimate and means "no wait", which
    // is what a board with no gate wants.
    uint16_t power_gate_settle_ms;
} board_rgb_t;

typedef struct {
    const char *name;

    // CAN. The ComfoAir Q bus runs at 50 kbps; see docs/reference/comfoair-pdo-rmi.md §0
    // for why that is not negotiable.
    gpio_num_t  can_tx;
    gpio_num_t  can_rx;
    uint32_t    can_bitrate;

    // Reset gesture. On the NanoC6 this pin is ALSO the download strap: holding
    // it while power is applied drops the chip into the bootloader. That is why
    // the gesture is a hold AFTER boot, never a hold through power-on.
    gpio_num_t  button;
    bool        button_inverted;
    bool        button_is_download_strap;

    // A plain LED that carries the heartbeat and nothing else: "the firmware is running".
    // GPIO_NUM_NC IS THE "THERE IS NONE" SENTINEL HERE TOO, exactly as it is for power_gate above;
    // that was said of the gate alone until 2026-09-06, and the AtomS3 -- whose only free LED sits
    // on the boot comparator net (docs/reference/esp32-node-hardware.md §3, "Full pin map") -- is
    // the board that needs it said.
    gpio_num_t  status_led;
    // True when the LED is wired cathode-to-pin, so a level of 0 is what lights it. The button
    // already treats polarity as a property of the board (button_inverted above); without the
    // matching field the driver would hard-code "1 means lit" and the first board built the other
    // way round would blink its heartbeat inverted -- lit for 900 ms of every second, which reads
    // as a fault rather than as life. The NanoC6 is active HIGH and documented as such (anode on
    // G7, 22 R to GND: docs/reference/esp32-node-hardware.md §4, "On-board peripherals").
    bool        status_led_inverted;
    board_rgb_t rgb;

    board_net_transport_t net_transport;
} board_t;

const board_t *board_get(void);

#ifdef __cplusplus
}
#endif
