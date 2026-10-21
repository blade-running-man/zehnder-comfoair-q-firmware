// The MQTT retry ladder as a table a test can read: the six failure classes, the two rows and the
// flat rung, and the reset rule, as docs/interfaces/mqtt.md (Reconnect and backoff) describes them.
//
// The rows below are TRANSCRIBED from the table in that page and are never read back from the
// implementation: a table that answers a test with its own contents proves nothing
// (test/test_led_pattern/test_led_pattern.cpp:6-7). Shifting the implementation's row by one rung
// has to fail here, and test_the_ladder_is_the_table asserts the rung it is NOT on for exactly
// that reason.
//
// THIS FILE INCLUDES NOT ONE ESP-IDF HEADER, and the omission is itself an assertion:
// the decider is a pure function of its arguments -- no clock, no randomness, no client handle --
// which is the only reason "the sixteenth attempt" and "the flat rung's lower edge" are cases here
// instead of things somebody once watched in a log.
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "comfoair_mqtt_backoff.h"

// The SECOND reader of the CONNACK code space (comfoair_phase.h:172-192,
// comfoair_wire_render.c:144-160). Included by the TEST and nowhere in the component -- which keeps
// an empty REQUIRES to build on the host -- so that "DO NOT teach one of them a code without the
// others" is a check rather than a hope. test_the_refusal_classes_agree_with_the_phase_reader is
// what it buys.
#include "comfoair_phase.h"

// No esp_random(): the MAC seeds the jitter, so a device keeps one ladder from boot to boot.
// Defined here so that the day someone reaches for it
// the suite says WHICH rule broke, instead of the linker saying "undefined reference to
// esp_random" in an environment that has no ESP-IDF. Never called by a correct implementation.
extern "C" uint32_t esp_random(void);
extern "C" uint32_t esp_random(void)
{
  TEST_FAIL_MESSAGE("the decider called esp_random(): the jitter is seeded from the MAC");
  return 0;
}

void setUp(void) {}
void tearDown(void) {}

// The table, transcribed. Seven rungs because the table's rows are six long plus the ceiling
// they hold at, and because the DROPPED row is the fast row shifted down by one -- seven is the
// first length at which both rows have reached 60 000 and can be compared there.
static const uint32_t ROWS = 7;
static const uint32_t FAST[ROWS] = {2000u, 4000u, 8000u, 16000u, 32000u, 60000u, 60000u};
static const uint32_t DROP[ROWS] = {1000u, 2000u, 4000u,  8000u, 16000u, 32000u, 60000u};
static const uint32_t FLAT[ROWS] = {300000u, 300000u, 300000u, 300000u, 300000u, 300000u, 300000u};

// The attempt counter saturates at 16.
static const uint32_t SATURATE_AT = 16u;

// Ten "MAC addresses". Ten because the anti-storm property IS the spread
// across devices; one seed everywhere would let a constant multiplier pass every band check in
// this file.
static const uint32_t SEEDS[] = {0x24a160ffu, 0xdeadbeefu, 0x0c8a1bc4u, 0x5a5a5a5au,
                                 0xffffffffu, 0u,          1u,          7u,
                                 12345u,      0x98f4abc0u};
static const uint32_t SEED_N = (uint32_t)(sizeof SEEDS / sizeof SEEDS[0]);

// The row a class climbs. A switch and not a table lookup: -Werror=switch (platformio.ini) then
// makes a seventh failure class a compile error in the suite that is supposed to pin its ladder,
// instead of a silently untested enumerator.
static const uint32_t *row_of(comfoair_mqtt_fail_t why)
{
  switch (why) {
  case COMFOAIR_MQTT_FAIL_DNS:
  case COMFOAIR_MQTT_FAIL_TCP:
  case COMFOAIR_MQTT_FAIL_UNAVAILABLE: return FAST;
  case COMFOAIR_MQTT_FAIL_DROPPED:     return DROP;
  case COMFOAIR_MQTT_FAIL_PROTOCOL:
  case COMFOAIR_MQTT_FAIL_REJECTED:    return FLAT;
  case COMFOAIR_MQTT_FAIL_NONE:
  case COMFOAIR_MQTT_FAIL_COUNT:       break;
  }
  return NULL;
}

// The jitter rule: delay = base x (80 + hash mod 41) / 100. Both edges are inclusive and both are
// checked -- a band written as ">= 0.8 x base" alone passes an implementation that never jitters
// downward at all.
static void in_band(uint32_t base, uint32_t got, const char *what, uint32_t seed, uint32_t attempt)
{
  char msg[192];
  snprintf(msg, sizeof msg, "%s: attempt %u seed %08x gave %u ms, not %u ms +/-20%%",
           what, (unsigned)attempt, (unsigned)seed, (unsigned)got, (unsigned)base);
  TEST_ASSERT_TRUE_MESSAGE(got >= base * 80u / 100u, msg);
  TEST_ASSERT_TRUE_MESSAGE(got <= base * 120u / 100u, msg);
}

static comfoair_mqtt_signal_t sig(comfoair_mqtt_err_kind_t kind, uint32_t connack,
                                  bool unresolved, bool was_connected)
{
  comfoair_mqtt_signal_t s{};
  s.kind = kind;
  s.connack = connack;
  s.name_unresolved = unresolved;
  s.was_connected = was_connected;
  return s;
}

static comfoair_mqtt_fail_t classify(comfoair_mqtt_err_kind_t kind, uint32_t connack,
                                     bool unresolved, bool was_connected)
{
  comfoair_mqtt_signal_t s = sig(kind, connack, unresolved, was_connected);
  return comfoair_mqtt_classify(&s);
}

// ---------------------------------------------------------------------------------------------
// The nine properties of the ladder, one case each.
// ---------------------------------------------------------------------------------------------

// Every class, every rung of the transcribed table, every seed. The second half of each iteration
// is what makes the table load-bearing: the answer must also be OUTSIDE the band of the
// neighbouring rungs, so an implementation that starts one step up or one step down cannot pass by
// sitting in an overlap. The rows are a factor of two apart, so the bands are disjoint by
// construction, and asserting it says which mutation this case is for.
static void test_the_ladder_is_the_table(void)
{
  for (int c = COMFOAIR_MQTT_FAIL_DNS; c < COMFOAIR_MQTT_FAIL_COUNT; c++) {
    comfoair_mqtt_fail_t why = (comfoair_mqtt_fail_t)c;
    const uint32_t *row = row_of(why);
    TEST_ASSERT_NOT_NULL_MESSAGE(row, "a failure class with no row in the table");
    for (uint32_t n = 0; n < ROWS; n++) {
      for (uint32_t i = 0; i < SEED_N; i++) {
        uint32_t got = comfoair_mqtt_backoff_ms(why, n, SEEDS[i]);
        in_band(row[n], got, comfoair_mqtt_fail_name(why), SEEDS[i], n);
        if (n + 1u < ROWS && row[n + 1u] != row[n]) {
          TEST_ASSERT_TRUE_MESSAGE(got < row[n + 1u] * 80u / 100u,
                                   "the ladder is one rung too high: a row shifted up");
        }
        if (n > 0u && row[n - 1u] != row[n]) {
          TEST_ASSERT_TRUE_MESSAGE(got > row[n - 1u] * 120u / 100u,
                                   "the ladder is one rung too low: a row shifted down");
        }
      }
    }
  }
}

// The attempt counter saturates at 16. Observable ONLY through the jitter -- every row
// is already sitting on its ceiling long before rung 16 -- which is why the number can only be
// pinned by also asserting that rung 15 is still distinguishable from rung 16. Saturating one
// rung early would otherwise be invisible.
static void test_attempt_saturates_at_sixteen(void)
{
  const uint32_t beyond[] = {17u, 100u, 1000u, 10000u, 0xffffffffu};
  bool fifteen_differs = false;
  for (int c = COMFOAIR_MQTT_FAIL_DNS; c < COMFOAIR_MQTT_FAIL_COUNT; c++) {
    comfoair_mqtt_fail_t why = (comfoair_mqtt_fail_t)c;
    for (uint32_t i = 0; i < SEED_N; i++) {
      uint32_t at_cap = comfoair_mqtt_backoff_ms(why, SATURATE_AT, SEEDS[i]);
      for (uint32_t b = 0; b < sizeof beyond / sizeof beyond[0]; b++) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(at_cap,
                                         comfoair_mqtt_backoff_ms(why, beyond[b], SEEDS[i]),
                                         "attempt did not saturate at 16");
      }
      if (comfoair_mqtt_backoff_ms(why, SATURATE_AT - 1u, SEEDS[i]) != at_cap) {
        fifteen_differs = true;
      }
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(fifteen_differs,
                           "attempt 15 and attempt 16 are one value: the ladder saturates early");
}

// Three properties in one, because they are one rule: the same seed always gives the
// same millisecond (the hardware scenarios are otherwise unreproducible), the answer never leaves
// [0.8, 1.2] x base, and BOTH edges of that band are actually reached -- an implementation that
// only ever multiplies by 100 satisfies every other case in this file.
static void test_the_jitter_is_deterministic_and_twenty_percent(void)
{
  for (int c = COMFOAIR_MQTT_FAIL_DNS; c < COMFOAIR_MQTT_FAIL_COUNT; c++) {
    comfoair_mqtt_fail_t why = (comfoair_mqtt_fail_t)c;
    const uint32_t *row = row_of(why);
    for (uint32_t n = 0; n <= SATURATE_AT; n++) {
      for (uint32_t i = 0; i < SEED_N; i++) {
        uint32_t a = comfoair_mqtt_backoff_ms(why, n, SEEDS[i]);
        uint32_t b = comfoair_mqtt_backoff_ms(why, n, SEEDS[i]);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(a, b, "two calls, one input, two answers");
        TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(0u, a, "a zero delay is a reconnect busy-loop");
        in_band(row[n < ROWS ? n : ROWS - 1u], a, comfoair_mqtt_fail_name(why), SEEDS[i], n);
      }
    }
  }

  // Every base in the table is a multiple of 100, so base/100 x percent is exact and the
  // percentage can be recovered without reimplementing the hash here.
  bool seen[41] = {false};
  uint32_t distinct = 0;
  for (uint32_t seed = 0; seed < 4000u; seed++) {
    uint32_t got = comfoair_mqtt_backoff_ms(COMFOAIR_MQTT_FAIL_TCP, 0u, seed);
    uint32_t pct = got * 100u / FAST[0];
    TEST_ASSERT_TRUE_MESSAGE(pct >= 80u && pct <= 120u, "the jitter left the +/-20% band");
    if (!seen[pct - 80u]) { seen[pct - 80u] = true; distinct++; }
  }
  TEST_ASSERT_TRUE_MESSAGE(seen[0], "the jitter never reached its lower edge, 0.8 x base");
  TEST_ASSERT_TRUE_MESSAGE(seen[40], "the jitter never reached its upper edge, 1.2 x base");
  TEST_ASSERT_TRUE_MESSAGE(distinct >= 35u, "the jitter collapses onto a handful of values");
}

// "The broker has accepted us before" and "we have never reached it" are different facts and keep
// different first rungs; one shared rung would erase the distinction for no gain. It is paid for --
// the two rows stay exactly one rung apart all the way up, and meet only on the ceiling, where
// 64 000 would have been.
static void test_a_dropped_session_starts_a_second_faster(void)
{
  for (uint32_t i = 0; i < SEED_N; i++) {
    uint32_t dropped = comfoair_mqtt_backoff_ms(COMFOAIR_MQTT_FAIL_DROPPED, 0u, SEEDS[i]);
    uint32_t unreach = comfoair_mqtt_backoff_ms(COMFOAIR_MQTT_FAIL_TCP, 0u, SEEDS[i]);
    in_band(1000u, dropped, "dropped", SEEDS[i], 0u);
    in_band(2000u, unreach, "tcp", SEEDS[i], 0u);
    TEST_ASSERT_TRUE_MESSAGE(dropped < unreach, "the two first rungs were merged into one");

    // One rung apart, exactly, while both rows are still doubling: the same seed and attempt give
    // the same multiplier, so the ratio survives the jitter.
    for (uint32_t n = 0; n < 5u; n++) {
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(
          comfoair_mqtt_backoff_ms(COMFOAIR_MQTT_FAIL_TCP, n, SEEDS[i]),
          2u * comfoair_mqtt_backoff_ms(COMFOAIR_MQTT_FAIL_DROPPED, n, SEEDS[i]),
          "the dropped row is not the fast row shifted by exactly one rung");
    }
    // And the ceiling is shared: it is a ceiling on the delay, not a different number per row.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        comfoair_mqtt_backoff_ms(COMFOAIR_MQTT_FAIL_TCP, 6u, SEEDS[i]),
        comfoair_mqtt_backoff_ms(COMFOAIR_MQTT_FAIL_DROPPED, 6u, SEEDS[i]),
        "the two rows do not meet on the 60 s ceiling");
  }
}

// A credential refusal waits five minutes -- a broker hammered with a wrong password may ban the
// address -- as a number: 300 000 ms from the FIRST attempt, and flat: attempt 0 and attempt 7 are
// the same rung. The second half: the flat rung carries the SAME +/-20 % as every row and not a
// jitter of its own, so the spread has to be wider than +/-30 s. Without that assertion
// "300 s +/- 30 s" passes the band check silently.
static void test_a_refusal_waits_five_minutes_flat(void)
{
  for (uint32_t n = 0; n <= SATURATE_AT; n++) {
    for (uint32_t i = 0; i < SEED_N; i++) {
      in_band(300000u, comfoair_mqtt_backoff_ms(COMFOAIR_MQTT_FAIL_REJECTED, n, SEEDS[i]),
              "bad-credentials", SEEDS[i], n);
    }
  }
  bool below_thirty_s = false, above_thirty_s = false;
  for (uint32_t seed = 0; seed < 4000u; seed++) {
    uint32_t got = comfoair_mqtt_backoff_ms(COMFOAIR_MQTT_FAIL_REJECTED, 0u, seed);
    if (got < 300000u - 30000u) below_thirty_s = true;
    if (got > 300000u + 30000u) above_thirty_s = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(below_thirty_s && above_thirty_s,
                           "the flat rung carries its own +/-30 s instead of the +/-20 % rule");
}

// CONNACK 1 and 2 send the owner to the broker's settings, the same action as 4 and 5, so they
// wait as long -- and they are STILL a different class, because /api/status must say
// "protocol" where it would otherwise say "bad-credentials" and send him hunting for a password
// that is correct.
static void test_a_protocol_refusal_waits_as_long_as_a_rejection_but_is_a_different_class(void)
{
  for (uint32_t n = 0; n <= SATURATE_AT; n++) {
    for (uint32_t i = 0; i < SEED_N; i++) {
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(
          comfoair_mqtt_backoff_ms(COMFOAIR_MQTT_FAIL_REJECTED, n, SEEDS[i]),
          comfoair_mqtt_backoff_ms(COMFOAIR_MQTT_FAIL_PROTOCOL, n, SEEDS[i]),
          "a protocol refusal and a credential refusal wait different lengths");
    }
  }
  TEST_ASSERT_EQUAL_INT(COMFOAIR_MQTT_FAIL_PROTOCOL, classify(COMFOAIR_MQTT_ERR_REFUSED, 1, false, false));
  TEST_ASSERT_EQUAL_INT(COMFOAIR_MQTT_FAIL_PROTOCOL, classify(COMFOAIR_MQTT_ERR_REFUSED, 2, false, false));
  TEST_ASSERT_EQUAL_INT(COMFOAIR_MQTT_FAIL_REJECTED, classify(COMFOAIR_MQTT_ERR_REFUSED, 4, false, false));
  TEST_ASSERT_EQUAL_INT(COMFOAIR_MQTT_FAIL_REJECTED, classify(COMFOAIR_MQTT_ERR_REFUSED, 5, false, false));
  TEST_ASSERT_NOT_EQUAL_INT_MESSAGE(COMFOAIR_MQTT_FAIL_PROTOCOL, COMFOAIR_MQTT_FAIL_REJECTED,
                                    "the two refusals were merged into one class");
  // The `mqtt.failure` words of /api/status: the whole point of keeping the classes apart.
  TEST_ASSERT_EQUAL_STRING("protocol", comfoair_mqtt_fail_name(COMFOAIR_MQTT_FAIL_PROTOCOL));
  TEST_ASSERT_EQUAL_STRING("bad-credentials", comfoair_mqtt_fail_name(COMFOAIR_MQTT_FAIL_REJECTED));
}

// CONNACK 3 is what a Mosquitto add-on answers in the first seconds
// after a restart. On the slow rung a five-second restart becomes a five-minute hole in the
// owner's Home Assistant graphs, so 3 climbs the fast row and shares it with "no route to the
// broker at all".
static void test_connack_three_climbs_the_fast_ladder(void)
{
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_MQTT_FAIL_UNAVAILABLE,
                                classify(COMFOAIR_MQTT_ERR_REFUSED, 3, false, false),
                                "CONNACK 3 was sorted with the refusals");
  for (uint32_t n = 0; n < ROWS; n++) {
    for (uint32_t i = 0; i < SEED_N; i++) {
      uint32_t got = comfoair_mqtt_backoff_ms(COMFOAIR_MQTT_FAIL_UNAVAILABLE, n, SEEDS[i]);
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(comfoair_mqtt_backoff_ms(COMFOAIR_MQTT_FAIL_TCP, n, SEEDS[i]),
                                       got, "CONNACK 3 does not share the fast row");
      in_band(FAST[n], got, "unavailable", SEEDS[i], n);
      TEST_ASSERT_TRUE_MESSAGE(got < 300000u * 80u / 100u,
                               "a five-second broker restart became a five-minute hole");
    }
  }
}

// The ladder is reset by a session that LASTED, not by a connection that happened. A
// broker that accepts and drops 200 ms later -- a duplicate client id, an exhausted
// max_connections, an ACL that cuts after CONNACK -- would otherwise give an endless cycle of
// one-second reconnects, each one republishing the whole state.
static void test_the_ladder_resets_only_after_a_minute_of_session(void)
{
  TEST_ASSERT_FALSE_MESSAGE(comfoair_mqtt_backoff_settled(0u), "a connection is not a session");
  TEST_ASSERT_FALSE_MESSAGE(comfoair_mqtt_backoff_settled(200u),
                            "a broker that drops after 200 ms resets the ladder");
  TEST_ASSERT_FALSE_MESSAGE(comfoair_mqtt_backoff_settled(59999u), "settled(59 999) must be false");
  TEST_ASSERT_TRUE_MESSAGE(comfoair_mqtt_backoff_settled(60000u), "settled(60 000) must be true");
  TEST_ASSERT_TRUE(comfoair_mqtt_backoff_settled(60001u));
  TEST_ASSERT_TRUE(comfoair_mqtt_backoff_settled(0xffffffffu));
}

// link_of() does not turn the decider into "what to show" -- it stays a pure function
// of the same input. The sweep is over the WHOLE input space that reaches it, and its extent is
// written in terms of the enumerations it sweeps, so a seventh error kind or an eighth CONNACK
// meaning widens the sweep instead of slipping past it.
static void test_link_of_is_a_pure_function_of_the_same_input(void)
{
  const uint32_t KINDS   = (uint32_t)COMFOAIR_MQTT_ERR_REFUSED + 1u;
  const uint32_t CONNACK = 7u;   // 0..5 plus one code outside the specification
  bool reached[COMFOAIR_MQTT_LINK_COUNT] = {false};
  uint32_t swept = 0;

  for (uint32_t k = 0; k < KINDS; k++) {
    for (uint32_t code = 0; code < CONNACK; code++) {
      for (int unresolved = 0; unresolved < 2; unresolved++) {
        for (int was = 0; was < 2; was++) {
          for (int configured = 0; configured < 2; configured++) {
            for (int connected = 0; connected < 2; connected++) {
              for (int scheduled = 0; scheduled < 2; scheduled++) {
                comfoair_mqtt_signal_t s = sig((comfoair_mqtt_err_kind_t)k, code,
                                               unresolved != 0, was != 0);
                s.configured = configured != 0;
                s.connected = connected != 0;
                s.retry_scheduled = scheduled != 0;
                comfoair_mqtt_link_t a = comfoair_mqtt_link_of(&s);
                TEST_ASSERT_EQUAL_INT_MESSAGE(a, comfoair_mqtt_link_of(&s),
                                              "link_of answered twice differently for one input");
                TEST_ASSERT_TRUE_MESSAGE(a < COMFOAIR_MQTT_LINK_COUNT, "link_of left its enum");
                reached[a] = true;
                swept++;
              }
            }
          }
        }
      }
    }
  }
  TEST_ASSERT_EQUAL_UINT32(KINDS * CONNACK * 32u, swept);

  // The five words of `mqtt.link`, each reachable, each spelled once.
  TEST_ASSERT_TRUE_MESSAGE(reached[COMFOAIR_MQTT_LINK_OFF], "no input gives off");
  TEST_ASSERT_TRUE_MESSAGE(reached[COMFOAIR_MQTT_LINK_CONNECTING], "no input gives connecting");
  TEST_ASSERT_TRUE_MESSAGE(reached[COMFOAIR_MQTT_LINK_ONLINE], "no input gives online");
  TEST_ASSERT_TRUE_MESSAGE(reached[COMFOAIR_MQTT_LINK_BACKOFF], "no input gives backoff");
  TEST_ASSERT_TRUE_MESSAGE(reached[COMFOAIR_MQTT_LINK_REJECTED], "no input gives rejected");

  // And the four rules, named, so a sweep that goes green on a constant cannot pass.
  comfoair_mqtt_signal_t s = sig(COMFOAIR_MQTT_ERR_NONE, 0, false, false);
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_MQTT_LINK_OFF, comfoair_mqtt_link_of(&s),
                                "a device with no broker configured is not off");
  s.configured = true;
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_MQTT_LINK_CONNECTING, comfoair_mqtt_link_of(&s),
                                "an attempt in flight is connecting");
  s.retry_scheduled = true;
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_MQTT_LINK_BACKOFF, comfoair_mqtt_link_of(&s),
                                "a scheduled retry is backoff, not connecting");
  s.connected = true;
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_MQTT_LINK_ONLINE, comfoair_mqtt_link_of(&s),
                                "a connected broker is online whatever else is set");
  s = sig(COMFOAIR_MQTT_ERR_REFUSED, 5, false, false);
  s.configured = true;
  s.retry_scheduled = true;
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_MQTT_LINK_REJECTED, comfoair_mqtt_link_of(&s),
                                "a refused device shows generic backoff: a refusal needs its own");
  // NULL is off and never online: a caller with nothing to say has no broker, and answering
  // online there would make the quietest bug look like the healthiest device.
  TEST_ASSERT_EQUAL_INT(COMFOAIR_MQTT_LINK_OFF, comfoair_mqtt_link_of(NULL));
}

// ---------------------------------------------------------------------------------------------
// The remaining cases.
// ---------------------------------------------------------------------------------------------

// Overflow protection. The shift that builds the row must never run off the top of a
// uint32_t, whatever attempt number a saturating counter is handed before it saturates.
static void test_the_ceiling_holds_for_every_attempt_number(void)
{
  for (int c = COMFOAIR_MQTT_FAIL_NONE; c < COMFOAIR_MQTT_FAIL_COUNT; c++) {
    for (uint32_t n = 0; n <= 10000u; n++) {
      uint32_t got = comfoair_mqtt_backoff_ms((comfoair_mqtt_fail_t)c, n, SEEDS[n % SEED_N]);
      TEST_ASSERT_TRUE_MESSAGE(got <= 300000u * 120u / 100u, "a delay above the flat rung's band");
      TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(0u, got, "a zero delay is a reconnect busy-loop");
    }
  }
  // The four rows that have a 60 s ceiling never approach the flat rung's.
  const comfoair_mqtt_fail_t fast[] = {COMFOAIR_MQTT_FAIL_DNS, COMFOAIR_MQTT_FAIL_TCP,
                                       COMFOAIR_MQTT_FAIL_UNAVAILABLE, COMFOAIR_MQTT_FAIL_DROPPED};
  for (uint32_t f = 0; f < 4u; f++) {
    for (uint32_t n = 0; n <= 10000u; n++) {
      TEST_ASSERT_TRUE_MESSAGE(comfoair_mqtt_backoff_ms(fast[f], n, SEEDS[n % SEED_N]) <= 72000u,
                               "the 60 s ceiling was passed");
    }
  }
}

// The whole point of seeding from the MAC: ten devices, one class, one attempt,
// and the answers must not line up. A house of ten nodes waking together after a power cut is the
// case this buys.
static void test_two_seeds_do_not_produce_the_same_delay(void)
{
  uint32_t seen[SEED_N];
  uint32_t distinct = 0;
  for (uint32_t i = 0; i < SEED_N; i++) {
    uint32_t got = comfoair_mqtt_backoff_ms(COMFOAIR_MQTT_FAIL_TCP, 3u, SEEDS[i]);
    bool dup = false;
    for (uint32_t j = 0; j < distinct; j++) { if (seen[j] == got) dup = true; }
    if (!dup) { seen[distinct++] = got; }
  }
  TEST_ASSERT_TRUE_MESSAGE(distinct >= 6u, "ten MAC addresses gave fewer than six delays");
}

// The transport error is what separates "the name did not resolve" from "the name
// resolved and nothing answered". The two share a row, so the only thing riding on the split is
// the word in /api/status -- which is the difference between the owner editing the host field and
// the owner starting his broker.
static void test_dns_failure_is_classified_from_the_transport_error(void)
{
  TEST_ASSERT_EQUAL_INT(COMFOAIR_MQTT_FAIL_DNS, classify(COMFOAIR_MQTT_ERR_TRANSPORT, 0, true, false));
  TEST_ASSERT_EQUAL_INT(COMFOAIR_MQTT_FAIL_TCP, classify(COMFOAIR_MQTT_ERR_TRANSPORT, 0, false, false));
  // A stale CONNACK left in an atomic from an earlier attempt must not colour a transport error.
  TEST_ASSERT_EQUAL_INT(COMFOAIR_MQTT_FAIL_TCP, classify(COMFOAIR_MQTT_ERR_TRANSPORT, 5, false, false));
  TEST_ASSERT_EQUAL_INT(COMFOAIR_MQTT_FAIL_DNS, classify(COMFOAIR_MQTT_ERR_TRANSPORT, 4, true, false));
  TEST_ASSERT_EQUAL_STRING("dns", comfoair_mqtt_fail_name(COMFOAIR_MQTT_FAIL_DNS));
  TEST_ASSERT_EQUAL_STRING("tcp", comfoair_mqtt_fail_name(COMFOAIR_MQTT_FAIL_TCP));
}

// was_connected wins over everything. The handler's last_connack is a LATCH
// (comfoair_mqtt.h:86-90), so an established session that drops still has the old refusal code
// lying in the atomic; reading that as "the broker refused our password" would put a live
// installation on the five-minute rung.
static void test_a_disconnect_after_a_connect_is_a_drop_whatever_the_error_says(void)
{
  for (uint32_t code = 0; code < 7u; code++) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_MQTT_FAIL_DROPPED,
                                  classify(COMFOAIR_MQTT_ERR_REFUSED, code, false, true),
                                  "a stale CONNACK outranked an established session");
  }
  TEST_ASSERT_EQUAL_INT(COMFOAIR_MQTT_FAIL_DROPPED, classify(COMFOAIR_MQTT_ERR_NONE, 0, false, true));
  TEST_ASSERT_EQUAL_INT(COMFOAIR_MQTT_FAIL_DROPPED, classify(COMFOAIR_MQTT_ERR_TRANSPORT, 0, true, true));
  // And with no session behind it, silence is not a failure at all.
  TEST_ASSERT_EQUAL_INT(COMFOAIR_MQTT_FAIL_NONE, classify(COMFOAIR_MQTT_ERR_NONE, 0, false, false));
  TEST_ASSERT_EQUAL_INT(COMFOAIR_MQTT_FAIL_NONE, comfoair_mqtt_classify(NULL));
}

// The CONNACK table, read as the code space it sorts: 0 is no refusal, 3 is temporary, 1 and 2
// are the broker's settings, 4, 5 and everything outside 0..5 are the credentials.
static void test_connack_codes_split_into_three_classes(void)
{
  TEST_ASSERT_EQUAL_INT(COMFOAIR_MQTT_FAIL_NONE,        classify(COMFOAIR_MQTT_ERR_REFUSED, 0, false, false));
  TEST_ASSERT_EQUAL_INT(COMFOAIR_MQTT_FAIL_PROTOCOL,    classify(COMFOAIR_MQTT_ERR_REFUSED, 1, false, false));
  TEST_ASSERT_EQUAL_INT(COMFOAIR_MQTT_FAIL_PROTOCOL,    classify(COMFOAIR_MQTT_ERR_REFUSED, 2, false, false));
  TEST_ASSERT_EQUAL_INT(COMFOAIR_MQTT_FAIL_UNAVAILABLE, classify(COMFOAIR_MQTT_ERR_REFUSED, 3, false, false));
  TEST_ASSERT_EQUAL_INT(COMFOAIR_MQTT_FAIL_REJECTED,    classify(COMFOAIR_MQTT_ERR_REFUSED, 4, false, false));
  TEST_ASSERT_EQUAL_INT(COMFOAIR_MQTT_FAIL_REJECTED,    classify(COMFOAIR_MQTT_ERR_REFUSED, 5, false, false));
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_MQTT_FAIL_REJECTED,
                                classify(COMFOAIR_MQTT_ERR_REFUSED, 9, false, false),
                                "a code outside 0..5 is a refusal");
  for (uint32_t code = 6u; code < 300u; code++) {
    TEST_ASSERT_EQUAL_INT(COMFOAIR_MQTT_FAIL_REJECTED, classify(COMFOAIR_MQTT_ERR_REFUSED, code, false, false));
  }
  TEST_ASSERT_EQUAL_INT(COMFOAIR_MQTT_FAIL_REJECTED, classify(COMFOAIR_MQTT_ERR_REFUSED, 0xffffffffu, false, false));
}

// THE THIRD READER AGREES WITH THE SECOND. comfoair_phase_connack_is_refusal() latches
// BROKER_REJECTED for 1, 2, 4, 5 and everything outside 0..5 (comfoair_phase.c:73-79); the two
// refusal classes here are exactly that set. comfoair_phase.h:186-192 says "DO NOT teach one of
// them a code without the others" -- this case is the sentence made checkable, and it is why the
// classifier does not bring a second hand-written code table.
static void test_the_refusal_classes_agree_with_the_phase_reader(void)
{
  for (uint32_t code = 0; code < 300u; code++) {
    comfoair_mqtt_fail_t why = classify(COMFOAIR_MQTT_ERR_REFUSED, code, false, false);
    bool refused_here = (why == COMFOAIR_MQTT_FAIL_PROTOCOL || why == COMFOAIR_MQTT_FAIL_REJECTED);
    char msg[128];
    snprintf(msg, sizeof msg, "CONNACK %u: the failure class and the phase disagree", (unsigned)code);
    TEST_ASSERT_EQUAL_INT_MESSAGE(comfoair_phase_connack_is_refusal(code), refused_here, msg);
  }
  TEST_ASSERT_EQUAL_INT(comfoair_phase_connack_is_refusal(0xffffffffu),
                        classify(COMFOAIR_MQTT_ERR_REFUSED, 0xffffffffu, false, false) == COMFOAIR_MQTT_FAIL_REJECTED);
}

// A number that is not a class must still buy a wait. Zero would be a reconnect
// busy-loop, and 1 ms would be the same thing with extra steps.
static void test_an_unknown_class_never_returns_zero(void)
{
  const comfoair_mqtt_fail_t junk[] = {(comfoair_mqtt_fail_t)COMFOAIR_MQTT_FAIL_COUNT,
                                       (comfoair_mqtt_fail_t)99, (comfoair_mqtt_fail_t)0x7fffffff};
  for (uint32_t j = 0; j < 3u; j++) {
    for (uint32_t n = 0; n <= SATURATE_AT; n++) {
      uint32_t got = comfoair_mqtt_backoff_ms(junk[j], n, SEEDS[n % SEED_N]);
      TEST_ASSERT_TRUE_MESSAGE(got >= 1000u, "an unrecognised class asked for a sub-second retry");
      TEST_ASSERT_TRUE_MESSAGE(got <= 72000u, "an unrecognised class passed the 60 s ceiling");
    }
  }
  // FAIL_NONE is inside the enumeration and means "nothing failed"; a caller that asks anyway is
  // still owed a delay, not a spin.
  TEST_ASSERT_TRUE(comfoair_mqtt_backoff_ms(COMFOAIR_MQTT_FAIL_NONE, 0u, 0u) >= 1000u);
}

// /api/status renders all three enumerations as strings. One spelling each, decided here and not in
// comfoair_wire -- the same argument as comfoair_phase_name, and comfoair_wire.h:230-236 is the
// record of what a second spelling costs. NEVER NULL, including for a number that is not a value.
static void test_every_class_has_a_name(void)
{
  const char *expect[COMFOAIR_MQTT_FAIL_COUNT] = {"none", "dns", "tcp", "unavailable",
                                                  "protocol", "bad-credentials", "dropped"};
  for (int c = 0; c < COMFOAIR_MQTT_FAIL_COUNT; c++) {
    const char *got = comfoair_mqtt_fail_name((comfoair_mqtt_fail_t)c);
    TEST_ASSERT_NOT_NULL(got);
    TEST_ASSERT_EQUAL_STRING(expect[c], got);
  }
  TEST_ASSERT_EQUAL_STRING("unknown", comfoair_mqtt_fail_name((comfoair_mqtt_fail_t)COMFOAIR_MQTT_FAIL_COUNT));
  TEST_ASSERT_EQUAL_STRING("unknown", comfoair_mqtt_fail_name((comfoair_mqtt_fail_t)77));

  const char *links[COMFOAIR_MQTT_LINK_COUNT] = {"off", "connecting", "online", "backoff", "rejected"};
  for (int l = 0; l < COMFOAIR_MQTT_LINK_COUNT; l++) {
    TEST_ASSERT_EQUAL_STRING(links[l], comfoair_mqtt_link_name((comfoair_mqtt_link_t)l));
  }
  TEST_ASSERT_EQUAL_STRING("unknown", comfoair_mqtt_link_name((comfoair_mqtt_link_t)77));

  // The four subscription words, and the table the third enumeration went without while the other
  // two had one. test_wire_status.cpp builds its expected string by calling this very function, so
  // it pins that the renderer asks the right speaker and not one letter of the answer: GRANTED
  // respelled "online" passed the whole tree, and `granted` and `online` sit in ONE block of that
  // document -- an installation whose ACL refused the SUBSCRIBE would read as a healthy one.
  const char *subs[COMFOAIR_MQTT_SUBS_COUNT] = {"none", "pending", "granted", "refused"};
  for (int s = 0; s < COMFOAIR_MQTT_SUBS_COUNT; s++) {
    TEST_ASSERT_EQUAL_STRING(subs[s], comfoair_mqtt_subs_name((comfoair_mqtt_subs_t)s));
  }
  TEST_ASSERT_EQUAL_STRING("unknown", comfoair_mqtt_subs_name((comfoair_mqtt_subs_t)77));
}

// Defined in test_mqtt_ladder.cpp (the publisher's pass) and test_mqtt_wiring.cpp (what the
// publisher must carry into it). Registered HERE, with the rest, because this directory links into
// one binary and a case registered where it is defined is a case that goes missing the next time a
// file is split -- which has now happened twice.
void test_a_live_broker_is_online_and_not_off(void);
void test_an_address_returning_does_not_burn_an_attempt(void);
void test_only_a_settled_session_resets_the_ladder(void);
void test_a_reconfigure_leaves_the_rejection_rung_at_once(void);
void test_the_counter_saturates_where_the_table_does(void);
void test_the_ladder_survives_the_millisecond_wrap(void);
void test_a_dropped_session_takes_the_dropped_row(void);
void test_a_client_that_has_not_parked_yet_costs_no_attempt(void);
void test_a_device_with_no_broker_is_off_and_dials_nothing(void);
void test_no_sequence_of_inputs_can_park_the_ladder_for_ever(void);
void test_a_fresh_client_is_left_to_dial_by_itself(void);
void test_a_flapping_broker_is_never_forgiven(void);
void test_two_devices_do_not_retry_in_lockstep(void);
void test_the_failure_word_survives_a_round_trip(void);
void test_no_rung_is_armed_over_any_dial_not_just_the_first(void);

int main(int, char **)
{
  UNITY_BEGIN();
  RUN_TEST(test_the_ladder_is_the_table);
  RUN_TEST(test_attempt_saturates_at_sixteen);
  RUN_TEST(test_the_jitter_is_deterministic_and_twenty_percent);
  RUN_TEST(test_a_dropped_session_starts_a_second_faster);
  RUN_TEST(test_a_refusal_waits_five_minutes_flat);
  RUN_TEST(test_a_protocol_refusal_waits_as_long_as_a_rejection_but_is_a_different_class);
  RUN_TEST(test_connack_three_climbs_the_fast_ladder);
  RUN_TEST(test_the_ladder_resets_only_after_a_minute_of_session);
  RUN_TEST(test_link_of_is_a_pure_function_of_the_same_input);
  RUN_TEST(test_the_ceiling_holds_for_every_attempt_number);
  RUN_TEST(test_two_seeds_do_not_produce_the_same_delay);
  RUN_TEST(test_dns_failure_is_classified_from_the_transport_error);
  RUN_TEST(test_a_disconnect_after_a_connect_is_a_drop_whatever_the_error_says);
  RUN_TEST(test_connack_codes_split_into_three_classes);
  RUN_TEST(test_the_refusal_classes_agree_with_the_phase_reader);
  RUN_TEST(test_an_unknown_class_never_returns_zero);
  RUN_TEST(test_every_class_has_a_name);
  RUN_TEST(test_a_live_broker_is_online_and_not_off);
  RUN_TEST(test_an_address_returning_does_not_burn_an_attempt);
  RUN_TEST(test_only_a_settled_session_resets_the_ladder);
  RUN_TEST(test_a_reconfigure_leaves_the_rejection_rung_at_once);
  RUN_TEST(test_the_counter_saturates_where_the_table_does);
  RUN_TEST(test_the_ladder_survives_the_millisecond_wrap);
  RUN_TEST(test_a_dropped_session_takes_the_dropped_row);
  RUN_TEST(test_a_client_that_has_not_parked_yet_costs_no_attempt);
  RUN_TEST(test_a_device_with_no_broker_is_off_and_dials_nothing);
  RUN_TEST(test_no_sequence_of_inputs_can_park_the_ladder_for_ever);
  RUN_TEST(test_a_fresh_client_is_left_to_dial_by_itself);
  RUN_TEST(test_a_flapping_broker_is_never_forgiven);
  RUN_TEST(test_two_devices_do_not_retry_in_lockstep);
  RUN_TEST(test_the_failure_word_survives_a_round_trip);
  RUN_TEST(test_no_rung_is_armed_over_any_dial_not_just_the_first);
  return UNITY_END();
}
