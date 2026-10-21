// The permutation, and nothing else. No IDF, no pin, no board -- which is what lets
// test/test_board_rgb_order compile it on a laptop and check it against the diagnostic table of
// hardware scenario HV-LED-01 (docs/hardware-verification/07-indication-and-button.md).
#include "board_rgb_order.h"

// EACH ROW IS THE ENUMERATOR'S OWN NAME READ LEFT TO RIGHT, and the row holds the positions of the
// LOGICAL channels in that sequence. BOARD_RGB_ORDER_GRB means the chip wants green, red, blue, so
// green is at 0 and the logical red's position is 1: the row is {1, 0, 2}, r first. Reading the row
// as "the wire order" instead of "where my channels go" gives the inverse permutation, which is
// right for the three orders that are their own inverse and wrong for the two that are not --
// exactly the mistake that once put three wrong rows in that diagnostic table.
static const uint8_t k_positions[BOARD_RGB_ORDER_COUNT][3] = {
    {1, 0, 2},  // GRB: green, red, blue
    {0, 1, 2},  // RGB
    {0, 2, 1},  // RBG
    {2, 0, 1},  // GBR
    {1, 2, 0},  // BRG
    {2, 1, 0},  // BGR
};
_Static_assert(sizeof(k_positions) / sizeof(k_positions[0]) == BOARD_RGB_ORDER_COUNT,
               "an order was added to the enumeration and not to the table");

void board_rgb_positions(board_rgb_order_t order, uint8_t *r_pos, uint8_t *g_pos, uint8_t *b_pos) {
    unsigned i = (unsigned)order;
    if (i >= (unsigned)BOARD_RGB_ORDER_COUNT) i = (unsigned)BOARD_RGB_ORDER_GRB;
    if (r_pos) *r_pos = k_positions[i][0];
    if (g_pos) *g_pos = k_positions[i][1];
    if (b_pos) *b_pos = k_positions[i][2];
}
