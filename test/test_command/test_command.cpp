// What bytes leave this device and go into somebody's ventilation unit.
//
// Every other suite in this project checks that the firmware read something correctly. This one
// checks what it WRITES, and that is a different kind of stake: the reference document records
// that a single wrong unit byte turns a diagnostic read into a bricking write
// (comfoair-pdo-rmi.md §0, "Commands that can brick the unit" -- "Never send any >= 0x80 command
// to unit 0x01 (NODE)"). So the unit byte of every frame this file can produce is pinned below by
// number, not by reasoning.
//
// The other reason this suite exists is a failure that already happened. Phase 3's counter-review
// found that two of the thirteen writable entities were dead -- a select's wire byte is its option
// INDEX while an enum's is a VALUE from the table, the two shared one lookup, and no test touched
// a select, so those selects never received a value at all. The first test here walks the whole
// registry and demands a builder for every writable entity, so the next entity added without one
// fails the build rather than the house.
#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "comfoair_command.h"
#include "registry_generated.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

// A frame as hex, for readable failures. TEST_ASSERT_EQUAL_HEX8_ARRAY prints indices; a wrong
// unit byte has to be legible at a glance in the failure line.
std::string hex(const comfoair_command_frame_t &f) {
  static const char *digits = "0123456789abcdef";
  std::string out;
  for (uint8_t i = 0; i < f.len; i++) {
    if (i)
      out += ' ';
    out += digits[f.bytes[i] >> 4];
    out += digits[f.bytes[i] & 0x0f];
  }
  return out;
}

std::string hex(const comfoair_command_t &c, uint8_t index) {
  return index < c.count ? hex(c.frames[index]) : std::string("<no such frame>");
}

comfoair_command_durations_t defaults(void) {
  comfoair_command_durations_t d;
  comfoair_command_durations_defaults(&d);
  return d;
}

comfoair_command_t op(const char *name, const comfoair_command_params_t &params,
                      comfoair_command_err_t *err) {
  comfoair_command_t out = {};
  const comfoair_command_durations_t d = defaults();
  *err = comfoair_command_from_op(name, &params, &d, &out);
  return out;
}

comfoair_command_params_t no_params(void) {
  comfoair_command_params_t p = {};
  return p;
}

}  // namespace

// --- the registry is walked whole ------------------------------------------------------------

void test_every_writable_entity_has_a_builder(void) {
  // THE TEST THIS SUITE EXISTS FOR. A writable entity with no way to write it is an entity the
  // API offers and the device silently ignores -- which is exactly what shipped in phase 3.
  size_t writable = 0;
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
    const comfoair_entity_t *e = &COMFOAIR_ENTITIES[i];
    if (!e->writable)
      continue;
    writable++;
    TEST_ASSERT_TRUE_MESSAGE(comfoair_command_knows_entity(e->key), e->key);
  }
  // Pinned so that a table change which drops a writable entity is also visible here rather than
  // silently making this test weaker.
  TEST_ASSERT_EQUAL_size_t_MESSAGE(13, writable, "the registry's writable count moved");
}

void test_no_builder_exists_for_an_entity_the_registry_does_not_have(void) {
  // The other direction. A builder for a key nobody can reach is dead code that looks live, and
  // the usual way it appears is a key renamed in pdo_table.py and not here.
  const char *const invented[] = {"", "not_an_entity", "bypass", "fan_speed", "boost"};
  for (size_t i = 0; i < sizeof invented / sizeof invented[0]; i++)
    TEST_ASSERT_FALSE_MESSAGE(comfoair_command_knows_entity(invented[i]), invented[i]);
}

void test_a_read_only_entity_is_refused_rather_than_silently_accepted(void) {
  // "Supply Temperature" is a sensor. A firmware that accepted a write to it would answer 200 to
  // a request it did no work for, and the owner would be waiting for something to happen.
  TEST_ASSERT_FALSE(comfoair_command_knows_entity("supply_temperature"));

  comfoair_command_t out = {};
  const comfoair_command_durations_t d = defaults();
  comfoair_command_value_t v = {};
  v.kind = COMFOAIR_COMMAND_VALUE_NUMBER;
  v.number = 1;
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_ERR_UNKNOWN_ENTITY,
                    comfoair_command_from_entity("supply_temperature", &v, &d, &out));
}

// --- the rule that is not about correctness but about damage ---------------------------------

void test_no_frame_this_component_can_build_addresses_a_unit_that_would_be_bricked(void) {
  // comfoair-pdo-rmi.md §0, "Commands that can brick the unit": never send any command >= 0x80 to
  // unit 0x01 (NODE), and never send 0x80 or 0x82 to unit 0x20. The legitimate >= 0x80 set is
  // 0x84/0x85/0x83 on SCHEDULE (0x15). Byte 0 is the command, byte 1 is the unit.
  //
  // Asserted over EVERY command this component can produce, by walking the registry and the six
  // operations, rather than per builder: a builder added later is covered without anybody
  // remembering to extend this.
  std::vector<comfoair_command_t> all;

  const comfoair_command_durations_t d = defaults();
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
    const comfoair_entity_t *e = &COMFOAIR_ENTITIES[i];
    if (!e->writable)
      continue;
    // Every option index, every boolean, and a couple of numbers: enough to reach every branch.
    for (int n = 0; n < 8; n++) {
      comfoair_command_value_t v = {};
      v.kind = COMFOAIR_COMMAND_VALUE_NUMBER;
      v.number = n;
      comfoair_command_t out = {};
      if (comfoair_command_from_entity(e->key, &v, &d, &out) == COMFOAIR_COMMAND_OK)
        all.push_back(out);
      v.kind = COMFOAIR_COMMAND_VALUE_BOOL;
      v.boolean = (n & 1) != 0;
      out = comfoair_command_t{};
      if (comfoair_command_from_entity(e->key, &v, &d, &out) == COMFOAIR_COMMAND_OK)
        all.push_back(out);
    }
    if (e->options != NULL) {
      for (uint8_t o = 0; o < e->option_count; o++) {
        comfoair_command_value_t v = {};
        v.kind = COMFOAIR_COMMAND_VALUE_TEXT;
        v.text = e->options[o];
        comfoair_command_t out = {};
        if (comfoair_command_from_entity(e->key, &v, &d, &out) == COMFOAIR_COMMAND_OK)
          all.push_back(out);
      }
    }
  }

  struct { const char *name; const char *mode; const char *profile; int level; int duration; bool enabled; }
  const ops[] = {
      {"boost", nullptr, nullptr, 0, 600, false},
      {"boost", nullptr, nullptr, 0, 0, false},
      {"set_bypass", "auto", nullptr, 0, 600, false},
      {"set_bypass", "open", nullptr, 0, 600, false},
      {"set_bypass", "closed", nullptr, 0, 0, false},
      {"set_balance_mode", "balanced", nullptr, 0, 600, false},
      {"set_balance_mode", "supply_only", nullptr, 0, 600, false},
      {"set_balance_mode", "exhaust_only", nullptr, 0, 0, false},
      {"set_ventilation_level", nullptr, nullptr, 0, 0, false},
      {"set_ventilation_level", nullptr, nullptr, 3, 0, false},
      {"set_temperature_profile", nullptr, "normal", 0, 0, false},
      {"set_temperature_profile", nullptr, "warm", 0, 0, false},
      {"set_away", nullptr, nullptr, 0, 600, true},
      {"set_away", nullptr, nullptr, 0, 0, false},
  };
  for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++) {
    comfoair_command_params_t p = no_params();
    p.mode = ops[i].mode;
    p.profile = ops[i].profile;
    p.has_level = true;
    p.level = ops[i].level;
    p.has_duration = true;
    p.duration_seconds = ops[i].duration;
    p.has_enabled = true;
    p.enabled = ops[i].enabled;
    comfoair_command_err_t err;
    const comfoair_command_t c = op(ops[i].name, p, &err);
    if (err == COMFOAIR_COMMAND_OK)
      all.push_back(c);
  }

  TEST_ASSERT_TRUE_MESSAGE(all.size() >= 20, "the sweep did not reach enough builders");

  for (const comfoair_command_t &c : all) {
    for (uint8_t f = 0; f < c.count; f++) {
      const comfoair_command_frame_t &frame = c.frames[f];
      TEST_ASSERT_TRUE_MESSAGE(frame.len >= 4, hex(frame).c_str());
      const uint8_t command = frame.bytes[0];
      const uint8_t unit = frame.bytes[1];
      if (command >= 0x80) {
        // Only 0x83/0x84/0x85 on SCHEDULE. Nothing else, ever.
        TEST_ASSERT_TRUE_MESSAGE(command == 0x83 || command == 0x84 || command == 0x85,
                                 hex(frame).c_str());
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x15, unit, hex(frame).c_str());
      } else {
        // The only sub-0x80 command this firmware sends is a property write, and the only unit it
        // writes to is TEMPHUMCONTROL.
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, command, hex(frame).c_str());
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x1d, unit, hex(frame).c_str());
      }
    }
  }
}

void test_no_frame_is_empty_and_none_carries_padding(void) {
  // An empty command becomes a valid DLC=0 CAN frame: a silent no-op that looks like a successful
  // send (rmi_frame.h:154-155). And padding is not part of the protocol -- enable is exactly 13
  // bytes, disable exactly 4 (rmi_frame.h:11). Three extra trailing zero bytes are harmless, but
  // this firmware does not emit them.
  comfoair_command_params_t p = no_params();
  p.has_duration = true;
  p.duration_seconds = 600;
  comfoair_command_err_t err;
  const comfoair_command_t c = op("boost", p, &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK, err);
  TEST_ASSERT_EQUAL_UINT8(1, c.count);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(13, c.frames[0].len, hex(c, 0).c_str());

  p.duration_seconds = 0;
  const comfoair_command_t off = op("boost", p, &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK, err);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(4, off.frames[0].len, hex(off, 0).c_str());
}

// --- the six operations, byte for byte --------------------------------------------------------

void test_boost_on_and_off(void) {
  // 84 15 01 06 00000000 <secs LE> 03 -- and the value byte is 0x03, the HIGH preset, not a
  // boolean (comfoair-pdo-rmi.md §3, "Command table").
  comfoair_command_params_t p = no_params();
  p.has_duration = true;
  p.duration_seconds = 600;  // 0x258
  comfoair_command_err_t err;
  comfoair_command_t c = op("boost", p, &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK, err);
  TEST_ASSERT_EQUAL_STRING("84 15 01 06 00 00 00 00 58 02 00 00 03", hex(c, 0).c_str());

  // 0 CANCELS boost -- it does not mean "forever". That is the opposite of what it means to the
  // other three timed operations, and the asymmetry is deliberate: it is the only way the `boost`
  // operation can end a boost early.
  p.duration_seconds = 0;
  c = op("boost", p, &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK, err);
  TEST_ASSERT_EQUAL_STRING("85 15 01 06", hex(c, 0).c_str());

  // Negative folds into the same branch as zero.
  p.duration_seconds = -1;
  c = op("boost", p, &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK, err);
  TEST_ASSERT_EQUAL_STRING("85 15 01 06", hex(c, 0).c_str());
}

void test_set_bypass(void) {
  comfoair_command_params_t p = no_params();
  p.has_duration = true;
  p.duration_seconds = 600;
  comfoair_command_err_t err;

  p.mode = "open";
  TEST_ASSERT_EQUAL_STRING("84 15 02 01 00 00 00 00 58 02 00 00 01",
                           hex(op("set_bypass", p, &err), 0).c_str());
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK, err);

  p.mode = "closed";
  TEST_ASSERT_EQUAL_STRING("84 15 02 01 00 00 00 00 58 02 00 00 02",
                           hex(op("set_bypass", p, &err), 0).c_str());

  // auto is a release, and it IGNORES the duration -- clear_timer takes no timeout.
  p.mode = "auto";
  const comfoair_command_t a = op("set_bypass", p, &err);
  TEST_ASSERT_EQUAL_UINT8(1, a.count);
  TEST_ASSERT_EQUAL_STRING("85 15 02 01", hex(a, 0).c_str());

  // 0 means "until told otherwise" here, which on the wire is the indefinite timeout ffffffff.
  p.mode = "open";
  p.duration_seconds = 0;
  TEST_ASSERT_EQUAL_STRING("84 15 02 01 00 00 00 00 ff ff ff ff 01",
                           hex(op("set_bypass", p, &err), 0).c_str());

  p.mode = "sideways";
  op("set_bypass", p, &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_ERR_BAD_VALUE, err);
}

void test_set_balance_mode_always_sends_both_frames(void) {
  // THE RULE. Dropping the second frame leaves the other subunit's timer running, and switching
  // extract-only -> supply-only would then leave the unit in an undefined state with both timers
  // set.
  //
  // And the value byte is 01 for BOTH subunits 06 and 07: the GETTIMERENTRY read-back says 01, and
  // 00 is the easy mistake (rmi_frame.h:107-108).
  comfoair_command_params_t p = no_params();
  p.has_duration = true;
  p.duration_seconds = 600;
  comfoair_command_err_t err;

  comfoair_command_t c = (p.mode = "supply_only", op("set_balance_mode", p, &err));
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK, err);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, c.count, "supply_only must disable exhaust_only too");
  TEST_ASSERT_EQUAL_STRING("84 15 06 01 00 00 00 00 58 02 00 00 01", hex(c, 0).c_str());
  TEST_ASSERT_EQUAL_STRING("85 15 07 01", hex(c, 1).c_str());

  c = (p.mode = "exhaust_only", op("set_balance_mode", p, &err));
  TEST_ASSERT_EQUAL_UINT8(2, c.count);
  TEST_ASSERT_EQUAL_STRING("84 15 07 01 00 00 00 00 58 02 00 00 01", hex(c, 0).c_str());
  TEST_ASSERT_EQUAL_STRING("85 15 06 01", hex(c, 1).c_str());

  c = (p.mode = "balanced", op("set_balance_mode", p, &err));
  TEST_ASSERT_EQUAL_UINT8(2, c.count);
  TEST_ASSERT_EQUAL_STRING("85 15 06 01", hex(c, 0).c_str());
  TEST_ASSERT_EQUAL_STRING("85 15 07 01", hex(c, 1).c_str());

  p.mode = "supply";
  op("set_balance_mode", p, &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_ERR_BAD_VALUE, err);
}

void test_set_ventilation_level(void) {
  // 84 15 01 01 00000000 01000000 <level> -- the timeout is ONE SECOND and that is what makes the
  // change stick. rmi_frame.h:33-34: "The official app sends a 1-second timer for level and
  // manual-mode changes and that is what makes them stick. Do not substitute TIMEOUT_INDEFINITE."
  comfoair_command_params_t p = no_params();
  p.has_level = true;
  p.level = 2;
  comfoair_command_err_t err;

  // With permanent manual OFF: one frame.
  comfoair_command_durations_t d = defaults();
  d.permanent_manual = false;
  comfoair_command_t c = {};
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK,
                    comfoair_command_from_op("set_ventilation_level", &p, &d, &c));
  TEST_ASSERT_EQUAL_UINT8(1, c.count);
  TEST_ASSERT_EQUAL_STRING("84 15 01 01 00 00 00 00 01 00 00 00 02", hex(c, 0).c_str());

  // With permanent manual ON (the default): the manual command comes FIRST, then the level.
  d.permanent_manual = true;
  c = comfoair_command_t{};
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK,
                    comfoair_command_from_op("set_ventilation_level", &p, &d, &c));
  TEST_ASSERT_EQUAL_UINT8(2, c.count);
  TEST_ASSERT_EQUAL_STRING("84 15 08 01 00 00 00 00 01 00 00 00 01", hex(c, 0).c_str());
  TEST_ASSERT_EQUAL_STRING("84 15 01 01 00 00 00 00 01 00 00 00 02", hex(c, 1).c_str());

  // 0 is LEGAL and means Away -- the lowest preset, not "off". An HRV never stops ventilating.
  p.level = 0;
  c = comfoair_command_t{};
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK,
                    comfoair_command_from_op("set_ventilation_level", &p, &d, &c));
  TEST_ASSERT_EQUAL_STRING("84 15 01 01 00 00 00 00 01 00 00 00 00", hex(c, 1).c_str());

  p.level = 4;
  op("set_ventilation_level", p, &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_ERR_BAD_VALUE, err);
  p.level = -1;
  op("set_ventilation_level", p, &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_ERR_BAD_VALUE, err);
}

void test_set_temperature_profile(void) {
  // The timeout is hardcoded ffffffff. There is no duration parameter and there must not be one.
  comfoair_command_params_t p = no_params();
  comfoair_command_err_t err;

  p.profile = "normal";
  TEST_ASSERT_EQUAL_STRING("84 15 03 01 00 00 00 00 ff ff ff ff 00",
                           hex(op("set_temperature_profile", p, &err), 0).c_str());
  p.profile = "cool";
  TEST_ASSERT_EQUAL_STRING("84 15 03 01 00 00 00 00 ff ff ff ff 01",
                           hex(op("set_temperature_profile", p, &err), 0).c_str());
  p.profile = "warm";
  TEST_ASSERT_EQUAL_STRING("84 15 03 01 00 00 00 00 ff ff ff ff 02",
                           hex(op("set_temperature_profile", p, &err), 0).c_str());

  // PROTOCOL order 0/1/2 is Normal/Cool/Warm. The unit's own menu prints WARM/NORMAL/COOL, and
  // "fixing" the order to match the display would silently send Warm when the owner picks Normal
  // (pdo_table.py, SELECTS row temperature_profile).
  p.profile = "auto";
  op("set_temperature_profile", p, &err);
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_COMMAND_ERR_BAD_VALUE, err,
                            "85 15 03 01 is a hypothesis nobody has ever sent -- see the spec");
}

void test_set_away(void) {
  // 84 15 01 0b 00000000 <secs> 00 -- the value byte is 0x00, not 0x01.
  comfoair_command_params_t p = no_params();
  p.has_enabled = true;
  p.has_duration = true;
  p.enabled = true;
  p.duration_seconds = 600;
  comfoair_command_err_t err;
  TEST_ASSERT_EQUAL_STRING("84 15 01 0b 00 00 00 00 58 02 00 00 00",
                           hex(op("set_away", p, &err), 0).c_str());

  // 0 is indefinite here.
  p.duration_seconds = 0;
  TEST_ASSERT_EQUAL_STRING("84 15 01 0b 00 00 00 00 ff ff ff ff 00",
                           hex(op("set_away", p, &err), 0).c_str());

  // Off ignores the duration entirely.
  p.enabled = false;
  p.duration_seconds = 600;
  const comfoair_command_t off = op("set_away", p, &err);
  TEST_ASSERT_EQUAL_UINT8(1, off.count);
  TEST_ASSERT_EQUAL_STRING("85 15 01 0b", hex(off, 0).c_str());
}

void test_a_missing_operation_parameter_is_refused_because_none_has_a_default(void) {
  // No operation parameter has a default (docs/interfaces/rest-api.md lists them). A firmware
  // that invented one would act on a number the caller never chose -- and for three of these
  // operations the invented number would be "forever".
  comfoair_command_err_t err;
  op("boost", no_params(), &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_ERR_MISSING_PARAM, err);
  op("set_bypass", no_params(), &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_ERR_MISSING_PARAM, err);
  op("set_balance_mode", no_params(), &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_ERR_MISSING_PARAM, err);
  op("set_ventilation_level", no_params(), &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_ERR_MISSING_PARAM, err);
  op("set_temperature_profile", no_params(), &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_ERR_MISSING_PARAM, err);
  op("set_away", no_params(), &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_ERR_MISSING_PARAM, err);
}

void test_an_unknown_operation_is_refused(void) {
  comfoair_command_err_t err;
  op("set_everything", no_params(), &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_ERR_UNKNOWN_OP, err);
  op("", no_params(), &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_ERR_UNKNOWN_OP, err);
}

// --- the entities ------------------------------------------------------------------------------

namespace {
comfoair_command_t by_text(const char *key, const char *text, comfoair_command_err_t *err) {
  comfoair_command_value_t v = {};
  v.kind = COMFOAIR_COMMAND_VALUE_TEXT;
  v.text = text;
  const comfoair_command_durations_t d = defaults();
  comfoair_command_t out = {};
  *err = comfoair_command_from_entity(key, &v, &d, &out);
  return out;
}
}  // namespace

void test_a_select_is_written_by_its_option_label_and_the_index_is_the_wire_byte(void) {
  // For five of the seven selects the option INDEX is literally the byte sent. The option order in
  // pdo_table.py is PROTOCOL order for exactly this reason, and reordering it to match the unit's
  // own menu would silently invert On and Off (pdo_table.py, SELECTS row temperature_passive).
  comfoair_command_err_t err;
  TEST_ASSERT_EQUAL_STRING("03 1d 01 04 00", hex(by_text("temperature_passive", "Off", &err), 0).c_str());
  TEST_ASSERT_EQUAL_STRING("03 1d 01 04 01", hex(by_text("temperature_passive", "Auto only", &err), 0).c_str());
  TEST_ASSERT_EQUAL_STRING("03 1d 01 04 02", hex(by_text("temperature_passive", "On", &err), 0).c_str());
  TEST_ASSERT_EQUAL_STRING("03 1d 01 06 02", hex(by_text("humidity_comfort", "On", &err), 0).c_str());
  TEST_ASSERT_EQUAL_STRING("03 1d 01 07 00", hex(by_text("humidity_protection", "Off", &err), 0).c_str());
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK, err);

  // A label that is not in the list is refused rather than mapped to the nearest one.
  by_text("temperature_passive", "on", &err);  // case matters: these are the table's labels
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_ERR_BAD_VALUE, err);
  by_text("temperature_passive", "Maybe", &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_ERR_BAD_VALUE, err);
}

void test_balance_mode_uses_its_index_to_choose_frames_not_as_a_wire_byte(void) {
  // The exception that makes "the index is the byte" false in general. Writing "Supply Only" is
  // the same two frames set_balance_mode builds -- NOT a property write with value 1.
  comfoair_command_err_t err;
  const comfoair_command_t c = by_text("balance_mode", "Supply Only", &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK, err);
  TEST_ASSERT_EQUAL_UINT8(2, c.count);
  TEST_ASSERT_EQUAL_STRING("85 15 07 01", hex(c, 1).c_str());
}

void test_writing_the_bypass_select_is_the_same_command_as_the_operation(void) {
  comfoair_command_err_t err;
  const comfoair_command_t c = by_text("bypass_mode", "Auto", &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK, err);
  TEST_ASSERT_EQUAL_STRING("85 15 02 01", hex(c, 0).c_str());
}

void test_the_away_switch_maps_to_the_away_command(void) {
  comfoair_command_value_t v = {};
  v.kind = COMFOAIR_COMMAND_VALUE_BOOL;
  v.boolean = false;
  const comfoair_command_durations_t d = defaults();
  comfoair_command_t out = {};
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK, comfoair_command_from_entity("away_mode", &v, &d, &out));
  TEST_ASSERT_EQUAL_STRING("85 15 01 0b", hex(out, 0).c_str());
}

void test_a_duration_entity_produces_no_frame_at_all(void) {
  // The four durations are never on the wire by themselves: the unit is told a duration only as
  // part of a command (pdo_table.py, NumberSpec). Writing one is a local state change, and a builder
  // that invented a frame for it would send something nobody asked for.
  const char *const durations[] = {"boost_duration", "bypass_duration", "balance_duration",
                                   "away_duration"};
  for (size_t i = 0; i < sizeof durations / sizeof durations[0]; i++) {
    comfoair_command_value_t v = {};
    v.kind = COMFOAIR_COMMAND_VALUE_NUMBER;
    v.number = 30;
    const comfoair_command_durations_t d = defaults();
    comfoair_command_t out = {};
    TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_COMMAND_OK,
                              comfoair_command_from_entity(durations[i], &v, &d, &out),
                              durations[i]);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, out.count, durations[i]);
    TEST_ASSERT_TRUE_MESSAGE(out.stores_duration, durations[i]);
  }
}

void test_a_duration_outside_the_registry_range_is_refused(void) {
  // The range is the registry's, not a second copy: away_duration allows 0 (indefinite) and the
  // other three start at 1, and a firmware with its own idea of the range would refuse a value
  // the API advertises.
  comfoair_command_value_t v = {};
  v.kind = COMFOAIR_COMMAND_VALUE_NUMBER;
  const comfoair_command_durations_t d = defaults();
  comfoair_command_t out = {};

  v.number = 0;
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_ERR_BAD_VALUE,
                    comfoair_command_from_entity("boost_duration", &v, &d, &out));
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK,
                    comfoair_command_from_entity("away_duration", &v, &d, &out));
  v.number = 1441;
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_ERR_BAD_VALUE,
                    comfoair_command_from_entity("boost_duration", &v, &d, &out));
  v.number = 1440;
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK,
                    comfoair_command_from_entity("boost_duration", &v, &d, &out));
}

void test_the_stored_duration_is_what_the_command_carries_and_minutes_become_seconds(void) {
  // Minutes in the UI, seconds on the wire -- getting the x60 wrong makes a 60-minute boost last a
  // minute.
  comfoair_command_durations_t d = defaults();
  d.boost_minutes = 5;
  comfoair_command_value_t v = {};
  v.kind = COMFOAIR_COMMAND_VALUE_TEXT;
  v.text = "Boost";
  comfoair_command_t out = {};
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK,
                    comfoair_command_from_entity("ventilation_mode", &v, &d, &out));
  // 5 minutes = 300 s = 0x12c
  TEST_ASSERT_EQUAL_STRING("84 15 01 06 00 00 00 00 2c 01 00 00 03", hex(out, 0).c_str());
}

void test_the_fan_preset_auto_releases_boost_and_away_and_manual_last(void) {
  // Three frames, and the third is DEFERRED: manual_off() goes 1000 ms after the two releases.
  // Both clears are needed because boost and away override the scheduler; the delay is not
  // cosmetic.
  comfoair_command_err_t err;
  const comfoair_command_t c = by_text("ventilation_mode", "Auto", &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK, err);
  TEST_ASSERT_EQUAL_UINT8(3, c.count);
  TEST_ASSERT_EQUAL_STRING("85 15 01 06", hex(c, 0).c_str());
  TEST_ASSERT_EQUAL_STRING("85 15 01 0b", hex(c, 1).c_str());
  TEST_ASSERT_EQUAL_STRING("85 15 08 01", hex(c, 2).c_str());
  TEST_ASSERT_EQUAL_UINT16(0, c.frames[0].delay_ms);
  TEST_ASSERT_EQUAL_UINT16(0, c.frames[1].delay_ms);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(1000, c.frames[2].delay_ms, "manual_off is deferred");
}

void test_the_fan_is_written_by_speed_and_off_is_the_away_level(void) {
  // "Off" on this unit is the Away level; an HRV never stops ventilating entirely.
  const comfoair_command_durations_t d = defaults();
  comfoair_command_value_t v = {};
  comfoair_command_t out = {};

  v.kind = COMFOAIR_COMMAND_VALUE_BOOL;
  v.boolean = false;
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK, comfoair_command_from_entity("fan", &v, &d, &out));
  TEST_ASSERT_EQUAL_STRING("84 15 01 01 00 00 00 00 01 00 00 00 00",
                           hex(out, out.count - 1).c_str());

  v.kind = COMFOAIR_COMMAND_VALUE_NUMBER;
  v.number = 3;
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK, comfoair_command_from_entity("fan", &v, &d, &out));
  TEST_ASSERT_EQUAL_STRING("84 15 01 01 00 00 00 00 01 00 00 00 03",
                           hex(out, out.count - 1).c_str());

  v.number = 4;
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_ERR_BAD_VALUE,
                    comfoair_command_from_entity("fan", &v, &d, &out));
}

void test_the_fan_accepts_the_preset_labels_its_own_metadata_advertises(void) {
  // The registry marks `fan` writable with six options. An entity that advertises an option and
  // then answers 422 for it is a contract the API published and the firmware does not keep.
  comfoair_command_err_t err;
  const comfoair_command_t boost = by_text("fan", "Boost", &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK, err);
  TEST_ASSERT_EQUAL_STRING("84 15 01 06 00 00 00 00 10 0e 00 00 03", hex(boost, 0).c_str());

  const comfoair_command_t automatic = by_text("fan", "Auto", &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK, err);
  TEST_ASSERT_EQUAL_UINT8(3, automatic.count);

  by_text("fan", "Turbo", &err);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_ERR_BAD_VALUE, err);
}

void test_turning_the_fan_on_resumes_its_level_instead_of_dropping_the_house_to_low(void) {
  // Always sending Low would turn "on" into a downgrade for a house that was on High. Low is the
  // fallback for a unit that has reported nothing yet, and nothing else.
  comfoair_command_durations_t d;
  comfoair_command_durations_defaults(&d);
  comfoair_command_value_t v = {};
  v.kind = COMFOAIR_COMMAND_VALUE_BOOL;
  v.boolean = true;
  comfoair_command_t out = {};

  d.fan_resume_level = 3;
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK, comfoair_command_from_entity("fan", &v, &d, &out));
  TEST_ASSERT_EQUAL_STRING("84 15 01 01 00 00 00 00 01 00 00 00 03",
                           hex(out, out.count - 1).c_str());

  d.fan_resume_level = 0;  // nothing reported yet
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK, comfoair_command_from_entity("fan", &v, &d, &out));
  TEST_ASSERT_EQUAL_STRING("84 15 01 01 00 00 00 00 01 00 00 00 01",
                           hex(out, out.count - 1).c_str());

  // Off is still the Away level whatever was running.
  d.fan_resume_level = 3;
  v.boolean = false;
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK, comfoair_command_from_entity("fan", &v, &d, &out));
  TEST_ASSERT_EQUAL_STRING("84 15 01 01 00 00 00 00 01 00 00 00 00",
                           hex(out, out.count - 1).c_str());
}

void test_a_null_argument_is_answered_rather_than_dereferenced(void) {
  const comfoair_command_durations_t d = defaults();
  comfoair_command_value_t v = {};
  comfoair_command_t out = {};
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_COMMAND_OK, comfoair_command_from_entity(nullptr, &v, &d, &out));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_COMMAND_OK,
                        comfoair_command_from_entity("fan", nullptr, &d, &out));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_COMMAND_OK, comfoair_command_from_entity("fan", &v, &d, nullptr));
  TEST_ASSERT_FALSE(comfoair_command_knows_entity(nullptr));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_every_writable_entity_has_a_builder);
  RUN_TEST(test_no_builder_exists_for_an_entity_the_registry_does_not_have);
  RUN_TEST(test_a_read_only_entity_is_refused_rather_than_silently_accepted);
  RUN_TEST(test_no_frame_this_component_can_build_addresses_a_unit_that_would_be_bricked);
  RUN_TEST(test_no_frame_is_empty_and_none_carries_padding);

  RUN_TEST(test_boost_on_and_off);
  RUN_TEST(test_set_bypass);
  RUN_TEST(test_set_balance_mode_always_sends_both_frames);
  RUN_TEST(test_set_ventilation_level);
  RUN_TEST(test_set_temperature_profile);
  RUN_TEST(test_set_away);
  RUN_TEST(test_a_missing_operation_parameter_is_refused_because_none_has_a_default);
  RUN_TEST(test_an_unknown_operation_is_refused);

  RUN_TEST(test_a_select_is_written_by_its_option_label_and_the_index_is_the_wire_byte);
  RUN_TEST(test_balance_mode_uses_its_index_to_choose_frames_not_as_a_wire_byte);
  RUN_TEST(test_writing_the_bypass_select_is_the_same_command_as_the_operation);
  RUN_TEST(test_the_away_switch_maps_to_the_away_command);
  RUN_TEST(test_a_duration_entity_produces_no_frame_at_all);
  RUN_TEST(test_a_duration_outside_the_registry_range_is_refused);
  RUN_TEST(test_the_stored_duration_is_what_the_command_carries_and_minutes_become_seconds);
  RUN_TEST(test_the_fan_preset_auto_releases_boost_and_away_and_manual_last);
  RUN_TEST(test_the_fan_is_written_by_speed_and_off_is_the_away_level);
  RUN_TEST(test_the_fan_accepts_the_preset_labels_its_own_metadata_advertises);
  RUN_TEST(test_turning_the_fan_on_resumes_its_level_instead_of_dropping_the_house_to_low);
  RUN_TEST(test_a_null_argument_is_answered_rather_than_dereferenced);
  return UNITY_END();
}
