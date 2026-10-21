// The probe report against the FIRST REAL REPORT: a ComfoAir Q350, firmware R1.11.0
// (docs/unit/data/q350-probe.json). Every vector below is a hex string copied
// from that file, and each test pins what the report SHOULD have said where the first renderer got
// it wrong. A suite of its own because test_probe_report is near the 600-line ceiling, and because
// these vectors come from a device, not from the documented property table -- when the two
// disagree (FILTER 0x02 below), the reader must be able to tell which source a test trusts.
//
// The first test reads comfoair_probe_results() BEFORE any comfoair_probe_begin(): it must stay
// first, and nothing in this suite may start a walk.
#include <unity.h>

#include <cstring>
#include <initializer_list>
#include <string>

#include "../../components/comfoair_probe_report/comfoair_probe_report_internal.h"
#include "comfoair_probe.h"
#include "comfoair_probe_report.h"

using namespace probe_report;

static comfoair_probe_results_t R;  // ~30 KB: static, never on the stack

static bool sink_write(void *ctx, const char *text, size_t len)
{
    static_cast<std::string *>(ctx)->append(text, len);
    return true;
}

static std::string render(const comfoair_probe_results_t *r)
{
    std::string out;
    TEST_ASSERT_TRUE(comfoair_probe_render(r, nullptr, sink_write, &out));
    return out;
}

template <typename V> static void data(V &v, std::initializer_list<uint8_t> b)
{
    v.kind = COMFOAIR_PROBE_ANSWER_DATA;
    v.len = (uint8_t)b.size();
    size_t i = 0;
    for (uint8_t x : b)
        v.bytes[i++] = x;
}

// One live property, rendered alone; returns its object from the "properties" array.
static std::string entry(uint8_t unit, uint8_t sub, uint8_t prop, std::initializer_list<uint8_t> v,
                         std::initializer_list<uint8_t> range = {},
                         std::initializer_list<uint8_t> step = {})
{
    std::memset(&R, 0, sizeof R);
    comfoair_probe_plan_t plan = COMFOAIR_PROBE_DEFAULT_PLAN();
    R.plan = plan;
    R.state = COMFOAIR_PROBE_DONE;
    comfoair_probe_entry_t &e = R.entries[R.entry_count++];
    e.node = 1;
    e.unit = unit;
    e.subunit = sub;
    e.property = prop;
    data(e.value, v);
    if (range.size() != 0)
        data(e.range, range);
    if (step.size() != 0)
        data(e.step, step);
    const std::string out = render(&R);
    const size_t from = out.find("\"properties\":[") + 14;
    return out.substr(from, out.find("],\"active_errors\"") - from);
}

static std::string head(uint8_t unit, uint8_t sub, uint8_t prop)
{
    return "{\"node\":1,\"unit\":" + std::to_string(unit) + ",\"subunit\":" + std::to_string(sub) +
           ",\"property\":" + std::to_string(prop) + ",";
}

// --- 4: the idle report names the node a walk would target --------------------------------------

void test_an_idle_report_targets_the_default_node(void)
{
    // Live: the report fetched before the first walk said "node":0 -- a node that does not exist.
    const std::string out = render(comfoair_probe_results());
    const char *want = "{\"state\":\"idle\",\"failure\":null,\"node\":1,";
    TEST_ASSERT_EQUAL_STRING_LEN(want, out.c_str(), std::strlen(want));
}

// --- 1: text needs two printable characters before its NUL --------------------------------------

void test_a_two_byte_number_ending_in_nul_is_not_text(void)
{
    // Live POSTHEATER 0x27/01/0x03 `6400` was rendered as the text "d"; it is 100 (range 50..150).
    uint8_t b[] = {0x64, 0x00};
    Decoded d = decode_value(nullptr, b, 2, 2);
    TEST_ASSERT_EQUAL(Format::I16, d.format);
    TEST_ASSERT_EQUAL_INT64(100, d.number.raw);
    TEST_ASSERT_EQUAL(Text::None, d.text);
}

void test_live_strings_stay_text(void)
{
    // Live VALVE 0x16/01/0x03: "100.0\0", uncatalogued (its no_range answer is left out here).
    TEST_ASSERT_EQUAL_STRING(
        (head(22, 1, 3) +
         "\"name\":null,\"access\":null,\"format\":\"string\",\"unit_label\":null,"
         "\"value\":{\"hex\":\"3130302e3000\",\"len\":6,\"number\":null,\"text\":\"100.0\","
         "\"label\":null},\"range\":null,\"step\":null}")
            .c_str(),
        entry(0x16, 1, 0x03, {0x31, 0x30, 0x30, 0x2e, 0x30, 0x00}).c_str());
    uint8_t ok[] = {'O', 'K', 0x00};  // the shortest text: two characters and the NUL
    TEST_ASSERT_EQUAL(Format::String, decode_value(nullptr, ok, 3, 3).format);
}

// --- 2: a range that contradicts a signed reading makes the value unsigned ----------------------

static std::string numeric(const char *format, const char *unit_label, const char *name,
                           const char *hex, const char *number, const char *range_hex,
                           const char *min, const char *max, const char *step,
                           const char *access = nullptr)
{
    std::string n = name ? "\"" + std::string(name) + "\"" : "null";
    std::string a = access ? "\"" + std::string(access) + "\"" : "null";
    std::string u = unit_label ? "\"" + std::string(unit_label) + "\"" : "null";
    return "\"name\":" + n + ",\"access\":" + a + ",\"format\":\"" + format + "\",\"unit_label\":" + u +
           ",\"value\":{\"hex\":\"" + hex + "\",\"len\":" + std::to_string(std::strlen(hex) / 2) +
           ",\"number\":" + number + ",\"text\":null,\"label\":null},\"range\":{\"hex\":\"" +
           range_hex + "\",\"len\":4,\"min\":" + min + ",\"max\":" + max +
           "},\"step\":{\"hex\":\"0100\",\"len\":2,\"number\":" + step +
           ",\"text\":null,\"label\":null}}";
}

void test_a_0000ffff_range_is_unsigned(void)
{
    // Live: every one of these rendered "min":0,"max":-1.
    TEST_ASSERT_EQUAL_STRING(
        (head(22, 1, 2) + numeric("u16", nullptr, nullptr, "e803", "1000", "0000ffff", "0",
                                  "65535", "1")).c_str(),
        entry(0x16, 1, 0x02, {0xe8, 0x03}, {0x00, 0x00, 0xff, 0xff}, {0x01, 0x00}).c_str());
    TEST_ASSERT_EQUAL_STRING(  // PREHEATER 0x19/01/0x04
        (head(25, 1, 4) + numeric("u16", nullptr, nullptr, "0000", "0", "0000ffff", "0",
                                  "65535", "1")).c_str(),
        entry(0x19, 1, 0x04, {0x00, 0x00}, {0x00, 0x00, 0xff, 0xff}, {0x01, 0x00}).c_str());
    TEST_ASSERT_EQUAL_STRING(  // VENTILATIONCONFIG 0x15, a catalogued "??" INT16 row
        (head(30, 1, 0x15) + numeric("u16", nullptr, nullptr, "1a0a", "2586", "0000ffff", "0",
                                     "65535", "1")).c_str(),
        entry(0x1E, 1, 0x15, {0x1a, 0x0a}, {0x00, 0x00, 0xff, 0xff}, {0x01, 0x00}).c_str());
    TEST_ASSERT_EQUAL_STRING(  // VENTILATIONCONFIG 0x1F, past the documented table
        (head(30, 1, 0x1F) + numeric("u16", nullptr, nullptr, "4501", "325", "0000ffff", "0",
                                     "65535", "1")).c_str(),
        entry(0x1E, 1, 0x1F, {0x45, 0x01}, {0x00, 0x00, 0xff, 0xff}, {0x01, 0x00}).c_str());
    TEST_ASSERT_EQUAL_STRING(  // FILTER 0x03 was already u16 and must stay 0..65535
        (head(28, 1, 3) + numeric("u16", "days", "Filter lifetime", "b400", "180", "0000ffff",
                                  "0", "65535", "1", "rw")).c_str(),
        entry(0x1C, 1, 0x03, {0xb4, 0x00}, {0x00, 0x00, 0xff, 0xff}, {0x01, 0x00}).c_str());
}

void test_a_genuinely_signed_range_stays_signed(void)
{
    TEST_ASSERT_EQUAL_STRING(  // VENTILATIONCONFIG 0x0A: -20 in -999..1000
        (head(30, 1, 0x0A) + numeric("i16", nullptr, nullptr, "ecff", "-20", "19fce803", "-999",
                                     "1000", "1")).c_str(),
        entry(0x1E, 1, 0x0A, {0xec, 0xff}, {0x19, 0xfc, 0xe8, 0x03}, {0x01, 0x00}).c_str());
    TEST_ASSERT_EQUAL_STRING(  // Unbalance 0x12: tenths, -15.0..15.0
        (head(30, 1, 0x12) + numeric("i16", "%", "Unbalance", "0000", "0.0", "6aff9600",
                                     "-15.0", "15.0", "0.1")).c_str(),
        entry(0x1E, 1, 0x12, {0x00, 0x00}, {0x6a, 0xff, 0x96, 0x00}, {0x01, 0x00}).c_str());
    TEST_ASSERT_EQUAL_STRING(  // unit 0x2A 0x09: the full INT16 span, -400 inside it
        (head(42, 1, 9) + numeric("i16", nullptr, nullptr, "70fe", "-400", "0080ff7f", "-32768",
                                  "32767", "1")).c_str(),
        entry(0x2A, 1, 0x09, {0x70, 0xfe}, {0x00, 0x80, 0xff, 0x7f}, {0x01, 0x00}).c_str());
}

// --- 3 and 5: the catalogue, corrected and extended from the live unit --------------------------

static const Property *at(uint8_t unit, uint8_t sub, uint8_t prop)
{
    return catalogue_find(0x01, 0x01, unit, sub, prop);
}

void test_filter_pre_warning_is_u16_on_the_wire(void)
{
    // The documented table says UINT8; the live unit answers two bytes, `1500` = 21 days.
    TEST_ASSERT_EQUAL(Format::U16, at(0x1C, 1, 0x02)->format);
    TEST_ASSERT_EQUAL_STRING(
        (head(28, 1, 2) + numeric("u16", "days", "Filter pre-warning period", "1500", "21",
                                  "0000ffff", "0", "65535", "1")).c_str(),
        entry(0x1C, 1, 0x02, {0x15, 0x00}, {0x00, 0x00, 0xff, 0xff}, {0x01, 0x00}).c_str());
}

void test_hmi_rows(void)
{
    const Property *p = at(0x1A, 1, 0x03);
    TEST_ASSERT_EQUAL_STRING("Display brightness (inferred)", p->name);
    TEST_ASSERT_EQUAL(Format::U8, p->format);
    TEST_ASSERT_EQUAL_STRING("%", p->unit_label);
    const std::string b = entry(0x1A, 1, 0x03, {0x46}, {0x0a, 0x64}, {0x05});
    TEST_ASSERT_NOT_NULL(std::strstr(b.c_str(), "\"number\":70,"));
    TEST_ASSERT_NOT_NULL(std::strstr(b.c_str(), "\"min\":10,\"max\":100}"));
    TEST_ASSERT_EQUAL_STRING("Display language", at(0x1A, 1, 0x04)->name);
    TEST_ASSERT_EQUAL(Format::String, at(0x1A, 1, 0x04)->format);
    const std::string l = entry(0x1A, 1, 0x04, {'E', 'N', 'G', 'L', 'I', 'S', 'H', 0x00});
    TEST_ASSERT_NOT_NULL(std::strstr(l.c_str(), "\"text\":\"ENGLISH\""));
}

void test_airflow_upper_limit_and_postheater(void)
{
    const Property *p = at(0x1E, 1, 0x08);
    TEST_ASSERT_EQUAL_STRING("Airflow upper limit (inferred)", p->name);
    TEST_ASSERT_EQUAL(Format::U16, p->format);
    TEST_ASSERT_EQUAL_STRING("m³/h", p->unit_label);
    p = at(0x27, 1, 0x03);
    TEST_ASSERT_NULL(p->name);
    TEST_ASSERT_EQUAL(Format::U16, p->format);
    // Live POSTHEATER 0x27/01/0x03: 100 in 50..150, step 50 (the value the old renderer called "d").
    const std::string out = entry(0x27, 1, 0x03, {0x64, 0x00}, {0x32, 0x00, 0x96, 0x00}, {0x32, 0x00});
    TEST_ASSERT_EQUAL_STRING(
        (head(39, 1, 3) +
         "\"name\":null,\"access\":null,\"format\":\"u16\",\"unit_label\":null,"
         "\"value\":{\"hex\":\"6400\",\"len\":2,\"number\":100,\"text\":null,\"label\":null},"
         "\"range\":{\"hex\":\"32009600\",\"len\":4,\"min\":50,\"max\":150},"
         "\"step\":{\"hex\":\"3200\",\"len\":2,\"number\":50,\"text\":null,\"label\":null}}")
            .c_str(),
        out.c_str());
}

void test_co2_zones_apply_to_every_subunit(void)
{
    TEST_ASSERT_EQUAL_STRING("CO2ZONES (inferred)", unit_name(1, 1, 0x2B));
    for (uint8_t sub = 1; sub <= 8; sub++) {
        TEST_ASSERT_EQUAL_STRING("Zone bit (inferred)", at(0x2B, sub, 0x01)->name);
        TEST_ASSERT_EQUAL(Format::U8, at(0x2B, sub, 0x01)->format);
        TEST_ASSERT_EQUAL_STRING("CO2 upper limit (inferred)", at(0x2B, sub, 0x02)->name);
        TEST_ASSERT_EQUAL_STRING("CO2 lower limit (inferred)", at(0x2B, sub, 0x03)->name);
        TEST_ASSERT_EQUAL_STRING("CO2 setpoint (inferred)", at(0x2B, sub, 0x05)->name);
        TEST_ASSERT_EQUAL(Format::U16, at(0x2B, sub, 0x05)->format);
        TEST_ASSERT_EQUAL_STRING("ppm", at(0x2B, sub, 0x05)->unit_label);
        TEST_ASSERT_NULL(at(0x2B, sub, 0x06));  // undocumented and not inferred
    }
    TEST_ASSERT_NULL(catalogue_find(0x01, 0x30, 0x2B, 1, 0x05));  // the target's rows only
    // Live 0x2B/03/0x05: 1050 ppm in 0..2000, step 50.
    const std::string out = entry(0x2B, 3, 0x05, {0x1a, 0x04}, {0x00, 0x00, 0xd0, 0x07}, {0x32, 0x00});
    TEST_ASSERT_NOT_NULL(std::strstr(out.c_str(), "\"format\":\"u16\",\"unit_label\":\"ppm\","));
    TEST_ASSERT_NOT_NULL(std::strstr(out.c_str(), "\"number\":1050,"));
    TEST_ASSERT_NOT_NULL(std::strstr(out.c_str(), "\"min\":0,\"max\":2000}"));
}

// --- 6: an undocumented error code ---------------------------------------------------------------

void test_an_undocumented_error_code_is_unknown(void)
{
    // Live HMI 0x1A/01/0x09 answered error 53, which the documented RMI error codes do not name.
    std::memset(&R, 0, sizeof R);
    R.plan.target_node = 1;
    comfoair_probe_entry_t &e = R.entries[R.entry_count++];
    e = {1, 0x1A, 1, 0x09, {COMFOAIR_PROBE_ANSWER_ERROR, 53, 1, {0x35}}, {}, {}};
    TEST_ASSERT_NOT_NULL(std::strstr(render(&R).c_str(),
                                     "\"value\":{\"error\":53,\"error_name\":\"unknown\",\"hex\":\"35\"}"));
}

void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_idle_report_targets_the_default_node);  // first: before anything else
    RUN_TEST(test_a_two_byte_number_ending_in_nul_is_not_text);
    RUN_TEST(test_live_strings_stay_text);
    RUN_TEST(test_a_0000ffff_range_is_unsigned);
    RUN_TEST(test_a_genuinely_signed_range_stays_signed);
    RUN_TEST(test_filter_pre_warning_is_u16_on_the_wire);
    RUN_TEST(test_hmi_rows);
    RUN_TEST(test_airflow_upper_limit_and_postheater);
    RUN_TEST(test_co2_zones_apply_to_every_subunit);
    RUN_TEST(test_an_undocumented_error_code_is_unknown);
    return UNITY_END();
}
