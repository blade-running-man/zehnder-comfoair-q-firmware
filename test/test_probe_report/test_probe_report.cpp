// The probe report: catalogue, decoders, redaction, JSON.
//
// Every value is a hand-built comfoair_probe_results_t -- the walk is test_probe_scan's business,
// and driving it here would make a decoder failure look like a walk failure. Expected bytes and
// meanings are copied from the documented NODE/unit property table and its worked examples
// (docs/reference/comfoair-pdo-rmi.md §3/§4 where it holds them), not from the implementation.
//
// The seams (catalogue, decoders, emitter) are reached through the component's PRIVATE header by
// relative path: [env:native] puts only include/ on the path, on purpose, and a render-level test
// alone cannot say WHICH layer produced a wrong character.
#include <unity.h>

#include <cstring>
#include <initializer_list>
#include <string>

#include "../../components/comfoair_probe_report/comfoair_probe_report_internal.h"
#include "comfoair_probe_report.h"

using namespace probe_report;

// ~30 KB: static, never on the stack.
static comfoair_probe_results_t R;

static void reset(void)
{
    std::memset(&R, 0, sizeof R);
    comfoair_probe_plan_t plan = COMFOAIR_PROBE_DEFAULT_PLAN();
    R.plan = plan;
    R.state = COMFOAIR_PROBE_DONE;
}

template <typename V> static void data(V &v, std::initializer_list<uint8_t> b, size_t len = 0)
{
    v.kind = COMFOAIR_PROBE_ANSWER_DATA;
    v.len = (uint8_t)(len ? len : b.size());
    size_t i = 0;
    for (uint8_t x : b)
        if (i < sizeof v.bytes)
            v.bytes[i++] = x;
}

template <typename V> static void error(V &v, uint8_t code)
{
    v.kind = COMFOAIR_PROBE_ANSWER_ERROR;
    v.error = code;
    v.len = 1;
    v.bytes[0] = code;
}

template <typename V> static void timeout(V &v) { v.kind = COMFOAIR_PROBE_ANSWER_TIMEOUT; }

static comfoair_probe_entry_t &add(uint8_t node, uint8_t unit, uint8_t sub, uint8_t prop)
{
    comfoair_probe_entry_t &e = R.entries[R.entry_count++];
    e.node = node;
    e.unit = unit;
    e.subunit = sub;
    e.property = prop;
    return e;
}

static void add_unit(uint8_t node, uint8_t unit, comfoair_probe_unit_status_t st, uint8_t subs,
                     uint16_t absent)
{
    R.units[R.unit_count++] = {node, unit, st, subs, absent};
}

struct Sink {
    std::string out;
    size_t calls = 0, max_piece = 0, fail_at = 0;
};

static bool sink_write(void *ctx, const char *text, size_t len)
{
    Sink *s = static_cast<Sink *>(ctx);
    s->calls++;
    if (len > s->max_piece)
        s->max_piece = len;
    if (s->fail_at != 0 && s->calls == s->fail_at)
        return false;
    s->out.append(text, len);
    return true;
}

static std::string render(const comfoair_probe_meta_t *meta = nullptr)
{
    Sink s;
    TEST_ASSERT_TRUE(comfoair_probe_render(&R, meta, sink_write, &s));
    return s.out;
}

static Decoded decode(const Property *p, std::initializer_list<uint8_t> b)
{
    uint8_t buf[COMFOAIR_PROBE_VALUE_MAX];
    size_t n = 0;
    for (uint8_t x : b)
        buf[n++] = x;
    return decode_value(p, buf, n, n);
}

static std::string number(const Number &n)
{
    char buf[32];
    size_t len = format_number(n, buf, sizeof buf);
    return std::string(buf, len);
}

static const Property *target(uint8_t unit, uint8_t prop)
{
    return catalogue_find(0x01, 0x01, unit, 0x01, prop);
}

// --- decoders ----------------------------------------------------------------------------------

void test_string_stops_at_the_first_nul(void)
{
    // The documented password read answers `34 32 31 30 00` (4210\0); serial is a string row.
    Decoded d = decode(target(0x01, 0x04), {'B', 'E', 'A', 0x00, 'X'});
    TEST_ASSERT_EQUAL(Format::String, d.format);
    TEST_ASSERT_EQUAL(Text::Bytes, d.text);
    TEST_ASSERT_EQUAL_size_t(3, d.text_len);
    TEST_ASSERT_FALSE(d.number.valid);
}

void test_i16_tenths_decodes_a_negative_unbalance(void)
{
    // comfoair-pdo-rmi.md §4 #9: Unbalance `9dff` = -99 -> -9.9.
    Decoded d = decode(target(0x1E, 0x12), {0x9d, 0xff});
    TEST_ASSERT_EQUAL(Format::I16, d.format);
    TEST_ASSERT_TRUE(d.number.valid);
    TEST_ASSERT_EQUAL_INT64(-99, d.number.raw);
    TEST_ASSERT_EQUAL_STRING("-9.9", number(d.number).c_str());
}

void test_number_formatting(void)
{
    TEST_ASSERT_EQUAL_STRING("-0.5", number({true, -5, 10}).c_str());
    TEST_ASSERT_EQUAL_STRING("18.0", number({true, 180, 10}).c_str());
    TEST_ASSERT_EQUAL_STRING("-20", number({true, -20, 1}).c_str());
    TEST_ASSERT_EQUAL_STRING("3222278144", number({true, 3222278144LL, 1}).c_str());
    TEST_ASSERT_EQUAL_STRING("", number({false, 7, 1}).c_str());
}

void test_u32_and_u16_are_little_endian_unsigned(void)
{
    int64_t v = 0;
    // A documented UINT32 example: `0004f0c0` = 3236955136.
    const uint8_t u32[] = {0x00, 0x04, 0xf0, 0xc0};
    TEST_ASSERT_TRUE(decode_int(Format::U32, u32, 4, &v));
    TEST_ASSERT_EQUAL_INT64(3236955136LL, v);
    const uint8_t u16[] = {0x96, 0xff};
    TEST_ASSERT_TRUE(decode_int(Format::U16, u16, 2, &v));
    TEST_ASSERT_EQUAL_INT64(0xff96, v);
    TEST_ASSERT_FALSE(decode_int(Format::U16, u16, 1, &v));  // wrong width: no number
    // A catalogued numeric of the wrong width keeps its format but has no number.
    Decoded d = decode(target(0x1C, 0x03), {0x96});
    TEST_ASSERT_EQUAL(Format::U16, d.format);
    TEST_ASSERT_FALSE(d.number.valid);
}

void test_version_word_decodes(void)
{
    char buf[16];
    // The error-table selector, 3222278144, decodes to exactly R1.4.0 (comfoair-pdo-rmi.md §4).
    version_text(3222278144u, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("R1.4.0", buf);
    version_text(0x00000000u, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("U0.0.0", buf);
    version_text(0x7FFFFFFFu, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("D1023.1023.1023", buf);
    version_text(0x80100C02u, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("P1.3.2", buf);
    // The documented firmware property answers `00 10 10 c0`.
    Decoded d = decode(target(0x01, 0x06), {0x00, 0x10, 0x10, 0xc0});
    TEST_ASSERT_EQUAL(Format::Version, d.format);
    TEST_ASSERT_EQUAL(Text::Version, d.text);
    TEST_ASSERT_EQUAL_INT64(3222278144LL, d.number.raw);
}

void test_enum_labels(void)
{
    // The documented enum labels (TEMPHUMCONTROL, VENTILATIONCONFIG, NODECONFIGURATION rows).
    TEST_ASSERT_EQUAL_STRING("auto_only", decode(target(0x1D, 0x04), {1}).label);
    TEST_ASSERT_EQUAL_STRING("external_setpoint", decode(target(0x1D, 0x08), {2}).label);
    TEST_ASSERT_EQUAL_STRING("1500-2000m", decode(target(0x1E, 0x07), {3}).label);
    TEST_ASSERT_EQUAL_STRING("right", decode(target(0x20, 0x04), {1}).label);
    Decoded d = decode(target(0x1D, 0x04), {7});  // undocumented value: number, no label
    TEST_ASSERT_TRUE(d.number.valid);
    TEST_ASSERT_EQUAL_INT64(7, d.number.raw);
    TEST_ASSERT_NULL(d.label);
}

void test_heuristic_for_uncatalogued_values(void)
{
    Decoded d = decode(nullptr, {'H', 'i', 0x00});
    TEST_ASSERT_EQUAL(Format::String, d.format);
    TEST_ASSERT_EQUAL_size_t(2, d.text_len);
    TEST_ASSERT_EQUAL(Format::U8, decode(nullptr, {0x00}).format);  // a lone NUL is a number
    d = decode(nullptr, {'a', 0x00});  // one character + NUL is a number (test_probe_report_live)
    TEST_ASSERT_EQUAL(Format::I16, d.format);
    TEST_ASSERT_EQUAL_INT64(97, d.number.raw);
    d = decode(nullptr, {0xec, 0xff});  // a documented INT16: `ecff` = -20
    TEST_ASSERT_EQUAL(Format::I16, d.format);
    TEST_ASSERT_EQUAL_INT64(-20, d.number.raw);
    TEST_ASSERT_EQUAL(Format::U32, decode(nullptr, {4, 0, 0, 0}).format);
    d = decode(nullptr, {1, 2, 3});
    TEST_ASSERT_EQUAL(Format::None, d.format);
    TEST_ASSERT_FALSE(d.number.valid);
    TEST_ASSERT_EQUAL(Format::None, decode(nullptr, {'a', 0x01, 0x00}).format);  // not printable
}

// --- catalogue ---------------------------------------------------------------------------------

void test_catalogue_applies_to_the_target_node(void)
{
    TEST_ASSERT_EQUAL_size_t(74, catalogue_size());  // 67 documented rows + 7 from the live unit
    TEST_ASSERT_EQUAL_STRING("Model", target(0x01, 0x08)->name);
    const Property *p = target(0x1E, 0x04);
    TEST_ASSERT_EQUAL_STRING("rw", p->access);
    TEST_ASSERT_EQUAL_STRING("m³/h", p->unit_label);
    p = target(0x1E, 0x15);  // a "??" row: no name, still a format
    TEST_ASSERT_NULL(p->name);
    TEST_ASSERT_EQUAL(Format::I16, p->format);
    TEST_ASSERT_EQUAL_UINT8(10, target(0x1D, 0x0A)->divisor);
    TEST_ASSERT_NULL(target(0x1E, 0x10));                         // not documented at all
    TEST_ASSERT_NULL(catalogue_find(1, 1, 0x1E, 0x02, 0x04));     // wrong subunit
    TEST_ASSERT_NOT_NULL(catalogue_find(0x2D, 0x2D, 0x1E, 1, 4)); // a Flex at 0x2D is a target
}

void test_other_nodes_get_only_serial_firmware_and_name(void)
{
    TEST_ASSERT_EQUAL_STRING("Serial number", catalogue_find(1, 0x30, 1, 1, 0x04)->name);
    TEST_ASSERT_EQUAL(Format::Version, catalogue_find(1, 0x30, 1, 1, 0x06)->format);
    TEST_ASSERT_EQUAL_STRING("Unit name", catalogue_find(1, 0x30, 1, 1, 0x14)->name);
    TEST_ASSERT_NULL(catalogue_find(1, 0x30, 1, 1, 0x08));  // the unit's model, not the LAN C's
    TEST_ASSERT_NULL(catalogue_find(1, 0x30, 1, 1, 0x01));
    TEST_ASSERT_NULL(catalogue_find(1, 0x30, 0x1E, 1, 0x04));
    TEST_ASSERT_NULL(catalogue_find(0x2D, 0x01, 0x1E, 1, 0x04));  // node 1 is not the target here
}

void test_unit_names(void)
{
    size_t named = 0;
    for (unsigned u = 0; u < 256; u++)
        named += unit_name(1, 1, (uint8_t)u) != nullptr;
    TEST_ASSERT_EQUAL_size_t(23, named);  // the documented units, plus the inferred CO2ZONES
    TEST_ASSERT_EQUAL_STRING("NODE", unit_name(1, 1, 0x01));
    TEST_ASSERT_EQUAL_STRING("NODECONFIGURATION", unit_name(1, 1, 0x20));
    TEST_ASSERT_EQUAL_STRING("COMFOFOND", unit_name(1, 1, 0x28));
    TEST_ASSERT_NULL(unit_name(1, 1, 0x04));
    TEST_ASSERT_EQUAL_STRING("NODE", unit_name(1, 0x30, 0x01));
    TEST_ASSERT_NULL(unit_name(1, 0x30, 0x1E));
}

void test_error_names(void)
{
    // comfoair-pdo-rmi.md §3, the RMI error codes.
    const struct { uint8_t code; const char *name; } want[] = {
        {11, "unknown_command"}, {12, "unknown_unit"},   {13, "unknown_subunit"},
        {14, "unknown_property"}, {15, "no_range"},      {30, "out_of_range"},
        {32, "not_gettable"},     {40, "internal_error"}, {41, "internal_error_bad_command"},
        {0, "unknown"},           {31, "unknown"},        {255, "unknown"},
    };
    for (const auto &w : want)
        TEST_ASSERT_EQUAL_STRING(w.name, error_name(w.code));
}

// --- redaction ---------------------------------------------------------------------------------

void test_the_maintainer_password_never_reaches_the_report(void)
{
    TEST_ASSERT_TRUE(is_redacted(0x20, 0x01, 0x03));
    TEST_ASSERT_FALSE(is_redacted(0x20, 0x01, 0x04));
    TEST_ASSERT_FALSE(is_redacted(0x20, 0x02, 0x03));
    reset();
    comfoair_probe_entry_t &e = add(1, 0x20, 1, 0x03);
    data(e.value, {'4', '2', '1', '0', 0x00});  // the documented password answer
    data(e.range, {'4', '2', '1', '0'});        // even if the unit echoed it for another type
    data(e.step, {'4', '2'});
    comfoair_probe_entry_t &o = add(0x30, 0x20, 1, 0x03);  // and on any node
    data(o.value, {'4', '2', '1', '0', 0x00});
    std::string out = render();
    TEST_ASSERT_NOT_NULL(std::strstr(out.c_str(), "\"value\":{\"redacted\":true}"));
    TEST_ASSERT_NULL(std::strstr(out.c_str(), "3432"));
    TEST_ASSERT_NULL(std::strstr(out.c_str(), "4210"));
    TEST_ASSERT_NULL(std::strstr(out.c_str(), "42"));
}

// --- JSON --------------------------------------------------------------------------------------

void test_escaping(void)
{
    Sink s;
    Emitter em(sink_write, &s);
    const char dev[] = {'"', '\\', 0x01, 0x7f, (char)0xb0, 'a'};
    em.put_string(dev, sizeof dev, true);
    em.put_string("m³/h");
    em.put_string(nullptr);
    TEST_ASSERT_TRUE(em.flush());
    TEST_ASSERT_EQUAL_STRING("\"\\\"\\\\\\u0001\\u007f\\u00b0a\"\"m³/h\"null", s.out.c_str());
}

void test_active_errors_forms(void)
{
    reset();
    R.errors_kind = COMFOAIR_PROBE_ANSWER_DATA;  // LSB first, index = id (§4 #7)
    R.errors_len = 4;
    R.errors[0] = 0x05;
    R.errors[3] = 0x80;
    TEST_ASSERT_NOT_NULL(std::strstr(render().c_str(),
                                     "\"active_errors\":{\"hex\":\"05000080\",\"ids\":[0,2,31]}"));
    R.errors_kind = COMFOAIR_PROBE_ANSWER_ERROR;
    R.errors_code = 11;
    R.errors_len = 1;
    R.errors[0] = 11;
    TEST_ASSERT_NOT_NULL(std::strstr(
        render().c_str(),
        "\"active_errors\":{\"error\":11,\"error_name\":\"unknown_command\",\"hex\":\"0b\"}"));
    R.errors_kind = COMFOAIR_PROBE_ANSWER_TIMEOUT;
    TEST_ASSERT_NOT_NULL(std::strstr(render().c_str(), "\"active_errors\":{\"timeout\":true}"));
    R.errors_kind = COMFOAIR_PROBE_ANSWER_NONE;
    TEST_ASSERT_NOT_NULL(std::strstr(render().c_str(), "\"active_errors\":null,"));
}

void test_a_failed_walk_and_a_truncated_value(void)
{
    reset();
    R.state = COMFOAIR_PROBE_FAILED;
    R.failure = COMFOAIR_PROBE_FAILURE_NO_ANSWER;
    R.truncated = true;
    comfoair_probe_entry_t &e = add(1, 0x01, 1, 0x08);  // model, 45 bytes received, 40 stored
    data(e.value, {'C', 'o', 'm', 'f', 'o'}, 45);
    comfoair_probe_entry_t &r = add(1, 0x1E, 1, 0x04);  // a range of the wrong width
    data(r.value, {0xd2, 0x00});
    data(r.range, {0x4b, 0x00, 0xc2});
    std::string out = render();
    const char *head = "{\"state\":\"failed\",\"failure\":\"no_answer\",\"node\":1,";
    TEST_ASSERT_EQUAL_STRING_LEN(head, out.c_str(), std::strlen(head));
    TEST_ASSERT_NOT_NULL(std::strstr(out.c_str(), "\"len\":45,\"number\":null,\"text\":\"Comfo"));
    TEST_ASSERT_NOT_NULL(std::strstr(out.c_str(),
                                     "\"range\":{\"hex\":\"4b00c2\",\"len\":3,\"min\":null,\"max\":null}"));
    TEST_ASSERT_NOT_NULL(std::strstr(out.c_str(), "\"truncated\":true}"));
}

static const char *const FULL =
    "{\"state\":\"done\",\"failure\":null,\"node\":1,\"another_counter\":0,\"elapsed_ms\":1234,"
    "\"progress\":{\"requests\":10,\"timeouts\":1,\"node\":48,\"unit\":1,\"subunit\":1,\"property\":20},"
    "\"nodes_seen\":[1,48],"
    "\"units\":["
    "{\"node\":1,\"unit\":1,\"name\":\"NODE\",\"status\":\"present\",\"subunits\":1,\"absent_properties\":43},"
    "{\"node\":1,\"unit\":2,\"name\":\"COMFOBUS\",\"status\":\"absent\",\"subunits\":0,\"absent_properties\":0},"
    "{\"node\":1,\"unit\":41,\"name\":null,\"status\":\"timeout\",\"subunits\":0,\"absent_properties\":0},"
    "{\"node\":48,\"unit\":1,\"name\":\"NODE\",\"status\":\"present\",\"subunits\":1,\"absent_properties\":0}],"
    "\"properties\":["
    "{\"node\":1,\"unit\":1,\"subunit\":1,\"property\":4,\"name\":\"Serial number\",\"access\":\"ro\","
    "\"format\":\"string\",\"unit_label\":null,"
    "\"value\":{\"hex\":\"4245413000\",\"len\":5,\"number\":null,\"text\":\"BEA0\",\"label\":null},"
    "\"range\":{\"error\":15,\"error_name\":\"no_range\",\"hex\":\"0f\"},\"step\":null},"
    "{\"node\":1,\"unit\":1,\"subunit\":1,\"property\":6,\"name\":\"Firmware version\",\"access\":\"ro\","
    "\"format\":\"version\",\"unit_label\":null,"
    "\"value\":{\"hex\":\"001010c0\",\"len\":4,\"number\":3222278144,\"text\":\"R1.4.0\",\"label\":null},"
    "\"range\":{\"timeout\":true},\"step\":null},"
    "{\"node\":1,\"unit\":30,\"subunit\":1,\"property\":18,\"name\":\"Unbalance\",\"access\":null,"
    "\"format\":\"i16\",\"unit_label\":\"%\","
    "\"value\":{\"hex\":\"9dff\",\"len\":2,\"number\":-9.9,\"text\":null,\"label\":null},"
    "\"range\":{\"hex\":\"6aff9600\",\"len\":4,\"min\":-15.0,\"max\":15.0},"
    "\"step\":{\"hex\":\"0a00\",\"len\":2,\"number\":1.0,\"text\":null,\"label\":null}},"
    "{\"node\":1,\"unit\":30,\"subunit\":1,\"property\":7,\"name\":\"Altitude\",\"access\":null,"
    "\"format\":\"enum\",\"unit_label\":null,"
    "\"value\":{\"hex\":\"01\",\"len\":1,\"number\":1,\"text\":null,\"label\":\"500-1000m\"},"
    "\"range\":null,\"step\":null},"
    "{\"node\":1,\"unit\":32,\"subunit\":1,\"property\":3,\"name\":\"Maintainer password\",\"access\":null,"
    "\"format\":\"string\",\"unit_label\":null,\"value\":{\"redacted\":true},\"range\":null,\"step\":null},"
    "{\"node\":1,\"unit\":30,\"subunit\":1,\"property\":58,\"name\":null,\"access\":null,"
    "\"format\":\"string\",\"unit_label\":null,"
    "\"value\":{\"hex\":\"486900\",\"len\":3,\"number\":null,\"text\":\"Hi\",\"label\":null},"
    "\"range\":null,\"step\":null},"
    "{\"node\":1,\"unit\":37,\"subunit\":2,\"property\":5,\"name\":null,\"access\":null,"
    "\"format\":null,\"unit_label\":null,"
    "\"value\":{\"error\":32,\"error_name\":\"not_gettable\",\"hex\":\"20\"},\"range\":null,\"step\":null},"
    "{\"node\":1,\"unit\":38,\"subunit\":1,\"property\":1,\"name\":null,\"access\":null,"
    "\"format\":null,\"unit_label\":null,\"value\":{\"timeout\":true},\"range\":null,\"step\":null},"
    "{\"node\":48,\"unit\":1,\"subunit\":1,\"property\":20,\"name\":\"Unit name\",\"access\":\"ro\","
    "\"format\":\"string\",\"unit_label\":null,"
    "\"value\":{\"hex\":\"4c414e00\",\"len\":4,\"number\":null,\"text\":\"LAN\",\"label\":null},"
    "\"range\":null,\"step\":null},"
    "{\"node\":48,\"unit\":1,\"subunit\":1,\"property\":8,\"name\":null,\"access\":null,"
    "\"format\":\"u32\",\"unit_label\":null,"
    "\"value\":{\"hex\":\"01000000\",\"len\":4,\"number\":1,\"text\":null,\"label\":null},"
    "\"range\":null,\"step\":null}],"
    "\"active_errors\":{\"hex\":\"05000080\",\"ids\":[0,2,31]},\"truncated\":false}";

static const uint8_t SEEN[] = {0x01, 0x30};
static const comfoair_probe_meta_t META = {1234, 0, SEEN, 2};

static void build_full(void)
{
    reset();
    R.progress = {10, 1, 0x30, 0x01, 0x01, 0x14};
    add_unit(1, 0x01, COMFOAIR_PROBE_UNIT_PRESENT, 1, 43);
    add_unit(1, 0x02, COMFOAIR_PROBE_UNIT_ABSENT, 0, 0);
    add_unit(1, 0x29, COMFOAIR_PROBE_UNIT_TIMEOUT, 0, 0);
    add_unit(0x30, 0x01, COMFOAIR_PROBE_UNIT_PRESENT, 1, 0);
    comfoair_probe_entry_t *e = &add(1, 0x01, 1, 0x04);
    data(e->value, {'B', 'E', 'A', '0', 0x00});
    error(e->range, 15);
    e = &add(1, 0x01, 1, 0x06);
    data(e->value, {0x00, 0x10, 0x10, 0xc0});
    timeout(e->range);
    e = &add(1, 0x1E, 1, 0x12);
    data(e->value, {0x9d, 0xff});
    data(e->range, {0x6a, 0xff, 0x96, 0x00});  // -150 .. 150 tenths
    data(e->step, {0x0a, 0x00});
    e = &add(1, 0x1E, 1, 0x07);
    data(e->value, {0x01});
    e = &add(1, 0x20, 1, 0x03);
    data(e->value, {'4', '2', '1', '0', 0x00});
    e = &add(1, 0x1E, 1, 0x3A);
    data(e->value, {'H', 'i', 0x00});
    e = &add(1, 0x25, 2, 0x05);
    error(e->value, 32);
    e = &add(1, 0x26, 1, 0x01);
    timeout(e->value);
    e = &add(0x30, 0x01, 1, 0x14);
    data(e->value, {'L', 'A', 'N', 0x00});
    e = &add(0x30, 0x01, 1, 0x08);
    data(e->value, {0x01, 0x00, 0x00, 0x00});
    R.errors_kind = COMFOAIR_PROBE_ANSWER_DATA;
    R.errors_len = 4;
    R.errors[0] = 0x05;
    R.errors[3] = 0x80;
}

void test_full_render_matches_the_schema_exactly(void)
{
    build_full();
    TEST_ASSERT_EQUAL_STRING(FULL, render(&META).c_str());
}

void test_a_failed_write_stops_the_render(void)
{
    build_full();
    for (size_t fail_at = 1; fail_at <= 3; fail_at++) {
        Sink s;
        s.fail_at = fail_at;
        TEST_ASSERT_FALSE(comfoair_probe_render(&R, &META, sink_write, &s));
        TEST_ASSERT_EQUAL_size_t(fail_at, s.calls);  // not one write after the refusal
    }
}

void test_every_piece_fits_and_the_pieces_join_to_the_report(void)
{
    reset();
    // A full store of worst-case values: 40 stored bytes of `"`, each escaped to two characters,
    // plus 40 bytes of hex -- the largest property object the store can hold.
    for (size_t i = 0; i < COMFOAIR_PROBE_MAX_ENTRIES; i++) {
        comfoair_probe_entry_t &e = add(1, 0x01, 1, 0x04);
        e.value.kind = COMFOAIR_PROBE_ANSWER_DATA;
        e.value.len = 200;
        std::memset(e.value.bytes, '"', sizeof e.value.bytes);
    }
    for (size_t i = 0; i < COMFOAIR_PROBE_MAX_UNITS; i++)
        add_unit(1, (uint8_t)i, COMFOAIR_PROBE_UNIT_PRESENT, 16, 1000);
    Sink s;
    TEST_ASSERT_TRUE(comfoair_probe_render(&R, &META, sink_write, &s));
    TEST_ASSERT_TRUE(s.max_piece <= EMIT_PIECE);
    TEST_ASSERT_TRUE(s.calls > 100);
    TEST_ASSERT_EQUAL_CHAR('}', s.out.back());
    size_t count = 0;
    for (size_t at = 0; (at = s.out.find("\"len\":200", at)) != std::string::npos; at++)
        count++;
    TEST_ASSERT_EQUAL_size_t(COMFOAIR_PROBE_MAX_ENTRIES, count);
}

void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_string_stops_at_the_first_nul);
    RUN_TEST(test_i16_tenths_decodes_a_negative_unbalance);
    RUN_TEST(test_number_formatting);
    RUN_TEST(test_u32_and_u16_are_little_endian_unsigned);
    RUN_TEST(test_version_word_decodes);
    RUN_TEST(test_enum_labels);
    RUN_TEST(test_heuristic_for_uncatalogued_values);
    RUN_TEST(test_catalogue_applies_to_the_target_node);
    RUN_TEST(test_other_nodes_get_only_serial_firmware_and_name);
    RUN_TEST(test_unit_names);
    RUN_TEST(test_error_names);
    RUN_TEST(test_the_maintainer_password_never_reaches_the_report);
    RUN_TEST(test_escaping);
    RUN_TEST(test_active_errors_forms);
    RUN_TEST(test_a_failed_walk_and_a_truncated_value);
    RUN_TEST(test_full_render_matches_the_schema_exactly);
    RUN_TEST(test_a_failed_write_stops_the_render);
    RUN_TEST(test_every_piece_fits_and_the_pieces_join_to_the_report);
    return UNITY_END();
}
