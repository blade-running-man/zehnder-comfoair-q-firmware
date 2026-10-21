// What the publisher task must carry into the ladder, and the three seams the review of be70cbd
// found untested -- the jitter seed, the packed failure word, and the rule that keeps a rung off
// an attempt that is in flight.
//
// WHY THESE ARE ONE FILE: each of them is a place where the DECISION was already proved and the
// WIRING was not, and each was caught by a mutation that the whole suite survived. A seed that is
// thrown away, a bit that collides with its neighbour and a stale failure flag are all invisible
// in a document that renders and a ladder that climbs.
//
// SPLIT FROM test_mqtt_ladder.cpp at the 600-line ceiling (AGENTS.md); ladder_harness.h is shared
// and every RUN_TEST is in test_mqtt_backoff.cpp beside main().
#include "ladder_harness.h"

// Two mutations survived be70cbd: `in->seed -> 0` inside the pass, and `return 0;` in
// the FNV that the publisher feeds it. The second one passed the ENTIRE suite and produced a
// firmware in which every device in a house retries in lockstep -- the whole anti-storm argument
// for seeding the jitter from the MAC, deleted by code that no test could see, because the existing
// case called comfoair_mqtt_backoff_ms() directly and never went through the wiring.
//
// So this drives the pass, twice, with the seeds of two devices, and it derives those seeds the
// way the caller does. Ten device ids, because the property IS the spread across a house.
void test_two_devices_do_not_retry_in_lockstep(void)
{
  static const char *IDS[] = {"24a160ff1234", "24a160ff1235", "24a160ff1236", "24a160ff1237",
                              "24a160ff1238", "aabbccddeeff", "000000000000", "ffffffffffff",
                              "comfoair-q-01", "comfoair-q-02"};
  const uint32_t N = (uint32_t)(sizeof IDS / sizeof IDS[0]);

  uint32_t delay[16];
  for (uint32_t i = 0; i < N; i++) {
    comfoair_mqtt_ladder_t    st{};
    comfoair_mqtt_ladder_in_t in = healthy(1000u);
    transport_failure(&in, false);
    in.seed = comfoair_mqtt_backoff_seed(IDS[i]);
    delay[i] = tick(&st, &in).retry_in_ms;
    TEST_ASSERT_TRUE_MESSAGE(delay[i] > 0u, "a device armed a rung of zero: that is a busy loop");
  }

  // test_two_seeds_do_not_produce_the_same_delay's rule for the same property one level down: at
  // least six distinct values out of ten. A seed that is thrown away anywhere on the path gives
  // exactly one.
  uint32_t distinct = 0;
  for (uint32_t i = 0; i < N; i++) {
    bool seen = false;
    for (uint32_t j = 0; j < i; j++) {
      seen = seen || delay[j] == delay[i];
    }
    distinct += seen ? 0u : 1u;
  }
  char msg[128];
  snprintf(msg, sizeof msg, "ten devices produced %u distinct first rungs, not six or more",
           (unsigned)distinct);
  TEST_ASSERT_TRUE_MESSAGE(distinct >= 6u, msg);

  // And the seed is a FUNCTION of the id: the same device answers the same from boot to boot,
  // which is what makes the hardware scenarios reproducible, and different ids differ.
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(comfoair_mqtt_backoff_seed(IDS[0]),
                                   comfoair_mqtt_backoff_seed("24a160ff1234"),
                                   "the seed of one device changed between two calls");
  TEST_ASSERT_TRUE_MESSAGE(comfoair_mqtt_backoff_seed(IDS[0]) != comfoair_mqtt_backoff_seed(IDS[1]),
                           "two device ids one byte apart share a ladder");
  // A caller with nothing to say is answered rather than dereferenced.
  TEST_ASSERT_EQUAL_UINT32(comfoair_mqtt_backoff_seed(NULL), comfoair_mqtt_backoff_seed(""));
}

// The bit layout of the failure word is the last seam on the path from esp-mqtt to the
// ladder that nothing checked. The offsets were right when they were written -- but FAILED at bit
// 10 instead of 11 collides with the low bit of the kind, and then every transport failure is
// classified `none` FOR EVER: the suite stays green, the ladder still climbs, /api/status still
// renders, and the word it prints is wrong for the life of the product.
//
// The sweep is over the whole product of what a handler can observe, and it also asks the two
// questions that are not round trips: an empty word means "nothing has failed", and a word with
// bits nobody has defined still yields a kind inside the enumeration.
void test_the_failure_word_survives_a_round_trip(void)
{
  const uint32_t KINDS = (uint32_t)COMFOAIR_MQTT_ERR_REFUSED + 1u;
  for (uint32_t k = 0; k < KINDS; k++) {
    for (uint32_t code = 0; code < 7u; code++) {
      for (int unresolved = 0; unresolved < 2; unresolved++) {
        for (int was = 0; was < 2; was++) {
          const uint32_t word = comfoair_mqtt_failword_pack((comfoair_mqtt_err_kind_t)k, code,
                                                            unresolved != 0, was != 0);
          comfoair_mqtt_ladder_in_t in{};
          comfoair_mqtt_failword_unpack(word, &in);
          char msg[160];
          snprintf(msg, sizeof msg, "kind %u connack %u unresolved %d was_connected %d did not "
                                    "survive the word", (unsigned)k, (unsigned)code, unresolved, was);
          TEST_ASSERT_EQUAL_INT_MESSAGE(k, (uint32_t)in.kind, msg);
          TEST_ASSERT_EQUAL_UINT32_MESSAGE(code, in.connack, msg);
          TEST_ASSERT_EQUAL_INT_MESSAGE(unresolved != 0, in.name_unresolved, msg);
          TEST_ASSERT_EQUAL_INT_MESSAGE(was != 0, in.was_connected, msg);
          // Packing is only ever done for a failure, so this is not a field but a post-condition.
          TEST_ASSERT_TRUE_MESSAGE(in.failed, "a packed failure did not say that anything failed");
        }
      }
    }
  }

  // An empty word is the state after MQTT_EVENT_CONNECTED and after MQTT_EVENT_BEFORE_CONNECT: it
  // must say that nothing has failed, or the ladder arms a rung over a live attempt.
  comfoair_mqtt_ladder_in_t empty{};
  empty.failed = true;
  comfoair_mqtt_failword_unpack(0u, &empty);
  TEST_ASSERT_FALSE_MESSAGE(empty.failed, "an empty failure word reported a failure");
  TEST_ASSERT_EQUAL_INT(COMFOAIR_MQTT_ERR_NONE, empty.kind);
  TEST_ASSERT_EQUAL_UINT32(0u, empty.connack);

  // And a word full of bits nobody defined stays inside the field: the kind cannot pick up a
  // neighbour's bit, which is the collision this case exists for. It need not be a value of the
  // enumeration -- classify() answers NONE for one that is not, and backoff_ms() gives it the fast
  // row -- but it must not be some OTHER class.
  comfoair_mqtt_ladder_in_t junk{};
  comfoair_mqtt_failword_unpack(0xffffffffu, &junk);
  TEST_ASSERT_TRUE_MESSAGE((uint32_t)junk.kind <= COMFOAIR_MQTT_FAILWORD_KIND_MASK,
                           "an undefined bit reached the error kind");
  comfoair_mqtt_signal_t wild{};
  wild.kind = junk.kind;
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_MQTT_FAIL_NONE, comfoair_mqtt_classify(&wild),
                                "a kind outside the enumeration was given a failure class");
  comfoair_mqtt_failword_unpack(0u, NULL);
}

// TWO SYMPTOMS OF ONE DEFECT in be70cbd. The device half of the fix is one branch in
// comfoair_mqtt_event.c -- MQTT_EVENT_BEFORE_CONNECT clears the failure word,
// mqtt_client.c:1702-1703 -- and cannot run here. What CAN run here, and is what actually went
// wrong, is the protocol between that branch and the ladder: while a dial is in flight the ladder
// is told "nothing has failed", and it must then arm nothing, count nothing and say `connecting`.
//
// Before the fix only the FIRST dial of a boot behaved that way, because the word survived from
// the previous failure. From the second onwards the ladder armed a rung over a live attempt --
// link "backoff" with a countdown while esp-mqtt was inside a connect -- and, once that rung
// expired, called esp_mqtt_client_reconnect() once a second for the rest of the dial. That function
// logs a line BEFORE it checks the client's state (mqtt_client.c:1901), so a twenty-second dial
// wrote twenty lines into an eighty-line ring and made the ten-minute reading of GET /api/log in
// hardware scenario HV-MQTT-16 (docs/hardware-verification/06-mqtt.md) impossible.
//
// The trace this prints is the evidence for both halves, on the third dial and not the first.
void test_no_rung_is_armed_over_any_dial_not_just_the_first(void)
{
  comfoair_mqtt_ladder_t    st{};
  comfoair_mqtt_ladder_in_t in = healthy(1000u);
  // A dial that reaches the full network timeout: esp-mqtt spends it twice per attempt
  // (mqtt_client.c:1705-1708 and :1715), and this is the black hole of scenario HV-MQTT-18.
  const uint32_t IN_FLIGHT_TICKS = 20u;
  uint32_t       calls_into_esp_mqtt = 0;

  for (uint32_t dial = 1; dial <= 4u; dial++) {
    // The client is dialling. MQTT_EVENT_BEFORE_CONNECT has just emptied the word.
    in.failed          = false;
    in.kind            = COMFOAIR_MQTT_ERR_NONE;
    in.name_unresolved = false;
    for (uint32_t n = 0; n < IN_FLIGHT_TICKS; n++) {
      const comfoair_mqtt_ladder_out_t out = tick(&st, &in);
      calls_into_esp_mqtt += out.reconnect ? 1u : 0u;
      if (n == 0u || n == IN_FLIGHT_TICKS - 1u) {
        printf("  dial %u  t=%6u  inflight  link=%-10s retry=%-6s attempts=%u\n", (unsigned)dial,
               (unsigned)in.now_ms, comfoair_mqtt_link_name(out.link),
               out.retry_known ? "set" : "null", (unsigned)out.attempts);
      }
      char msg[160];
      snprintf(msg, sizeof msg, "dial %u tick %u: a rung was armed over an attempt in flight",
               (unsigned)dial, (unsigned)n);
      TEST_ASSERT_FALSE_MESSAGE(out.retry_known, msg);
      TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_MQTT_LINK_CONNECTING, out.link, msg);
      TEST_ASSERT_FALSE_MESSAGE(out.reconnect, msg);
    }

    // The dial fails. Now, and only now, a rung is armed.
    transport_failure(&in, false);
    const comfoair_mqtt_ladder_out_t armed = tick(&st, &in);
    printf("  dial %u  t=%6u  FAILED    link=%-10s retry=%-6u attempts=%u\n", (unsigned)dial,
           (unsigned)in.now_ms, comfoair_mqtt_link_name(armed.link), (unsigned)armed.retry_in_ms,
           (unsigned)armed.attempts);
    TEST_ASSERT_TRUE_MESSAGE(armed.retry_known, "a failure armed no rung");
    TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_MQTT_LINK_BACKOFF, armed.link,
                                  "a rung being waited out is backoff");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(dial, armed.attempts,
                                     "the attempt count is not one per dial");
    run_until_dial(&st, &in, 120u, "the rung after a failed dial");
    calls_into_esp_mqtt++;
  }

  // The log flood, as a number: four dials, four calls into esp-mqtt, so four of its log lines --
  // not the eighty-plus that one call per tick of four twenty-second dials would have written.
  printf("  calls into esp_mqtt_client_reconnect(): %u for 4 dials\n",
         (unsigned)calls_into_esp_mqtt);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(4u, calls_into_esp_mqtt,
                                   "esp_mqtt_client_reconnect() was called more than once a dial");
}
