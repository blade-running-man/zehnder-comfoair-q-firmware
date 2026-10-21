// The seams inside comfoair_probe_report: the catalogue, the decoders, the JSON emitter.
//
// PRIVATE. Not under include/, so nothing outside this component can depend on it; the host suite
// test_probe_report includes it by relative path, because the render-level test alone cannot tell
// "the catalogue has the wrong divisor" from "the number formatter drops the sign" -- both show up
// as one wrong character in a 2 KB string. DO NOT move it to include/: the public contract is the
// one render function, and the web UI must take names from the JSON, never from these tables.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "comfoair_probe_report.h"

namespace probe_report {

// --- the catalogue (comfoair_probe_catalogue.cpp) ----------------------------------------------

// How a property's bytes are read. Enum is a UINT8 with documented labels: every enumerated row in
// the documented NODE/unit property table is UINT8, so there is no wider enum to provide for.
enum class Format : uint8_t { None = 0, String, U8, U16, I16, U32, Version, Enum };

// The JSON spelling, or NULL for None.
const char *format_name(Format f);
// Bytes per value; 0 for None and String (no fixed width -- a range cannot be split in two).
size_t format_width(Format f);

struct Property {
    uint8_t unit, subunit, property;
    const char *name;        // NULL for the documented "??" rows -- the format still decodes them
    const char *access;      // "ro" / "rw" / NULL where the table leaves the column blank
    Format format;
    uint8_t divisor;         // 1 = as read; 10 = tenths (°C x10, unbalance in 0.1 %)
    const char *unit_label;  // UTF-8, or NULL
    const char *const *labels;  // Enum only: labels[value], NULL entries allowed
    uint8_t label_count;
};

// The catalogue row for a property, or NULL. A row's subunit may be a wildcard matching every
// subunit >= 1 (CO2ZONES 0x2B only, see the catalogue). Rows describe the TARGET node; for any other node only
// NODE 0x01/01 properties 0x04, 0x06 and 0x14 apply (the documented LAN C table gives those three
// the same meaning; the rest of its rows disagree with the unit's).
const Property *catalogue_find(uint8_t target_node, uint8_t node, uint8_t unit, uint8_t subunit,
                               uint8_t property);
size_t catalogue_size(void);

// The documented unit name, or one ending in " (inferred)" from the live
// unit, or NULL. Same node rule as catalogue_find.
const char *unit_name(uint8_t target_node, uint8_t node, uint8_t unit);

// The RMI error code's name (comfoair-pdo-rmi.md §3); "unknown" for anything undocumented.
const char *error_name(uint8_t code);

// True for the one value the report must never contain: the maintainer password,
// NODECONFIGURATION 0x20/01 property 0x03. Deliberately on EVERY
// node, not only the target: another node may carry the same unit, and redacting a value that was
// not secret costs a hex string nobody needed. DO NOT make this a catalogue column: deleting or
// renaming a catalogue row must not be able to leak it.
bool is_redacted(uint8_t unit, uint8_t subunit, uint8_t property);

// --- the decoders (comfoair_probe_decode.cpp) --------------------------------------------------

struct Number {
    bool valid;
    int64_t raw;      // as read off the wire, sign applied
    uint8_t divisor;  // copied from the catalogue; 1 when uncatalogued
};

enum class Text : uint8_t { None = 0, Bytes, Version };

// What a DATA value means. Copy-safe: text is described, not pointed at.
struct Decoded {
    Format format;      // the catalogue's, else the heuristic's choice, else None
    Number number;
    Text text;          // Bytes: bytes[0 .. text_len); Version: version_text(number.raw)
    size_t text_len;
    const char *label;  // the enum label, or NULL
};

// `stored` bytes are present, `received` arrived (received > stored means truncated).
// `p` may be NULL: the heuristic then picks text / u8 / i16 / u32 by shape.
Decoded decode_value(const Property *p, const uint8_t *bytes, size_t stored, size_t received);

// Re-reads an I16 as U16 when the unit's own range contradicts a signed reading (min > max read
// signed), and re-decodes the value. The range and the step then follow d.format. Why: the device
// answers `0000ffff` for most undocumented 2-byte properties (live Q350 R1.11.0:
// VENTILATIONCONFIG 0x08/0x13-0x1F, VALVE 0x02, PREHEATER 0x04), which signed is "0 .. -1" -- a
// range no unit means. A genuinely signed range stays signed: `19fce803` is -999..1000, `0080ff7f`
// is -32768..32767 (read unsigned the latter would be 32768 > 32767, so signed is the one
// consistent reading). Applies to catalogued I16 rows too: the table's "??" rows guess INT16 and
// the unit's own range is better evidence than that guess. DO NOT widen this to U8/U32: no live
// range of those widths contradicted its format, and a rule with no case is a guess.
void settle_sign(Decoded &d, const uint8_t *value, size_t value_len, const uint8_t *range,
                 size_t range_stored, size_t range_received);

// A little-endian integer of format `f` from exactly format_width(f) bytes. False otherwise.
bool decode_int(Format f, const uint8_t *bytes, size_t len, int64_t *out);

// The firmware version word: two bits of release kind (U/D/P/R), then major, minor and patch as
// three 10-bit fields, e.g. 0xC0101000 -> "R1.4.0". Returns the length written; `cap` of 16
// always suffices ("R1023.1023.1023" + NUL).
size_t version_text(uint32_t version, char *out, size_t cap);

// The JSON number: integers without a fraction, divided values with exactly one decimal.
// Returns the length written (0 if `n` is not valid or `cap` is too small).
size_t format_number(const Number &n, char *out, size_t cap);

// --- the JSON emitter (comfoair_probe_decode.cpp) -----------------------------------------------

// The largest single write. The HTTP layer streams each piece as one chunk; a few hundred bytes
// keeps its stack small and is still far fewer chunks than one per token.
constexpr size_t EMIT_PIECE = 512;

// Buffers output and hands it to `write` in pieces of at most EMIT_PIECE. Once a write fails, it
// stays failed and every later put is a no-op -- the render checks ok() between entries and stops.
class Emitter {
public:
    Emitter(comfoair_probe_write_fn write, void *ctx) : write_(write), ctx_(ctx) {}
    void put(const char *s, size_t n);
    void put(const char *s);
    void put_uint(uint64_t v);
    void put_number(const Number &n);  // `null` when not valid
    // A JSON string, or `null` for a NULL pointer. `device` bytes come from the unit and may be
    // anything: every byte < 0x20 or >= 0x7F becomes \u00XX, i.e. the bytes are read as Latin-1,
    // which is always valid JSON. Catalogue strings are our own UTF-8 ("°C", "m³/h") and pass
    // bytes >= 0x80 through. DO NOT escape catalogue strings as device bytes: "°" (c2 b0) would
    // come out as "Â°", which every reader shows as "Â°".
    void put_string(const char *s, size_t n, bool device);
    void put_string(const char *s);  // a catalogue string or NULL
    void put_hex(const uint8_t *b, size_t n);  // a quoted lowercase hex string
    bool flush();
    bool ok() const { return ok_; }

private:
    comfoair_probe_write_fn write_;
    void *ctx_;
    bool ok_ = true;
    size_t used_ = 0;
    char buf_[EMIT_PIECE];
};

// --- the census section (comfoair_probe_report_pdos.cpp) ---------------------------------------

// The ",\"pdos\":{...}" section, placed by the renderer immediately before "truncated". Emits
// nothing at all when `meta` or its pdo_summary is NULL.
void put_pdos(Emitter &em, const comfoair_probe_meta_t *meta);

}  // namespace probe_report
