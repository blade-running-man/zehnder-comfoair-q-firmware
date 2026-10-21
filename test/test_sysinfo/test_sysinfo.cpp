// GET /api/system: the document's exact shape, its null rules, the error-text table and its
// firmware selector, and the renderer's buffer discipline.
//
// Expected strings are written by hand from the schema (docs/device/ota-and-system.md, GET
// /api/system; key order as in its field table) and the texts from the documented error table,
// not from the implementation. The realistic snapshot is the live unit: Q350 R1.11.0
// (rmi-probe-findings-q350.md).
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

#include "comfoair_probe_report.h"
#include "comfoair_sysinfo.h"

static const uint8_t NODES_LIVE[] = {1};

// The live unit, a connected device, a quiet bus.
static comfoair_sysinfo_t live(void)
{
    comfoair_sysinfo_t s;
    std::memset(&s, 0, sizeof s);
    s.unit.identity_state = COMFOAIR_SYSINFO_READ_OK;
    s.unit.identity_age_ms = 1234;
    s.unit.identity_ever_read = true;
    std::strcpy(s.unit.model, "ComfoAir Q350 NL R VV ST");
    std::strcpy(s.unit.serial, "DEM0123456789012345678901234567");
    std::strcpy(s.unit.article, "471502057");
    std::strcpy(s.unit.country, "NL");
    std::strcpy(s.unit.name, "ComfoAir Q");
    s.unit.has_firmware = true;
    s.unit.firmware_raw = 0xC0102C00u;
    s.unit.errors_state = COMFOAIR_SYSINFO_READ_OK;
    s.device = {"0.1.0", "v5.5.5", "Oct 17 2026 10:00:09", "m5stack-nanoc6", "esp32c6", 2,
                4194304, "ota_0", 123456, "power_on", 150000, 120000, 90000, "9c139ed48ae4",
                "9c:13:9e:d4:8a:e4"};
    s.network = {true, "home", "192.168.50.123", true, -61, 6};
    s.bus = {true, "active", 1, 0, 0, 0, 0, 0, NODES_LIVE, 1};
    s.commands = {0, 0, 0, 0};
    s.mqtt = {true, false, 0, 0, 0, 0};
    return s;
}

static const char LIVE_JSON[] =
    "{\"unit\":{\"state\":\"ok\",\"age_ms\":1234,\"model\":\"ComfoAir Q350 NL R VV ST\","
    "\"serial\":\"DEM0123456789012345678901234567\",\"firmware\":\"R1.11.0\","
    "\"firmware_raw\":3222285312,\"article\":\"471502057\",\"country\":\"NL\","
    "\"name\":\"ComfoAir Q\",\"errors\":{\"state\":\"ok\",\"ids\":[],\"items\":[]}},"
    "\"device\":{\"firmware\":\"0.1.0\",\"idf\":\"v5.5.5\",\"built\":\"Oct 17 2026 10:00:09\","
    "\"board\":\"m5stack-nanoc6\",\"chip\":\"esp32c6\",\"chip_revision\":2,"
    "\"flash_bytes\":4194304,\"partition\":\"ota_0\",\"uptime_ms\":123456,"
    "\"reset_reason\":\"power_on\",\"heap_free\":150000,\"heap_min_free\":120000,"
    "\"heap_largest_block\":90000,\"device_id\":\"9c139ed48ae4\","
    "\"mac\":\"9c:13:9e:d4:8a:e4\"},"
    "\"network\":{\"connected\":true,\"ssid\":\"home\",\"ip\":\"192.168.50.123\",\"rssi\":-61,"
    "\"channel\":6},"
    "\"bus\":{\"started\":true,\"state\":\"active\",\"rx_frames\":1,\"rx_dropped\":0,"
    "\"bus_errors\":0,\"bus_recoveries\":0,\"tx_error_count\":0,\"rx_error_count\":0,"
    "\"nodes_seen\":[1]},"
    "\"commands\":{\"queued\":0,\"sent\":0,\"failed\":0,\"dropped\":0},"
    "\"mqtt\":{\"configured\":true,\"connected\":false,\"published\":0,\"commands\":0,"
    "\"rejected\":0,\"reconnects\":0}}";

// 64 KiB: comfortably above the worst case measured below; static, never on the stack.
static char BUF[65536];

static std::string render(const comfoair_sysinfo_t &s)
{
    const size_t n = comfoair_sysinfo_render(&s, BUF, sizeof BUF);
    TEST_ASSERT_NOT_EQUAL(0, n);
    TEST_ASSERT_EQUAL_size_t(std::strlen(BUF), n);
    return std::string(BUF, n);
}

// The text between the first `"<key>":` and the next `,"<next>":` -- so it includes the closing
// braces of the objects that end between the two.
static std::string section(const std::string &json, const char *key, const char *next)
{
    const std::string open = std::string("\"") + key + "\":";
    const size_t a = json.find(open);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, a);
    const size_t from = a + open.size();
    const size_t b = next ? json.find(std::string(",\"") + next + "\":", from) : json.size() - 1;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, b);
    return json.substr(from, b - from);
}

static void set_error(comfoair_sysinfo_t &s, unsigned id)
{
    // Byte i, bit b (LSB first) is error id i*8 + b (comfoair-pdo-rmi.md §4 #7).
    s.unit.errors[id / 8] |= (uint8_t)(1u << (id % 8));
}

static const char T21[] = "DANGER! OVERHEATING! Two or more sensors are detecting an incorrect "
                          "temperature. Ventilation has stopped.";
static const char T70_NEW[] =
    "Analog input 1 was present, but is no longer detected (ANALOG_1_PRES ERROR)";
static const char T70_OLD[] =
    "ComfoHood was present, but is no longer detected (HOOD_CONNECT ERROR)";

void test_full_render_of_the_live_unit(void)
{
    TEST_ASSERT_EQUAL_STRING(LIVE_JSON, render(live()).c_str());
}

void test_a_never_read_unit_is_all_null(void)
{
    comfoair_sysinfo_t s = live();
    std::memset(&s.unit, 0, sizeof s.unit);
    s.unit.identity_age_ms = 999;  // ignored: never read
    TEST_ASSERT_EQUAL_STRING(
        "{\"state\":\"unknown\",\"age_ms\":null,\"model\":null,\"serial\":null,"
        "\"firmware\":null,\"firmware_raw\":null,\"article\":null,\"country\":null,"
        "\"name\":null,\"errors\":{\"state\":\"unknown\",\"ids\":[],\"items\":[]}}",
        section(render(s), "unit", "device").c_str());
}

void test_no_answer_keeps_old_identity_but_shows_no_stale_errors(void)
{
    comfoair_sysinfo_t s = live();
    s.unit.identity_state = COMFOAIR_SYSINFO_READ_NO_ANSWER;
    s.unit.errors_state = COMFOAIR_SYSINFO_READ_NO_ANSWER;
    set_error(s, 21);  // left over from an earlier read: must not be shown
    const std::string unit = section(render(s), "unit", "device");
    TEST_ASSERT_EQUAL(0u, unit.find("{\"state\":\"no_answer\",\"age_ms\":1234,\"model\":\"ComfoAir"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
                          unit.find("\"errors\":{\"state\":\"no_answer\",\"ids\":[],\"items\":[]}}"));
}

void test_error_text_tables_and_the_selector(void)
{
    const uint32_t R1_11_0 = 0xC0102C00u, R1_4_0 = 3222278144u;  // comfoair-pdo-rmi.md §4
    // The base rows (ids 21-69) are in both tables.
    TEST_ASSERT_EQUAL_STRING(T21, comfoair_sysinfo_error_text(21, true, R1_11_0));
    TEST_ASSERT_EQUAL_STRING(T21, comfoair_sysinfo_error_text(21, true, R1_4_0));
    // 70 differs: NEWER above R1.4.0, OLDER at it and below; the boundary itself is old.
    TEST_ASSERT_EQUAL_STRING(T70_NEW, comfoair_sysinfo_error_text(70, true, R1_11_0));
    TEST_ASSERT_EQUAL_STRING(T70_OLD, comfoair_sysinfo_error_text(70, true, R1_4_0));
    TEST_ASSERT_EQUAL_STRING(T70_NEW, comfoair_sysinfo_error_text(70, true, R1_4_0 + 1));
    TEST_ASSERT_EQUAL_STRING(T70_OLD, comfoair_sysinfo_error_text(70, true, 0));
    // No firmware read: the newer table.
    TEST_ASSERT_EQUAL_STRING(T70_NEW, comfoair_sysinfo_error_text(70, false, 0));
    // 90 exists only in NEWER; 86 is the last OLDER row and a different text in NEWER.
    TEST_ASSERT_EQUAL_STRING("ComfoCool is overheating",
                             comfoair_sysinfo_error_text(90, true, R1_11_0));
    TEST_ASSERT_NULL(comfoair_sysinfo_error_text(90, true, R1_4_0));
    TEST_ASSERT_EQUAL_STRING("ComfoCool supply air temperature sensor error (CCOOL_TEMP ERROR)",
                             comfoair_sysinfo_error_text(86, true, R1_4_0));
    TEST_ASSERT_EQUAL_STRING("Analog input 2 error (ANALOG_2_IN ERROR)",
                             comfoair_sysinfo_error_text(86, true, R1_11_0));
    // The first and last rows of each part, so a dropped edge row shows.
    TEST_ASSERT_EQUAL_STRING("Postheater was present, but is no longer detected "
                             "(POSTHEAT_CONNECT ERROR)",
                             comfoair_sysinfo_error_text(69, true, R1_4_0));
    TEST_ASSERT_EQUAL_STRING("CO\xe2\x82\x82 Sensor C error",
                             comfoair_sysinfo_error_text(104, true, R1_11_0));
    // Unknown: below the table, a gap in BASE, past the end, past a byte.
    TEST_ASSERT_NULL(comfoair_sysinfo_error_text(0, true, R1_11_0));
    TEST_ASSERT_NULL(comfoair_sysinfo_error_text(20, true, R1_11_0));
    TEST_ASSERT_NULL(comfoair_sysinfo_error_text(36, true, R1_11_0));
    TEST_ASSERT_NULL(comfoair_sysinfo_error_text(105, true, R1_11_0));
    TEST_ASSERT_NULL(comfoair_sysinfo_error_text(87, true, R1_4_0));
    TEST_ASSERT_NULL(comfoair_sysinfo_error_text(255, true, R1_11_0));
    TEST_ASSERT_NULL(comfoair_sysinfo_error_text(256 + 21, true, R1_11_0));
}

void test_rendered_errors_follow_the_firmware(void)
{
    comfoair_sysinfo_t s = live();
    set_error(s, 21);
    set_error(s, 70);
    set_error(s, 200);  // no text in either table
    std::string want = std::string("{\"state\":\"ok\",\"ids\":[21,70,200],\"items\":[{\"id\":21,"
                                   "\"text\":\"") + T21 + "\"},{\"id\":70,\"text\":\"" + T70_NEW +
                       "\"},{\"id\":200,\"text\":null}]}}";
    TEST_ASSERT_EQUAL_STRING(want.c_str(), section(render(s), "errors", "device").c_str());

    s.unit.firmware_raw = 3222278144u;  // R1.4.0: the OLDER table
    want = std::string("{\"state\":\"ok\",\"ids\":[21,70,200],\"items\":[{\"id\":21,\"text\":\"") +
           T21 + "\"},{\"id\":70,\"text\":\"" + T70_OLD + "\"},{\"id\":200,\"text\":null}]}}";
    TEST_ASSERT_EQUAL_STRING(want.c_str(), section(render(s), "errors", "device").c_str());
}

void test_own_utf8_error_text_passes_through(void)
{
    // Id 104's "CO₂": the table is our UTF-8, not unit bytes; read as Latin-1 it would be
    // "COâ\u0082\u0082".
    comfoair_sysinfo_t s = live();
    set_error(s, 104);
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
                          render(s).find("{\"id\":104,\"text\":\"CO\xe2\x82\x82 Sensor C error\"}"));
}

void test_a_hostile_serial_is_escaped(void)
{
    comfoair_sysinfo_t s = live();
    std::strcpy(s.unit.serial, "a\"b\\c\nd\x01\x7f\xff/");
    TEST_ASSERT_EQUAL_STRING("\"a\\\"b\\\\c\\u000ad\\u0001\\u007f\\u00ff/\"",
                             section(render(s), "serial", "firmware").c_str());
}

void test_an_unterminated_unit_string_stops_at_its_field(void)
{
    comfoair_sysinfo_t s = live();
    std::memset(s.unit.model, 'M', sizeof s.unit.model);  // no NUL: the next field must not leak
    const std::string want = "\"" + std::string(COMFOAIR_SYSINFO_TEXT_MAX, 'M') + "\"";
    TEST_ASSERT_EQUAL_STRING(want.c_str(), section(render(s), "model", "serial").c_str());
}

void test_a_disconnected_network_is_null(void)
{
    comfoair_sysinfo_t s = live();
    s.network.connected = false;  // the stale values must not be shown
    TEST_ASSERT_EQUAL_STRING(
        "{\"connected\":false,\"ssid\":null,\"ip\":null,\"rssi\":null,\"channel\":null}",
        section(render(s), "network", "bus").c_str());

    s.network = {true, "", nullptr, false, -61, 0};  // connected, but nothing known yet
    TEST_ASSERT_EQUAL_STRING(
        "{\"connected\":true,\"ssid\":null,\"ip\":null,\"rssi\":null,\"channel\":null}",
        section(render(s), "network", "bus").c_str());
}

void test_null_pointers_render_null(void)
{
    comfoair_sysinfo_t s = live();
    s.device.firmware = nullptr;
    s.bus.state = nullptr;
    s.bus.nodes_seen = nullptr;
    s.bus.nodes_seen_count = 3;  // a count without an array is no nodes
    const std::string json = render(s);
    TEST_ASSERT_EQUAL(0u, section(json, "device", "network").find("{\"firmware\":null,"));
    TEST_ASSERT_EQUAL_STRING("[]}", section(json, "nodes_seen", "commands").c_str());
    TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find("\"started\":true,\"state\":null,"));
}

void test_a_buffer_too_small_returns_zero_and_never_overruns(void)
{
    const comfoair_sysinfo_t s = live();
    const size_t len = std::strlen(LIVE_JSON);
    static char small[sizeof LIVE_JSON + 16];
    for (size_t cap = 0; cap <= len; cap++) {
        std::memset(small, 0xA5, sizeof small);
        TEST_ASSERT_EQUAL_size_t(0, comfoair_sysinfo_render(&s, small, cap));
        for (size_t i = cap; i < sizeof small; i++)
            TEST_ASSERT_EQUAL_HEX8(0xA5, (uint8_t)small[i]);
    }
    std::memset(small, 0xA5, sizeof small);
    TEST_ASSERT_EQUAL_size_t(len, comfoair_sysinfo_render(&s, small, len + 1));
    TEST_ASSERT_EQUAL_STRING(LIVE_JSON, small);
    TEST_ASSERT_EQUAL_HEX8(0xA5, (uint8_t)small[len + 1]);
}

// Every field at its widest: unit strings full of bytes that escape to six, the device strings at
// their esp_app_desc_t / partition-label sizes and escaping too, 64 nodes, all 256 error bits set.
static size_t worst_case(uint32_t firmware_raw)
{
    static char dev[16][33];
    for (auto &d : dev) {
        std::memset(d, 0x01, 32);
        d[32] = 0;
    }
    comfoair_sysinfo_t s;
    std::memset(&s, 0, sizeof s);
    s.unit.identity_state = COMFOAIR_SYSINFO_READ_NO_ANSWER;
    s.unit.identity_age_ms = UINT32_MAX;
    s.unit.identity_ever_read = true;
    for (char *f : {s.unit.model, s.unit.serial, s.unit.article, s.unit.country, s.unit.name})
        std::memset(f, 0xFF, COMFOAIR_SYSINFO_TEXT_MAX);  // unterminated: the full field
    s.unit.has_firmware = true;
    s.unit.firmware_raw = firmware_raw;
    s.unit.errors_state = COMFOAIR_SYSINFO_READ_OK;
    std::memset(s.unit.errors, 0xFF, sizeof s.unit.errors);
    static uint8_t nodes[COMFOAIR_SYSINFO_MAX_NODES + 8];
    for (size_t i = 0; i < sizeof nodes; i++)
        nodes[i] = (uint8_t)(200 + i % 56);
    s.device = {dev[0], dev[1], dev[2], dev[3], dev[4], UINT16_MAX, UINT32_MAX, dev[5],
                UINT64_MAX, dev[6], UINT32_MAX, UINT32_MAX, UINT32_MAX, dev[7], dev[8]};
    s.network = {true, dev[9], dev[10], true, -128, 255};
    s.bus = {true, dev[11], UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT16_MAX,
             UINT16_MAX, nodes, sizeof nodes};
    s.commands = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
    s.mqtt = {true, false, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
    const std::string json = render(s);
    // More nodes than the bound are cut at COMFOAIR_SYSINFO_MAX_NODES, so the bound holds.
    const std::string seen = section(json, "nodes_seen", "commands");
    size_t commas = 0;
    for (char c : seen)
        commas += c == ',';
    TEST_ASSERT_EQUAL_size_t(COMFOAIR_SYSINFO_MAX_NODES - 1, commas);
    return json.size();
}

void test_worst_case_size(void)
{
    const size_t newer = worst_case(0xC0102C00u), older = worst_case(3222278144u);
    char msg[96];
    std::snprintf(msg, sizeof msg, "worst case: NEWER %zu bytes, OLDER %zu bytes", newer,
                  older);
    TEST_MESSAGE(msg);
    // 17 KiB is what the HTTP handler allocates; a new field that grows the document past it must
    // fail here, not come back as a 0 from the device. Nearly all of it is 256 error items, which
    // no unit will ever raise at once -- but "never" is not a bound, and the cost is one heap block
    // for the length of one request.
    TEST_ASSERT_LESS_OR_EQUAL_size_t(17 * 1024, newer);
    TEST_ASSERT_LESS_OR_EQUAL_size_t(17 * 1024, older);
}

void test_probe_version_text(void)
{
    char buf[16];
    // 0xC0102C00 is the live unit's 0x06 answer.
    TEST_ASSERT_EQUAL_size_t(7, comfoair_probe_version_text(0xC0102C00u, buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING("R1.11.0", buf);
    TEST_ASSERT_EQUAL_size_t(6, comfoair_probe_version_text(3222278144u, buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING("R1.4.0", buf);
    TEST_ASSERT_EQUAL_size_t(6, comfoair_probe_version_text(0, buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING("U0.0.0", buf);
    TEST_ASSERT_EQUAL_size_t(15, comfoair_probe_version_text(0xFFFFFFFFu, buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING("R1023.1023.1023", buf);
    // Too small: 0, and nothing past cap.
    std::memset(buf, 0xA5, sizeof buf);
    TEST_ASSERT_EQUAL_size_t(0, comfoair_probe_version_text(0xC0102C00u, buf, 7));
    for (size_t i = 7; i < sizeof buf; i++)
        TEST_ASSERT_EQUAL_HEX8(0xA5, (uint8_t)buf[i]);
    TEST_ASSERT_EQUAL_size_t(7, comfoair_probe_version_text(0xC0102C00u, buf, 8));
    TEST_ASSERT_EQUAL_STRING("R1.11.0", buf);
    TEST_ASSERT_EQUAL_size_t(0, comfoair_probe_version_text(0xC0102C00u, nullptr, 0));
}

void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_full_render_of_the_live_unit);
    RUN_TEST(test_a_never_read_unit_is_all_null);
    RUN_TEST(test_no_answer_keeps_old_identity_but_shows_no_stale_errors);
    RUN_TEST(test_error_text_tables_and_the_selector);
    RUN_TEST(test_rendered_errors_follow_the_firmware);
    RUN_TEST(test_own_utf8_error_text_passes_through);
    RUN_TEST(test_a_hostile_serial_is_escaped);
    RUN_TEST(test_an_unterminated_unit_string_stops_at_its_field);
    RUN_TEST(test_a_disconnected_network_is_null);
    RUN_TEST(test_null_pointers_render_null);
    RUN_TEST(test_a_buffer_too_small_returns_zero_and_never_overruns);
    RUN_TEST(test_worst_case_size);
    RUN_TEST(test_probe_version_text);
    return UNITY_END();
}
