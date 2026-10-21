// comfoair_command_readback_pdo(): which PDO to request after a command (comfoair_command.h).
// A file of its own: it reads a built command back and builds nothing.
#include <cstring>

#include "comfoair_command.h"
#include "registry_generated.h"
#include "rmi_frame.h"

namespace {

// A frame that starts (ENABLETIMERENTRY) or cancels (DISABLETIMERENTRY) SCHEDULE subunit 1
// property 11 -- the away timer. All four bytes are checked: property 0x0B on another subunit, or
// under a property write, is a different thing.
bool touches_away_timer(const comfoair_command_frame_t &f)
{
    namespace rmi = comfoair::rmi;
    const bool timer = f.bytes[0] == rmi::CMD_ENABLE_TIMER || f.bytes[0] == rmi::CMD_DISABLE_TIMER;
    return f.len >= 4 && timer && f.bytes[1] == rmi::UNIT_SCHEDULE && f.bytes[2] == rmi::SUBUNIT_FAN &&
           f.bytes[3] == rmi::PROP_AWAY;
}

// The away_mode switch's PDO, from the generated row. DO NOT write the number here: the switch's
// source is decided in tools/pdo_table.py (AWAY_SWITCH), and a second copy would keep requesting
// the old PDO the day the table moves it.
int32_t away_switch_pdo()
{
    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++)
        if (COMFOAIR_ENTITIES[i].kind == COMFOAIR_KIND_SWITCH &&
            strcmp(COMFOAIR_ENTITIES[i].key, "away_mode") == 0)
            return COMFOAIR_ENTITIES[i].pdo;
    return -1;
}

}  // namespace

int32_t comfoair_command_readback_pdo(const comfoair_command_t *command)
{
    if (command == nullptr)
        return -1;
    for (uint8_t i = 0; i < command->count && i < COMFOAIR_COMMAND_MAX_FRAMES; i++)
        if (touches_away_timer(command->frames[i]))
            return away_switch_pdo();
    return -1;
}
