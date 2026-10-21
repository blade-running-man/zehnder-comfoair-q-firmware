// What a stored secret does on the way out and on the way back.
#include <unity.h>

#include <cstring>

#include "comfoair_secrets.h"

void setUp(void) {}
void tearDown(void) {}

// --- which keys are secret ------------------------------------------------------------------

void test_a_key_is_secret_by_the_words_in_it_not_by_a_list(void) {
  // Matched by substring on purpose. The stored document is user-editable and has carried
  // different key sets across versions, so a future "MQTT Password" or "API Token" is
  // redacted without anyone remembering to extend a whitelist. Over-redaction shows a field
  // as unchanged and the user retypes it; under-redaction publishes a credential.
  TEST_ASSERT_TRUE(comfoair_secret_key("mqtt_password"));
  TEST_ASSERT_TRUE(comfoair_secret_key("MQTT Password"));
  TEST_ASSERT_TRUE(comfoair_secret_key("wifi_psk"));
  TEST_ASSERT_TRUE(comfoair_secret_key("api_token"));
  TEST_ASSERT_TRUE(comfoair_secret_key("web_secret"));
  TEST_ASSERT_TRUE(comfoair_secret_key("auth_key"));
}

void test_an_ordinary_key_is_not_secret(void) {
  TEST_ASSERT_FALSE(comfoair_secret_key("mqtt_host"));
  TEST_ASSERT_FALSE(comfoair_secret_key("wifi_ssid"));
  TEST_ASSERT_FALSE(comfoair_secret_key("device_name"));
}

// --- on the way out --------------------------------------------------------------------------

void test_a_set_secret_reads_back_as_the_sentinel(void) {
  TEST_ASSERT_EQUAL_STRING(COMFOAIR_SECRET_SENTINEL, comfoair_secret_redact("hunter2"));
}

void test_an_unset_secret_reads_back_empty_not_as_the_sentinel(void) {
  // So the page can still show "nothing is set here", and so clearing an unset field stays
  // a no-op rather than looking like a change.
  TEST_ASSERT_EQUAL_STRING("", comfoair_secret_redact(""));
  TEST_ASSERT_EQUAL_STRING("", comfoair_secret_redact(nullptr));
}

// --- on the way back -------------------------------------------------------------------------

void test_an_untouched_field_keeps_the_stored_value(void) {
  // The SPA echoes back every key it loaded, so an untouched password arrives as the
  // sentinel. Were the key simply omitted instead, it would come back as "" and be
  // indistinguishable from a deliberate clear -- one save of an unrelated setting would
  // silently wipe the broker credentials.
  TEST_ASSERT_EQUAL(COMFOAIR_SECRET_KEEP,
                    comfoair_secret_decide(true, COMFOAIR_SECRET_SENTINEL));
}

void test_an_empty_submission_clears_the_secret_on_purpose(void) {
  TEST_ASSERT_EQUAL(COMFOAIR_SECRET_CLEAR, comfoair_secret_decide(true, ""));
}

void test_anything_else_is_stored(void) {
  TEST_ASSERT_EQUAL(COMFOAIR_SECRET_STORE, comfoair_secret_decide(true, "new one"));
  TEST_ASSERT_EQUAL(COMFOAIR_SECRET_STORE, comfoair_secret_decide(false, "first one"));
}

void test_a_missing_key_keeps_the_stored_value(void) {
  TEST_ASSERT_EQUAL(COMFOAIR_SECRET_KEEP, comfoair_secret_decide(true, nullptr));
}

void test_the_sentinel_is_refused_when_nothing_is_stored(void) {
  // Otherwise the sentinel becomes a password that works on every device that has never
  // been configured -- it is a published string. It is easy to miss because the sentinel
  // normally only travels outward; nothing stops a client from submitting it as a new value.
  TEST_ASSERT_EQUAL(COMFOAIR_SECRET_REJECT,
                    comfoair_secret_decide(false, COMFOAIR_SECRET_SENTINEL));
}

void test_clearing_something_already_unset_is_a_no_op(void) {
  TEST_ASSERT_EQUAL(COMFOAIR_SECRET_KEEP, comfoair_secret_decide(false, ""));
}

// --- comparing -------------------------------------------------------------------------------

void test_a_correct_password_matches(void) {
  TEST_ASSERT_TRUE(comfoair_secret_equals("hunter2", "hunter2"));
}

void test_a_wrong_password_does_not(void) {
  TEST_ASSERT_FALSE(comfoair_secret_equals("hunter2", "hunter3"));
  TEST_ASSERT_FALSE(comfoair_secret_equals("hunter2", "hunter"));
  TEST_ASSERT_FALSE(comfoair_secret_equals("hunter2", "hunter22"));
}

void test_comparison_does_not_stop_at_the_first_wrong_byte(void) {
  // Timing tells an attacker how much of a guess was right, one byte at a time. This test
  // cannot measure time, so it pins the property that makes it true: every byte of the
  // longer string is read, which comfoair_secret_compared_bytes() reports.
  comfoair_secret_equals("hunter2", "xxxxxxx");
  TEST_ASSERT_EQUAL_size_t_MESSAGE(7, comfoair_secret_compared_bytes(),
                                   "the comparison returned early");
  comfoair_secret_equals("hunter2", "x");
  TEST_ASSERT_EQUAL_size_t_MESSAGE(7, comfoair_secret_compared_bytes(),
                                   "a short guess was cheaper to reject");
}

void test_an_absent_password_matches_nothing(void) {
  TEST_ASSERT_FALSE(comfoair_secret_equals(nullptr, "anything"));
  TEST_ASSERT_FALSE(comfoair_secret_equals("", "anything"));
  TEST_ASSERT_FALSE_MESSAGE(comfoair_secret_equals("", ""),
                            "an empty stored password accepted an empty guess");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_key_is_secret_by_the_words_in_it_not_by_a_list);
  RUN_TEST(test_an_ordinary_key_is_not_secret);
  RUN_TEST(test_a_set_secret_reads_back_as_the_sentinel);
  RUN_TEST(test_an_unset_secret_reads_back_empty_not_as_the_sentinel);
  RUN_TEST(test_an_untouched_field_keeps_the_stored_value);
  RUN_TEST(test_an_empty_submission_clears_the_secret_on_purpose);
  RUN_TEST(test_anything_else_is_stored);
  RUN_TEST(test_a_missing_key_keeps_the_stored_value);
  RUN_TEST(test_the_sentinel_is_refused_when_nothing_is_stored);
  RUN_TEST(test_clearing_something_already_unset_is_a_no_op);
  RUN_TEST(test_a_correct_password_matches);
  RUN_TEST(test_a_wrong_password_does_not);
  RUN_TEST(test_comparison_does_not_stop_at_the_first_wrong_byte);
  RUN_TEST(test_an_absent_password_matches_nothing);
  return UNITY_END();
}
