// The unit's error id -> English text.
//
// Confidence: the id -> text table is documented (comfoair-pdo-rmi.md §4, capability #7) but not
// published by Zehnder, and not yet confirmed on hardware with an active error. Zehnder's service
// manual documents only the display codes for R1.6.0 (comfoair-q-hardware.md §16), whose numbering
// against these ids is itself unverified.
//
// The unit numbers its errors differently by firmware: at R1.4.0 and below, ids from 70 on are
// shifted (no analog-input rows, so 70 is "ComfoHood ..." where newer firmware has "Analog input
// 1 ..."). So there are three tables: a shared BASE (ids 21-69), the rows from 70 for firmware
// above R1.4.0 (NEWER) and those for R1.4.0 and below (OLDER). The extra rows all start at 70, so
// none overrides a base row and the lookup below is base first, then the selected extra table.
//
// Strings are verbatim, typos included ("condensor", "PREHEAT ERROR , 1081"): they are what a
// Zehnder service manual or a search will match. Three are UTF-8 ("…", "–", "CO₂") and the
// renderer passes this table's bytes through unescaped for that reason.
//
// INFORMATIONAL (`INFO` below): an id that is a notice, not a fault, and so never turns the
// `unit_error` problem sensor on (it is still listed in `active_errors` and /api/system). The unit
// reports both kinds through the same bitset; neither aiocomfoconnect (const.py ERRORS /
// ERRORS_140: texts only) nor the bitset itself separates them. Evidence, per id (newer / older):
//   79 / 75 "Order new filters now": the user manual (5.5.1, 5.5.2) calls EXPECT FILTER CHANGE
//           SOON a "warning message" the owner may SUPPRESS for a day; the service manual's status
//           LED legend (4.2) lists "Change filters" under Warning (slow flash), not Error.
//   80 / 76 "Service mode is active": service manual 4.2, LED "Warning (... SERVICE MODE
//           active)"; entered by hand from the menu (4.8.9) and ends by itself.
//   89 / 81 "Bypass is in manual mode": the owner's own override (user manual 3.5.3, "Bypass heat
//           recovery for a specific duration"; our set_bypass). DO NOT make it a fault: a
//           user-initiated bypass must never raise a Home Assistant problem.
//   96 / -  "ComfoHood is activated": a state, not a failure -- the cooker hood was switched on;
//           no ERROR code, and the text names no failure.
// Deliberately NOT informational, although they look like notices:
//   77 / 73 "filters ... must be replaced now": user manual 5.5.1 calls CHANGE FILTERS NOW an
//           "error message", against the "warning message" of 79 / 75. (Service manual 6.3 is no
//           evidence either way: its table lists the notices 79 and 80 beside the faults.)
//   78 / 74 "replace or clean the external filter": EXTERNAL FILTER ALARM (1078), "the external
//           filter is dirty" -- a measured condition, not a calendar.
//   34 "The front door is open" and 33 "not commissioned": the unit does not ventilate.
//   100 "Error analysis is in progress…": not in either manual; it is the unit chasing a
//           suspected fault, so it is left raising -- an HV item to revisit on a live unit.
//   97, 98 (no documented meaning) and any id no table knows: unknown is never assumed harmless.
// Every other row carries an ERROR code or describes a malfunction, a lost peripheral or an
// out-of-range reading, and is a fault.
// BEFORE THE FIRMWARE WORD IS READ no id is informational: which numbering applies is unknown, and
// the newer table's notices are faults on an R1.4.0 unit (old 79 POSTHEAT_TEMP, old 80
// GROUND_HEAT_TEMP). The probe task therefore publishes no error entity until the firmware is
// known (they stay unknown), rather than a manual-bypass false alarm. DO NOT reuse the text's
// newer-table guess for this.
#include <cstddef>

#include "comfoair_sysinfo.h"

namespace {

struct Row {
    uint8_t id;
    const char *text;
    bool informational = false;  // see the evidence above
};

constexpr bool INFO = true;

// Ids 21-69, the same on every firmware.
const Row BASE[] = {
    {21, "DANGER! OVERHEATING! Two or more sensors are detecting an incorrect temperature. "
         "Ventilation has stopped."},
    {22, "Temperature too high for ComfoAir Q (TEMP_HRU ERROR)"},
    {23, "The extract air temperature sensor has a malfunction (SENSOR_ETA ERROR)"},
    {24, "The extract air temperature sensor is detecting an incorrect temperature "
         "(TEMP_SENSOR_ETA ERROR)"},
    {25, "The exhaust air temperature sensor has a malfunction (SENSOR_EHA ERROR)"},
    {26, "The exhaust air temperature sensor is detecting an incorrect temperature "
         "(TEMP_SENSOR_EHA ERROR)"},
    {27, "The outdoor air temperature sensor has a malfunction (SENSOR_ODA ERROR)"},
    {28, "The outdoor air temperature sensor is detecting an incorrect temperature "
         "(TEMP_SENSOR_ODA ERROR)"},
    {29, "The pre-conditioned outdoor air temperature sensor has a malfunction"},
    {30, "The pre-conditioned outdoor air temperature sensor is detecting an incorrect "
         "temperature (TEMP_SENSOR_P-ODA ERROR)"},
    {31, "The supply air temperature sensor has a malfunction (SENSOR_SUP ERROR)"},
    {32, "The supply air temperature sensor is detecting an incorrect temperature "
         "(TEMP_SENSOR_SUP ERROR)"},
    {33, "The Ventilation Unit has not been commissioned (INIT ERROR)"},
    {34, "The front door is open"},
    {35, "The Pre-heater is present, but not in the correct position (right/left). "
         "(PREHEAT_LOCATION ERROR)"},
    {37, "The pre-heater has a malfunction (PREHEAT ERROR)"},
    {38, "The pre-heater has a malfunction (PREHEAT ERROR)"},
    {39, "The extract air humidity sensor has a malfunction (SENSOR_ETA ERROR)"},
    {41, "The exhaust air humidity sensor has a malfunction (SENSOR_EHA ERROR)"},
    {43, "The outdoor air humidity sensor has a malfunction (SENSOR_ODA ERROR)"},
    {45, "The outdoor air humidity sensor has a malfunction (SENSOR_P-ODA ERROR)"},
    {47, "The supply air humidity sensor has a malfunction (SENSOR_SUP ERROR)"},
    {49, "The exhaust air flow sensor has a malfunction (SENSOR_EHA ERROR)"},
    {50, "The supply air flow sensor has a malfunction (SENSOR_SUP ERROR)"},
    {51, "The extract air fan has a malfunction (FAN_EHA ERROR)"},
    {52, "The supply air fan has a malfunction (FAN_SUP ERROR)"},
    {53, "Exhaust air pressure too high. Check air outlets, ducts and filters for pollution and "
         "obstructions. Check valve settings (EXT_PRESSURE_EHA ERROR)"},
    {54, "Supply air pressure too high. Check air outlets, ducts and filters for pollution and "
         "obstructions. Check valve settings. (EXT_PRESSURE_SUP ERROR)"},
    {55, "The extract air fan has a malfunction (FAN_EHA ERROR)"},
    {56, "The supply air fan has a malfunction (FAN_SUP ERROR)"},
    {57, "The exhaust air flow is not reaching its set point (AIRFLOW_EHA ERROR)"},
    {58, "The supply air flow is not reaching its set point (AIRFLOW_SUP ERROR)"},
    {59, "Failed to reach required temperature too often for outdoor air after pre-heater "
         "(TEMPCONTROL_P-ODA ERROR)"},
    {60, "Failed to reach required temperature too often for supply air. The modulating by-pass "
         "may have a malfunction. (TEMPCONTROL_SUP ERROR)"},
    {61, "Supply air temperature is too low too often (TEMP_SUP_MIN ERROR)"},
    {62, "Unbalance occurred too often beyond tolerance levels in past period (UNBALANCE ERROR)"},
    {63, "Postheater was present, but is no longer detected (POSTHEAT_CONNECT ERROR)"},
    {64, "Temperature sensor value for supply air ComfoCool exceeded limit too often "
         "(CCOOL_TEMP ERROR)"},
    {65, "Room temperature sensor was present, but is no longer detected (T_ROOM_PRES ERROR)"},
    {66, "RF Communication hardware was present, but is no longer detected (RF_PRES ERROR)"},
    {67, "Option Box was present, but is no longer detected (OPTION_BOX CONNECT ERROR)"},
    {68, "Pre-heater was present, but is no longer detected (PREHEAT_PRES ERROR)"},
    {69, "Postheater was present, but is no longer detected (POSTHEAT_CONNECT ERROR)"},
};

// Firmware above R1.4.0: the rows past BASE.
const Row NEWER[] = {
    {70, "Analog input 1 was present, but is no longer detected (ANALOG_1_PRES ERROR)"},
    {71, "Analog input 2 was present, but is no longer detected (ANALOG_2_PRES ERROR)"},
    {72, "Analog input 3 was present, but is no longer detected (ANALOG_3_PRES ERROR)"},
    {73, "Analog input 4 was present, but is no longer detected (ANALOG_4_PRES ERROR)"},
    {74, "ComfoHood was present, but is no longer detected (HOOD_CONNECT ERROR)"},
    {75, "ComfoCool was present, but is no longer detected (CCOOL_CONNECT ERROR)"},
    {76, "ComfoFond was present, but is no longer detected (GROUND_HEAT_CONNECT ERROR)"},
    {77, "The filters of the Ventilation Unit must be replaced now"},
    {78, "It is necessary to replace or clean the external filter"},
    {79, "Order new filters now, because the remaining filter life time is limited", INFO},
    {80, "Service mode is active (SERVICE MODE)", INFO},
    {81, "Preheater has no communication with the ComfoAir unit (PREHEAT ERROR , 1081)"},
    {82, "ComfoHood temperature error (HOOD_TEMP ERROR)"},
    {83, "Postheater temperature error (POSTHEAT_TEMP ERROR)"},
    {84, "Outdoor temperature of ComfoFond error (GROUND_HEAT_TEMP ERROR)"},
    {85, "Analog input 1 error (ANALOG_1_IN ERROR)"},
    {86, "Analog input 2 error (ANALOG_2_IN ERROR)"},
    {87, "Analog input 3 error (ANALOG_3_IN ERROR)"},
    {88, "Analog input 4 error (ANALOG_4_IN ERROR)"},
    {89, "Bypass is in manual mode", INFO},
    {90, "ComfoCool is overheating"},
    {91, "ComfoCool compressor error (CCOOL_COMPRESSOR ERROR)"},
    {92, "ComfoCool room temperature sensor error (CCOOL_TEMP ERROR)"},
    {93, "ComfoCool condensor temperature sensor error (CCOOL_TEMP ERROR)"},
    {94, "ComfoCool supply air temperature sensor error (CCOOL_TEMP ERROR)"},
    {95, "ComfoHood temperature is too high (HOOD_TEMP ERROR)"},
    {96, "ComfoHood is activated", INFO},
    {97, "QM_Constraint_min_ERR"},  // no meaning documented
    {98, "H_21_qm_min_ERR"},        // no meaning documented
    {99, "Configuration error"},
    {100, "Error analysis is in progress\xe2\x80\xa6"},  // U+2026
    {101, "ComfoNet Error"},
    {102, "The number of CO2 sensors has decreased \xe2\x80\x93 one or more sensors are no longer "
          "detected"},  // U+2013
    {103, "More than 8 sensors detected in a zone"},
    {104, "CO\xe2\x82\x82 Sensor C error"},  // U+2082
};

// Firmware R1.4.0 and below: the rows past BASE.
const Row OLDER[] = {
    {70, "ComfoHood was present, but is no longer detected (HOOD_CONNECT ERROR)"},
    {71, "ComfoCool was present, but is no longer detected (CCOOL_CONNECT ERROR)"},
    {72, "ComfoFond was present, but is no longer detected (GROUND_HEAT_CONNECT ERROR)"},
    {73, "The filters of the Ventilation Unit must be replaced now"},
    {74, "It is necessary to replace or clean the external filter"},
    {75, "Order new filters now, because the remaining filter life time is limited", INFO},
    {76, "Service mode is active (SERVICE MODE)", INFO},
    {77, "Preheater has no communication with the ComfoAir unit (PREHEAT ERROR , 1081)"},
    {78, "ComfoHood temperature error (HOOD_TEMP ERROR)"},
    {79, "Postheater temperature error (POSTHEAT_TEMP ERROR)"},
    {80, "Outdoor temperature of ComfoFond error (GROUND_HEAT_TEMP ERROR)"},
    {81, "Bypass is in manual mode", INFO},
    {82, "ComfoCool is overheating"},
    {83, "ComfoCool compressor error (CCOOL_COMPRESSOR ERROR)"},
    {84, "ComfoCool room temperature sensor error (CCOOL_TEMP ERROR)"},
    {85, "ComfoCool condensor temperature sensor error (CCOOL_TEMP ERROR)"},
    {86, "ComfoCool supply air temperature sensor error (CCOOL_TEMP ERROR)"},
};

// A firmware word `<= 3222278144` (0xC0101000, exactly "R1.4.0") selects OLDER (comfoair-pdo-rmi.md
// §4, capability #7). DO NOT write `<`: R1.4.0 itself is the old numbering.
constexpr uint32_t LAST_OLD_NUMBERING = 3222278144u;

template <size_t N> const Row *find(const Row (&rows)[N], uint16_t id)
{
    for (const Row &r : rows)
        if (r.id == id)
            return &r;
    return nullptr;
}

const Row *lookup(uint16_t id, bool has_firmware, uint32_t firmware_raw)
{
    if (const Row *r = find(BASE, id))
        return r;
    // Without a firmware read the newer numbering: every unit met so far runs R1.11.0, and a
    // wrong guess costs a misleading text beside the id, which the document always carries.
    const bool older = has_firmware && firmware_raw <= LAST_OLD_NUMBERING;
    return older ? find(OLDER, id) : find(NEWER, id);
}

}  // namespace

const char *comfoair_sysinfo_error_text(uint16_t id, bool has_firmware, uint32_t firmware_raw)
{
    const Row *r = lookup(id, has_firmware, firmware_raw);
    return r != nullptr ? r->text : nullptr;
}

bool comfoair_sysinfo_error_informational(uint16_t id, bool has_firmware, uint32_t firmware_raw)
{
    // The text may guess the numbering; the verdict may not (see the evidence above).
    if (!has_firmware)
        return false;
    const Row *r = lookup(id, has_firmware, firmware_raw);
    return r != nullptr && r->informational;
}
