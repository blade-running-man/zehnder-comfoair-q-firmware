// RMI *read* requests for the probe, and the classification of their responses.
//
// rmi_frame.h builds writes, rmi_response.h parses responses; this header is the narrow seam
// between them that the RMI probe needs. It reuses Frame, command_can_id(), needs_multi_frame()
// and the rmi_* id accessors rather than restating any bit layout.
//
// The load-bearing rule (docs/reference/comfoair-pdo-rmi.md §0 "Commands that can brick the
// unit"): ANY command byte
// >= 0x80 sent to unit 0x01 (NODE) can brick the ventilation unit, and 0x80/0x82 on
// NODECONFIGURATION (0x20) break its configuration. So this header can build exactly two frame
// shapes and no others:
//   get one       : 01 <unit> <subunit> <type> <property>
//   active errors : 80 03 01
// (both in comfoair-pdo-rmi.md §3, "RMI read commands and the response frame")
// Both are pinned by test/test_rmi_read, which sweeps every unit, type and property.
//
// DO NOT add a builder that takes the command byte as a parameter, and DO NOT give
// get_active_errors() a unit argument. A single wrong unit byte turns this diagnostic read into
// the bricking frame; the only safe >= 0x80 frame is the one whose every byte is a literal.
#pragma once

#include <cstddef>
#include <cstdint>

#include "rmi_frame.h"
#include "rmi_response.h"

namespace comfoair::rmi {

inline constexpr uint8_t CMD_GET_ONE = 0x01;
inline constexpr uint8_t CMD_GET_ACTIVE_ERRORS = 0x80;
/// The ERROR unit. The one unit on which this header sends a >= 0x80 command.
inline constexpr uint8_t UNIT_ERROR = 0x03;

/// The `<type>` byte of GET ONE (comfoair-pdo-rmi.md §3).
inline constexpr uint8_t READ_TYPE_VALUE = 0x10, READ_TYPE_RANGE = 0x20, READ_TYPE_STEP = 0x40;

/// `01 unit subunit type property`. Returns an EMPTY Frame unless `type` is exactly one of the
/// three READ_TYPE_* values. The types are OR-able on the wire, but the
/// response is then a concatenation with no documented separator, so the probe asks for one at a
/// time; 0x00 ("None") asks for nothing and would only cost a bus round trip.
///
/// DO NOT relax the type check to "any nonzero byte": the empty Frame is what makes a bad call
/// visible, because split_into_can_frames() (rmi_frame.h) refuses to send an empty command.
inline Frame get_one(uint8_t unit, uint8_t subunit, uint8_t type, uint8_t property) {
  if (type != READ_TYPE_VALUE && type != READ_TYPE_RANGE && type != READ_TYPE_STEP)
    return Frame{};
  return Frame{CMD_GET_ONE, unit, subunit, type, property};
}

/// `80 03 01` GETACTIVEERRORS (comfoair-pdo-rmi.md §3; §0 lists 0x80 on ERROR as one
/// of the legitimate >= 0x80 uses). The only >= 0x80 frame the probe may send; the unit byte is
/// pinned to UNIT_ERROR, and the test pins UNIT_ERROR itself to the number 0x03.
inline Frame get_active_errors() { return Frame{CMD_GET_ACTIVE_ERRORS, UNIT_ERROR, 0x01}; }

/// The CAN id of a read request. `another_counter` is passed through rather than fixed: the
/// documented captures carry 1 (they were taken from a LAN C), while this firmware's writes send 0
/// and work on real hardware (comfoair-pdo-rmi.md §0, "AnotherCounter"). Which one the unit wants
/// for reads is a hardware question, so the caller chooses. The multi-frame bit follows the frame
/// so this id stays consistent with split_into_can_frames() for any length.
inline uint32_t read_request_can_id(uint8_t src, uint8_t dst, uint8_t another_counter,
                                    const Frame &frame, uint8_t sequence) {
  return command_can_id(src, dst, another_counter, needs_multi_frame(frame), /*error*/ false,
                        /*is_request*/ true, sequence);
}

/// True iff `can_id` is an RMI RESPONSE from `src` to `dst` carrying `sequence`. The sequence is
/// copied from the request into the response, and is what keeps two outstanding reads to the
/// same node apart. is_rmi_response() also rejects the firmware-upload id 0x1F800000
/// (comfoair-pdo-rmi.md §3), which would otherwise "match" src 0, dst 0, seq 0.
inline bool response_matches(uint32_t can_id, uint8_t src, uint8_t dst, uint8_t sequence) {
  return is_rmi_response(can_id) && rmi_src_node(can_id) == (src & 0x3f) &&
         rmi_dst_node(can_id) == (dst & 0x3f) && rmi_sequence(can_id) == (sequence & 0x03);
}

struct ReadAnswer {
  bool is_error;
  uint8_t error;
};

/// Classifies a REASSEMBLED response (ResponseReassembler::message(), rmi_response.h).
/// Error bit clear -> {false, 0}. Error bit set -> {true, message[0]}; an empty error message ->
/// {true, 0}, since 0 is not among the documented codes (comfoair-pdo-rmi.md §3).
///
/// The error code is the first payload byte. No document shows an error payload -- only the error
/// id layout (1F009D01) -- but the owner's Q350 answered codes 12-15 exactly there
/// (rmi-probe-findings-q350.md §1). The caller keeps the raw bytes alongside this answer, so a
/// unit that differs is visible on hardware rather than silently misreported.
inline ReadAnswer classify_response(uint32_t can_id, const uint8_t *message, size_t len) {
  if (!rmi_response_has_error(can_id))
    return ReadAnswer{false, 0};
  if (message == nullptr || len == 0)
    return ReadAnswer{true, 0};
  return ReadAnswer{true, message[0]};
}

}  // namespace comfoair::rmi
