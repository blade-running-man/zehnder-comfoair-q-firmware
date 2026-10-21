// The bus freshness verdict: the ONE CAN silence resolver, read both by the bus-silent phase and
// by the <prefix>/bus availability topic (docs/device/indication.md, Hold-offs; and
// docs/interfaces/mqtt.md, Availability: status and bus).
//
// WHAT THIS SUITE IS FOR, in one line: it is the only place the four states are ever driven, so
// that "the installation stopped talking" is a case a host can run rather than a step somebody
// once watched in front of a ventilation unit with its breaker off.
//
// NOT ONE ESP-IDF HEADER, and that is an assertion, not an accident. The resolver owns no clock,
// no TWAI handle and no poll task: `now_ms` and `rx_frames` are NUMBERS the caller reads, and the
// provocation leaves as a bool for the caller to turn into comfoair_poll_request_now(). Written
// the other way round -- with the call inside -- nothing below could exist, because
// comfoair_poll.h reaches FreeRTOS and this binary would stop linking.
//
// The suite is one file on purpose: at the ceiling it splits the way test_phase and
// test_mqtt_backoff did, but the whole machine is thirteen cases and one table.
#include <unity.h>

#include <stdio.h>
#include <string.h>

#include "comfoair_bus_health.h"

void setUp(void) {}
void tearDown(void) {}

// The two thresholds as they stand TODAY (probing after 60 s, silent after 90 s), transcribed from
// docs/device/indication.md (Hold-offs) and never read back from the implementation -- which is
// what lets a mutation of the implementation fail here.
//
// AN OPEN QUESTION: the period with which the unit broadcasts PDOs on its own has never been
// measured in this tree, so 60/90 are argued from BELOW (a start sweep costs one frame per polled
// PDO x 100 ms, seconds, comfoair_poll.h:26-27) and not from above. When hardware scenario
// HV-CAN-05 (docs/hardware-verification/02-can-bus-and-polling.md) produces a number,
// COMFOAIR_BUS_QUIET_MS moves in the header and these two lines move with it -- and nothing else in
// this file does, because everything below is written in terms of these two.
static const uint32_t PROBE_AT_MS  = 60000u;
static const uint32_t SILENT_AT_MS = 90000u;

// One second of the monitor task, which is the resolution the real one runs at (the
// comfoair_phase task's 1 s period).
static const uint32_t TICK_MS = 1000u;

// A bus that is doing well: the driver is up and the counter is moving. Every case starts here and
// spoils exactly what it is about.
static comfoair_bus_health_in_t healthy(uint32_t now_ms, uint32_t rx_frames)
{
  comfoair_bus_health_in_t in{};
  in.now_ms    = now_ms;
  in.started   = true;
  in.rx_frames = rx_frames;
  return in;
}

// One pass, returning what the caller would act on.
static comfoair_bus_health_out_t pass(comfoair_bus_health_state_t *st, uint32_t now_ms,
                                      uint32_t rx_frames, bool started = true)
{
  comfoair_bus_health_in_t in = healthy(now_ms, rx_frames);
  in.started                  = started;
  comfoair_bus_health_out_t out{};
  comfoair_bus_health_pass(st, &in, &out);
  return out;
}

// A device that has just heard its first frame, wound forward to `now_ms`. Returns the state with
// one frame counted at time 0 and the counter standing still since.
static void first_frame_at_zero(comfoair_bus_health_state_t *st)
{
  *st = comfoair_bus_health_state_t{};
  (void)pass(st, 0u, 1u);
}

// Runs the monitor from `from_ms` to `to_ms` in one-second ticks with the counter frozen, and
// returns how many provocations were asked for. THE COUNT IS THE POINT: the traffic
// argument is that a provocation once a minute turns an hourly sweep into a per-minute one -- a
// sixtyfold rise on a 50 kbps bus, against which comfoair_poll.h:4-7 warns by name.
static uint32_t probes_between(comfoair_bus_health_state_t *st, uint32_t from_ms, uint32_t to_ms,
                               uint32_t frozen_at)
{
  uint32_t probes = 0;
  for (uint32_t t = from_ms; t <= to_ms; t += TICK_MS) {
    if (pass(st, t, frozen_at).probe)
      probes++;
  }
  return probes;
}

// --- the four states ---------------------------------------------------------------------------

// Case 1. NEVER_SEEN exists so that a third number does not: a device whose loom was never
// plugged in must not be told it went silent, and a boot grace period of 120 s -- with `uptime` as
// a fourth input to a resolver that has no clock -- is what its absence would cost.
void test_a_device_that_never_heard_a_frame_is_not_silent_it_is_new(void)
{
  comfoair_bus_health_state_t st{};

  // Two hours with the driver up and nothing on the wire. Not one second of it is SILENT.
  for (uint32_t t = 0; t <= 2u * 3600u * 1000u; t += 60u * TICK_MS) {
    const comfoair_bus_health_out_t out = pass(&st, t, 0u);
    TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_BUS_NEVER_SEEN, out.state,
                                  "a bus that never spoke reported something other than NEVER_SEEN");
    TEST_ASSERT_FALSE_MESSAGE(comfoair_bus_health_is_silent(out.state),
                              "NEVER_SEEN painted BUS_SILENT; a never-heard bus is new");
    TEST_ASSERT_FALSE_MESSAGE(comfoair_bus_health_available(out.state),
                              "a bus that never spoke was published as available");
    TEST_ASSERT_FALSE_MESSAGE(out.probe,
                              "a bus that never spoke was provoked; the boot sweep already asked "
                              "(comfoair_poll_plan_init() makes a sweep due at once)");
  }
}

// Case 2. The question, not the verdict: at PROBE_AT_MS the resolver asks and keeps saying
// the bus is available, because the verdict is 90 s and publishing offline at 60 would silently
// make the threshold 60.
void test_sixty_seconds_without_a_frame_asks_the_bus_a_question(void)
{
  comfoair_bus_health_state_t st{};
  first_frame_at_zero(&st);

  const comfoair_bus_health_out_t just_before = pass(&st, PROBE_AT_MS - 1u, 1u);
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_BUS_FRESH, just_before.state,
                                "the bus was probed before the threshold");
  TEST_ASSERT_FALSE_MESSAGE(just_before.probe, "a provocation was asked for before the threshold");

  const comfoair_bus_health_out_t at = pass(&st, PROBE_AT_MS, 1u);
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_BUS_PROBING, at.state,
                                "sixty seconds of silence did not reach PROBING");
  TEST_ASSERT_TRUE_MESSAGE(at.probe, "PROBING was entered without asking the bus anything");
  TEST_ASSERT_TRUE_MESSAGE(comfoair_bus_health_available(at.state),
                           "PROBING published the bus offline; the verdict is ninety seconds");
  TEST_ASSERT_FALSE_MESSAGE(comfoair_bus_health_is_silent(at.state),
                            "PROBING painted BUS_SILENT before the verdict was reached");
}

// Case 3. The verdict, and both readers of it move together.
void test_ninety_seconds_is_the_verdict(void)
{
  comfoair_bus_health_state_t st{};
  first_frame_at_zero(&st);

  const comfoair_bus_health_out_t just_before = pass(&st, SILENT_AT_MS - 1u, 1u);
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_BUS_PROBING, just_before.state,
                                "the verdict was passed before ninety seconds");

  const comfoair_bus_health_out_t at = pass(&st, SILENT_AT_MS, 1u);
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_BUS_SILENT, at.state,
                                "ninety seconds of silence did not reach SILENT");
  TEST_ASSERT_FALSE_MESSAGE(comfoair_bus_health_available(at.state),
                            "a silent bus was still published as available");
  TEST_ASSERT_TRUE_MESSAGE(comfoair_bus_health_is_silent(at.state),
                           "a silent bus did not paint the BUS_SILENT phase");

  // And it stays there. An hour of SILENT is one verdict, not a state that decays further.
  const comfoair_bus_health_out_t later = pass(&st, SILENT_AT_MS + 3600u * TICK_MS, 1u);
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_BUS_SILENT, later.state,
                                "SILENT did not hold");
}

// Case 4, and the case with a bill attached. comfoair_poll.h:4-7 says a sweep is one remote
// transmission request per polled PDO, spaced 100 ms apart, once an hour, and that shortening it is
// not free on a 50 kbps bus. One provocation per ENTRY into PROBING keeps that hourly; one per PASS
// would make it per-second, and one per minute -- the version that looks reasonable -- makes it
// sixtyfold.
void test_one_provocation_per_entry_into_probing(void)
{
  comfoair_bus_health_state_t st{};
  first_frame_at_zero(&st);

  // An hour of silence, one second at a time.
  const uint32_t first = probes_between(&st, TICK_MS, 3600u * TICK_MS, 1u);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, first,
                                   "an hour of silence asked the bus more than once");

  // A frame arrives, the silence ends, and a NEW silence earns its own single provocation --
  // otherwise a bus that flaps once a day is only ever probed once, on the first day.
  (void)pass(&st, 3601u * TICK_MS, 2u);
  const uint32_t second = probes_between(&st, 3602u * TICK_MS, 7200u * TICK_MS, 2u);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, second,
                                   "the second silence was either never probed or probed twice");
}

// Case 5, and the asymmetry of the verdict: OFFLINE after a threshold, ONLINE on the first
// frame with no debounce at all. A frame either arrived or it did not, and a debounce would cost
// the owner another 90 s of a grey card after he closed the breaker.
void test_a_frame_at_any_point_returns_to_fresh(void)
{
  const uint32_t after[] = {
      TICK_MS,                    // during FRESH
      PROBE_AT_MS,                // the instant of the provocation
      SILENT_AT_MS - TICK_MS,     // deep in PROBING
      SILENT_AT_MS,               // the instant of the verdict
      6u * 3600u * 1000u,         // six hours of a dead bus
  };

  for (size_t i = 0; i < sizeof after / sizeof after[0]; i++) {
    comfoair_bus_health_state_t st{};
    first_frame_at_zero(&st);
    const comfoair_bus_health_out_t silent = pass(&st, after[i], 1u);
    char msg[96];
    snprintf(msg, sizeof msg, "%u ms of silence was still FRESH, so this case proves nothing",
             (unsigned)after[i]);
    if (after[i] >= PROBE_AT_MS)
      TEST_ASSERT_NOT_EQUAL_MESSAGE(COMFOAIR_BUS_FRESH, silent.state, msg);

    const comfoair_bus_health_out_t back = pass(&st, after[i] + TICK_MS, 2u);
    snprintf(msg, sizeof msg, "a frame after %u ms of silence did not restore FRESH", (unsigned)after[i]);
    TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_BUS_FRESH, back.state, msg);
    TEST_ASSERT_TRUE_MESSAGE(comfoair_bus_health_available(back.state),
                             "a bus that just spoke was still published offline");
    TEST_ASSERT_FALSE_MESSAGE(comfoair_bus_health_is_silent(back.state),
                              "a bus that just spoke still painted BUS_SILENT");
  }
}

// Case 6, and it is a COMPILABLE assertion rather than a comparison of two numbers, because
// the failure it guards against is not a wrong number -- it is two numbers that stop being one.
// Both thresholds are derived from COMFOAIR_BUS_QUIET_MS and from nothing else, so a hardware
// measurement costs one line and cannot move one threshold while leaving the other behind.
static_assert(COMFOAIR_BUS_PROBE_MS == 2u * COMFOAIR_BUS_QUIET_MS,
              "the probing threshold stopped being derived from COMFOAIR_BUS_QUIET_MS");
static_assert(COMFOAIR_BUS_SILENT_MS == 3u * COMFOAIR_BUS_QUIET_MS,
              "the silence threshold stopped being derived from COMFOAIR_BUS_QUIET_MS");
static_assert(COMFOAIR_BUS_PROBE_MS < COMFOAIR_BUS_SILENT_MS,
              "the question would be asked after the verdict was passed");

void test_both_thresholds_come_from_one_define(void)
{
  // The static assertions above are the case; this body pins the numbers those definitions
  // currently produce to the two docs/device/indication.md states, so that a mutation which keeps
  // the derivation but changes the base -- 3N to 4N, say -- still fails something.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(PROBE_AT_MS, (uint32_t)COMFOAIR_BUS_PROBE_MS,
                                   "the probing threshold is no longer sixty seconds");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(SILENT_AT_MS, (uint32_t)COMFOAIR_BUS_SILENT_MS,
                                   "the verdict threshold is no longer ninety seconds");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(30000u, (uint32_t)COMFOAIR_BUS_QUIET_MS,
                                   "the one number the other two are made of moved without them");
}

// --- the two readers, and the reason they are two ----------------------------------------------

// One verdict, two projections, TOTAL over the enumeration -- a second threshold answering the
// same question drifts away from the first sooner or later, and the phase aggregate's own header
// says so at comfoair_phase.h:148-151.
//
// The row that carries the whole argument is NEVER_SEEN: offline on the topic, because there are
// no values and nothing to lie with, and NOT silent for the phase, because "the loom was never
// connected" and "the unit stopped answering" are different sentences to an owner. Anyone tempted
// to write is_silent() as !available() has to delete this row first.
void test_the_topic_and_the_phase_read_one_verdict(void)
{
  struct row {
    comfoair_bus_health_t state;
    bool                  available;
    bool                  silent;
  };
  const row table[] = {
      {COMFOAIR_BUS_NEVER_SEEN, false, false},
      {COMFOAIR_BUS_FRESH, true, false},
      {COMFOAIR_BUS_PROBING, true, false},
      {COMFOAIR_BUS_SILENT, false, true},
  };
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(COMFOAIR_BUS_HEALTH_COUNT, sizeof table / sizeof table[0],
                                   "a state was added and this table was not");

  // True once a state is found for which the two answers are NOT each other's opposite. Exactly
  // one state can do that -- NEVER_SEEN, which is offline and not silent -- so this doubles as the
  // assertion that the row is still in the table above.
  bool not_a_negation = false;
  for (const row &r : table) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(r.available, comfoair_bus_health_available(r.state),
                                  comfoair_bus_health_name(r.state));
    TEST_ASSERT_EQUAL_INT_MESSAGE(r.silent, comfoair_bus_health_is_silent(r.state),
                                  comfoair_bus_health_name(r.state));
    if (r.silent == r.available)
      not_a_negation = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(not_a_negation,
                           "is_silent() is the negation of available(), so NEVER_SEEN would paint "
                           "BUS_SILENT and a boot grace period would have to come back");
}

// --- the edges ---------------------------------------------------------------------------------

// comfoair_can.h:69 is a uint32_t and comfoair_can.h:65-66 says the counters are zeroed by
// comfoair_can_start(), not that they never wrap. At 50 kbps a shortest frame is about 780 a
// second, so the counter comes back to zero after roughly two months of a busy bus. A comparison
// written as "greater than" reads that as silence and takes the whole installation offline on an
// otherwise perfect day; the test is "did it move".
void test_a_counter_that_wraps_is_movement_and_not_silence(void)
{
  comfoair_bus_health_state_t st{};
  (void)pass(&st, 0u, 0xFFFFFFFEu);

  // The counter is frozen one short of the wrap for long enough to earn a verdict, so that the
  // step across the wrap is being read against a machine that demonstrably does declare silence.
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_BUS_SILENT, pass(&st, SILENT_AT_MS, 0xFFFFFFFEu).state,
                                "the frozen counter never reached a verdict, so the wrap below "
                                "would prove nothing");

  const comfoair_bus_health_out_t wrapped = pass(&st, SILENT_AT_MS + TICK_MS, 0x00000001u);
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_BUS_FRESH, wrapped.state,
                                "a wrapped counter was read as a bus that stopped talking");

  // And the wrap does not make the device new again: a counter standing at 0 after it has moved
  // is not the same fact as a counter that has never moved.
  const comfoair_bus_health_out_t zero = pass(&st, SILENT_AT_MS + 2u * TICK_MS, 0x00000000u);
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_BUS_FRESH, zero.state,
                                "a counter that wrapped exactly onto zero was read as NEVER_SEEN");
}

// The millisecond clock wraps every 49.7 days and a device is expected to run longer than that.
// The elapsed time is an unsigned difference for exactly this reason.
void test_the_verdict_survives_the_millisecond_wrap(void)
{
  // The last frame lands ten seconds before the counter runs out, so every measurement below is
  // taken on the far side of the wrap.
  const uint32_t before_wrap = 0xFFFFFFFFu - 10000u;

  comfoair_bus_health_state_t st{};
  (void)pass(&st, before_wrap, 1u);
  const comfoair_bus_health_out_t straddling = pass(&st, before_wrap + PROBE_AT_MS, 1u);
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_BUS_PROBING, straddling.state,
                                "the clock wrap moved the verdict");
  TEST_ASSERT_TRUE_MESSAGE(straddling.probe, "the provocation was lost across the clock wrap");

  const comfoair_bus_health_out_t verdict = pass(&st, before_wrap + SILENT_AT_MS, 1u);
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_BUS_SILENT, verdict.state,
                                "the verdict was lost across the clock wrap");
}

// comfoair_can_stop() exists and comfoair_can.h:65-66 zeroes every counter on the next start. A
// resolver that kept its old anchor across that would compare a fresh zero against a counter from
// the previous session and answer SILENT on a driver that has been up for one second.
//
// This is also the half of comfoair_phase.h:135-146's distinction that lives here: `started` is
// the driver NOW, and a driver that is down has heard nothing rather than gone quiet.
void test_a_stopped_driver_has_heard_nothing_rather_than_gone_quiet(void)
{
  comfoair_bus_health_state_t st{};
  (void)pass(&st, 0u, 5u);
  const comfoair_bus_health_out_t stopped = pass(&st, TICK_MS, 5u, /*started=*/false);
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_BUS_NEVER_SEEN, stopped.state,
                                "a stopped driver reported a verdict about the wire");
  TEST_ASSERT_FALSE_MESSAGE(stopped.probe, "a stopped driver was asked to poll the bus");

  // Restarted, counters back at zero: NEVER_SEEN again, and a frame starts the clock afresh
  // instead of being measured against the old session.
  (void)pass(&st, 2u * TICK_MS, 0u);
  const comfoair_bus_health_out_t restarted = pass(&st, 2u * TICK_MS + SILENT_AT_MS, 0u);
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_BUS_NEVER_SEEN, restarted.state,
                                "a restarted driver inherited the previous session's silence");
}

// The retained topic is written only when the verdict changes (docs/interfaces/mqtt.md,
// Availability: status and bus), so the resolver must answer the same thing for the same facts
// however often it is asked -- including twice within the same millisecond, which is what a
// monitor tick and an /api/status request together produce.
void test_the_verdict_does_not_move_while_nothing_moves(void)
{
  comfoair_bus_health_state_t st{};
  first_frame_at_zero(&st);

  const comfoair_bus_health_out_t first  = pass(&st, PROBE_AT_MS, 1u);
  const comfoair_bus_health_out_t second = pass(&st, PROBE_AT_MS, 1u);
  TEST_ASSERT_EQUAL_INT_MESSAGE(first.state, second.state,
                                "two passes at the same instant gave two verdicts");
  TEST_ASSERT_TRUE_MESSAGE(first.probe, "the entry into PROBING asked nothing");
  TEST_ASSERT_FALSE_MESSAGE(second.probe, "the same instant was charged a second provocation");
}

// The same rule as comfoair_phase_name and comfoair_mqtt_fail_name: these words travel through
// JSON documents, so a renderer must never be handed a null pointer, and a state inside the
// enumeration must never answer "unknown".
void test_every_verdict_has_a_name(void)
{
  for (int i = 0; i < COMFOAIR_BUS_HEALTH_COUNT; i++) {
    const char *name = comfoair_bus_health_name((comfoair_bus_health_t)i);
    TEST_ASSERT_NOT_NULL_MESSAGE(name, "a state has no spelling");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, name[0], "a state is spelled as an empty string");
    TEST_ASSERT_TRUE_MESSAGE(strcmp(name, "unknown") != 0,
                             "a state inside the enumeration answered unknown");
  }
  TEST_ASSERT_EQUAL_STRING_MESSAGE("unknown", comfoair_bus_health_name((comfoair_bus_health_t)99),
                                   "a number that is not a state was given a real spelling");
}

// Totality, the same contract comfoair_mqtt_ladder_pass carries: any input is answered, NULLs
// included. A monitor that crashed on a null would take the whole indication down with it.
void test_null_arguments_are_answered(void)
{
  comfoair_bus_health_state_t st{};
  comfoair_bus_health_in_t    in  = healthy(0u, 0u);
  comfoair_bus_health_out_t   out{};
  out.state = COMFOAIR_BUS_SILENT;
  out.probe = true;

  comfoair_bus_health_pass(nullptr, &in, &out);
  comfoair_bus_health_pass(&st, nullptr, &out);
  comfoair_bus_health_pass(&st, &in, nullptr);
  TEST_ASSERT_FALSE_MESSAGE(out.probe, "a call with a missing argument asked for a sweep");
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_BUS_NEVER_SEEN, out.state,
                                "a call with a missing argument left a verdict standing");

  TEST_ASSERT_FALSE_MESSAGE(comfoair_bus_health_available((comfoair_bus_health_t)99),
                            "a number that is not a state was published as an available bus");
  TEST_ASSERT_FALSE_MESSAGE(comfoair_bus_health_is_silent((comfoair_bus_health_t)99),
                            "a number that is not a state painted BUS_SILENT");
}

int main(int, char **)
{
  UNITY_BEGIN();
  RUN_TEST(test_a_device_that_never_heard_a_frame_is_not_silent_it_is_new);
  RUN_TEST(test_sixty_seconds_without_a_frame_asks_the_bus_a_question);
  RUN_TEST(test_ninety_seconds_is_the_verdict);
  RUN_TEST(test_one_provocation_per_entry_into_probing);
  RUN_TEST(test_a_frame_at_any_point_returns_to_fresh);
  RUN_TEST(test_both_thresholds_come_from_one_define);
  RUN_TEST(test_the_topic_and_the_phase_read_one_verdict);
  RUN_TEST(test_a_counter_that_wraps_is_movement_and_not_silence);
  RUN_TEST(test_the_verdict_survives_the_millisecond_wrap);
  RUN_TEST(test_a_stopped_driver_has_heard_nothing_rather_than_gone_quiet);
  RUN_TEST(test_the_verdict_does_not_move_while_nothing_moves);
  RUN_TEST(test_every_verdict_has_a_name);
  RUN_TEST(test_null_arguments_are_answered);
  return UNITY_END();
}
