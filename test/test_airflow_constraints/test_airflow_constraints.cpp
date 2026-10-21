// PDO 230 airflow-constraint bitset.
//
// Bit indices are the documented PDO 230 bitset (docs/reference/comfoair-pdo-rmi.md §4 #4);
// the wording and the +/- grouping come from Zehnder's STATUS > FLOW RESTRICTIONS screen
// (docs/reference/comfoair-q-hardware.md §13.6; User p. 19).
#include <unity.h>

#include <string>

#include "airflow_constraints.h"

using comfoair::airflow_constraints_to_string;
using comfoair::AIRFLOW_CONSTRAINTS_MAX_LENGTH;

static uint64_t bits(std::initializer_list<int> indices) {
  uint64_t v = 0;
  for (int i : indices) v |= UINT64_C(1) << i;
  return v;
}

void test_without_bit_45_there_is_no_restriction(void) {
  // Bit 45 clear is "no constraint list" (aiocomfoconnect calculate_airflow_constraints() returns
  // None for it), and the Q350 reads all zeros at rest -- so it renders "No restrictions", whatever
  // else is set. "Unknown" read as a fault on a unit that was simply unrestricted.
  TEST_ASSERT_EQUAL_STRING("No restrictions", airflow_constraints_to_string(0).c_str());
  TEST_ASSERT_EQUAL_STRING("No restrictions", airflow_constraints_to_string(bits({9, 26})).c_str());
}

void test_a_list_with_no_reason_reads_no_restrictions(void) {
  // Never "None": Home Assistant's MQTT sensor takes the payload "None" for an unknown state, so
  // an unrestricted unit would show as unknown.
  TEST_ASSERT_EQUAL_STRING("No restrictions", airflow_constraints_to_string(bits({45})).c_str());
}

void test_a_throttling_reason_is_reported_below(void) {
  // Bit 9 = Frost Protection, one of the "-" reasons: the unit is held BELOW the request.
  TEST_ASSERT_EQUAL_STRING("Below: Frost Protection",
                           airflow_constraints_to_string(bits({45, 9})).c_str());
}

void test_a_demand_reason_is_reported_above(void) {
  // Bit 26 = Humidity Comfort, a "+" reason: the unit ventilates HARDER than asked.
  // Flattening this into one list would make it read as a restriction -- the exact
  // opposite of what the unit is doing. That is what this test guards.
  TEST_ASSERT_EQUAL_STRING("Above: Humidity Comfort",
                           airflow_constraints_to_string(bits({45, 26})).c_str());
}

void test_both_directions_are_kept_apart(void) {
  TEST_ASSERT_EQUAL_STRING("Below: Frost Protection | Above: Humidity Comfort",
                           airflow_constraints_to_string(bits({45, 9, 26})).c_str());
}

void test_two_bits_for_one_reason_are_not_repeated(void) {
  // Bits 2 and 3 are both Resistance; the screen prints it once.
  TEST_ASSERT_EQUAL_STRING("Below: Resistance",
                           airflow_constraints_to_string(bits({45, 2, 3})).c_str());
}

void test_preheater_appears_in_whichever_direction_reported_it(void) {
  // These are sometimes labelled PreheaterNegative (4) and PreheaterPositive (22); here the
  // direction is carried by the group, so both collapse to one name.
  TEST_ASSERT_EQUAL_STRING("Below: Preheater",
                           airflow_constraints_to_string(bits({45, 4})).c_str());
  TEST_ASSERT_EQUAL_STRING("Above: Preheater",
                           airflow_constraints_to_string(bits({45, 22})).c_str());
}

void test_bit_25_is_temperature_passive(void) {
  // Not "TemperatureComfort", which appears in none of the four manuals and
  // would contradict the select this firmware already publishes.
  TEST_ASSERT_EQUAL_STRING("Above: Temperature Passive",
                           airflow_constraints_to_string(bits({45, 25})).c_str());
}

void test_undocumented_bits_are_ignored(void) {
  // Bit 44 is listed nowhere; it must not invent a reason.
  TEST_ASSERT_EQUAL_STRING("No restrictions",
                           airflow_constraints_to_string(bits({45, 44})).c_str());
}

void test_the_result_fits_what_home_assistant_accepts(void) {
  // HA refuses entity states longer than 255 characters, so every bit set at once must
  // still produce a legal state.
  uint64_t all = 0;
  for (int i = 0; i < 64; i++) all |= UINT64_C(1) << i;
  const std::string s = airflow_constraints_to_string(all);
  TEST_ASSERT_LESS_OR_EQUAL_size_t(AIRFLOW_CONSTRAINTS_MAX_LENGTH, s.size());
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_without_bit_45_there_is_no_restriction);
  RUN_TEST(test_a_list_with_no_reason_reads_no_restrictions);
  RUN_TEST(test_a_throttling_reason_is_reported_below);
  RUN_TEST(test_a_demand_reason_is_reported_above);
  RUN_TEST(test_both_directions_are_kept_apart);
  RUN_TEST(test_two_bits_for_one_reason_are_not_repeated);
  RUN_TEST(test_preheater_appears_in_whichever_direction_reported_it);
  RUN_TEST(test_bit_25_is_temperature_passive);
  RUN_TEST(test_undocumented_bits_are_ignored);
  RUN_TEST(test_the_result_fits_what_home_assistant_accepts);
  return UNITY_END();
}
