// M5Stack NanoC6 (ESP32-C6, 4 MB).
//
// Every value here is taken from the vendor documentation, and
// docs/reference/esp32-node-hardware.md cites the page for each. Do not "correct" a
// number against the vendor product page without reading that file first -- it
// records where the page and the hardware disagree.
#include "board.h"

static const board_t k_nanoc6 = {
    .name        = "m5stack-nanoc6",

    .can_tx      = GPIO_NUM_2,
    .can_rx      = GPIO_NUM_1,
    .can_bitrate = 50000,

    .button                   = GPIO_NUM_9,
    .button_inverted          = true,
    .button_is_download_strap = true,

    // Blue LED1, anode on G7 with a 22 R resistor to ground, so a HIGH lights it
    // (docs/reference/esp32-node-hardware.md §4, "On-board peripherals"; p. 6 and p. 5 artwork).
    .status_led          = GPIO_NUM_7,
    .status_led_inverted = false,

    // One pixel, and it can only ever be one: the schematic draws the WS2812's
    // DO pin not connected, so a chain is physically impossible.
    .rgb = {
        .present    = true,
        .data       = GPIO_NUM_20,
        .power_gate = GPIO_NUM_19,
        .count      = 1,

        // BOTH OF THESE ARE GUESSES AND STAY GUESSES UNTIL THE BENCH SAYS OTHERWISE. They are
        // written here, spelled out rather than left to the zero initialiser, so that the two
        // hardware scenarios that settle them (HV-LED-01 and HV-LED-02,
        // docs/hardware-verification/07-indication-and-button.md) each land on one line of one
        // file. The date of the run goes in a comment beside the value that survives it -- that
        // is what closes docs/reference/esp32-node-hardware.md §12, which records both as
        // undocumented, rather than merely restating a guess in a second place.
        //
        // NOT VERIFIED ON HARDWARE as of 2026-09-06. The vendor does not document the colour
        // order; GRB is the common WS2812 order and the one observed to work on this board.
        .color_order = BOARD_RGB_ORDER_GRB,
        // NOT VERIFIED ON HARDWARE as of 2026-09-06. 10 ms has no documentary source at all: the
        // vendor only says to drive G19 high (NanoC6 p. 6). It has been seen working on this board.
        .power_gate_settle_ms = 10,
    },

    .net_transport = BOARD_NET_WIFI,
};

const board_t *board_get(void) { return &k_nanoc6; }
