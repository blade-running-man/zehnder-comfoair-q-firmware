// What an arriving MQTT message means -- the half of the broker connection that has no broker in
// it.
//
// This branching is where a regression is SILENT. Every other failure mode of comfoair_mqtt
// announces itself: a client that will not connect logs, a publish that does not fit returns 0. A
// command that quietly stops being applied returns nothing, logs nothing, and is discovered when
// the owner presses a button in Home Assistant and the house does not change. Until this suite
// existed, none of it was covered.
//
// Three of the assertions below are guards against a failure with real consequences rather than
// preferences:
//   * a retained command is refused, or one `mosquitto_pub -r` reopens the bypass on every boot
//     for as long as the broker keeps the message (comfoair_mqtt.h, third rule);
//   * a non-numeric payload is refused, because strtod("hello") is 0 and 0 on the fan is the AWAY
//     level -- a typo would put the house into Away;
//   * a topic from another tree is IGNORED and not counted as a refusal, because a broker is
//     shared and somebody else's traffic must not read as this device's fault.
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "comfoair_mqtt_inbound.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

// This device's own tree, the way comfoair_mqtt builds it from the MAC.
#define P "comfoair/aabbccddeeff"

comfoair_ha_ctx_t ctx(void) {
  comfoair_ha_ctx_t c = {};
  c.device_id    = "aabbccddeeff";
  c.device_name  = "ComfoAir";
  c.topic_prefix = P;
  c.sw_version   = "1.0.0";
  c.mac          = "aa:bb:cc:dd:ee:ff";
  c.url          = "http://192.168.1.9/";
  return c;
}

comfoair_mqtt_inbound_t msg(const char *topic, const char *payload, bool retained = false) {
  comfoair_mqtt_inbound_t in = {};
  // The topic is copied by length and NOT terminated here, exactly as the event handler leaves it:
  // esp-mqtt hands over a pointer into its own receive buffer with a separate length.
  in.topic_len = strlen(topic);
  memcpy(in.topic, topic, in.topic_len);
  memcpy(in.payload, payload, strlen(payload));
  in.retained = retained;
  return in;
}

comfoair_command_durations_t defaults(void) {
  comfoair_command_durations_t d;
  comfoair_command_durations_defaults(&d);
  return d;
}

comfoair_mqtt_decision_t decide(const comfoair_mqtt_inbound_t &in,
                                const comfoair_command_durations_t &d,
                                const comfoair_fan_state_t &f) {
  const comfoair_ha_ctx_t c = ctx();
  return comfoair_mqtt_decide(&c, &in, &d, &f);
}

// The everyday case: default durations, a unit that has reported nothing.
comfoair_mqtt_decision_t decide(const char *topic, const char *payload, bool retained = false) {
  const comfoair_command_durations_t d = defaults();
  const comfoair_fan_state_t f = {};
  return decide(msg(topic, payload, retained), d, f);
}

// A frame as hex. A wrong level byte has to be legible at a glance in the failure line, and the
// fan's level is the LAST byte of its last frame.
std::string hex(const comfoair_command_t &c, uint8_t index) {
  static const char *digits = "0123456789abcdef";
  if (index >= c.count)
    return "<no such frame>";
  std::string out;
  for (uint8_t i = 0; i < c.frames[index].len; i++) {
    if (i)
      out += ' ';
    out += digits[c.frames[index].bytes[i] >> 4];
    out += digits[c.frames[index].bytes[i] & 0x0f];
  }
  return out;
}

std::string last_frame(const comfoair_command_t &c) {
  return c.count == 0 ? std::string("<no frames>") : hex(c, (uint8_t)(c.count - 1));
}

}  // namespace

// --- what is ours, and what is not -------------------------------------------------------------

void test_a_retained_command_is_refused_because_the_broker_replays_it_for_ever(void) {
  // MQTT 3.1.1 §3.3.1.3: a broker delivers a retained message with the retain flag SET to every
  // new subscription -- so one `mosquitto_pub -r -t .../away_mode/command -m ON` typed during
  // testing is re-delivered on every reconnect and every boot until somebody clears it. The same
  // section makes delivery to an ALREADY-ESTABLISHED subscription arrive with the flag CLEARED,
  // which is why the second half of this test must still be a SUBMIT: what is fixed is the replay.
  const comfoair_mqtt_decision_t replayed = decide(P "/away_mode/command", "ON", true);
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_REJECT, replayed.kind);
  TEST_ASSERT_NOT_NULL(replayed.reason);
  TEST_ASSERT_TRUE_MESSAGE(replayed.reason[0] != '\0', "a refusal with no reason logs nothing");

  const comfoair_mqtt_decision_t live = decide(P "/away_mode/command", "ON");
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_MQTT_SUBMIT, live.kind,
                            "the retain flag is the only difference between these two messages");
}

void test_a_topic_from_another_tree_is_ignored_rather_than_counted_as_a_refusal(void) {
  // A broker is shared with everything else in the household. IGNORE and REJECT are different
  // answers because the caller moves the rejection counter on one of them: a device that reports a
  // rising refusal count because the neighbour's thermostat is chatty is a fault report about
  // nothing, and it buries the refusals that matter.
  const comfoair_mqtt_decision_t d = decide("someoneelse/x/command", "ON");
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_IGNORE, d.kind);
  TEST_ASSERT_NULL_MESSAGE(d.reason, "there is nothing to report about somebody else's topic");

  // Nor does a topic that merely starts with the prefix belong to us.
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_IGNORE, decide(P "-other/away_mode/command", "ON").kind);

  // AND IT STAYS IGNORE WHEN IT IS RETAINED, which is the combination that used to slip through:
  // the retained refusal ran before the topic was parsed, so a neighbour's retained message -- and
  // most of what a shared broker holds is retained -- was counted against this device.
  const comfoair_mqtt_decision_t retained = decide("someoneelse/x/command", "ON", true);
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_MQTT_IGNORE, retained.kind,
                            "a neighbour's retained message moved our rejection counter");
  TEST_ASSERT_NULL(retained.reason);
}

// --- the payload shapes the registry dictates ----------------------------------------------------

void test_a_switch_takes_on_or_off_and_nothing_else(void) {
  // away_mode is the registry's only switch. "Maybe" is not a truth value, and mapping it to
  // either one would act on a command nobody sent.
  const comfoair_mqtt_decision_t on = decide(P "/away_mode/command", "ON");
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_SUBMIT, on.kind);
  TEST_ASSERT_EQUAL_STRING("away_mode", on.key);

  const comfoair_mqtt_decision_t off = decide(P "/away_mode/command", "OFF");
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_SUBMIT, off.kind);
  // The release, pinned by test_command as the away switch's OFF frame. ON and OFF must not be
  // the same bytes, which is what a boolean read the wrong way round would produce.
  TEST_ASSERT_EQUAL_STRING("85 15 01 0b", hex(off.command, 0).c_str());
  TEST_ASSERT_TRUE_MESSAGE(hex(on.command, 0) != hex(off.command, 0),
                           "ON and OFF produced the same frame");

  const comfoair_mqtt_decision_t bad = decide(P "/away_mode/command", "Maybe");
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_REJECT, bad.kind);
  TEST_ASSERT_NOT_NULL(bad.reason);
}

void test_the_fan_takes_on_or_off_on_its_own_topic_and_never_a_number(void) {
  // <prefix>/fan/command is Home Assistant's on/off topic for a fan; the percentage arrives on
  // <prefix>/fan/speed/command. "1" here is a speed sent to the wrong topic and is refused rather
  // than read as truthy -- the two topics mean different things and conflating them would make
  // "off" unreachable.
  const comfoair_mqtt_decision_t off = decide(P "/fan/command", "OFF");
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_SUBMIT, off.kind);
  TEST_ASSERT_EQUAL_STRING("fan", off.key);
  // Off on this unit is the AWAY level -- an HRV never stops ventilating entirely -- and the level
  // is the last byte.
  TEST_ASSERT_EQUAL_STRING("84 15 01 01 00 00 00 00 01 00 00 00 00", last_frame(off.command).c_str());

  // ON, on its own rather than only inside the fan-resume test: with nothing reported the level
  // falls back to Low, which is the one case that must NOT be read as a downgrade.
  const comfoair_mqtt_decision_t on = decide(P "/fan/command", "ON");
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_SUBMIT, on.kind);
  TEST_ASSERT_EQUAL_STRING("fan", on.key);
  TEST_ASSERT_FALSE_MESSAGE(on.optimistic_text, "the fan is confirmed by PDO 65 and must not be");
  TEST_ASSERT_EQUAL_STRING("84 15 01 01 00 00 00 00 01 00 00 00 01", last_frame(on.command).c_str());

  const comfoair_mqtt_decision_t bad = decide(P "/fan/command", "1");
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_REJECT, bad.kind);
  TEST_ASSERT_NOT_NULL(bad.reason);
}

void test_a_fan_speed_must_be_a_number_or_the_house_goes_to_away(void) {
  const comfoair_mqtt_decision_t two = decide(P "/fan/speed/command", "2");
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_SUBMIT, two.kind);
  TEST_ASSERT_EQUAL_STRING("84 15 01 01 00 00 00 00 01 00 00 00 02", last_frame(two.command).c_str());

  // THE ONE THAT MATTERS. strtod("fast") is 0 with no error, and level 0 is Away: a firmware that
  // took the return value alone would answer a typo by emptying the house of ventilation.
  const comfoair_mqtt_decision_t bad = decide(P "/fan/speed/command", "fast");
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_REJECT, bad.kind);
  TEST_ASSERT_NOT_NULL(bad.reason);
}

void test_a_number_that_is_not_a_number_is_refused_rather_than_read_as_zero(void) {
  // The guard behind the test above, pinned directly. Every case here is a payload strtod accepts
  // in part, answers 0 for, or answers something that is not a number at all for.
  double v = 12345;
  TEST_ASSERT_FALSE(comfoair_mqtt_parse_number("hello", &v));
  TEST_ASSERT_FALSE(comfoair_mqtt_parse_number("", &v));
  TEST_ASSERT_FALSE_MESSAGE(comfoair_mqtt_parse_number("2x", &v),
                            "a number with trailing text is not a number");
  TEST_ASSERT_FALSE(comfoair_mqtt_parse_number(NULL, &v));

  // THE ONES THAT SHIPPED BROKEN. strtod consumes all of "nan" and "inf", so the "did it consume
  // the whole string" test passes them -- and then EVERY downstream range check passes too,
  // because all comparisons against NaN are false. The cast that follows is undefined: 0 on the
  // host, which is the fan's AWAY level, and 65535 on riscv32, which stores a duration the
  // registry's 1..1440 range exists to forbid. Anybody who can reach the broker can send them.
  const char *const not_numbers[] = {"nan",  "NaN",       "NAN",  "-nan", "inf",
                                     "INF",  "-inf",      "Inf",  "infinity", "INFINITY",
                                     "0x10", "0X1p4",     " 2",   "\t2",  "2 ",
                                     "1e400", "-1e400",   "+",    ".",    "e5"};
  for (size_t i = 0; i < sizeof not_numbers / sizeof not_numbers[0]; i++) {
    char why[80];
    snprintf(why, sizeof why, "'%s' was accepted as a number", not_numbers[i]);
    TEST_ASSERT_FALSE_MESSAGE(comfoair_mqtt_parse_number(not_numbers[i], &v), why);
  }
  TEST_ASSERT_EQUAL_FLOAT_MESSAGE(12345.0f, (float)v,
                                  "a refused parse must not write through the pointer");

  // And it is still a NUMBER parser, not a digit parser.
  TEST_ASSERT_TRUE(comfoair_mqtt_parse_number("2", &v));
  TEST_ASSERT_EQUAL_FLOAT(2.0f, (float)v);
  TEST_ASSERT_TRUE(comfoair_mqtt_parse_number("-1.5", &v));
  TEST_ASSERT_EQUAL_FLOAT(-1.5f, (float)v);
  TEST_ASSERT_TRUE(comfoair_mqtt_parse_number("1e2", &v));
  TEST_ASSERT_EQUAL_FLOAT(100.0f, (float)v);
  TEST_ASSERT_TRUE(comfoair_mqtt_parse_number("0", &v));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, (float)v);
}

void test_a_non_finite_payload_reaches_neither_the_fan_nor_a_stored_duration(void) {
  // The guard above, seen from the outside -- because the range checks downstream cannot catch
  // this: `v < min || v > max` is FALSE for NaN, so a decision built from one passes every bound
  // the registry advertises and arrives looking legitimate.
  const char *const spellings[] = {"nan", "inf", "-inf", "1e400"};
  for (size_t i = 0; i < sizeof spellings / sizeof spellings[0]; i++) {
    char why[96];

    const comfoair_mqtt_decision_t speed = decide(P "/fan/speed/command", spellings[i]);
    snprintf(why, sizeof why, "'%s' reached the fan", spellings[i]);
    TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_MQTT_REJECT, speed.kind, why);

    const comfoair_mqtt_decision_t duration = decide(P "/boost_duration/command", spellings[i]);
    snprintf(why, sizeof why, "'%s' reached flash as a duration", spellings[i]);
    TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_MQTT_REJECT, duration.kind, why);
  }
}

// --- the three outcomes --------------------------------------------------------------------------

void test_a_duration_is_stored_rather_than_sent_and_carries_its_minutes(void) {
  // The four durations produce NO frame: the unit is told a duration only as part of a command
  // (pdo_table.py, NumberSpec), so writing one is a local state change. A caller that submitted this
  // like any other command would send nothing and report success.
  const comfoair_mqtt_decision_t d = decide(P "/boost_duration/command", "45");
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_STORE, d.kind);
  TEST_ASSERT_TRUE(d.command.stores_duration);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, d.command.count, "a duration must not put a frame on the bus");
  TEST_ASSERT_NOT_NULL(d.command.duration_key);
  TEST_ASSERT_EQUAL_STRING("boost_duration", d.command.duration_key);
  TEST_ASSERT_EQUAL_UINT16(45, d.command.duration_minutes);

  // The registry's range is 1..1440 minutes and it is the registry that refuses, not this file.
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_REJECT, decide(P "/boost_duration/command", "9000").kind);

  // A KIND_NUMBER entity that is handed a word is refused here, before the registry sees it --
  // the same strtod guard the fan's speed has, on the path that writes flash instead of the bus.
  const comfoair_mqtt_decision_t word = decide(P "/boost_duration/command", "soon");
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_REJECT, word.kind);
  TEST_ASSERT_NOT_NULL(word.reason);
}

void test_a_select_is_submitted_and_its_label_carried_for_the_optimistic_write(void) {
  // Nothing on the bus ever confirms a select this device wrote -- three of the seven have no PDO
  // at all -- so the label is applied to the state model after the submit, and it has to survive
  // that far.
  const comfoair_mqtt_decision_t d = decide(P "/bypass_mode/command", "Open");
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_SUBMIT, d.kind);
  TEST_ASSERT_TRUE(d.optimistic_text);
  TEST_ASSERT_EQUAL_STRING("bypass_mode", d.key);
  TEST_ASSERT_EQUAL_STRING("Open", d.text);
  // The timeout is the STORED bypass duration -- 60 minutes by default, 3600 seconds little-endian
  // -- because a select carries no duration of its own. The operation further down sends the same
  // command with its own 600, which is what makes these two frames differ in exactly those bytes.
  TEST_ASSERT_EQUAL_STRING("84 15 02 01 00 00 00 00 10 0e 00 00 01", hex(d.command, 0).c_str());

  // A switch is confirmed by PDO 337, requested ~1.5 s after the command, and must NOT be
  // written optimistically.
  TEST_ASSERT_FALSE(decide(P "/away_mode/command", "ON").optimistic_text);

  // An option the registry does not list under this entity is refused, not mapped to the nearest.
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_REJECT, decide(P "/bypass_mode/command", "Ajar").kind);
}

void test_the_carried_label_is_a_copy_and_not_a_pointer_into_the_callers_buffer(void) {
  // `in` is one slot of a FreeRTOS queue that the caller reuses for the next message. An earlier
  // shape of this code set value.text = in->payload, which was safe only because deciding and
  // acting happened inside a single function -- it stopped being true the moment the decision
  // outlived the call. This test overwrites the caller's buffer and demands the label survives.
  comfoair_mqtt_inbound_t in = msg(P "/temperature_profile/command", "Warm");
  const comfoair_ha_ctx_t c = ctx();
  const comfoair_command_durations_t dur = defaults();
  const comfoair_fan_state_t fan = {};

  const comfoair_mqtt_decision_t d = comfoair_mqtt_decide(&c, &in, &dur, &fan);
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_SUBMIT, d.kind);
  TEST_ASSERT_TRUE(d.optimistic_text);

  memset(in.payload, 'X', sizeof in.payload - 1);
  in.payload[sizeof in.payload - 1] = '\0';
  memset(in.topic, 'X', sizeof in.topic);

  TEST_ASSERT_EQUAL_STRING_MESSAGE("Warm", d.text, "the label aliased the caller's buffer");
  TEST_ASSERT_EQUAL_STRING_MESSAGE("temperature_profile", d.key,
                                   "the key aliased the caller's buffer");
}

void test_the_fan_preset_topic_writes_the_ventilation_mode_select(void) {
  // Home Assistant reaches a fan's preset on its own topic, but the six labels belong to an entity
  // called ventilation_mode -- so this branch REWRITES the key, and nothing downstream would
  // notice if it stopped: comfoair_command_from_entity("fan", ...) also accepts these labels and
  // would build a different command from the same payload.
  const comfoair_mqtt_decision_t d = decide(P "/fan/preset/command", "Auto");
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_SUBMIT, d.kind);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("ventilation_mode", d.key,
                                   "the preset topic must address the ventilation_mode select");
  TEST_ASSERT_TRUE_MESSAGE(d.optimistic_text,
                           "ventilation_mode has no feedback PDO and must be written optimistically");
  TEST_ASSERT_EQUAL_STRING("Auto", d.text);

  // "Auto" is three frames and the order is the contract: release boost, release away, and release
  // manual LAST, a second behind the other two.
  TEST_ASSERT_EQUAL_UINT8(3, d.command.count);
  TEST_ASSERT_EQUAL_STRING("85 15 01 06", hex(d.command, 0).c_str());
  TEST_ASSERT_EQUAL_STRING("85 15 01 0b", hex(d.command, 1).c_str());
  TEST_ASSERT_EQUAL_STRING("85 15 08 01", hex(d.command, 2).c_str());

  // A preset the registry does not list is refused, not mapped to the nearest.
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_REJECT, decide(P "/fan/preset/command", "Nonsense").kind);
  // And the preset topic is not the on/off topic: "ON" is not one of the six labels.
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_REJECT, decide(P "/fan/preset/command", "ON").kind);
}

void test_every_option_label_the_registry_carries_fits_the_decision(void) {
  // COMFOAIR_MQTT_LABEL_MAX is DERIVED from this table, and the table is generated -- so the
  // derivation has to be re-checked by the build rather than remembered. Truncating a label would
  // turn a valid command into one comfoair_state_set_option then declines, with nothing saying why.
  size_t longest = 0;
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
    const comfoair_entity_t &e = COMFOAIR_ENTITIES[i];
    for (uint8_t o = 0; o < e.option_count; o++) {
      const size_t n = strlen(e.options[o]);
      if (n > longest)
        longest = n;
      char why[160];
      snprintf(why, sizeof why, "'%s' (%s) is %zu bytes and does not fit COMFOAIR_MQTT_LABEL_MAX",
               e.options[o], e.key, n);
      TEST_ASSERT_TRUE_MESSAGE(n < COMFOAIR_MQTT_LABEL_MAX, why);
    }
  }
  // The header says 18. If this fails the header is out of date, not the bound.
  TEST_ASSERT_EQUAL_UINT_MESSAGE(18, longest,
                                 "the longest registry label changed; re-derive LABEL_MAX's comment");
}

void test_a_payload_that_fills_the_inbound_buffer_is_refused_and_never_truncated(void) {
  // The event handler admits up to COMFOAIR_MQTT_PAYLOAD_MAX - 1 bytes, and the decision now
  // carries only a label. A 511-byte payload must be refused for being no option the registry
  // lists -- not shortened until it happens to match one, and not read past its end.
  std::string huge(COMFOAIR_MQTT_PAYLOAD_MAX - 1, 'A');
  TEST_ASSERT_EQUAL_UINT(511, huge.size());

  const comfoair_mqtt_decision_t select = decide(P "/bypass_mode/command", huge.c_str());
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_REJECT, select.kind);
  TEST_ASSERT_NOT_NULL(select.reason);

  // The same length on a switch, a number and an operation: each is refused by its own guard.
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_REJECT, decide(P "/away_mode/command", huge.c_str()).kind);
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_REJECT, decide(P "/boost_duration/command", huge.c_str()).kind);
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_REJECT, decide(P "/ops/set_bypass/set", huge.c_str()).kind);

  // A label that IS listed still works at the far end of the same buffer, so the refusal above is
  // about the label and not about the length of the buffer it arrived in.
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_SUBMIT, decide(P "/bypass_mode/command", "Closed").kind);
}

// --- what is refused ------------------------------------------------------------------------------

void test_an_entity_the_registry_does_not_have_is_refused(void) {
  // Inside our tree, so it is a refusal and not an IGNORE: somebody addressed this device with a
  // name it does not carry, and that is worth counting.
  const comfoair_mqtt_decision_t d = decide(P "/no_such_entity/command", "ON");
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_REJECT, d.kind);
  TEST_ASSERT_NOT_NULL(d.reason);
}

void test_a_read_only_entity_is_refused_rather_than_silently_accepted(void) {
  // The registry marks most entities unwritable. A temperature reading is one of them, and
  // accepting a write to it would be an entity Home Assistant offers and the firmware ignores.
  const comfoair_mqtt_decision_t d = decide(P "/supply_air_temp/command", "21");
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_REJECT, d.kind);
  TEST_ASSERT_NOT_NULL(d.reason);
}

void test_an_operation_body_that_cannot_be_read_is_refused_and_never_defaulted(void) {
  // "a missing or ill-typed field is a refusal, never a default" -- none of the six operations has
  // one (docs/interfaces/rest-api.md lists them), and for three of them an invented duration means FOREVER.
  const comfoair_mqtt_decision_t bad = decide(P "/ops/set_bypass/set", "{");
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_REJECT, bad.kind);
  TEST_ASSERT_NOT_NULL(bad.reason);

  // An operation this device does not have is a refusal too, and not a crash.
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_REJECT,
                    decide(P "/ops/set_nothing/set", "{\"mode\":\"open\"}").kind);

  // And the path works: a readable body becomes the command test_command pins for set_bypass.
  const comfoair_mqtt_decision_t ok =
      decide(P "/ops/set_bypass/set", "{\"mode\":\"open\",\"duration\":600}");
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_SUBMIT, ok.kind);
  TEST_ASSERT_EQUAL_STRING("84 15 02 01 00 00 00 00 58 02 00 00 01", hex(ok.command, 0).c_str());
  TEST_ASSERT_FALSE_MESSAGE(ok.optimistic_text,
                            "an operation has no entity to write optimistically");
}

// --- the one rule that reads state ----------------------------------------------------------------

void test_turning_the_fan_on_resumes_the_level_the_unit_is_actually_running_at(void) {
  // Always sending Low would turn "on" into a downgrade for a house that was on High. The state
  // model's answer wins over the stored default, but ONLY when the unit has reported (`known`) and
  // is running (`on`) -- a fan that is off reports speed 1, because a fan cannot show speed 0, and
  // taking that as a resume level would be the downgrade this rule exists to prevent.
  comfoair_command_durations_t d = defaults();
  d.fan_resume_level = 2;

  comfoair_fan_state_t fan = {};
  fan.known = true;
  fan.on    = true;
  fan.speed = 3;
  const comfoair_mqtt_decision_t running = decide(msg(P "/fan/command", "ON"), d, fan);
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_SUBMIT, running.kind);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("84 15 01 01 00 00 00 00 01 00 00 00 03",
                                   last_frame(running.command).c_str(),
                                   "a running fan's own level must win");

  fan.on = false;
  const comfoair_mqtt_decision_t stopped = decide(msg(P "/fan/command", "ON"), d, fan);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("84 15 01 01 00 00 00 00 01 00 00 00 02",
                                   last_frame(stopped.command).c_str(),
                                   "a fan that is off reports speed 1 and must not override");

  fan.on    = true;
  fan.known = false;
  const comfoair_mqtt_decision_t unreported = decide(msg(P "/fan/command", "ON"), d, fan);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("84 15 01 01 00 00 00 00 01 00 00 00 02",
                                   last_frame(unreported.command).c_str(),
                                   "nothing has been reported, so the stored default stands");

  // And a caller with no fan at all is answered rather than dereferenced.
  const comfoair_ha_ctx_t c = ctx();
  const comfoair_mqtt_inbound_t in = msg(P "/fan/command", "ON");
  const comfoair_mqtt_decision_t none = comfoair_mqtt_decide(&c, &in, &d, NULL);
  TEST_ASSERT_EQUAL_STRING("84 15 01 01 00 00 00 00 01 00 00 00 02", last_frame(none.command).c_str());
}

void test_a_null_argument_is_answered_rather_than_dereferenced(void) {
  // This runs on a message from anything that can reach the broker; a NULL here is a bug upstream
  // and it must not be a crash on somebody's ventilation unit.
  const comfoair_ha_ctx_t c = ctx();
  const comfoair_mqtt_inbound_t in = msg(P "/away_mode/command", "ON");
  const comfoair_command_durations_t d = defaults();
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_IGNORE, comfoair_mqtt_decide(NULL, &in, &d, NULL).kind);
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_IGNORE, comfoair_mqtt_decide(&c, NULL, &d, NULL).kind);
  TEST_ASSERT_EQUAL(COMFOAIR_MQTT_IGNORE, comfoair_mqtt_decide(&c, &in, NULL, NULL).kind);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_a_retained_command_is_refused_because_the_broker_replays_it_for_ever);
  RUN_TEST(test_a_topic_from_another_tree_is_ignored_rather_than_counted_as_a_refusal);

  RUN_TEST(test_a_switch_takes_on_or_off_and_nothing_else);
  RUN_TEST(test_the_fan_takes_on_or_off_on_its_own_topic_and_never_a_number);
  RUN_TEST(test_a_fan_speed_must_be_a_number_or_the_house_goes_to_away);
  RUN_TEST(test_a_number_that_is_not_a_number_is_refused_rather_than_read_as_zero);

  RUN_TEST(test_a_non_finite_payload_reaches_neither_the_fan_nor_a_stored_duration);

  RUN_TEST(test_a_duration_is_stored_rather_than_sent_and_carries_its_minutes);
  RUN_TEST(test_a_select_is_submitted_and_its_label_carried_for_the_optimistic_write);
  RUN_TEST(test_the_carried_label_is_a_copy_and_not_a_pointer_into_the_callers_buffer);

  RUN_TEST(test_the_fan_preset_topic_writes_the_ventilation_mode_select);
  RUN_TEST(test_every_option_label_the_registry_carries_fits_the_decision);
  RUN_TEST(test_a_payload_that_fills_the_inbound_buffer_is_refused_and_never_truncated);

  RUN_TEST(test_an_entity_the_registry_does_not_have_is_refused);
  RUN_TEST(test_a_read_only_entity_is_refused_rather_than_silently_accepted);
  RUN_TEST(test_an_operation_body_that_cannot_be_read_is_refused_and_never_defaulted);

  RUN_TEST(test_turning_the_fan_on_resumes_the_level_the_unit_is_actually_running_at);
  RUN_TEST(test_a_null_argument_is_answered_rather_than_dereferenced);
  return UNITY_END();
}
