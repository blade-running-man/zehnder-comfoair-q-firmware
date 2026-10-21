// ComfoNet RMI frame construction.
//
// This header deliberately includes nothing but the standard library so that the host tests
// compile the exact same code the firmware runs. Do not add ESP-IDF includes here.
//
// Frame layout (docs/reference/comfoair-pdo-rmi.md section 3):
//   enable : 84 <unit=15> <subunit> <property> 00 00 00 00 <timeout u32 LE signed> <value u8>
//   disable: 85 <unit=15> <subunit> <property>
//   write  : 03 <unit> <subunit> <property> <value u8>
// The four zero bytes read back as 00000000 in every capture; always send zeros.
// Trailing padding is NOT part of the protocol: enable is exactly 13 bytes, disable exactly 4.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace comfoair::rmi {

using Frame = std::vector<uint8_t>;

/// RMI unit ids.
static constexpr uint8_t UNIT_SCHEDULE = 0x15;
static constexpr uint8_t UNIT_TEMPHUMCONTROL = 0x1d;

/// RMI command bytes.
static constexpr uint8_t CMD_SET_PROPERTY = 0x03;
static constexpr uint8_t CMD_ENABLE_TIMER = 0x84;
static constexpr uint8_t CMD_DISABLE_TIMER = 0x85;

/// The u32 timeout is signed: -1 means "no expiry".
static constexpr int32_t TIMEOUT_INDEFINITE = -1;
/// The official app sends a 1-second timer for level and manual-mode changes and that is what
/// makes them stick. Do not substitute TIMEOUT_INDEFINITE here.
static constexpr int32_t TIMEOUT_STICKY = 1;

/// SCHEDULE subunits.
static constexpr uint8_t SUBUNIT_FAN = 0x01;
static constexpr uint8_t SUBUNIT_BYPASS = 0x02;
static constexpr uint8_t SUBUNIT_TEMPERATURE_PROFILE = 0x03;
static constexpr uint8_t SUBUNIT_SUPPLY_ONLY = 0x06;
static constexpr uint8_t SUBUNIT_EXHAUST_ONLY = 0x07;
static constexpr uint8_t SUBUNIT_MODE = 0x08;

/// SCHEDULE properties.
static constexpr uint8_t PROP_DEFAULT = 0x01;
static constexpr uint8_t PROP_BOOST = 0x06;
static constexpr uint8_t PROP_AWAY = 0x0b;

/// TEMPHUMCONTROL properties (sensor-based ventilation).
static constexpr uint8_t PROP_TEMPERATURE_PASSIVE = 0x04;
static constexpr uint8_t PROP_HUMIDITY_COMFORT = 0x06;
static constexpr uint8_t PROP_HUMIDITY_PROTECTION = 0x07;

/// ENABLETIMERENTRY: 13 bytes, no padding.
inline Frame set_timer(uint8_t subunit, uint8_t property, uint8_t value, int32_t timeout_secs) {
  const uint32_t t = static_cast<uint32_t>(timeout_secs);
  return Frame{CMD_ENABLE_TIMER,
               UNIT_SCHEDULE,
               subunit,
               property,
               0x00,
               0x00,
               0x00,
               0x00,
               static_cast<uint8_t>(t & 0xff),
               static_cast<uint8_t>((t >> 8) & 0xff),
               static_cast<uint8_t>((t >> 16) & 0xff),
               static_cast<uint8_t>((t >> 24) & 0xff),
               value};
}

/// DISABLETIMERENTRY: 4 bytes.
inline Frame clear_timer(uint8_t subunit, uint8_t property) {
  return Frame{CMD_DISABLE_TIMER, UNIT_SCHEDULE, subunit, property};
}

/// Direct RMI property write. Needs no response parsing.
inline Frame set_property(uint8_t unit, uint8_t subunit, uint8_t property, uint8_t value) {
  return Frame{CMD_SET_PROPERTY, unit, subunit, property, value};
}

// --- the command table of docs/reference/comfoair-pdo-rmi.md section 3 ----------------------

inline Frame ventilation_level(uint8_t level) {
  return set_timer(SUBUNIT_FAN, PROP_DEFAULT, level, TIMEOUT_STICKY);
}
inline Frame boost_on(int32_t seconds) {
  return set_timer(SUBUNIT_FAN, PROP_BOOST, 0x03, seconds);
}
inline Frame boost_off() { return clear_timer(SUBUNIT_FAN, PROP_BOOST); }
inline Frame away_on(int32_t seconds) {
  return set_timer(SUBUNIT_FAN, PROP_AWAY, 0x00, seconds);
}
inline Frame away_off() { return clear_timer(SUBUNIT_FAN, PROP_AWAY); }
inline Frame manual_on() {
  return set_timer(SUBUNIT_MODE, PROP_DEFAULT, 0x01, TIMEOUT_STICKY);
}
inline Frame manual_off() { return clear_timer(SUBUNIT_MODE, PROP_DEFAULT); }
inline Frame bypass_activate(int32_t seconds) {
  return set_timer(SUBUNIT_BYPASS, PROP_DEFAULT, 0x01, seconds);
}
inline Frame bypass_deactivate(int32_t seconds) {
  return set_timer(SUBUNIT_BYPASS, PROP_DEFAULT, 0x02, seconds);
}
inline Frame bypass_auto() { return clear_timer(SUBUNIT_BYPASS, PROP_DEFAULT); }
/// The value byte is 01 for BOTH subunit 06 and subunit 07 -- not 00 for either, which is the easy
/// mistake: the GETTIMERENTRY read-back reports 01 (docs/reference/comfoair-pdo-rmi.md §3).
inline Frame supply_only_on(int32_t seconds) {
  return set_timer(SUBUNIT_SUPPLY_ONLY, PROP_DEFAULT, 0x01, seconds);
}
inline Frame supply_only_off() { return clear_timer(SUBUNIT_SUPPLY_ONLY, PROP_DEFAULT); }
inline Frame exhaust_only_on(int32_t seconds) {
  return set_timer(SUBUNIT_EXHAUST_ONLY, PROP_DEFAULT, 0x01, seconds);
}
inline Frame exhaust_only_off() { return clear_timer(SUBUNIT_EXHAUST_ONLY, PROP_DEFAULT); }
inline Frame temperature_profile(uint8_t profile) {
  return set_timer(SUBUNIT_TEMPERATURE_PROFILE, PROP_DEFAULT, profile, TIMEOUT_INDEFINITE);
}
inline Frame temperature_passive(uint8_t off_auto_on) {
  return set_property(UNIT_TEMPHUMCONTROL, 0x01, PROP_TEMPERATURE_PASSIVE, off_auto_on);
}
inline Frame humidity_comfort(uint8_t off_auto_on) {
  return set_property(UNIT_TEMPHUMCONTROL, 0x01, PROP_HUMIDITY_COMFORT, off_auto_on);
}
inline Frame humidity_protection(uint8_t off_auto_on) {
  return set_property(UNIT_TEMPHUMCONTROL, 0x01, PROP_HUMIDITY_PROTECTION, off_auto_on);
}

// --- CAN transport -------------------------------------------------------------------------

/// Build the 29-bit CAN id for an RMI command frame.
inline uint32_t command_can_id(uint8_t src_node_id, uint8_t dst_node_id, uint8_t unknown_counter,
                               bool is_multi_frame, bool response_error, bool is_request,
                               uint8_t sequence_number) {
  return (uint32_t) 0x1f << 24 |                              //
         (uint32_t) (sequence_number & 0x03) << 17 |          //
         (uint32_t) (is_request ? 1 : 0) << 16 |              //
         (uint32_t) (response_error ? 1 : 0) << 15 |          //
         (uint32_t) (is_multi_frame ? 1 : 0) << 14 |          //
         (uint32_t) (unknown_counter & 0x03) << 12 |          //
         (uint32_t) (dst_node_id & 0x3f) << 6 |               //
         (uint32_t) (src_node_id & 0x3f);
}

/// A CAN frame carries at most 8 bytes, so anything longer is chunked.
inline bool needs_multi_frame(const Frame &command) { return command.size() > 8; }

/// Split a command into CAN payloads. Short commands are sent verbatim; long ones are prefixed
/// with a chunk counter whose high bit marks the final chunk.
inline std::vector<Frame> split_into_can_frames(const Frame &command) {
  std::vector<Frame> frames;
  // An empty command would otherwise become a valid DLC=0 CAN frame: a silent no-op that looks
  // like a successful send.
  if (command.empty())
    return frames;
  if (!needs_multi_frame(command)) {
    frames.push_back(command);
    return frames;
  }
  const size_t chunk = 7;
  uint8_t counter = 0;
  for (size_t pos = 0; pos < command.size(); pos += chunk) {
    const bool is_last = command.size() - pos <= chunk;
    Frame out;
    out.push_back(static_cast<uint8_t>(counter | (is_last ? 0x80 : 0x00)));
    const size_t end = is_last ? command.size() : pos + chunk;
    out.insert(out.end(), command.begin() + pos, command.begin() + end);
    frames.push_back(out);
    counter++;
  }
  return frames;
}

}  // namespace comfoair::rmi
