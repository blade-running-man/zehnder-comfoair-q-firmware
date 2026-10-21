// The collector: the half of comfoair_phase_monitor that does not need a device.
//
// THE THIRD FILE OF ONE SUITE. PlatformIO links every .cpp in this directory into one binary, so
// main() and every RUN_TEST stay in test_phase_cases.cpp -- a case registered where it is defined
// is a case that goes missing the next time a file is split.
//
// What is checked here is the WIRING, and the suite is written that way on purpose: which read
// fills which of the seventeen fields, when a hold-off has expired, and whether this pass is a
// transition. What the phase then IS belongs to test_phase_sweeps.cpp, and the first case below
// exists to keep that boundary honest -- if the collector ever answers a phase itself, it fails.
#include <unity.h>

#include "comfoair_phase_collect.h"
#include "phase_inputs.h"

namespace {

// A device where all seven start branches came up, the network is on, the bus has just spoken and
// the broker is connected. Every case spoils exactly one fact, so the fact a case is about is the
// fact it names.
comfoair_phase_facts_t healthy(uint32_t now_ms = 1000) {
  comfoair_phase_facts_t f{};
  f.now_ms            = now_ms;
  f.machine_ready     = true;
  f.faults            = 0;
  f.prov_state        = COMFOAIR_PROV_CONNECTED;
  f.prov_failure      = COMFOAIR_PROV_FAIL_NONE;
  f.can_started       = true;
  f.can_rx_frames     = 1;
  f.broker_configured = true;
  f.broker_connected  = true;
  return f;
}

device_phase_t pass(comfoair_phase_collect_t &s, const comfoair_phase_facts_t &f,
                    comfoair_phase_collect_out_t *out = nullptr) {
  comfoair_phase_collect_out_t local{};
  comfoair_phase_collect_out_t *o = out ? out : &local;
  comfoair_phase_collect_pass(&s, &f, o);
  return o->phase;
}

}  // namespace

// --- the claim the whole file is named after ---------------------------------------------------

void test_the_monitor_asks_the_aggregate_and_never_decides_on_its_own(void) {
  // The collector maps reads onto comfoair_phase_inputs_t and hands the result to
  // comfoair_phase_derive(). It is allowed to decide WHAT THE INPUTS ARE and nothing else, so over
  // the whole product of facts its answer must equal derive()'s answer on the inputs it reports.
  //
  // A REAL GUARD AND NOT A TAUTOLOGY: it fails the moment the collector special-cases a phase --
  // "if the bus is silent and the broker is down, prefer the bus" -- which is precisely the shape
  // of edit that would put a second, drifting copy of the priority cascade in a file that section
  // 2.3 says has none. Verified by mutation: returning COMFOAIR_PHASE_ONLINE from the pass when
  // every fault bit is clear fails here and nowhere else in this suite.
  const comfoair_prov_state_t   states[]   = {COMFOAIR_PROV_UNCONFIGURED, COMFOAIR_PROV_TRIAL,
                                              COMFOAIR_PROV_FALLBACK_AP, COMFOAIR_PROV_RETRYING,
                                              COMFOAIR_PROV_WINDOW_CLOSED, COMFOAIR_PROV_CONNECTED};
  const comfoair_prov_failure_t failures[] = {COMFOAIR_PROV_FAIL_NONE,
                                              COMFOAIR_PROV_FAIL_WRONG_PASSWORD,
                                              COMFOAIR_PROV_FAIL_NETWORK_ABSENT};
  const uint32_t                connacks[] = {0, 3, 5};

  // Seven bits: six of them move a fact each, and all seven are the fault mask, so every
  // start branch is reached in every combination with every other.
  for (unsigned bits = 0; bits < 128; ++bits) {
    for (comfoair_prov_state_t state : states) {
      for (comfoair_prov_failure_t failure : failures) {
        for (uint32_t connack : connacks) {
          comfoair_phase_facts_t f{};
          f.now_ms            = 1000 + bits;
          f.machine_ready     = (bits >> 0) & 1u;
          f.ota_in_progress   = (bits >> 1) & 1u;
          f.ap_on_air         = (bits >> 2) & 1u;
          f.can_started       = (bits >> 3) & 1u;
          f.broker_configured = (bits >> 4) & 1u;
          f.broker_connected  = (bits >> 5) & 1u;
          f.faults            = bits;  // every branch reached, in every combination
          f.can_rx_frames     = bits;
          f.last_connack      = connack;
          f.prov_state        = state;
          f.prov_failure      = failure;

          comfoair_phase_collect_t     s{};
          comfoair_phase_collect_out_t out{};
          comfoair_phase_collect_pass(&s, &f, &out);
          TEST_ASSERT_EQUAL_INT_MESSAGE(
              (int)comfoair_phase_derive(&out.inputs), (int)out.phase,
              "the collector answered a phase of its own instead of the aggregate's");
          // AND the inputs it reported are the facts it was given. Without this half the case is a
          // tautology and says so out loud: a pass that zeroes everything reports BOOT beside a
          // zeroed input struct, from which derive() also answers BOOT, and the equality above
          // holds for a collector that read nothing at all. Measured: with the stub that memset the
          // output, the equality passed and these seven fail.
          // broker_connected is absent on purpose -- it is the one field a hold-off rewrites, and
          // the cases below own it.
          TEST_ASSERT_EQUAL_INT(f.machine_ready, out.inputs.machine_ready);
          TEST_ASSERT_EQUAL_INT(f.ota_in_progress, out.inputs.ota_in_progress);
          TEST_ASSERT_EQUAL_INT(f.ap_on_air, out.inputs.ap_on_air);
          TEST_ASSERT_EQUAL_INT(f.can_started, out.inputs.bus_started);
          TEST_ASSERT_EQUAL_INT(f.broker_configured, out.inputs.broker_configured);
          TEST_ASSERT_EQUAL_INT((int)f.prov_state, (int)out.inputs.prov_state);
          TEST_ASSERT_EQUAL_INT((int)f.prov_failure, (int)out.inputs.prov_failure);
        }
      }
    }
  }
}

// --- the bus verdict, which is the field this task was warned about by name --------------------

void test_a_bus_that_never_spoke_is_not_a_silent_bus(void) {
  // MUTATION M9, and the reason comfoair_phase_inputs_t.bus_silent carries a paragraph about it.
  // A device whose loom was never plugged in publishes bus = offline honestly -- there are no
  // values -- and must NOT be painted BUS_SILENT, whose owner instruction is "check the loom and
  // the unit's power". Writing this field as !comfoair_bus_health_available() passes every other
  // case in this file and fails here.
  comfoair_phase_collect_t s{};
  comfoair_phase_facts_t   f = healthy(600000);  // ten minutes up, and not one frame
  f.can_rx_frames            = 0;

  comfoair_phase_collect_out_t out{};
  comfoair_phase_collect_pass(&s, &f, &out);
  TEST_ASSERT_EQUAL_INT(COMFOAIR_BUS_NEVER_SEEN, (int)out.bus);
  TEST_ASSERT_FALSE_MESSAGE(comfoair_bus_health_available(out.bus),
                            "a bus that never spoke was published as available");
  TEST_ASSERT_FALSE_MESSAGE(out.inputs.bus_silent,
                            "bus_silent was filled from the topic's bit, so a loom that was never "
                            "plugged in reads as an installation that stopped answering");
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_ONLINE, (int)out.phase);
}

void test_a_bus_that_stopped_speaking_reaches_the_phase(void) {
  // The other half of the same distinction: heard, then silent past the verdict, so the phase is
  // BUS_SILENT and the topic says offline. Both projections of ONE resolver: the bus-silent phase
  // and the <prefix>/bus availability topic read the same verdict.
  comfoair_phase_collect_t s{};
  comfoair_phase_facts_t   f = healthy(1000);
  pass(s, f);  // one frame counted

  f.now_ms = 1000 + COMFOAIR_BUS_SILENT_MS;
  comfoair_phase_collect_out_t out{};
  comfoair_phase_collect_pass(&s, &f, &out);
  TEST_ASSERT_EQUAL_INT(COMFOAIR_BUS_SILENT, (int)out.bus);
  TEST_ASSERT_TRUE(out.inputs.bus_silent);
  TEST_ASSERT_TRUE_MESSAGE(out.inputs.bus_started,
                           "bus_started must be the driver NOW, and the driver is up");
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_BUS_SILENT, (int)out.phase);
}

void test_the_provocation_leaves_the_collector_as_one_bool(void) {
  // comfoair_bus_health_out_t.probe is the resolver's whole way of asking for a sweep, and the
  // caller turns it into comfoair_poll_request_now(). Hardware scenario HV-CAN-12 exists because
  // the turning is not observable on the host; what IS observable here is that the bool reaches the
  // caller unswallowed, and exactly once per entry into probing. A collector that dropped the field
  // would leave a quiet bus never provoked and the 90 s verdict decided by nothing.
  comfoair_phase_collect_t s{};
  comfoair_phase_facts_t   f = healthy(1000);
  pass(s, f);

  int asked = 0;
  for (uint32_t t = 1000; t <= 1000 + COMFOAIR_BUS_SILENT_MS; t += 1000) {
    f.now_ms = t;
    comfoair_phase_collect_out_t out{};
    comfoair_phase_collect_pass(&s, &f, &out);
    if (out.probe)
      asked++;
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, asked, "one sweep per entry into probing, and this was not one");
}

// --- the broker hold-off: facts enter at once, absences late ----------------------------------

void test_a_broker_that_has_just_dropped_is_still_online(void) {
  // A fact enters at once, an ABSENCE enters after a hold-off. esp-mqtt's own default reconnect is
  // ~10 s, so a device that lost its broker two seconds ago is a device that has not yet had one
  // attempt refused -- and painting it amber there is a colour that comes back on every broker
  // restart in the house.
  //
  // MEASURED FROM THE TICK THAT FIRST SAW THE BROKER DOWN, not from the last tick that saw it up.
  // At 1 Hz the two readings differ by one tick, and this one is what makes the pass a function of
  // what it was told this time rather than of when it was last told something else -- the same
  // shape comfoair_bus_health uses for its own anchor. `drop` below is that tick.
  comfoair_phase_collect_t s{};
  comfoair_phase_facts_t   f = healthy(1000);
  pass(s, f);

  const uint32_t drop = 2000;
  f.broker_connected  = false;
  f.now_ms            = drop;
  pass(s, f);  // the tick that saw it go

  f.now_ms = drop + COMFOAIR_PHASE_NO_BROKER_MS - 1;
  comfoair_phase_collect_out_t out{};
  comfoair_phase_collect_pass(&s, &f, &out);
  TEST_ASSERT_TRUE_MESSAGE(out.inputs.broker_connected,
                           "the hold-off did not hold: NO_BROKER entered on the first missed tick");
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_ONLINE, (int)out.phase);
}

void test_a_broker_gone_for_the_hold_off_is_no_broker(void) {
  comfoair_phase_collect_t s{};
  comfoair_phase_facts_t   f = healthy(1000);
  pass(s, f);

  const uint32_t drop = 2000;
  f.broker_connected  = false;
  f.now_ms            = drop;
  pass(s, f);

  f.now_ms = drop + COMFOAIR_PHASE_NO_BROKER_MS;
  comfoair_phase_collect_out_t out{};
  comfoair_phase_collect_pass(&s, &f, &out);
  TEST_ASSERT_FALSE(out.inputs.broker_connected);
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_NO_BROKER, (int)out.phase);
}

void test_a_broker_that_came_back_is_granted_the_hold_off_again(void) {
  // The anchor is cleared by a connection, not spent once. Without this a broker that flaps twice
  // an hour is reported NO_BROKER from the first missed tick of the second flap onwards -- the same
  // defect the retry ladder once had, where a bit was not cleared by a new attempt and the device
  // reported "waiting" over its own live dial.
  comfoair_phase_collect_t s{};
  comfoair_phase_facts_t   f = healthy(1000);
  pass(s, f);

  f.broker_connected = false;
  f.now_ms           = 2000;
  pass(s, f);  // the tick that saw it go
  f.now_ms = 2000 + COMFOAIR_PHASE_NO_BROKER_MS;
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_NO_BROKER, (int)pass(s, f));

  f.broker_connected = true;
  f.now_ms += 1000;
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_ONLINE, (int)pass(s, f));

  f.broker_connected = false;
  f.now_ms += 1000;
  comfoair_phase_collect_out_t out{};
  comfoair_phase_collect_pass(&s, &f, &out);
  TEST_ASSERT_TRUE_MESSAGE(out.inputs.broker_connected,
                           "the second drop was answered with the first drop's stopwatch");
}

void test_the_hold_off_survives_the_millisecond_wrap(void) {
  // esp_timer_get_time()/1000 is a 32-bit millisecond count in this tree, so it returns to zero
  // every 49.7 days -- and a device in a technical room is expected to run for years. Written as
  // `now >= since + span` the sum overflows and the hold-off never expires, so a broker that went
  // away on the wrong afternoon is reported ONLINE for ever. The unsigned difference is correct
  // across it, and this is the case that says so.
  comfoair_phase_collect_t s{};
  comfoair_phase_facts_t   f = healthy(0xFFFFF000u);
  pass(s, f);

  f.broker_connected = false;
  pass(s, f);  // the tick that saw it go, still before the wrap

  // ONE SECOND LATER AND STILL BEFORE THE WRAP, and this tick is the one that catches the
  // addition. Written as `now >= since + span` the sum itself wraps to a small number, so every
  // reading of `now` up to the wrap compares greater and the hold-off is over before it began --
  // the mirror image of the failure below, and the reading the first draft of this case missed
  // because the two forms happen to agree at exactly since + span.
  f.now_ms = 0xFFFFF000u + 1000u;
  comfoair_phase_collect_out_t mid{};
  comfoair_phase_collect_pass(&s, &f, &mid);
  TEST_ASSERT_TRUE_MESSAGE(mid.inputs.broker_connected,
                           "one second of absence was read as thirty, because the deadline was "
                           "computed by adding to a counter that is about to wrap");

  f.now_ms = (uint32_t)(0xFFFFF000u + COMFOAIR_PHASE_NO_BROKER_MS);  // wrapped
  comfoair_phase_collect_out_t out{};
  comfoair_phase_collect_pass(&s, &f, &out);
  TEST_ASSERT_FALSE_MESSAGE(out.inputs.broker_connected,
                            "the hold-off was computed with an addition, so the wrap made it "
                            "permanent");
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_NO_BROKER, (int)out.phase);
}

void test_a_refusal_needs_no_hold_off_at_all(void) {
  // A CONNACK the broker actually sent is a fact and enters at once. Under the 300 s retry after
  // a refusal `connected` is false for five minutes at a time, so a hold-off here would
  // report "check your broker" about a broker that answered, for five minutes, repeatedly.
  comfoair_phase_collect_t s{};
  comfoair_phase_facts_t   f = healthy(1000);
  f.broker_connected         = false;
  f.last_connack             = 5;  // not authorized

  comfoair_phase_collect_out_t out{};
  comfoair_phase_collect_pass(&s, &f, &out);
  TEST_ASSERT_TRUE(out.inputs.broker_rejected);
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_BROKER_REJECTED, (int)out.phase);
}

void test_a_server_unavailable_connack_is_not_a_refusal_here_either(void) {
  // Code 3 is what a Mosquitto add-on answers in the first seconds after a restart, and
  // comfoair_phase_connack_is_refusal() refuses to latch it. The collector must ask that function
  // rather than testing `last_connack != 0` -- which is the shorter line and the wrong one.
  comfoair_phase_collect_t s{};
  comfoair_phase_facts_t   f = healthy(1000);
  f.broker_connected         = false;
  f.last_connack             = 3;

  comfoair_phase_collect_out_t out{};
  comfoair_phase_collect_pass(&s, &f, &out);
  TEST_ASSERT_FALSE_MESSAGE(out.inputs.broker_rejected,
                            "a restarting broker latched the phase that says a person must act");
}

// --- the seven branches, and which field each one lands in -------------------------------------

void test_each_start_branch_lands_in_the_field_that_names_it(void) {
  // The mapping one bit at a time, because a mask that shifted by one would still produce FAULT for
  // every branch and be wrong about every one of them. mqtt_client is the row that matters most:
  // it is the sixth branch, and the one an earlier list of start branches missed entirely.
  struct {
    comfoair_phase_fault_t which;
    bool comfoair_phase_inputs_t::*field;
  } const rows[] = {
      {COMFOAIR_PHASE_FAULT_CAN, &comfoair_phase_inputs_t::can_started},
      {COMFOAIR_PHASE_FAULT_POLL, &comfoair_phase_inputs_t::poll_started},
      {COMFOAIR_PHASE_FAULT_CONTROL, &comfoair_phase_inputs_t::control_started},
      {COMFOAIR_PHASE_FAULT_NET, &comfoair_phase_inputs_t::net_started},
      {COMFOAIR_PHASE_FAULT_MQTT_QUEUE, &comfoair_phase_inputs_t::mqtt_ready},
      {COMFOAIR_PHASE_FAULT_MQTT_CLIENT, &comfoair_phase_inputs_t::mqtt_client_started},
      {COMFOAIR_PHASE_FAULT_HTTP, &comfoair_phase_inputs_t::http_started},
  };
  TEST_ASSERT_EQUAL_INT_MESSAGE((int)COMFOAIR_PHASE_FAULT_COUNT,
                                (int)(sizeof(rows) / sizeof(rows[0])),
                                "a start branch was added to the enumeration and not to this table");

  for (auto const &row : rows) {
    comfoair_phase_collect_t s{};
    comfoair_phase_facts_t   f = healthy();
    f.faults                   = COMFOAIR_PHASE_FAULT_BIT(row.which);

    comfoair_phase_collect_out_t out{};
    comfoair_phase_collect_pass(&s, &f, &out);
    TEST_ASSERT_FALSE_MESSAGE(out.inputs.*row.field, "this branch's failure reached another field");
    TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_FAULT, (int)out.phase);
    // And every other field is untouched: a mask read with the wrong shift lights two.
    int down = 0;
    for (auto const &other : rows)
      if (!(out.inputs.*other.field))
        down++;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, down, "one reported failure took two fields down with it");
  }
}

void test_a_driver_stopped_after_it_started_is_not_a_startup_fault(void) {
  // can_started (app_main's one-time verdict, from the fault mask) and bus_started (the driver NOW,
  // from comfoair_can_get_stats) are different facts, and comfoair_phase.h spends a paragraph on
  // the one situation where they diverge. Filling both from one read is the mutation: the phase
  // would then be FAULT ("the firmware could not") for a bus somebody stopped at runtime, or
  // BUS_SILENT ("check the loom") for a driver that never came up.
  comfoair_phase_collect_t s{};
  comfoair_phase_facts_t   f = healthy();
  f.can_started              = false;  // the driver is not running now
  f.faults                   = 0;      // but app_main's branch returned OK

  comfoair_phase_collect_out_t out{};
  comfoair_phase_collect_pass(&s, &f, &out);
  TEST_ASSERT_TRUE_MESSAGE(out.inputs.can_started, "the live driver state overwrote app_main's verdict");
  TEST_ASSERT_FALSE_MESSAGE(out.inputs.bus_started, "bus_started was filled from app_main's verdict");
  TEST_ASSERT_FALSE_MESSAGE(out.inputs.bus_silent, "a stopped driver was reported as a silent bus");
}

// --- one line per transition ---------------------------------------------------------------------

void test_a_transition_is_reported_once_and_a_steady_phase_never(void) {
  // At 1 Hz, "log the phase" and "log the transition" differ by 86 400 lines a day -- and
  // GET /api/log is a ring, so the second reading does not merely add noise, it DELETES the
  // transition the owner came to find.
  comfoair_phase_collect_t s{};
  comfoair_phase_facts_t   f = healthy(1000);

  comfoair_phase_collect_out_t first{};
  comfoair_phase_collect_pass(&s, &f, &first);
  TEST_ASSERT_TRUE_MESSAGE(first.changed, "the first pass out of BOOT was not reported at all");
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_PHASE_BOOT, (int)first.previous,
                                "the device was not showing BOOT before its first transition");

  for (int i = 0; i < 5; ++i) {
    f.now_ms += 1000;
    comfoair_phase_collect_out_t out{};
    comfoair_phase_collect_pass(&s, &f, &out);
    TEST_ASSERT_FALSE_MESSAGE(out.changed, "a steady phase was reported as a transition");
    TEST_ASSERT_EQUAL_INT(out.phase, (int)out.previous);
  }

  f.prov_failure = COMFOAIR_PROV_FAIL_WRONG_PASSWORD;
  f.now_ms += 1000;
  comfoair_phase_collect_out_t moved{};
  comfoair_phase_collect_pass(&s, &f, &moved);
  TEST_ASSERT_TRUE(moved.changed);
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_ONLINE, (int)moved.previous);
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_NET_REJECTED, (int)moved.phase);
}

void test_a_pass_with_nothing_to_say_is_answered_and_not_crashed(void) {
  // Nothing reboots over an absent peer (AGENTS.md), in the shape it takes here: the
  // monitor reads five subsystems, and not one failed read may take the task down. The pass is
  // total for the same reason comfoair_phase_derive() is.
  comfoair_phase_collect_t     s{};
  comfoair_phase_collect_out_t out{};
  comfoair_phase_collect_pass(&s, nullptr, &out);
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_BOOT, (int)out.phase);
  TEST_ASSERT_FALSE(out.probe);
  comfoair_phase_collect_pass(nullptr, nullptr, nullptr);
  const comfoair_phase_facts_t f = healthy();
  comfoair_phase_collect_pass(nullptr, &f, nullptr);
}
