// PDO frame decoding.
//
// Vectors come from docs/reference/comfoair-pdo-rmi.md, never from the implementation -- a test
// written from the code it tests proves only that the code equals itself.
#include <unity.h>

#include "pdo_decode.h"

using namespace comfoair;

// A ComfoNet PDO broadcast has bits 27..28 clear, bits 6..13 == 0x01, bits 0..5 the sending
// node id, bits 14..26 the PDO id.
static uint32_t make_pdo_can_id(uint16_t pdo_id, uint8_t node_id) {
  return (static_cast<uint32_t>(pdo_id) << 14) | 0x40u | (node_id & 0x3fu);
}

void test_recognises_a_pdo_broadcast_id(void) {
  TEST_ASSERT_TRUE(is_pdo_can_id(make_pdo_can_id(49, 1)));
  TEST_ASSERT_TRUE(is_pdo_can_id(make_pdo_can_id(1023, 63)));
}

void test_rejects_an_rmi_id(void) {
  // An RMI frame carries 0x1f in bits 24..28, which a PDO never does.
  TEST_ASSERT_FALSE(is_pdo_can_id(0x1f000001u));
}

void test_extracts_pdo_and_node_id(void) {
  const uint32_t id = make_pdo_can_id(276, 42);
  TEST_ASSERT_EQUAL_UINT16(276, pdo_id_from_can_id(id));
  TEST_ASSERT_EQUAL_UINT8(42, node_id_from_can_id(id));
}

void test_request_id_round_trips(void) {
  const uint32_t id = pdo_request_can_id(81, 3);
  TEST_ASSERT_TRUE(is_pdo_can_id(id));
  TEST_ASSERT_EQUAL_UINT16(81, pdo_id_from_can_id(id));
  TEST_ASSERT_EQUAL_UINT8(3, node_id_from_can_id(id));
}

void test_decodes_little_endian_unsigned(void) {
  // 0x0e10 = 3600, the "100e0000" duration the command table documents.
  const uint8_t data[] = {0x10, 0x0e};
  int64_t out = 0;
  TEST_ASSERT_EQUAL(DecodeResult::OK, decode_numeric(data, sizeof data, false, false, out));
  TEST_ASSERT_EQUAL_INT64(3600, out);
}

void test_sign_extends_a_signed_field(void) {
  // PDO 49/54/55/56 use all-ones as a legitimate -1 (auto/balanced).
  const uint8_t data[] = {0xff};
  int64_t out = 0;
  TEST_ASSERT_EQUAL(DecodeResult::OK, decode_numeric(data, sizeof data, true, true, out));
  TEST_ASSERT_EQUAL_INT64(-1, out);
}

void test_all_ones_is_no_value_only_when_unsigned(void) {
  // The countdown rows flagged none_when_all_ones send all-ones to mean "nothing pending".
  const uint8_t data[] = {0xff, 0xff};
  int64_t out = 12345;
  TEST_ASSERT_EQUAL(DecodeResult::NO_VALUE, decode_numeric(data, sizeof data, false, true, out));
  // Left untouched, so callers must initialise it -- pinned because it is easy to "improve".
  TEST_ASSERT_EQUAL_INT64(12345, out);
}

void test_rejects_a_length_the_wire_format_never_uses(void) {
  const uint8_t data[] = {1, 2, 3};
  int64_t out = 0;
  TEST_ASSERT_EQUAL(DecodeResult::UNSUPPORTED_LENGTH,
                    decode_numeric(data, sizeof data, false, false, out));
}

void test_decode_u64_accepts_seven_bytes(void) {
  // PDO 230's constraint bitset uses bit 54, which lives in byte 6, so a 7-byte frame
  // must decode -- decode_numeric's 1/2/4/8 rule would reject it.
  const uint8_t data[] = {0, 0, 0, 0, 0, 0, 0x40};
  uint64_t out = 0;
  TEST_ASSERT_TRUE(decode_u64(data, sizeof data, out));
  TEST_ASSERT_TRUE((out & (UINT64_C(1) << 54)) != 0);
}

void test_recognises_pdo_ids_above_1023(void) {
  // CO2 sensor 1 reports PDO 1024 and a ComfoClime 4145..4208 (aiocomfoconnect
  // docs/PROTOCOL-PDO.md). Under the old 0x1f003fc0 mask both were dropped as "unclaimed".
  TEST_ASSERT_TRUE(is_pdo_can_id(make_pdo_can_id(1024, 5)));
  TEST_ASSERT_TRUE(is_pdo_can_id(make_pdo_can_id(4145, 9)));
  TEST_ASSERT_TRUE(is_pdo_can_id(make_pdo_can_id(8191, 63)));
  TEST_ASSERT_EQUAL_UINT16(4145, pdo_id_from_can_id(make_pdo_can_id(4145, 9)));
  TEST_ASSERT_EQUAL_UINT8(9, node_id_from_can_id(make_pdo_can_id(4145, 9)));
}

void test_rejects_a_heartbeat_id(void) {
  // A node heartbeat is 0x10000000 | node: bit 28 set, which no PDO has.
  TEST_ASSERT_FALSE(is_pdo_can_id(0x10000000u | 0x01u));
  TEST_ASSERT_FALSE(is_pdo_can_id(0x10000000u | 0x2au));
}

void test_rejects_rmi_ids_whatever_their_low_bits(void) {
  // RMI frames carry 0x1f in bits 24..28 (rmi_response.h), so bits 27/28 are set.
  TEST_ASSERT_FALSE(is_pdo_can_id(0x1f000001u));
  TEST_ASSERT_FALSE(is_pdo_can_id(0x1f000040u | 0x01u));
  TEST_ASSERT_FALSE(is_pdo_can_id(0x1f800000u | (1u << 6) | 0x2au));
}

void test_a_request_id_above_1023_round_trips(void) {
  const uint32_t id = pdo_request_can_id(4201, 0x2a);
  TEST_ASSERT_TRUE(is_pdo_can_id(id));
  TEST_ASSERT_EQUAL_UINT16(4201, pdo_id_from_can_id(id));
}

void test_rejects_bit_27_alone(void) {
  TEST_ASSERT_FALSE(is_pdo_can_id(0x08000040u));
}

void test_pdo_id_extraction_masks_to_13_bits(void) {
  // Bit 27 must not leak into the PDO number for a caller that skipped is_pdo_can_id().
  TEST_ASSERT_EQUAL_UINT16(0x0001, pdo_id_from_can_id(0x08000000u | (1u << 14) | 0x40u));
}

void test_literal_reference_vector_pdo_1024_node_5(void) {
  // Not built through make_pdo_can_id: 1024 << 14 = 0x01000000, | 0x40 | node 5.
  TEST_ASSERT_TRUE(is_pdo_can_id(0x01000045u));
  TEST_ASSERT_EQUAL_UINT16(1024, pdo_id_from_can_id(0x01000045u));
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_recognises_a_pdo_broadcast_id);
  RUN_TEST(test_rejects_an_rmi_id);
  RUN_TEST(test_extracts_pdo_and_node_id);
  RUN_TEST(test_request_id_round_trips);
  RUN_TEST(test_decodes_little_endian_unsigned);
  RUN_TEST(test_sign_extends_a_signed_field);
  RUN_TEST(test_all_ones_is_no_value_only_when_unsigned);
  RUN_TEST(test_rejects_a_length_the_wire_format_never_uses);
  RUN_TEST(test_decode_u64_accepts_seven_bytes);
  RUN_TEST(test_recognises_pdo_ids_above_1023);
  RUN_TEST(test_rejects_a_heartbeat_id);
  RUN_TEST(test_rejects_rmi_ids_whatever_their_low_bits);
  RUN_TEST(test_a_request_id_above_1023_round_trips);
  RUN_TEST(test_rejects_bit_27_alone);
  RUN_TEST(test_pdo_id_extraction_masks_to_13_bits);
  RUN_TEST(test_literal_reference_vector_pdo_1024_node_5);
  return UNITY_END();
}
