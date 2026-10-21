// The ONE list of known RMI properties, and the names of units and error codes.
//
// Content: the documented NODE/unit property table (not published by Zehnder), one row per table
// row, in table order. Names follow docs/reference/comfoair-pdo-rmi.md §4 where it is better than
// the table's description (capabilities #1, #2, #3, #6, #9, #10): Zehnder's own terms (WARM /
// NORMAL / COOL, HEATING LIMIT RMOT, TEMPERATURE PASSIVE), because those are what the owner reads
// on the unit's display.
//
// The "??" rows are KEPT with a NULL name: their format is documented (e.g. `980d` as INT16), so
// the probe can still print a number where a heuristic would have to guess. DO NOT drop them to
// "tidy" the table -- an i16 guessed as two separate bytes is a wrong report.
//
// Rows from the live unit (Q350 R1.11.0, docs/reference/rmi-probe-findings-q350.md)
// are added where the bytes alone settle the format, and where the live unit contradicts the
// table's width the live width wins (FILTER 0x02, VENTILATIONCONFIG 0x08). A name nobody
// documented ends in " (inferred)": it is a diagnostic reading of the values, not a fact, and
// DO NOT promote such a name to a Home Assistant entity without a second source.
//
// Access is copied as the table states it: blank stays NULL even where a write is presumed
// (comfoair-pdo-rmi.md §4 footnote) -- this report says what is documented, not what we believe.
#include "comfoair_probe_report_internal.h"

namespace probe_report {

namespace {

constexpr uint8_t NODE = 0x01, HMI = 0x1A, FILTER = 0x1C, TEMPHUM = 0x1D, VENTCFG = 0x1E,
                  NODECFG = 0x20, POSTHEATER = 0x27, CO2ZONES = 0x2B;

// A row's subunit that matches every subunit >= 1. Subunits are numbered from 1 (the walk starts
// there; no documented row uses 0), so 0 cannot collide with a real one. Used ONLY by CO2ZONES,
// whose eight subunits carry the same properties; DO NOT use it to save rows elsewhere -- a
// wildcard on a unit whose subunits differ names a value it has never seen.
constexpr uint8_t ANY_SUBUNIT = 0x00;

const char *const OFF_AUTO_ON[] = {"off", "auto_only", "on"};
const char *const PROFILE_MODE[] = {"adaptive", "fixed", "external_setpoint"};
const char *const ALTITUDE[] = {"0-500m", "500-1000m", "1000-1500m", "1500-2000m"};
const char *const CONTROL_MODE[] = {"flow_control", "constant_flow"};
const char *const BATHROOM_MODE[] = {"fixed", "mirrored"};
const char *const ORIENTATION[] = {"left", "right"};

#define LABELS(a) a, (uint8_t)(sizeof(a) / sizeof(a[0]))
#define ROW(unit, prop, name, access, fmt, div, label) \
    {unit, 0x01, prop, name, access, Format::fmt, div, label, nullptr, 0}
#define ZONE(prop, name, fmt, label) \
    {CO2ZONES, ANY_SUBUNIT, prop, name, nullptr, Format::fmt, 1, label, nullptr, 0}
#define ENUM(unit, prop, name, access, labels) \
    {unit, 0x01, prop, name, access, Format::Enum, 1, nullptr, LABELS(labels)}

const char *const C = "°C";
const char *const M3H = "m³/h";
const char *const PPM = "ppm";

const Property CATALOGUE[] = {
    // NODE. 0x06 is a version word (see version_text()), not a counter.
    ROW(NODE, 0x01, nullptr, nullptr, U8, 1, nullptr),
    ROW(NODE, 0x02, nullptr, nullptr, U8, 1, nullptr),
    ROW(NODE, 0x03, nullptr, nullptr, U8, 1, nullptr),
    ROW(NODE, 0x04, "Serial number", "ro", String, 1, nullptr),
    ROW(NODE, 0x05, nullptr, nullptr, U8, 1, nullptr),
    ROW(NODE, 0x06, "Firmware version", "ro", Version, 1, nullptr),
    ROW(NODE, 0x07, nullptr, nullptr, U32, 1, nullptr),
    ROW(NODE, 0x08, "Model", "ro", String, 1, nullptr),
    ROW(NODE, 0x09, nullptr, nullptr, U32, 1, nullptr),
    ROW(NODE, 0x0A, nullptr, nullptr, U32, 1, nullptr),
    ROW(NODE, 0x0B, "Article number", "ro", String, 1, nullptr),
    ROW(NODE, 0x0C, nullptr, nullptr, String, 1, nullptr),
    ROW(NODE, 0x0D, "Country", "ro", String, 1, nullptr),
    ROW(NODE, 0x14, "Unit name", "ro", String, 1, nullptr),

    // HMI, not in the documented table. 0x03: live `46` = 70 in 10..100 step 5, a percentage that
    // moves in display-sized steps. 0x04: live "ENGLISH\0", the display language.
    ROW(HMI, 0x03, "Display brightness (inferred)", nullptr, U8, 1, "%"),
    ROW(HMI, 0x04, "Display language", nullptr, String, 1, nullptr),

    // FILTER. The table marks 0x02's name with a "?" (a guess, kept as the best available).
    // 0x02 is UINT16 although the table says UINT8: the live unit answers two bytes, `1500` = 21
    // days, range `0000ffff`. DO NOT restore U8: a catalogued numeric of the wrong width gets no
    // number at all, which is what the first live report showed.
    ROW(FILTER, 0x02, "Filter pre-warning period", nullptr, U16, 1, "days"),
    ROW(FILTER, 0x03, "Filter lifetime", "rw", U16, 1, "days"),

    // TEMPHUMCONTROL. Temperatures are INT16 tenths: `180 -> 18°C`.
    ROW(TEMPHUM, 0x01, nullptr, nullptr, U8, 1, nullptr),
    ROW(TEMPHUM, 0x02, "Heating limit RMOT", "rw", I16, 10, C),
    ROW(TEMPHUM, 0x03, "Cooling limit RMOT", "rw", I16, 10, C),
    ENUM(TEMPHUM, 0x04, "Temperature passive", "rw", OFF_AUTO_ON),
    ROW(TEMPHUM, 0x05, nullptr, "rw", U8, 1, nullptr),
    ENUM(TEMPHUM, 0x06, "Humidity comfort", "rw", OFF_AUTO_ON),
    ENUM(TEMPHUM, 0x07, "Humidity protection", "rw", OFF_AUTO_ON),
    ENUM(TEMPHUM, 0x08, "Temperature profile mode", "rw", PROFILE_MODE),
    ROW(TEMPHUM, 0x0A, "Target temperature, warm profile", "rw", I16, 10, C),
    ROW(TEMPHUM, 0x0B, "Target temperature, normal profile", "rw", I16, 10, C),
    ROW(TEMPHUM, 0x0C, "Target temperature, cool profile", "rw", I16, 10, C),
    ROW(TEMPHUM, 0x0D, nullptr, nullptr, U8, 1, nullptr),

    // VENTILATIONCONFIG. Presets are INT16 m³/h, NOT l/s (comfoair-pdo-rmi.md §4 #1).
    // Unbalance is INT16 tenths of a percent: `9dff` = -99 -> -9.9 % (§4 #9).
    ROW(VENTCFG, 0x01, nullptr, nullptr, U8, 1, nullptr),
    ROW(VENTCFG, 0x02, nullptr, nullptr, U8, 1, nullptr),
    ROW(VENTCFG, 0x03, "Airflow preset away", "rw", I16, 1, M3H),
    ROW(VENTCFG, 0x04, "Airflow preset low", "rw", I16, 1, M3H),
    ROW(VENTCFG, 0x05, "Airflow preset medium", "rw", I16, 1, M3H),
    ROW(VENTCFG, 0x06, "Airflow preset high", "rw", I16, 1, M3H),
    ENUM(VENTCFG, 0x07, "Altitude", nullptr, ALTITUDE),
    // 0x08 is a "??" INT16 in the table. Live it is 325 with range `0000ffff` (UINT16), and 325 is the
    // top of both the medium and high preset ranges on that Q350: the unit's airflow ceiling.
    ROW(VENTCFG, 0x08, "Airflow upper limit (inferred)", nullptr, U16, 1, M3H),
    ENUM(VENTCFG, 0x09, "Ventilation control mode", nullptr, CONTROL_MODE),
    ROW(VENTCFG, 0x0A, nullptr, nullptr, I16, 1, nullptr),
    ROW(VENTCFG, 0x0B, "Bathroom switch activation delay", nullptr, I16, 1, "s"),
    ROW(VENTCFG, 0x0C, "Bathroom switch deactivation delay", nullptr, U8, 1, "min"),
    ENUM(VENTCFG, 0x0D, "Bathroom switch mode", nullptr, BATHROOM_MODE),
    ROW(VENTCFG, 0x0E, nullptr, nullptr, U8, 1, nullptr),
    ROW(VENTCFG, 0x0F, nullptr, nullptr, U8, 1, nullptr),
    ROW(VENTCFG, 0x11, nullptr, nullptr, I16, 1, nullptr),
    ROW(VENTCFG, 0x12, "Unbalance", nullptr, I16, 10, "%"),
    ROW(VENTCFG, 0x13, nullptr, nullptr, I16, 1, nullptr),
    ROW(VENTCFG, 0x14, nullptr, nullptr, I16, 1, nullptr),
    ROW(VENTCFG, 0x15, nullptr, nullptr, I16, 1, nullptr),
    ROW(VENTCFG, 0x16, nullptr, nullptr, I16, 1, nullptr),
    ROW(VENTCFG, 0x17, nullptr, nullptr, I16, 1, nullptr),
    ROW(VENTCFG, 0x18, nullptr, nullptr, I16, 1, nullptr),
    ROW(VENTCFG, 0x19, nullptr, nullptr, I16, 1, nullptr),
    ROW(VENTCFG, 0x1A, nullptr, nullptr, I16, 1, nullptr),
    ROW(VENTCFG, 0x1B, nullptr, nullptr, I16, 1, nullptr),
    ROW(VENTCFG, 0x1C, nullptr, nullptr, I16, 1, nullptr),
    ROW(VENTCFG, 0x1D, nullptr, nullptr, I16, 1, nullptr),
    ROW(VENTCFG, 0x1E, nullptr, nullptr, I16, 1, nullptr),

    // NODECONFIGURATION. 0x03 is the maintainer password: named here so the report
    // says what was hidden, but the hiding is is_redacted()'s, not this row's.
    ROW(NODECFG, 0x01, nullptr, nullptr, U8, 1, nullptr),
    ROW(NODECFG, 0x03, "Maintainer password", nullptr, String, 1, nullptr),
    ENUM(NODECFG, 0x04, "Orientation", nullptr, ORIENTATION),
    ROW(NODECFG, 0x05, nullptr, nullptr, U8, 1, nullptr),
    ROW(NODECFG, 0x06, nullptr, nullptr, U8, 1, nullptr),
    ROW(NODECFG, 0x07, nullptr, nullptr, U8, 1, nullptr),
    ROW(NODECFG, 0x08, nullptr, nullptr, U8, 1, nullptr),
    ROW(NODECFG, 0x0A, nullptr, nullptr, U8, 1, nullptr),
    ROW(NODECFG, 0x0B, nullptr, nullptr, U8, 1, nullptr),
    ROW(NODECFG, 0x0C, nullptr, nullptr, U8, 1, nullptr),


    // POSTHEATER, not in the documented table. 0x03: live `6400` = 100 in 50..150 step 50; unsigned,
    // and unnamed because nothing says what the 100 is.
    ROW(POSTHEATER, 0x03, nullptr, nullptr, U16, 1, nullptr),

    // CO2ZONES, not in the documented table: eight subunits whose 0x01 is a one-hot bit (live 1, 2, 4
    // .. 128), matching PDO 230 bits 47-54 CO2ZoneX1..8. 0x02/0x03/0x05 live 2000/400/1050 in
    // 0..2000 step 50 -- ppm-sized, and the upper/lower/target reading is ours.
    ZONE(0x01, "Zone bit (inferred)", U8, nullptr),
    ZONE(0x02, "CO2 upper limit (inferred)", U16, PPM),
    ZONE(0x03, "CO2 lower limit (inferred)", U16, PPM),
    ZONE(0x05, "CO2 setpoint (inferred)", U16, PPM),
};

#undef ZONE
#undef ENUM
#undef ROW
#undef LABELS

// The documented unit names, spelled as documented: these are the names Zehnder's tools use.
struct UnitName {
    uint8_t id;
    const char *name;
};
const UnitName UNITS[] = {
    {0x01, "NODE"},          {0x02, "COMFOBUS"},          {0x03, "ERROR"},
    {0x15, "SCHEDULE"},      {0x16, "VALVE"},             {0x17, "FAN"},
    {0x18, "POWERSENSOR"},   {0x19, "PREHEATER"},         {0x1A, "HMI"},
    {0x1B, "RFCOMMUNICATION"}, {0x1C, "FILTER"},          {0x1D, "TEMPHUMCONTROL"},
    {0x1E, "VENTILATIONCONFIG"}, {0x20, "NODECONFIGURATION"}, {0x21, "TEMPERATURESENSOR"},
    {0x22, "HUMIDITYSENSOR"}, {0x23, "PRESSURESENSOR"},   {0x24, "PERIPHERALS"},
    {0x25, "ANALOGINPUT"},   {0x26, "COOKERHOOD"},        {0x27, "POSTHEATER"},
    {0x28, "COMFOFOND"},
    {0x2B, "CO2ZONES (inferred)"},  // undocumented: see the CO2ZONES rows above
};

// Why only these three on another node: the documented LAN C table gives 0x04, 0x06 and 0x14 the
// same meaning as the unit's, and gives 0x01-0x03/0x05/0x07 different values with no meaning at
// all. 0x08 (model) is absent there. Any other node is a stranger; claiming the unit's meanings
// for it would put a wrong name on a real value.
bool shared_identity(uint8_t unit, uint8_t subunit, uint8_t property)
{
    return unit == NODE && subunit == 0x01 &&
           (property == 0x04 || property == 0x06 || property == 0x14);
}

}  // namespace

const char *format_name(Format f)
{
    switch (f) {
    case Format::String: return "string";
    case Format::U8: return "u8";
    case Format::U16: return "u16";
    case Format::I16: return "i16";
    case Format::U32: return "u32";
    case Format::Version: return "version";
    case Format::Enum: return "enum";
    case Format::None: break;
    }
    return nullptr;
}

size_t format_width(Format f)
{
    switch (f) {
    case Format::U8:
    case Format::Enum: return 1;
    case Format::U16:
    case Format::I16: return 2;
    case Format::U32:
    case Format::Version: return 4;
    case Format::String:
    case Format::None: break;
    }
    return 0;
}

const Property *catalogue_find(uint8_t target_node, uint8_t node, uint8_t unit, uint8_t subunit,
                               uint8_t property)
{
    if (node != target_node && !shared_identity(unit, subunit, property))
        return nullptr;
    for (const Property &p : CATALOGUE)
        if (p.unit == unit && p.property == property &&
            (p.subunit == subunit || (p.subunit == ANY_SUBUNIT && subunit != 0)))
            return &p;
    return nullptr;
}

size_t catalogue_size(void) { return sizeof CATALOGUE / sizeof CATALOGUE[0]; }

const char *unit_name(uint8_t target_node, uint8_t node, uint8_t unit)
{
    // Every node answers NODE (the documented LAN C table); the rest of the table is the unit's.
    if (node != target_node && unit != NODE)
        return nullptr;
    for (const UnitName &u : UNITS)
        if (u.id == unit)
            return u.name;
    return nullptr;
}

const char *error_name(uint8_t code)
{
    switch (code) {
    case 11: return "unknown_command";
    case 12: return "unknown_unit";
    case 13: return "unknown_subunit";
    case 14: return "unknown_property";
    case 15: return "no_range";
    case 30: return "out_of_range";
    case 32: return "not_gettable";
    case 40: return "internal_error";
    case 41: return "internal_error_bad_command";
    default: return "unknown";
    }
}

bool is_redacted(uint8_t unit, uint8_t subunit, uint8_t property)
{
    return unit == NODECFG && subunit == 0x01 && property == 0x03;
}

}  // namespace probe_report
