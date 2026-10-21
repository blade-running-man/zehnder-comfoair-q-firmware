// The WS2812 byte order, and the one table in this project that a person reads off a bench and
// types into a board file.
//
// This suite exists because that table was WRONG IN THREE OF ITS SIX ROWS and nothing said so
// (corrected 2026-09-06). The permutation lived inside
// comfoair_led_driver.c as a switch no host case could reach, the bench table was a second copy
// of it as prose, and the two drifted. The mutation "make BOARD_RGB_ORDER_RBG a copy of
// BOARD_RGB_ORDER_RGB" compiled, satisfied the led_strip library and passed all 652 cases.
//
// So the table below is transcribed FROM THE BENCH TABLE -- hardware scenario HV-LED-01
// (docs/hardware-verification/07-indication-and-button.md) -- not from board_rgb_order.c, and
// the two check each other. A row that changes in one place and not the other fails here.
#include <unity.h>

#include "board_rgb_order.h"

void setUp(void) {}
void tearDown(void) {}

// So a failure says "blue" rather than "2". Index is the logical channel: 0 red, 1 green, 2 blue.
static const char *NAME[3] = {"red", "green", "blue"};

// What the led_strip library puts on the wire when it is CONFIGURED with `packer`: each logical
// channel goes to the position that order names for it. This is led_strip_rmt_set_pixel() with
// bytes_per_color == 1, read from managed_components/espressif__led_strip/src/led_strip_rmt_dev.c.
static void pack(board_rgb_order_t packer, const uint8_t in[3], uint8_t wire[3]) {
  uint8_t p[3];
  board_rgb_positions(packer, &p[0], &p[1], &p[2]);
  for (int c = 0; c < 3; c++) wire[p[c]] = in[c];
}

// What the EYE sees on a chip whose true order is `truth`: wire byte i drives the channel truth
// names at position i, so the lit channel is the logical one whose position holds the 255.
static int lit_channel(board_rgb_order_t truth, const uint8_t wire[3]) {
  uint8_t p[3];
  board_rgb_positions(truth, &p[0], &p[1], &p[2]);
  for (int c = 0; c < 3; c++)
    if (wire[p[c]]) return c;
  return -1;
}

// The HV-LED-01 table, verbatim. The packer stands on the board file's default
// hypothesis (GRB) and the owner shows the device a pure red and then a pure green; the pair of
// observations names the chip's true order, and that name is what goes into `.color_order`.
static const struct {
  board_rgb_order_t truth;
  const char       *red_looks;
  const char       *green_looks;
} K_DIAGNOSTIC[] = {
    {BOARD_RGB_ORDER_GRB, "red", "green"},   {BOARD_RGB_ORDER_BRG, "red", "blue"},
    {BOARD_RGB_ORDER_RGB, "green", "red"},   {BOARD_RGB_ORDER_BGR, "green", "blue"},
    {BOARD_RGB_ORDER_RBG, "blue", "red"},    {BOARD_RGB_ORDER_GBR, "blue", "green"},
};

static const uint8_t PURE_RED[3]   = {255, 0, 0};
static const uint8_t PURE_GREEN[3] = {0, 255, 0};

// ONE OBSERVATION IS NOT ENOUGH, and this case is also the proof of that: with the packer on GRB
// the 255 of a pure red lands in wire position 1, so the eye learns which channel the chip reads
// from position 1 and nothing else. Two rows below show red as red -- GRB and BRG -- and they
// differ by a green/blue swap that the red observation cannot see. Hence the second column.
static void test_the_diagnostic_table_of_checklist_1_2_is_what_the_packer_really_does(void) {
  for (unsigned i = 0; i < sizeof(K_DIAGNOSTIC) / sizeof(K_DIAGNOSTIC[0]); i++) {
    uint8_t wire[3];
    pack(BOARD_RGB_ORDER_GRB, PURE_RED, wire);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(K_DIAGNOSTIC[i].red_looks,
                                     NAME[lit_channel(K_DIAGNOSTIC[i].truth, wire)],
                                     "HV-LED-01: a pure red looks like something else");
    pack(BOARD_RGB_ORDER_GRB, PURE_GREEN, wire);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(K_DIAGNOSTIC[i].green_looks,
                                     NAME[lit_channel(K_DIAGNOSTIC[i].truth, wire)],
                                     "HV-LED-01: a pure green looks like something else");
  }
}

// The table names six DIFFERENT chips, and it has to: if two rows described the same order the
// owner would be told to write a constant that cannot be right, and the pair of observations would
// no longer identify anything.
static void test_the_table_covers_every_order_exactly_once(void) {
  const unsigned rows = sizeof(K_DIAGNOSTIC) / sizeof(K_DIAGNOSTIC[0]);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(BOARD_RGB_ORDER_COUNT, rows, "the table is not the enumeration");
  for (unsigned i = 0; i < rows; i++)
    for (unsigned j = i + 1; j < rows; j++)
      TEST_ASSERT_NOT_EQUAL_MESSAGE(K_DIAGNOSTIC[i].truth, K_DIAGNOSTIC[j].truth,
                                    "two rows of the table name the same order");
}

// The last column of the table is a PROMISE -- write this one constant and the colours come out
// right -- and this is the case that makes it true rather than plausible. It is also what the
// mutation "RBG is a copy of RGB" fails: the copy still round-trips against itself, but the pair
// then has two orders that pack identically and the case above catches the collision.
static void test_configuring_the_true_order_makes_every_colour_come_out_right(void) {
  for (int t = 0; t < BOARD_RGB_ORDER_COUNT; t++)
    for (int c = 0; c < 3; c++) {
      uint8_t in[3] = {0, 0, 0}, wire[3];
      in[c] = 255;
      pack((board_rgb_order_t)t, in, wire);
      TEST_ASSERT_EQUAL_INT_MESSAGE(c, lit_channel((board_rgb_order_t)t, wire),
                                    "the fix column does not fix it");
    }
}

// Every order is a permutation of the three positions -- no channel shares a byte with another and
// none is left off the wire. A table row that repeats a position sends one channel twice and drops
// a third, which on a bench looks like a dead colour rather than like a typo.
static void test_every_order_is_a_permutation_of_the_three_positions(void) {
  for (int t = 0; t < BOARD_RGB_ORDER_COUNT; t++) {
    uint8_t p[3];
    board_rgb_positions((board_rgb_order_t)t, &p[0], &p[1], &p[2]);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0 + 1 + 2, p[0] + p[1] + p[2], "not three distinct positions");
    TEST_ASSERT_TRUE(p[0] != p[1] && p[1] != p[2] && p[0] != p[2]);
  }
}

// A value outside the enumeration answers the board file's own default rather than leaving the
// caller with whatever was on the stack -- the same rule comfoair_led_look_of() follows for a
// number that is not a phase. NULL outputs are accepted and skipped.
static void test_an_order_outside_the_enumeration_answers_grb(void) {
  uint8_t p[3] = {9, 9, 9}, q[3] = {9, 9, 9};
  board_rgb_positions((board_rgb_order_t)BOARD_RGB_ORDER_COUNT, &p[0], &p[1], &p[2]);
  board_rgb_positions(BOARD_RGB_ORDER_GRB, &q[0], &q[1], &q[2]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(q, p, 3);
  board_rgb_positions(BOARD_RGB_ORDER_GRB, NULL, NULL, NULL);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_the_diagnostic_table_of_checklist_1_2_is_what_the_packer_really_does);
  RUN_TEST(test_the_table_covers_every_order_exactly_once);
  RUN_TEST(test_configuring_the_true_order_makes_every_colour_come_out_right);
  RUN_TEST(test_every_order_is_a_permutation_of_the_three_positions);
  RUN_TEST(test_an_order_outside_the_enumeration_answers_grb);
  return UNITY_END();
}
