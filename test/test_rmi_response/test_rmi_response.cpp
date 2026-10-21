// RMI response parsing.
//
// Vectors from docs/reference/comfoair-pdo-rmi.md §3. The GETTIMERENTRY reply below is quoted
// verbatim there.
#include <unity.h>

#include "rmi_response.h"

using namespace comfoair::rmi;

void test_recognises_an_rmi_id(void) {
  TEST_ASSERT_TRUE(is_rmi_can_id(0x1f000001u));
}

void test_a_firmware_upload_id_is_not_rmi(void) {
  // comfoair-pdo-rmi.md §3 -- firmware uploads use 0x1F800000, which passes a bits-24..28-only
  // test. Bits 19..23 must be clear too, and this is the test that says so.
  TEST_ASSERT_FALSE(is_rmi_can_id(0x1f800000u));
}

void test_a_response_is_an_rmi_frame_with_isrequest_clear(void) {
  TEST_ASSERT_TRUE(is_rmi_response(0x1f000001u));
  TEST_ASSERT_FALSE(is_rmi_response(0x1f000001u | (1u << 16)));
}

void test_extracts_source_destination_and_sequence(void) {
  const uint32_t id = 0x1f000000u | (2u << 17) | (5u << 6) | 9u;
  TEST_ASSERT_EQUAL_UINT8(9, rmi_src_node(id));
  TEST_ASSERT_EQUAL_UINT8(5, rmi_dst_node(id));
  TEST_ASSERT_EQUAL_UINT8(2, rmi_sequence(id));
}

// --- reassembly ---------------------------------------------------------------------------

static uint32_t single_frame_id(uint8_t src) { return 0x1f000000u | src; }
static uint32_t multi_frame_id(uint8_t src) { return 0x1f000000u | RMI_MULTI_FRAME_BIT | src; }

void test_a_single_frame_response_completes_immediately(void) {
  ResponseReassembler r;
  const uint8_t data[] = {1, 2, 3};
  TEST_ASSERT_EQUAL(ResponseReassembler::Result::COMPLETE,
                    r.feed(single_frame_id(1), data, sizeof data));
  TEST_ASSERT_EQUAL_size_t(3, r.message().size());
}

void test_an_empty_response_is_a_protocol_error(void) {
  // Indistinguishable from a response that never arrived.
  ResponseReassembler r;
  const uint8_t data[] = {0};
  TEST_ASSERT_EQUAL(ResponseReassembler::Result::PROTOCOL_ERROR,
                    r.feed(single_frame_id(1), data, 0));
}

void test_multi_frame_blocks_are_joined_in_order(void) {
  ResponseReassembler r;
  const uint8_t first[] = {0x00, 1, 2, 3, 4, 5, 6, 7};
  const uint8_t last[] = {0x81, 8, 9};
  TEST_ASSERT_EQUAL(ResponseReassembler::Result::NEED_MORE,
                    r.feed(multi_frame_id(1), first, sizeof first));
  TEST_ASSERT_EQUAL(ResponseReassembler::Result::COMPLETE,
                    r.feed(multi_frame_id(1), last, sizeof last));
  TEST_ASSERT_EQUAL_size_t(9, r.message().size());
  TEST_ASSERT_EQUAL_UINT8(9, r.message().back());
}

void test_an_out_of_order_block_is_rejected(void) {
  ResponseReassembler r;
  const uint8_t first[] = {0x00, 1, 2};
  const uint8_t skipped[] = {0x82, 3, 4};
  TEST_ASSERT_EQUAL(ResponseReassembler::Result::NEED_MORE,
                    r.feed(multi_frame_id(1), first, sizeof first));
  TEST_ASSERT_EQUAL(ResponseReassembler::Result::PROTOCOL_ERROR,
                    r.feed(multi_frame_id(1), skipped, sizeof skipped));
}

void test_another_node_cannot_splice_into_a_message_in_progress(void) {
  // Two replies interleaved would otherwise be reported as one plausible message.
  ResponseReassembler r;
  const uint8_t first[] = {0x00, 1, 2};
  const uint8_t intruder[] = {0x81, 3, 4};
  TEST_ASSERT_EQUAL(ResponseReassembler::Result::NEED_MORE,
                    r.feed(multi_frame_id(1), first, sizeof first));
  TEST_ASSERT_EQUAL(ResponseReassembler::Result::PROTOCOL_ERROR,
                    r.feed(multi_frame_id(7), intruder, sizeof intruder));
}

void test_a_single_frame_from_another_node_cannot_complete_over_one_in_progress(void) {
  ResponseReassembler r;
  const uint8_t first[] = {0x00, 1, 2};
  const uint8_t other[] = {9, 9};
  TEST_ASSERT_EQUAL(ResponseReassembler::Result::NEED_MORE,
                    r.feed(multi_frame_id(1), first, sizeof first));
  TEST_ASSERT_EQUAL(ResponseReassembler::Result::PROTOCOL_ERROR,
                    r.feed(single_frame_id(7), other, sizeof other));
}

// --- payloads -----------------------------------------------------------------------------

void test_parses_the_documented_gettimerentry_reply(void) {
  // §3: boost active reads "01 00000000 58020000 56020000 03" -- configured 600 s,
  // 598 s remaining. 0x258 = 600, 0x256 = 598.
  const uint8_t reply[] = {0x01, 0, 0, 0, 0, 0x58, 0x02, 0x00, 0x00,
                           0x56, 0x02, 0x00, 0x00, 0x03};
  TimerEntry e{};
  TEST_ASSERT_TRUE(parse_timer_entry(reply, sizeof reply, e));
  TEST_ASSERT_TRUE(e.enabled);
  TEST_ASSERT_EQUAL_INT32(600, e.timeout_seconds);
  TEST_ASSERT_EQUAL_INT32(598, e.remaining_seconds);
  TEST_ASSERT_EQUAL_UINT8(3, e.value);
}

void test_a_timer_reply_of_the_wrong_length_is_refused(void) {
  const uint8_t reply[13] = {0};
  TimerEntry e{};
  TEST_ASSERT_FALSE(parse_timer_entry(reply, sizeof reply, e));
}

void test_indefinite_durations_come_back_as_minus_one(void) {
  const uint8_t reply[] = {0x01, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff,
                           0xff, 0xff, 0xff, 0xff, 0x00};
  TimerEntry e{};
  TEST_ASSERT_TRUE(parse_timer_entry(reply, sizeof reply, e));
  TEST_ASSERT_EQUAL_INT32(-1, e.timeout_seconds);
  TEST_ASSERT_EQUAL_INT32(-1, e.remaining_seconds);
}

void test_active_errors_are_bit_indices_lsb_first(void) {
  // comfoair-pdo-rmi.md §4 #7 -- each byte walked from its least significant bit, the running
  // index IS the error id.
  const uint8_t field[] = {0x05, 0x80};  // bits 0 and 2 in byte 0; bit 7 in byte 1 => id 15
  const auto ids = active_error_ids(field, sizeof field);
  TEST_ASSERT_EQUAL_size_t(3, ids.size());
  TEST_ASSERT_EQUAL_UINT16(0, ids[0]);
  TEST_ASSERT_EQUAL_UINT16(2, ids[1]);
  TEST_ASSERT_EQUAL_UINT16(15, ids[2]);
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_recognises_an_rmi_id);
  RUN_TEST(test_a_firmware_upload_id_is_not_rmi);
  RUN_TEST(test_a_response_is_an_rmi_frame_with_isrequest_clear);
  RUN_TEST(test_extracts_source_destination_and_sequence);
  RUN_TEST(test_a_single_frame_response_completes_immediately);
  RUN_TEST(test_an_empty_response_is_a_protocol_error);
  RUN_TEST(test_multi_frame_blocks_are_joined_in_order);
  RUN_TEST(test_an_out_of_order_block_is_rejected);
  RUN_TEST(test_another_node_cannot_splice_into_a_message_in_progress);
  RUN_TEST(test_a_single_frame_from_another_node_cannot_complete_over_one_in_progress);
  RUN_TEST(test_parses_the_documented_gettimerentry_reply);
  RUN_TEST(test_a_timer_reply_of_the_wrong_length_is_refused);
  RUN_TEST(test_indefinite_durations_come_back_as_minus_one);
  RUN_TEST(test_active_errors_are_bit_indices_lsb_first);
  return UNITY_END();
}
