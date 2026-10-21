#pragma once

// Parsing of RMI *responses*. The exact mirror of rmi_frame.h, which builds requests.
//
// The one consumer is comfoair_probe_task: the read-only diagnostic walk, the unit's identity read,
// and the read-back of the three selects no PDO reports (COMFOAIR_RMI_READBACK, decoded by
// comfoair_readback) -- the only place an entity is fed from RMI. Everything here is host-tested.
//
// Sources, all in docs/reference/comfoair-pdo-rmi.md:
//   §3 "RMI read commands and the response frame"  CAN-id layout and multi-frame assembly
//   §3 command table                                 the GETTIMERENTRY captures
//   §4 capability #7                                 the active-error bitfield decode

#include <cstddef>
#include <cstdint>
#include <vector>

namespace comfoair::rmi {

/// Bits 24-28 of an RMI CAN id are 0x1f AND bits 19-23 are clear. The second half is not optional:
/// firmware uploads use 0x1F800000, which passes a bits-24-28-only test.
inline constexpr uint32_t RMI_CAN_ID_MASK = 0x1ff80000u;
inline constexpr uint32_t RMI_CAN_ID_MATCH = 0x1f000000u;
inline constexpr uint32_t RMI_MULTI_FRAME_BIT = 1u << 14;
inline constexpr uint32_t RMI_ERROR_BIT = 1u << 15;
inline constexpr uint32_t RMI_IS_REQUEST_BIT = 1u << 16;

inline bool is_rmi_can_id(uint32_t can_id) {
  return (can_id & RMI_CAN_ID_MASK) == RMI_CAN_ID_MATCH;
}

/// A response is an RMI frame with the IsRequest bit clear.
inline bool is_rmi_response(uint32_t can_id) {
  return is_rmi_can_id(can_id) && (can_id & RMI_IS_REQUEST_BIT) == 0;
}

inline bool rmi_response_has_error(uint32_t can_id) { return (can_id & RMI_ERROR_BIT) != 0; }
inline bool rmi_is_multi_frame(uint32_t can_id) { return (can_id & RMI_MULTI_FRAME_BIT) != 0; }
inline uint8_t rmi_src_node(uint32_t can_id) { return static_cast<uint8_t>(can_id & 0x3f); }
inline uint8_t rmi_dst_node(uint32_t can_id) { return static_cast<uint8_t>((can_id >> 6) & 0x3f); }
/// Copied from the request; use it to match a response to the request that asked for it.
inline uint8_t rmi_sequence(uint32_t can_id) { return static_cast<uint8_t>((can_id >> 17) & 0x03); }

/// Reassembles one RMI response. Mirror of split_into_can_frames() in rmi_frame.h: a single-frame
/// response is the payload verbatim, and a multi-frame one arrives as 7-byte blocks each prefixed
/// by its index, with 0x80 set on the last block's prefix. The mirror holds for messages up to 896
/// bytes (128 chunks x 7): rmi_frame.h's chunk counter is an unmasked uint8_t, so at chunk 128 the
/// prefix wraps to 0x80, which -- OR'ed with the last-block bit -- is indistinguishable on the wire
/// from "index 0, final block". Unreachable in this protocol, where the longest documented RMI
/// response is 32 bytes, so the TX-side arithmetic is left alone; this is recorded so nobody
/// mistakes the mirror for unconditional.
///
/// Holds one message at a time. It tracks only the source node of the reassembly in progress; it
/// never looks at the sequence number -- there is no sequence member, and rmi_sequence() is not
/// called anywhere inside this class. Matching a response to the request that elicited it, and
/// keeping two outstanding requests to the *same* node apart, is entirely the caller's job: hold
/// one instance per rmi_sequence(can_id), as the probe task
/// does. What this class guards on its own,
/// identically on the single-frame and multi-frame paths: a frame from a source other than the one
/// that opened the message in progress is rejected, the same way a duplicated or out-of-order
/// multi-frame index already is -- a different node's reply can neither splice into an in-progress
/// message nor silently complete over it.
class ResponseReassembler {
 public:
  enum class Result { NEED_MORE, COMPLETE, PROTOCOL_ERROR };

  void reset() {
    this->buffer_.clear();
    this->next_index_ = 0;
    this->active_ = false;
    this->src_node_ = 0;
  }

  Result feed(uint32_t can_id, const uint8_t *data, size_t len) {
    if (data == nullptr)
      return Result::PROTOCOL_ERROR;

    if (!rmi_is_multi_frame(can_id)) {
      // A response with no bytes at all is indistinguishable from one that never arrived.
      // rmi_frame.h refuses to send an empty command for the same reason -- symmetric here.
      if (len == 0) {
        this->reset();
        return Result::PROTOCOL_ERROR;
      }
      // Same hazard as the multi-frame source check below, on the path that check does not cover:
      // a single-frame reply from a different node must not silently complete over another node's
      // in-progress multi-frame reassembly and be reported as a successful read of its bytes.
      if (this->active_ && rmi_src_node(can_id) != this->src_node_) {
        this->reset();
        return Result::PROTOCOL_ERROR;
      }
      this->buffer_.assign(data, data + len);
      this->next_index_ = 0;
      this->active_ = false;
      return Result::COMPLETE;
    }

    // A multi-frame block always carries at least its index byte.
    if (len == 0) {
      this->reset();
      return Result::PROTOCOL_ERROR;
    }

    const uint8_t index = static_cast<uint8_t>(data[0] & 0x7f);
    const bool is_last = (data[0] & 0x80) != 0;
    const uint8_t source = rmi_src_node(can_id);

    // Index 0 always starts a message, so a retransmitted or interleaved response restarts cleanly
    // rather than appending to the previous one.
    if (index == 0) {
      this->buffer_.clear();
      this->next_index_ = 0;
      this->active_ = true;
      this->src_node_ = source;
    }
    // Two replies interleaved on the bus would otherwise splice together silently: source A's tail
    // appended to source B's head, reported as a successful COMPLETE with a spliced-but-plausible
    // message. Reject a source change mid-message the same way an out-of-order index already is.
    if (!this->active_ || index != this->next_index_ || source != this->src_node_) {
      this->reset();
      return Result::PROTOCOL_ERROR;
    }

    this->buffer_.insert(this->buffer_.end(), data + 1, data + len);
    this->next_index_++;

    if (!is_last)
      return Result::NEED_MORE;

    // Every block carried only its index byte: framing succeeded but the reassembled message is
    // empty. Same hazard and same fix as the single-frame case above.
    if (this->buffer_.empty()) {
      this->reset();
      return Result::PROTOCOL_ERROR;
    }

    this->next_index_ = 0;
    this->active_ = false;
    return Result::COMPLETE;
  }

  const std::vector<uint8_t> &message() const { return this->buffer_; }

 private:
  std::vector<uint8_t> buffer_;
  uint8_t next_index_{0};
  bool active_{false};
  uint8_t src_node_{0};
};

/// The 14-byte reply to `83 15 <subunit> <property>` (GETTIMERENTRY).
///
///   byte  0     enabled / override active
///   bytes 1..4  u32, zero in every capture, purpose undocumented -- ignored here
///   bytes 5..8  u32 LE signed, configured duration in seconds (-1 = indefinite)
///   bytes 9..12 u32 LE signed, remaining seconds (-1 = indefinite, 0 = not running)
///   byte  13    the preset or mode value the timer holds
struct TimerEntry {
  bool enabled;
  int32_t timeout_seconds;
  int32_t remaining_seconds;
  uint8_t value;
};

inline uint32_t decode_le32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline bool parse_timer_entry(const uint8_t *data, size_t len, TimerEntry &out) {
  if (data == nullptr || len != 14)
    return false;
  out.enabled = data[0] != 0;
  out.timeout_seconds = static_cast<int32_t>(decode_le32(data + 5));
  out.remaining_seconds = static_cast<int32_t>(decode_le32(data + 9));
  out.value = data[13];
  return true;
}

/// Decode the 32-byte bitfield returned by `80 03 01` (GETACTIVEERRORS) into error ids.
///
/// Each byte is walked from its LEAST significant bit, and the running bit index IS the error id
/// (byte i bit b is id i*8+b). The same field arrives on the LAN C as
/// CnAlarmNotification.errors, so this decode serves both transports.
///
/// The id -> message table is firmware-dependent and diverges from id 70 upward; choosing between
/// the two tables needs NODE 0x01/01 property 0x06. Deliberately not embedded here.
inline std::vector<uint16_t> active_error_ids(const uint8_t *data, size_t len) {
  std::vector<uint16_t> ids;
  if (data == nullptr)
    return ids;
  for (size_t byte = 0; byte < len; byte++) {
    for (uint8_t bit = 0; bit < 8; bit++) {
      if ((data[byte] & (1u << bit)) != 0)
        ids.push_back(static_cast<uint16_t>(byte * 8 + bit));
    }
  }
  return ids;
}

}  // namespace comfoair::rmi
