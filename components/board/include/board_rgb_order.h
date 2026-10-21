// The order in which one WS2812 expects its three bytes on the wire.
//
// A HYPOTHESIS, NOT A FACT, and it is a separate header so that the fact -- when the bench run
// finally produces it -- costs one line in one board file. The colour order is documented for
// neither board this project targets: docs/reference/esp32-node-hardware.md §12 ("WS2812 colour
// order (RGB vs GRB): never stated on either board"). GRB is the common WS2812 order and the one
// observed to work on the NanoC6, which is plausible evidence, not a datasheet fact and not yet a
// bench result for this driver.
//
// ITS OWN HEADER AND NOT A FIELD OF board.h's TYPE ALONE: board.h includes driver/gpio.h, so
// anything that only needs to NAME an order (the pure LED resolver, its host suite) would drag
// esp_driver_gpio into its own REQUIRES. Nothing here includes anything at all.
//
// ALL SIX PERMUTATIONS, not the three the bench procedure first listed. One observation of a
// pure logical red identifies the position that carries R and NOTHING ELSE: with the packer set to
// GRB it puts 255 in wire byte 1, so a chip whose true order is BRG lights red as well and the
// green/blue swap goes unseen. Two observations -- pure red and pure green -- pin the permutation,
// and the answer to the second one can be any of the six. Enumerating only three would make the
// promise "one constant, one file" false for exactly the outcomes nobody predicted.
//
// EACH NAME IS THE ORDER THE CHIP EXPECTS, first byte first -- not the permutation applied to it.
// The two readings are inverses of each other and swapping them silently is a wrong picture that
// looks deliberate; the driver's translation to the library's format is written from this reading
// (comfoair_led_driver.c) and the table of hardware scenario HV-LED-01
// (docs/hardware-verification/07-indication-and-button.md) is written from it too.
#pragma once

#include <stdint.h>

typedef enum {
    BOARD_RGB_ORDER_GRB,
    BOARD_RGB_ORDER_RGB,
    BOARD_RGB_ORDER_RBG,
    BOARD_RGB_ORDER_GBR,
    BOARD_RGB_ORDER_BRG,
    BOARD_RGB_ORDER_BGR,
    // Not an order. It exists so a seventh cannot be added in silence: board_rgb_order.c
    // static-asserts its table against this count, the same guard comfoair_led.h:88-93 puts on the
    // gesture tiers.
    BOARD_RGB_ORDER_COUNT,
} board_rgb_order_t;

#ifdef __cplusplus
extern "C" {
#endif

// Where each LOGICAL channel sits on the wire under `order`: for BOARD_RGB_ORDER_GRB the wire is
// green, red, blue, so *r_pos is 1, *g_pos is 0 and *b_pos is 2. Any of the three may be NULL.
//
// PURE AND HOST-TESTABLE, AND THAT IS THE WHOLE REASON IT IS NOT SPELLED OUT INSIDE THE DRIVER.
// It lived there for one review cycle as a switch nothing could reach from a laptop, and the copy
// of it that hardware scenario HV-LED-01 carries as a diagnostic table drifted in three of its
// six rows with every test still green -- the mutation "make RBG a copy of RGB" compiled, satisfied the
// led_strip library and passed all 652 cases. test/test_board_rgb_order now checks the table and
// this function against each other, so neither can move alone.
//
// An `order` outside the enumeration answers BOARD_RGB_ORDER_GRB -- the board file's own default
// hypothesis -- rather than leaving the caller with an uninitialised position.
void board_rgb_positions(board_rgb_order_t order, uint8_t *r_pos, uint8_t *g_pos, uint8_t *b_pos);

#ifdef __cplusplus
}
#endif
