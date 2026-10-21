// Which PDO to request after a command, so the state of what the command drove comes back in
// about a second instead of at the next hourly sweep.
//
// Its own suite because test_command.cpp is past the 600-line suite ceiling. The rule under test:
// every command that starts or cancels SCHEDULE subunit 1 timer 11 (the away timer) -- the
// away_mode switch, set_away, and the "Auto" preset's away release -- asks for the away_mode
// switch's own PDO, read from the registry, and nothing else asks for anything.
#include <unity.h>

#include <cstring>

#include "comfoair_command.h"
#include "registry_generated.h"

static comfoair_command_durations_t g_durations;
static comfoair_command_t g_cmd;

void setUp(void)
{
    comfoair_command_durations_defaults(&g_durations);
    memset(&g_cmd, 0, sizeof g_cmd);
}
void tearDown(void) {}

// The switch's PDO as the registry has it -- never a number restated here.
static int32_t away_switch_pdo(void)
{
    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++)
        if (strcmp(COMFOAIR_ENTITIES[i].key, "away_mode") == 0)
            return COMFOAIR_ENTITIES[i].pdo;
    TEST_FAIL_MESSAGE("no away_mode row");
    return -1;
}

static void from_entity(const char *key, comfoair_command_value_t v)
{
    TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK,
                      comfoair_command_from_entity(key, &v, &g_durations, &g_cmd));
}

static comfoair_command_value_t boolean(bool b)
{
    comfoair_command_value_t v{};
    v.kind = COMFOAIR_COMMAND_VALUE_BOOL;
    v.boolean = b;
    return v;
}

static comfoair_command_value_t text(const char *t)
{
    comfoair_command_value_t v{};
    v.kind = COMFOAIR_COMMAND_VALUE_TEXT;
    v.text = t;
    return v;
}

void test_the_switch_pdo_is_a_real_pdo(void) { TEST_ASSERT_TRUE(away_switch_pdo() > 0); }

void test_away_mode_on_and_off_ask_for_the_switch_pdo(void)
{
    from_entity("away_mode", boolean(true));
    TEST_ASSERT_EQUAL_INT32(away_switch_pdo(), comfoair_command_readback_pdo(&g_cmd));
    memset(&g_cmd, 0, sizeof g_cmd);
    from_entity("away_mode", boolean(false));
    TEST_ASSERT_EQUAL_INT32(away_switch_pdo(), comfoair_command_readback_pdo(&g_cmd));
}

void test_the_set_away_operation_asks_for_it_too(void)
{
    comfoair_command_params_t p{};
    p.has_enabled = true;
    p.enabled = true;
    p.has_duration = true;
    p.duration_seconds = 0;
    TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK,
                      comfoair_command_from_op("set_away", &p, &g_durations, &g_cmd));
    TEST_ASSERT_EQUAL_INT32(away_switch_pdo(), comfoair_command_readback_pdo(&g_cmd));
}

void test_the_auto_preset_releases_away_and_asks_for_it(void)
{
    from_entity("ventilation_mode", text("Auto"));
    TEST_ASSERT_EQUAL_INT32(away_switch_pdo(), comfoair_command_readback_pdo(&g_cmd));
    memset(&g_cmd, 0, sizeof g_cmd);
    from_entity("fan", text("Auto"));
    TEST_ASSERT_EQUAL_INT32(away_switch_pdo(), comfoair_command_readback_pdo(&g_cmd));
}

void test_commands_that_leave_the_away_timer_alone_ask_for_nothing(void)
{
    from_entity("fan", text("High"));
    TEST_ASSERT_EQUAL_INT32(-1, comfoair_command_readback_pdo(&g_cmd));
    memset(&g_cmd, 0, sizeof g_cmd);
    from_entity("ventilation_mode", text("Boost"));
    TEST_ASSERT_EQUAL_INT32(-1, comfoair_command_readback_pdo(&g_cmd));
    memset(&g_cmd, 0, sizeof g_cmd);
    from_entity("bypass_mode", text("Auto"));
    TEST_ASSERT_EQUAL_INT32(-1, comfoair_command_readback_pdo(&g_cmd));
}

void test_a_duration_write_has_no_frame_and_asks_for_nothing(void)
{
    comfoair_command_value_t v{};
    v.kind = COMFOAIR_COMMAND_VALUE_NUMBER;
    v.number = 30;
    from_entity("away_duration", v);
    TEST_ASSERT_EQUAL_INT32(-1, comfoair_command_readback_pdo(&g_cmd));
}

void test_only_schedule_subunit_1_property_11_counts(void)
{
    // The same property byte on another subunit, and the same bytes under a non-timer command,
    // are not the away timer.
    static const uint8_t other_subunit[] = {0x85, 0x15, 0x02, 0x0b};
    static const uint8_t set_property[] = {0x03, 0x15, 0x01, 0x0b, 0x00};
    static const uint8_t other_unit[] = {0x85, 0x1d, 0x01, 0x0b};
    const uint8_t *frames[] = {other_subunit, set_property, other_unit};
    const uint8_t lens[] = {4, 5, 4};
    for (int i = 0; i < 3; i++) {
        memset(&g_cmd, 0, sizeof g_cmd);
        memcpy(g_cmd.frames[0].bytes, frames[i], lens[i]);
        g_cmd.frames[0].len = lens[i];
        g_cmd.count = 1;
        TEST_ASSERT_EQUAL_INT32(-1, comfoair_command_readback_pdo(&g_cmd));
    }
}

void test_null_asks_for_nothing(void) { TEST_ASSERT_EQUAL_INT32(-1, comfoair_command_readback_pdo(NULL)); }

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_switch_pdo_is_a_real_pdo);
    RUN_TEST(test_away_mode_on_and_off_ask_for_the_switch_pdo);
    RUN_TEST(test_the_set_away_operation_asks_for_it_too);
    RUN_TEST(test_the_auto_preset_releases_away_and_asks_for_it);
    RUN_TEST(test_commands_that_leave_the_away_timer_alone_ask_for_nothing);
    RUN_TEST(test_a_duration_write_has_no_frame_and_asks_for_nothing);
    RUN_TEST(test_only_schedule_subunit_1_property_11_counts);
    RUN_TEST(test_null_asks_for_nothing);
    return UNITY_END();
}
