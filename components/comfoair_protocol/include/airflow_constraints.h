// PDO 230 airflow-constraint bitset.
//
// The bit *indices* are the documented PDO 230 bitset (docs/reference/comfoair-pdo-rmi.md §4,
// capability #4); not documented by the vendor. Indices are 0-based, LSB first. Do not guess at
// undocumented bits.
//
// The *wording* and the *+/- grouping* come from Zehnder's own STATUS > FLOW RESTRICTIONS screen
// (docs/reference/comfoair-q-hardware.md section 13.6; User p. 19). That screen prints a direction
// against every reason:
//
//   "+"  the unit is running ABOVE the requested airflow, because some control is asking for more
//   "-"  the unit is throttled BELOW the requested airflow
//
//   "-" (below the request): bits 2-10
//   "+" (above the request): bits 12-27 and 47-54
//
// DO NOT flatten the direction away. A flat list makes a "+" reason such as Humidity Comfort read
// as a restriction, which is the exact opposite of what the unit is doing: the unit is ventilating
// harder than asked, it is not being held back. The direction is the whole point of the screen.
//
// The direction split also explains why the preheater has two bits (4 and 22, sometimes labelled
// PreheaterNegative / PreheaterPositive): Zehnder's table simply lists PREHEATER twice, once in
// each direction. With the direction carried by the group, both collapse to a single "Preheater".
//
// Bit 45 gates the list: clear means "No restrictions"; every bit not listed below stays ignored.
//
// Includes nothing but the standard library so the host tests compile it directly.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace comfoair {

/// Home Assistant refuses entity states longer than 255 characters.
static constexpr std::size_t AIRFLOW_CONSTRAINTS_MAX_LENGTH = 255;

inline std::string airflow_constraints_to_string(uint64_t value) {
  // Bit 45 clear: no constraint list (aiocomfoconnect's calculate_airflow_constraints() returns
  // None), and the Q350 sends all zeros while nothing restricts it. Every other bit is ignored
  // then. DO NOT bring back "Unknown": it read as a fault on an unrestricted unit.
  if ((value & (UINT64_C(1) << 45)) == 0)
    return "No restrictions";

  std::string below;
  std::string above;
  const auto bit = [value](int index) { return (value & (UINT64_C(1) << index)) != 0; };
  const auto add = [](std::string &group, const char *name) {
    if (!group.empty())
      group += ", ";
    group += name;
  };

  // "-": the unit is running below the requested airflow. Wording per STATUS > FLOW RESTRICTIONS.
  if (bit(2) || bit(3))
    add(below, "Resistance");
  if (bit(4))
    add(below, "Preheater");  // not "PreheaterNegative": the direction is the group
  if (bit(5) || bit(7))
    add(below, "Noise Guard");
  if (bit(6) || bit(8))
    add(below, "Resistance Guard");
  if (bit(9))
    add(below, "Frost Protection");
  if (bit(10))
    add(below, "Bypass");

  // "+": the unit is running above the requested airflow because a control is asking for more.
  //
  // Bits 12-15 and 18: the display's group heading is ANALOG INPUT but its items are
  // "0-10V INPUT 1..4" (hardware sections 11 and 13.6; User p. 19), so the items get the item
  // wording. These terminals exist only on an Option Box, never on the main board.
  if (bit(12))
    add(above, "0-10V Input 1");
  if (bit(13))
    add(above, "0-10V Input 2");
  if (bit(14))
    add(above, "0-10V Input 3");
  if (bit(15))
    add(above, "0-10V Input 4");
  // Bit 16 "Hood" and bits 47-54 "CO2 Zone 1..8" have NO counterpart in any of the four Zehnder
  // PDFs: there is no "hood"/"cooker" occurrence anywhere and no CO2-zone row in FLOW RESTRICTIONS.
  // They are kept because the bitset documents them, presumably for other models or later
  // firmware, and they are grouped as "+" by bit position rather than by Zehnder's documentation.
  if (bit(16))
    add(above, "Hood");
  if (bit(18))
    add(above, "0-10V Input");
  if (bit(19))
    add(above, "ComfoCool");
  if (bit(22))
    add(above, "Preheater");  // not "PreheaterPositive": the direction is the group
  // Bits 23/24 keep the bitset's preset/proportional distinction. Zehnder's display collapses both
  // to a single "+ RF SENSOR", but RF SENSOR FUNCTION = FLOW PRESET | FLOW PROPORTIONAL is a real
  // documented setting (User p. 24), so keeping them apart adds information rather than invents it.
  if (bit(23))
    add(above, "RF Sensor (preset)");
  if (bit(24))
    add(above, "RF Sensor (proportional)");
  // Bit 25 is TEMPERATURE PASSIVE, not "TemperatureComfort". Zehnder calls this control
  // TEMPERATURE PASSIVE in both STATUS > FLOW RESTRICTIONS and ADVANCED SETTINGS > SENSOR
  // VENTILATION (hardware sections 13.5 and 13.6; User pp. 19, 23). "Temperature comfort" appears
  // nowhere in any of the four manuals - it is an invented label - and this firmware already
  // publishes a select named "Temperature Passive", so the two would contradict each other while
  // naming the same control on the same device.
  if (bit(25))
    add(above, "Temperature Passive");
  if (bit(26))
    add(above, "Humidity Comfort");
  if (bit(27))
    add(above, "Humidity Protection");
  if (bit(47))
    add(above, "CO2 Zone 1");
  if (bit(48))
    add(above, "CO2 Zone 2");
  if (bit(49))
    add(above, "CO2 Zone 3");
  if (bit(50))
    add(above, "CO2 Zone 4");
  if (bit(51))
    add(above, "CO2 Zone 5");
  if (bit(52))
    add(above, "CO2 Zone 6");
  if (bit(53))
    add(above, "CO2 Zone 7");
  if (bit(54))
    add(above, "CO2 Zone 8");

  std::string out;
  if (!below.empty())
    out = "Below: " + below;
  if (!above.empty()) {
    if (!out.empty())
      out += " | ";
    out += "Above: " + above;
  }

  // DO NOT return "None": Home Assistant's MQTT sensor reads that payload as an unknown state,
  // so an unrestricted unit would show as unknown.
  if (out.empty())
    return "No restrictions";
  if (out.size() > AIRFLOW_CONSTRAINTS_MAX_LENGTH) {
    out.resize(AIRFLOW_CONSTRAINTS_MAX_LENGTH - 3);
    out += "...";
  }
  return out;
}

}  // namespace comfoair
