// RMI read requests and the classification of their responses (rmi_read.h).
//
// The CAN ids below are documented LAN C captures (the active-error read is the worked example in
// docs/reference/comfoair-pdo-rmi.md §0 and §3). The safety sweep encodes §0 "Commands that can
// brick the unit": any command >= 0x80 on NODE (0x01) can brick the unit, so the read builders
// must be provably unable to emit one.
#include <unity.h>

#include <vector>

#include "rmi_read.h"

using namespace comfoair::rmi;

static void assert_frame(const char *what, const Frame &got, const std::vector<uint8_t> &want) {
  TEST_ASSERT_EQUAL_size_t_MESSAGE(want.size(), got.size(), what);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(want.data(), got.data(), want.size(), what);
}

// --- the builders -------------------------------------------------------------------------

void test_get_one_matches_the_documented_example(void) {
  // comfoair-pdo-rmi.md §3 -- `01 1D 01 10 0A`, TEMPHUMCONTROL warm setpoint, actual value.
  assert_frame("get one", get_one(0x1D, 0x01, READ_TYPE_VALUE, 0x0A), {0x01, 0x1D, 0x01, 0x10, 0x0A});
}

void test_get_one_accepts_range_and_step(void) {
  assert_frame("range", get_one(0x1E, 0x01, READ_TYPE_RANGE, 0x12), {0x01, 0x1E, 0x01, 0x20, 0x12});
  assert_frame("step", get_one(0x1E, 0x01, READ_TYPE_STEP, 0x12), {0x01, 0x1E, 0x01, 0x40, 0x12});
}

void test_get_one_refuses_none_and_or_ed_types(void) {
  // 0x00 asks for nothing; OR'ed types (comfoair-pdo-rmi.md §3) return a concatenation the
  // probe cannot split, so the builder refuses them.
  TEST_ASSERT_TRUE(get_one(0x1D, 0x01, 0x00, 0x0A).empty());
  TEST_ASSERT_TRUE(get_one(0x1D, 0x01, 0x30, 0x0A).empty());
  TEST_ASSERT_TRUE(get_one(0x1D, 0x01, 0x70, 0x0A).empty());
  TEST_ASSERT_TRUE(get_one(0x1D, 0x01, 0x11, 0x0A).empty());
}

void test_the_safety_sweep_get_one_never_emits_a_command_byte_ge_0x80(void) {
  // Every unit x every type x every property, at four subunits including both edges. A single
  // stray first byte >= 0x80 on unit 0x01 is the bricking frame of comfoair-pdo-rmi.md §0.
  const uint8_t subunits[] = {0x00, 0x01, 0x02, 0xFF};
  unsigned built = 0;
  for (uint8_t subunit : subunits) {
    for (unsigned unit = 0; unit < 256; unit++) {
      for (unsigned type = 0; type < 256; type++) {
        const bool valid =
            type == READ_TYPE_VALUE || type == READ_TYPE_RANGE || type == READ_TYPE_STEP;
        for (unsigned prop = 0; prop < 256; prop++) {
          const Frame f = get_one(static_cast<uint8_t>(unit), subunit, static_cast<uint8_t>(type),
                                  static_cast<uint8_t>(prop));
          if (f.empty()) {
            if (valid)
              TEST_FAIL_MESSAGE("a valid read type produced an empty frame");
            continue;
          }
          if (!valid || f.size() != 5 || f[0] != CMD_GET_ONE || f[1] != unit || f[2] != subunit ||
              f[3] != type || f[4] != prop)
            TEST_FAIL_MESSAGE("get_one built something other than 01 unit subunit type property");
          if (f[0] >= 0x80)
            TEST_FAIL_MESSAGE("get_one emitted a command byte >= 0x80");
          built++;
        }
      }
    }
  }
  TEST_ASSERT_EQUAL_UINT(4u * 256u * 3u * 256u, built);
}

void test_get_active_errors_is_pinned_by_number(void) {
  // comfoair-pdo-rmi.md §3 -- `80 03 01`. The unit byte is checked against the literal 0x03, NOT
  // against UNIT_ERROR: a typo in the constant must fail here rather than pass by agreement.
  assert_frame("active errors", get_active_errors(), {0x80, 0x03, 0x01});
  TEST_ASSERT_EQUAL_UINT8(0x03, UNIT_ERROR);
  TEST_ASSERT_EQUAL_UINT8(0x80, CMD_GET_ACTIVE_ERRORS);
  TEST_ASSERT_EQUAL_UINT8(0x01, CMD_GET_ONE);
}

// --- the request id -----------------------------------------------------------------------

void test_request_id_matches_the_single_frame_capture(void) {
  // comfoair-pdo-rmi.md §0 -- 1F011074 = 11111 0000 0001 0001 00 0001 110100:
  // seq 0, request, counter 1, dst 0x01, src 0x34.
  TEST_ASSERT_EQUAL_HEX32(0x1F011074u, read_request_can_id(0x34, 0x01, 1, get_active_errors(), 0));
  TEST_ASSERT_EQUAL_HEX32(0x1F011074u,
                          read_request_can_id(0x34, 0x01, 1, get_one(0x1D, 0x01, 0x10, 0x0A), 0));
}

void test_request_id_carries_the_sequence(void) {
  // A documented capture, 1F071074: the same request with SeqNr = 3.
  TEST_ASSERT_EQUAL_HEX32(0x1F071074u, read_request_can_id(0x34, 0x01, 1, get_active_errors(), 3));
}

void test_request_id_is_never_multi_frame_or_error_for_a_read(void) {
  const uint32_t id = read_request_can_id(0x34, 0x01, 0, get_one(0x01, 0x01, 0x10, 0x04), 2);
  TEST_ASSERT_EQUAL_UINT32(0, id & RMI_MULTI_FRAME_BIT);
  TEST_ASSERT_EQUAL_UINT32(0, id & RMI_ERROR_BIT);
  TEST_ASSERT_NOT_EQUAL(0, id & RMI_IS_REQUEST_BIT);
  TEST_ASSERT_TRUE(is_rmi_can_id(id));
}

// --- matching a response ------------------------------------------------------------------

void test_the_captured_responses_match_their_request(void) {
  // The documented captures: the unit (src 0x01) answers the LAN C (dst 0x34), seq 0 -- multi-frame,
  // single-frame and error forms alike.
  TEST_ASSERT_TRUE(response_matches(0x1F005D01u, 0x01, 0x34, 0));
  TEST_ASSERT_TRUE(response_matches(0x1F001D01u, 0x01, 0x34, 0));
  TEST_ASSERT_TRUE(response_matches(0x1F009D01u, 0x01, 0x34, 0));
}

void test_a_response_from_the_wrong_party_or_sequence_does_not_match(void) {
  TEST_ASSERT_FALSE(response_matches(0x1F001D01u, 0x02, 0x34, 0));  // wrong src
  TEST_ASSERT_FALSE(response_matches(0x1F001D01u, 0x01, 0x35, 0));  // wrong dst
  TEST_ASSERT_FALSE(response_matches(0x1F001D01u, 0x01, 0x34, 1));  // wrong seq
  TEST_ASSERT_FALSE(response_matches(0x1F001D01u | (3u << 17), 0x01, 0x34, 0));
  TEST_ASSERT_TRUE(response_matches(0x1F001D01u | (3u << 17), 0x01, 0x34, 3));
}

void test_a_request_or_a_non_rmi_id_does_not_match(void) {
  TEST_ASSERT_FALSE(response_matches(0x1F001D01u | RMI_IS_REQUEST_BIT, 0x01, 0x34, 0));
  // Our own request echoed back, read from the other side, is still a request.
  TEST_ASSERT_FALSE(response_matches(0x1F011074u, 0x34, 0x01, 0));
  // comfoair-pdo-rmi.md §3 -- the firmware-upload id; src 0, dst 0, seq 0 would otherwise all "match".
  TEST_ASSERT_FALSE(response_matches(0x1F800000u, 0x00, 0x00, 0));
}

// --- classifying a response ---------------------------------------------------------------

void test_a_response_without_the_error_bit_is_a_value(void) {
  // comfoair-pdo-rmi.md §3 -- `E6 00` to the warm-setpoint read.
  const uint8_t value[] = {0xE6, 0x00};
  const ReadAnswer a = classify_response(0x1F001D01u, value, sizeof value);
  TEST_ASSERT_FALSE(a.is_error);
  TEST_ASSERT_EQUAL_UINT8(0, a.error);
}

void test_an_error_response_carries_its_first_byte_as_the_code(void) {
  const uint8_t unknown_property[] = {14};
  const ReadAnswer a = classify_response(0x1F009D01u, unknown_property, 1);
  TEST_ASSERT_TRUE(a.is_error);
  TEST_ASSERT_EQUAL_UINT8(14, a.error);

  const uint8_t longer[] = {32, 0x99, 0x77};
  const ReadAnswer b = classify_response(0x1F009D01u, longer, sizeof longer);
  TEST_ASSERT_TRUE(b.is_error);
  TEST_ASSERT_EQUAL_UINT8(32, b.error);
}

void test_an_empty_error_response_is_an_error_with_code_zero(void) {
  const ReadAnswer a = classify_response(0x1F009D01u, nullptr, 0);
  TEST_ASSERT_TRUE(a.is_error);
  TEST_ASSERT_EQUAL_UINT8(0, a.error);
  const uint8_t unused[] = {14};
  const ReadAnswer b = classify_response(0x1F009D01u, unused, 0);
  TEST_ASSERT_TRUE(b.is_error);
  TEST_ASSERT_EQUAL_UINT8(0, b.error);
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_get_one_matches_the_documented_example);
  RUN_TEST(test_get_one_accepts_range_and_step);
  RUN_TEST(test_get_one_refuses_none_and_or_ed_types);
  RUN_TEST(test_the_safety_sweep_get_one_never_emits_a_command_byte_ge_0x80);
  RUN_TEST(test_get_active_errors_is_pinned_by_number);
  RUN_TEST(test_request_id_matches_the_single_frame_capture);
  RUN_TEST(test_request_id_carries_the_sequence);
  RUN_TEST(test_request_id_is_never_multi_frame_or_error_for_a_read);
  RUN_TEST(test_the_captured_responses_match_their_request);
  RUN_TEST(test_a_response_from_the_wrong_party_or_sequence_does_not_match);
  RUN_TEST(test_a_request_or_a_non_rmi_id_does_not_match);
  RUN_TEST(test_a_response_without_the_error_bit_is_a_value);
  RUN_TEST(test_an_error_response_carries_its_first_byte_as_the_code);
  RUN_TEST(test_an_empty_error_response_is_an_error_with_code_zero);
  return UNITY_END();
}
