// The in-memory log the web UI reads.
//
// A ring buffer, because the alternative is a device that runs out of RAM after a week of
// uptime. What matters is that it never grows, never blocks the task that logged, and that
// what it hands back is valid JSON even when a log line contains a quote or a newline --
// log lines are the one thing on this device that carries arbitrary text.
#include <unity.h>

#include <cstring>
#include <string>

#include "comfoair_log.h"

void setUp(void) { comfoair_log_reset(); }
void tearDown(void) {}

static std::string dump(void) {
  static char buf[8192];
  const size_t n = comfoair_log_render(buf, sizeof buf);
  return std::string(buf, n);
}

static bool contains(const std::string &h, const char *n) {
  return h.find(n) != std::string::npos;
}

void test_an_empty_log_is_an_empty_array(void) {
  TEST_ASSERT_EQUAL_STRING("[]", dump().c_str());
}

void test_lines_come_back_in_the_order_they_were_written(void) {
  comfoair_log_write("first");
  comfoair_log_write("second");
  const std::string json = dump();
  TEST_ASSERT_TRUE(contains(json, "\"first\""));
  TEST_ASSERT_TRUE(contains(json, "\"second\""));
  TEST_ASSERT_TRUE_MESSAGE(json.find("first") < json.find("second"), "order was lost");
}

void test_the_oldest_line_is_dropped_rather_than_the_newest(void) {
  // A log that stops recording once full is worse than useless during an incident: the
  // interesting lines are always the last ones.
  for (int i = 0; i < COMFOAIR_LOG_LINES + 5; i++) {
    char line[32];
    snprintf(line, sizeof line, "line%d", i);
    comfoair_log_write(line);
  }
  const std::string json = dump();
  TEST_ASSERT_FALSE_MESSAGE(contains(json, "\"line0\""), "the newest lines were dropped");
  char newest[32];
  snprintf(newest, sizeof newest, "\"line%d\"", COMFOAIR_LOG_LINES + 4);
  TEST_ASSERT_TRUE_MESSAGE(contains(json, newest), "the newest line is missing");
}

void test_a_line_longer_than_the_slot_is_truncated_not_overflowed(void) {
  std::string huge(COMFOAIR_LOG_LINE_LEN * 3, 'x');
  comfoair_log_write(huge.c_str());
  const std::string json = dump();
  TEST_ASSERT_TRUE(json.size() < COMFOAIR_LOG_LINE_LEN * 3);
}

void test_quotes_and_newlines_in_a_line_stay_valid_json(void) {
  // Log lines are the only place on this device that carries arbitrary text, so this is
  // where invalid JSON would come from.
  comfoair_log_write("said \"hello\"\nand left\\");
  const std::string json = dump();
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\\\""), "a quote was not escaped");
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\\n"), "a newline was not escaped");
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\\\\"), "a backslash was not escaped");
}

void test_a_null_line_is_ignored(void) {
  comfoair_log_write(nullptr);
  TEST_ASSERT_EQUAL_STRING("[]", dump().c_str());
}

void test_a_buffer_too_small_reports_failure(void) {
  comfoair_log_write("something");
  char small[4];
  TEST_ASSERT_EQUAL_size_t(0, comfoair_log_render(small, sizeof small));
}

// comfoair_log_feed assembles per-vprintf-call fragments into whole lines before they reach
// the ring. See comfoair_log.h for why: the WiFi driver blob makes three vprint_func calls
// per logical trace line (prefix, body, trailing "\n"), and feeding those straight to
// comfoair_log_write turned each WiFi trace line into three ring slots, one of them empty.
// Ordinary ESP-IDF logging in this build (CONFIG_LOG_VERSION=1) already arrives as one
// complete call per line -- feed has to be correct for both shapes without knowing which one
// it's looking at.

void test_feed_with_no_newline_accumulates_and_commits_nothing(void) {
  comfoair_log_feed("no newline here yet");
  TEST_ASSERT_EQUAL_STRING("[]", dump().c_str());
}

void test_feed_completing_a_line_commits_one_joined_entry(void) {
  comfoair_log_feed("I (2299) wifi:");
  comfoair_log_feed("(trc)band:2G\n");
  TEST_ASSERT_EQUAL_STRING("[\"I (2299) wifi:(trc)band:2G\"]", dump().c_str());
}

void test_feed_with_several_newlines_commits_several_entries_in_order(void) {
  comfoair_log_feed("one\ntwo\nthree\n");
  TEST_ASSERT_EQUAL_STRING("[\"one\",\"two\",\"three\"]", dump().c_str());
}

void test_feed_a_bare_newline_commits_pending_but_nothing_when_none_pending(void) {
  // This is the 26-empties bug: the WiFi blob's third vprintf call per logical trace line
  // carries only "\n". With nothing pending, that must commit no ring entry at all.
  comfoair_log_feed("\n");
  TEST_ASSERT_EQUAL_STRING("[]", dump().c_str());

  comfoair_log_feed("pending text");
  comfoair_log_feed("\n");
  TEST_ASSERT_EQUAL_STRING("[\"pending text\"]", dump().c_str());
}

void test_feed_leaves_text_after_the_last_newline_pending(void) {
  comfoair_log_feed("line one\nline two tail, no newline yet");
  TEST_ASSERT_EQUAL_STRING("[\"line one\"]", dump().c_str());
}

void test_feed_overflowing_a_pending_line_commits_truncated_and_recovers(void) {
  // Mirrors the device's actual failure mode, not a synthetic one: log_hook's own vsnprintf
  // truncates a single over-long hook call to COMFOAIR_LOG_LINE_LEN before comfoair_log_feed
  // ever sees it (comfoair_log.c's `char line[COMFOAIR_LOG_LINE_LEN]`), so the call carrying
  // an overflowing line reaches here already cut to exactly COMFOAIR_LOG_LINE_LEN - 1
  // characters with no trailing newline of its own -- vsnprintf discarded that along with the
  // rest of the overflow. An earlier version of this function committed a full pending buffer
  // only on the next newline it saw, which meant it kept absorbing the NEXT, unrelated call's
  // characters into "still waiting for the newline that already got cut," discarding that
  // whole next line. Committing as soon as the slot fills fixes that: the overflowing line
  // lands truncated in its own entry the moment it fills the slot, and the very next call
  // starts a clean pending line with nothing carried over.
  const std::string truncated_by_vsnprintf(COMFOAIR_LOG_LINE_LEN - 1, 'x');
  comfoair_log_feed(truncated_by_vsnprintf.c_str());  // no newline -- the case that regressed
  comfoair_log_feed("next line\n");

  TEST_ASSERT_EQUAL_STRING(("[\"" + truncated_by_vsnprintf + "\",\"next line\"]").c_str(),
                            dump().c_str());
}

void test_feed_at_exactly_the_slot_length_is_one_whole_entry(void) {
  // COMFOAIR_LOG_LINE_LEN - 1 is exactly what fits with room left for the nul -- this pins
  // the boundary an off-by-one in the cap check would live at. Not yet overflowing: it must
  // commit as one complete entry, not a truncated one.
  const std::string exact(COMFOAIR_LOG_LINE_LEN - 1, 'y');
  comfoair_log_feed(exact.c_str());
  comfoair_log_feed("\n");
  TEST_ASSERT_EQUAL_STRING(("[\"" + exact + "\"]").c_str(), dump().c_str());
}

void test_feed_one_character_past_the_slot_length_starts_a_second_entry(void) {
  // One character more than the previous test. The cap-hit commit fires while feed is still
  // appending this single chunk, not only between calls: the first COMFOAIR_LOG_LINE_LEN - 1
  // characters fill the slot and commit as their own entry, and the 160th character begins a
  // fresh pending line, which the terminating newline then commits as a second, separate
  // entry.
  const std::string one_over(COMFOAIR_LOG_LINE_LEN, 'z');
  comfoair_log_feed(one_over.c_str());
  comfoair_log_feed("\n");
  const std::string first_entry(COMFOAIR_LOG_LINE_LEN - 1, 'z');
  TEST_ASSERT_EQUAL_STRING(("[\"" + first_entry + "\",\"z\"]").c_str(), dump().c_str());
}

void test_feed_drops_a_carriage_return_without_buffering_it(void) {
  // "\r\n" line endings must not resurrect the empty-slot bug this function exists to fix: an
  // unpaired "\r" with nothing pending must commit nothing, and a "\r" immediately before a
  // real newline must not become a one-character pending line that comfoair_log_write would
  // then trim down to an empty ring entry.
  comfoair_log_feed("\r");
  comfoair_log_feed("\n");
  TEST_ASSERT_EQUAL_STRING("[]", dump().c_str());

  comfoair_log_feed("hello\r\n");
  TEST_ASSERT_EQUAL_STRING("[\"hello\"]", dump().c_str());
}

void test_reset_clears_a_pending_fragment(void) {
  comfoair_log_feed("dangling, no newline");
  comfoair_log_reset();
  comfoair_log_feed("next\n");
  TEST_ASSERT_EQUAL_STRING("[\"next\"]", dump().c_str());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_an_empty_log_is_an_empty_array);
  RUN_TEST(test_lines_come_back_in_the_order_they_were_written);
  RUN_TEST(test_the_oldest_line_is_dropped_rather_than_the_newest);
  RUN_TEST(test_a_line_longer_than_the_slot_is_truncated_not_overflowed);
  RUN_TEST(test_quotes_and_newlines_in_a_line_stay_valid_json);
  RUN_TEST(test_a_null_line_is_ignored);
  RUN_TEST(test_a_buffer_too_small_reports_failure);
  RUN_TEST(test_feed_with_no_newline_accumulates_and_commits_nothing);
  RUN_TEST(test_feed_completing_a_line_commits_one_joined_entry);
  RUN_TEST(test_feed_with_several_newlines_commits_several_entries_in_order);
  RUN_TEST(test_feed_a_bare_newline_commits_pending_but_nothing_when_none_pending);
  RUN_TEST(test_feed_leaves_text_after_the_last_newline_pending);
  RUN_TEST(test_feed_overflowing_a_pending_line_commits_truncated_and_recovers);
  RUN_TEST(test_feed_at_exactly_the_slot_length_is_one_whole_entry);
  RUN_TEST(test_feed_one_character_past_the_slot_length_starts_a_second_entry);
  RUN_TEST(test_feed_drops_a_carriage_return_without_buffering_it);
  RUN_TEST(test_reset_clears_a_pending_fragment);
  return UNITY_END();
}
