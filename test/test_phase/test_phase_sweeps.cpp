// The sweeps: every phase claim that is checked over the whole product of inputs rather than on a
// named example, and the compile-time guards that keep that product whole.
//
// SPLIT FROM test_phase_cases.cpp ALONG THAT SEAM -- machine-checked claims here, readable
// witnesses there -- because one file hit the 600-line ceiling (AGENTS.md). PlatformIO links every
// .cpp in a directory into one binary, so this is one suite in two files; main() and every
// RUN_TEST live next door, so that a case cannot be lost by forgetting to register it.
//
// The suite is written against the phase table of docs/device/indication.md (Device phases) --
// the Condition column, the "true exactly when" of each phase, stated per phase and unordered --
// and NOT against the ordered cascade that implements it. Re-deriving the conditions from the code
// would test the code against itself and would have let an earlier, inverted
// BUS_SILENT/BROKER_REJECTED order (a fact must outrank an inference) pass unnoticed.
#include <unity.h>

#include <cstring>

#include "phase_inputs.h"

// --- the phase table, transcribed as unordered conditions -------------------------------------

// The "true exactly when" column, one phase at a time and with no reference to any other phase.
// ONLINE is absent on purpose: the table defines it as "everything else is false", which is the
// one row that cannot be stated without the others, and the sweep below treats it as such.
static bool condition_holds(device_phase_t phase, const comfoair_phase_inputs_t &in) {
  switch (phase) {
    case COMFOAIR_PHASE_BOOT:        return !in.machine_ready;
    case COMFOAIR_PHASE_OTA:         return in.ota_in_progress;
    case COMFOAIR_PHASE_TRIAL:       return in.prov_state == COMFOAIR_PROV_TRIAL;
    case COMFOAIR_PHASE_SETUP_AP:
      return in.ap_on_air && in.prov_state == COMFOAIR_PROV_UNCONFIGURED;
    case COMFOAIR_PHASE_RECOVERY_AP:
      return in.ap_on_air && in.prov_state == COMFOAIR_PROV_FALLBACK_AP;
    case COMFOAIR_PHASE_FAULT:
      return !in.can_started || !in.poll_started || !in.control_started || !in.net_started ||
             !in.mqtt_ready || !in.mqtt_client_started || !in.http_started;
    case COMFOAIR_PHASE_NET_REJECTED:
      return in.prov_failure == COMFOAIR_PROV_FAIL_WRONG_PASSWORD ||
             in.prov_failure == COMFOAIR_PROV_FAIL_REFUSED;
    case COMFOAIR_PHASE_NET_LOST:
      return in.prov_failure == COMFOAIR_PROV_FAIL_NETWORK_ABSENT ||
             in.prov_failure == COMFOAIR_PROV_FAIL_NO_ADDRESS;
    case COMFOAIR_PHASE_SETUP_EXPIRED:
      return in.prov_state == COMFOAIR_PROV_WINDOW_CLOSED;
    // "and no verdict yet" from the table is not repeated here: a verdict is exactly what
    // NET_REJECTED and NET_LOST are, and they outrank this row already. Restating it would make
    // the table depend on its own order, which is what this function exists not to do.
    case COMFOAIR_PHASE_CONNECTING:
      return in.prov_state == COMFOAIR_PROV_CONNECTING || in.prov_state == COMFOAIR_PROV_RETRYING;
    case COMFOAIR_PHASE_BROKER_REJECTED: return in.broker_rejected;
    case COMFOAIR_PHASE_BUS_SILENT:      return in.bus_started && in.bus_silent;
    case COMFOAIR_PHASE_NO_BROKER:       return in.broker_configured && !in.broker_connected;
    case COMFOAIR_PHASE_ONLINE:
    case COMFOAIR_PHASE_COUNT:           break;
  }
  return false;
}

// --- totality and priority --------------------------------------------------------------------

void test_no_input_combination_falls_through_to_no_phase(void) {
  // The whole product of inputs: 2^15 flag combinations against all seven provisioning states and
  // all five failure verdicts. "There is no phase" is not a value comfoair_phase_derive() can
  // return, and this is what makes that a fact rather than a hope.
  //
  // HONESTLY: THIS CASE CANNOT FAIL ALONE. It is documentation of the property, not an independent
  // check. A value past the end already fails the priority sweep (condition_holds() answers false
  // for anything that is not a phase), and a phase with no spelling already fails
  // test_every_phase_has_a_name in fourteen iterations rather than 1 146 880. DO NOT read its
  // green as evidence of anything those two do not already say.
  for (unsigned bits = 0; bits < (1u << INPUT_BITS); ++bits) {
    for (comfoair_prov_state_t state : ALL_STATES) {
      for (comfoair_prov_failure_t failure : ALL_FAILURES) {
        comfoair_phase_inputs_t in = combination(bits, state, failure);
        device_phase_t          p  = comfoair_phase_derive(&in);
        TEST_ASSERT_TRUE_MESSAGE(p < COMFOAIR_PHASE_COUNT,
                                 "derive returned something that is not a phase");
        TEST_ASSERT_TRUE_MESSAGE(strcmp(comfoair_phase_name(p), "unknown") != 0,
                                 "derive returned a phase that comfoair_phase_name cannot spell");
      }
    }
  }
}

void test_every_bit_of_the_sweep_moves_a_flag_of_its_own(void) {
  // The shifts in combination() are a hand-written table, and nothing above checks them: the arity
  // assertion counts the struct's fields, not the bits that reach them. Two real mistakes live in
  // that gap, and both were green before this case existed.
  //
  //   * a typo of one character -- (bits >> 14) written as (bits >> 13) -- leaves bit 14 dead and
  //     ties broker_rejected to broker_connected, halving the space both sweeps claim to cover;
  //   * a sixteenth flag added correctly everywhere else -- INPUT_BITS bumped, named in the
  //     decomposition, typed, given a branch in derive() and a line in condition_holds() -- and
  //     missed HERE is a flag that stays false through all 1 146 880 iterations, and a branch of
  //     the cascade that never once runs.
  //
  // Neither is visible as a failure: the sweeps go on passing over a smaller space and go on
  // calling it the product. So each bit is required to move exactly one flag, and no two bits are
  // allowed to move the same one.
  static bool comfoair_phase_inputs_t::*const FLAGS[] = {
      &comfoair_phase_inputs_t::machine_ready,   &comfoair_phase_inputs_t::ota_in_progress,
      &comfoair_phase_inputs_t::can_started,     &comfoair_phase_inputs_t::poll_started,
      &comfoair_phase_inputs_t::control_started, &comfoair_phase_inputs_t::net_started,
      &comfoair_phase_inputs_t::mqtt_ready,      &comfoair_phase_inputs_t::mqtt_client_started,
      &comfoair_phase_inputs_t::http_started,    &comfoair_phase_inputs_t::ap_on_air,
      &comfoair_phase_inputs_t::bus_started,     &comfoair_phase_inputs_t::bus_silent,
      &comfoair_phase_inputs_t::broker_configured, &comfoair_phase_inputs_t::broker_connected,
      &comfoair_phase_inputs_t::broker_rejected,
  };
  static_assert(sizeof(FLAGS) / sizeof(FLAGS[0]) == INPUT_BITS,
                "this list and INPUT_BITS disagree, so one bit of the sweep is unaccounted for");

  const comfoair_phase_inputs_t none =
      combination(0, COMFOAIR_PROV_UNCONFIGURED, COMFOAIR_PROV_FAIL_NONE);
  int moved_by[INPUT_BITS];
  for (int bit = 0; bit < INPUT_BITS; ++bit) {
    const comfoair_phase_inputs_t one =
        combination(1u << bit, COMFOAIR_PROV_UNCONFIGURED, COMFOAIR_PROV_FAIL_NONE);
    int moved = -1, how_many = 0;
    for (int flag = 0; flag < INPUT_BITS; ++flag) {
      if (one.*FLAGS[flag] != none.*FLAGS[flag]) {
        moved = flag;
        ++how_many;
      }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, how_many,
                                  "a bit of the sweep moves no flag at all, or moves more than one");
    moved_by[bit] = moved;
  }
  for (int bit = 1; bit < INPUT_BITS; ++bit) {
    for (int earlier = 0; earlier < bit; ++earlier) {
      TEST_ASSERT_TRUE_MESSAGE(moved_by[bit] != moved_by[earlier],
                               "two bits move the same flag, so a field is never swept");
    }
  }
}

void test_every_phase_is_reachable_from_some_input(void) {
  // The opposite mistake to totality, and the one nothing else here catches: a phase added to the
  // enumeration -- to the LED table, to the API -- that no branch of the cascade ever returns. A
  // value no input reaches is a state the device can never be in, however many places downstream
  // are written to handle it. Some are reached by very few of the 1 146 880 combinations -- ONLINE
  // by 36, BUS_SILENT by 16, NO_BROKER by 12 -- so it is a sweep, not a list of chosen inputs.
  bool seen[COMFOAIR_PHASE_COUNT] = {false};
  for (unsigned bits = 0; bits < (1u << INPUT_BITS); ++bits) {
    for (comfoair_prov_state_t state : ALL_STATES) {
      for (comfoair_prov_failure_t failure : ALL_FAILURES) {
        comfoair_phase_inputs_t in = combination(bits, state, failure);
        seen[comfoair_phase_derive(&in)] = true;
      }
    }
  }
  for (int p = 0; p < (int)COMFOAIR_PHASE_COUNT; ++p) {
    TEST_ASSERT_TRUE_MESSAGE(seen[p], comfoair_phase_name((device_phase_t)p));
  }
}

void test_lower_numbered_phase_wins_when_both_conditions_hold(void) {
  // The priority rule checked mechanically over the same product: for whatever phase comes back, no
  // higher-priority phase had a true condition, and the phase that came back had one -- unless
  // it is ONLINE, which the table defines as the absence of all the others.
  for (unsigned bits = 0; bits < (1u << INPUT_BITS); ++bits) {
    for (comfoair_prov_state_t state : ALL_STATES) {
      for (comfoair_prov_failure_t failure : ALL_FAILURES) {
        comfoair_phase_inputs_t in = combination(bits, state, failure);
        device_phase_t          p  = comfoair_phase_derive(&in);

        for (int earlier = 0; earlier < (int)p; ++earlier) {
          TEST_ASSERT_FALSE_MESSAGE(condition_holds((device_phase_t)earlier, in),
                                    "a higher-priority phase was true and was not reported");
        }
        if (p == COMFOAIR_PHASE_ONLINE) {
          for (int other = 0; other < (int)COMFOAIR_PHASE_ONLINE; ++other) {
            TEST_ASSERT_FALSE_MESSAGE(condition_holds((device_phase_t)other, in),
                                      "ONLINE was reported while something else was true");
          }
        } else {
          TEST_ASSERT_TRUE_MESSAGE(condition_holds(p, in),
                                   "a phase was reported whose condition is false");
        }
      }
    }
  }
}

void test_the_phase_enumeration_has_fourteen_values(void) {
  // A guard, not a fact about arithmetic. A phase is admitted only when the owner's next
  // action differs or the expected duration differs by an order of magnitude; a fifteenth value
  // added without that argument breaks this line and has to be defended. It is also what keeps
  // the phase-to-look table of docs/device/indication.md total: the LED palette is written
  // against these fourteen.
  TEST_ASSERT_EQUAL_INT(14, (int)COMFOAIR_PHASE_COUNT);
}

// The hold-off contract stated so the compiler enforces it: the hold-offs are a measurement
// and belong to the collector, while derive is a decision. Mixing them gives a function that
// cannot be called twice with the same argument -- and every case in this file would then need a
// clock to be written down at all.
static_assert(std::is_same<decltype(&comfoair_phase_derive),
                           device_phase_t (*)(const comfoair_phase_inputs_t *)>::value,
              "comfoair_phase_derive grew a parameter; a clock belongs to the collector");

// The inputs, named one by one and then typed one by one. An eighteenth field of any width fails
// to compile here: "decomposes into 18 elements, but only 17 names were provided".
//
// EVERY FLAG, NOT A SAMPLE OF THEM -- a sample is how this guard was first written and how it was
// first defeated: with three names typed, turning bus_silent into a uint32_t of "milliseconds of
// silence", the exact hold-off-inside-derive violation it exists to stop, passed the whole suite.
#define A_FLAG_AND_NOT_A_DURATION(field) \
  static_assert(std::is_same<decltype(field), const bool>::value, \
                #field " stopped being a flag; a duration here belongs to the collector")

static void the_inputs_are_seventeen_typed_fields(const comfoair_phase_inputs_t &in) {
  const auto &[machine_ready, ota_in_progress, can_started, poll_started, control_started,
               net_started, mqtt_ready, mqtt_client_started, http_started, prov_state,
               prov_failure, ap_on_air, bus_started, bus_silent, broker_configured,
               broker_connected, broker_rejected] = in;
  A_FLAG_AND_NOT_A_DURATION(machine_ready);   A_FLAG_AND_NOT_A_DURATION(ota_in_progress);
  A_FLAG_AND_NOT_A_DURATION(can_started);     A_FLAG_AND_NOT_A_DURATION(poll_started);
  A_FLAG_AND_NOT_A_DURATION(control_started); A_FLAG_AND_NOT_A_DURATION(net_started);
  A_FLAG_AND_NOT_A_DURATION(mqtt_ready);      A_FLAG_AND_NOT_A_DURATION(mqtt_client_started);
  A_FLAG_AND_NOT_A_DURATION(http_started);    A_FLAG_AND_NOT_A_DURATION(ap_on_air);
  A_FLAG_AND_NOT_A_DURATION(bus_started);     A_FLAG_AND_NOT_A_DURATION(bus_silent);
  A_FLAG_AND_NOT_A_DURATION(broker_configured);
  A_FLAG_AND_NOT_A_DURATION(broker_connected);
  A_FLAG_AND_NOT_A_DURATION(broker_rejected);
  static_assert(std::is_same<decltype(prov_state), const comfoair_prov_state_t>::value,
                "prov_state is swept by name, not by bit; it has to stay the machine's own type");
  static_assert(std::is_same<decltype(prov_failure), const comfoair_prov_failure_t>::value, "");
}
#undef A_FLAG_AND_NOT_A_DURATION

void test_derive_takes_no_clock(void) {
  // The static assertions ARE this test -- the signature check above, the field count near
  // INPUT_BITS, and the seventeen types in the decomposition. They fail at compile time and would
  // do so with no caller at all; the call below is here so that the guard is not left as an unused
  // function, and so that the case has something to run. Asserting that two calls agree would
  // prove nothing: an implementation reading a clock agrees with itself too.
  comfoair_phase_inputs_t in = online();
  the_inputs_are_seventeen_typed_fields(in);
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_ONLINE, comfoair_phase_derive(&in));
}

