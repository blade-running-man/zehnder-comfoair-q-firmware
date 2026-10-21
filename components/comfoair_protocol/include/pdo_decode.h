// PDO frame decoding. Includes nothing but the standard library so the host tests compile it
// directly.
#pragma once

#include <cstddef>
#include <cstdint>

namespace comfoair {

/// A ComfoNet PDO broadcast has bits 27..28 clear and bits 6..13 equal to 0x01; bits 0..5 are
/// the sending node id and bits 14..26 are the PDO id (13 bits, 0..8191).
///
/// 13 bits, not 10: PDOs above 1023 exist -- CO2 sensors report 1024..1031 and a ComfoClime
/// 4145..4208 (aiocomfoconnect docs/PROTOCOL-PDO.md) -- and a 0x1f003fc0 mask dropped them all.
/// Requiring bits 27/28 clear is what still keeps the two other extended-id families out: an
/// RMI frame carries 0x1f in bits 24..28 (rmi_response.h) and a heartbeat is 0x10000000|node,
/// so both have bit 28 set. test_pdo_decode pins both rejections.
/// DO NOT narrow this back to bits 14..23 "to be safe": it silently drops every accessory PDO.
static constexpr uint32_t PDO_CAN_ID_MASK = 0x18003fc0UL;
static constexpr uint32_t PDO_CAN_ID_MATCH = 0x00000040UL;
static constexpr uint16_t PDO_ID_MAX = 0x1fff;

/// Checks the id bits only. The caller must also require an extended id and !rtr — a remote
/// transmission request from another node has an identical id.
inline bool is_pdo_can_id(uint32_t can_id) {
  return (can_id & PDO_CAN_ID_MASK) == PDO_CAN_ID_MATCH;
}
/// Masked to 13 bits: without the mask a caller that skipped is_pdo_can_id() would read bits
/// 27/28 into the PDO number.
inline uint16_t pdo_id_from_can_id(uint32_t can_id) {
  return static_cast<uint16_t>((can_id >> 14) & PDO_ID_MAX);
}
inline uint8_t node_id_from_can_id(uint32_t can_id) {
  return static_cast<uint8_t>(can_id & 0x3f);
}
/// Remote-transmission-request id used to poll one PDO.
/// PRECONDITION: pdo_id <= PDO_ID_MAX. This does not clamp: masking would silently poll a
/// different PDO. pdo_table.py is the only producer, and tests/python/test_registry.py
/// (test_every_pdo_fits_the_can_id) pins every table PDO at or below 8191.
inline uint32_t pdo_request_can_id(uint16_t pdo_id, uint8_t local_node_id) {
  return (static_cast<uint32_t>(pdo_id) << 14) | 0x40UL | (local_node_id & 0x3f);
}

enum class DecodeResult : uint8_t {
  OK = 0,
  /// The PDO carries its all-ones "nothing pending" sentinel.
  NO_VALUE = 1,
  /// The CAN DLC is not 1, 2, 4 or 8 bytes.
  UNSUPPORTED_LENGTH = 2,
};

/// Read a little-endian integer whose width comes from the CAN data-length code, never from a
/// hard-coded per-PDO width. This is what lets the decoder survive a field being widened.
/// `out` is written ONLY when the result is OK; it is left untouched on NO_VALUE and
/// UNSUPPORTED_LENGTH, so callers must initialise it.
inline DecodeResult decode_numeric(const uint8_t *data, size_t len, bool is_signed,
                                   bool none_when_all_ones, int64_t &out) {
  if (data == nullptr || (len != 1 && len != 2 && len != 4 && len != 8))
    return DecodeResult::UNSUPPORTED_LENGTH;

  uint64_t raw = 0;
  for (size_t i = 0; i < len; i++)
    raw |= static_cast<uint64_t>(data[i]) << (8 * i);

  const uint64_t width_mask = (len == 8) ? ~UINT64_C(0) : ((UINT64_C(1) << (len * 8)) - 1);
  // The sentinel is an UNSIGNED all-ones pattern, flagged only on the countdown rows the table
  // marks none_when_all_ones ("nothing pending"). Elsewhere it is data: 0xFF is Auto/Balanced on
  // PDO 49/54/55, and on a signed field (PDO 274) a legitimate -1.
  if (none_when_all_ones && !is_signed && raw == width_mask)
    return DecodeResult::NO_VALUE;

  // len == 8 is excluded because width_mask is then all-ones, so ~width_mask is 0 and the OR
  // below would be a deliberate no-op — raw is already the full 64-bit two's-complement value.
  if (is_signed && len < 8) {
    const uint64_t sign_bit = UINT64_C(1) << (len * 8 - 1);
    if ((raw & sign_bit) != 0)
      raw |= ~width_mask;
  }
  out = static_cast<int64_t>(raw);
  return DecodeResult::OK;
}

/// Zero-extending read used for the PDO 230 constraint bitset (documented as INT64, but the
/// bits are what matter). Accepts 1..8 bytes, unlike decode_numeric's 1/2/4/8: PDO 230's
/// constraint bitset uses bit 54, which lives in byte 6, so a 7-byte frame must decode.
inline bool decode_u64(const uint8_t *data, size_t len, uint64_t &out) {
  if (data == nullptr || len == 0 || len > 8)
    return false;
  uint64_t raw = 0;
  for (size_t i = 0; i < len; i++)
    raw |= static_cast<uint64_t>(data[i]) << (8 * i);
  out = raw;
  return true;
}

}  // namespace comfoair
