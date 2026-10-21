// The probe report's "pdos" section: the census of every PDO heard since boot.
//
// A suite of its own because test_probe_report is a 500-line binary already; the two share only
// the public render and the private EMIT_PIECE bound. The rows are hand-built
// comfoair_probe_pdo_row_t values handed over by a callback, exactly as the device hands them --
// the census store is its own component and its own suite, and driving it here would make a
// renderer failure look like a store failure.
#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "../../components/comfoair_probe_report/comfoair_probe_report_internal.h"
#include "comfoair_probe_report.h"

using probe_report::EMIT_PIECE;

// ~30 KB: static, never on the stack.
static comfoair_probe_results_t R;

static void reset(void)
{
    std::memset(&R, 0, sizeof R);
    comfoair_probe_plan_t plan = COMFOAIR_PROBE_DEFAULT_PLAN();
    R.plan = plan;
    R.state = COMFOAIR_PROBE_DONE;
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

// The device side: a table of rows, a set of indices the "copy" fails for, and a call count.
struct Rows {
    std::vector<comfoair_probe_pdo_row_t> rows;
    std::vector<size_t> refuse;
    size_t calls = 0;
};

static bool row_fn(void *ctx, size_t i, comfoair_probe_pdo_row_t *out)
{
    Rows *r = static_cast<Rows *>(ctx);
    r->calls++;
    for (size_t x : r->refuse)
        if (x == i)
            return false;
    *out = r->rows[i];
    return true;
}

static comfoair_probe_pdo_summary_t SUMMARY = {3912345, 48211, 3, 1};

static comfoair_probe_meta_t census_meta(Rows &rows)
{
    comfoair_probe_meta_t m = {};
    m.pdo_summary = &SUMMARY;
    m.pdo_rows = rows.rows.size();
    m.pdo_row = row_fn;
    m.pdo_ctx = &rows;
    return m;
}

static std::string render(const comfoair_probe_meta_t *meta)
{
    Sink s;
    TEST_ASSERT_TRUE(comfoair_probe_render(&R, meta, sink_write, &s));
    return s.out;
}

static const char TAIL[] = ",\"truncated\":false}";

// The document with the census must be the document without it, plus exactly `section` spliced in
// before "truncated" -- byte for byte, so no other key moved or changed.
static void expect_section(const comfoair_probe_meta_t *meta, const std::string &section)
{
    comfoair_probe_meta_t bare = *meta;
    bare.pdo_summary = nullptr;
    std::string want = render(&bare);
    TEST_ASSERT_TRUE(want.size() > sizeof TAIL);
    TEST_ASSERT_EQUAL_STRING(TAIL, want.c_str() + want.size() - (sizeof TAIL - 1));
    want.insert(want.size() - (sizeof TAIL - 1), section);
    TEST_ASSERT_EQUAL_STRING(want.c_str(), render(meta).c_str());
}

static comfoair_probe_pdo_row_t row(uint16_t pdo, bool known, uint32_t count, uint32_t first,
                                    uint32_t last, uint16_t dlc, uint64_t nodes,
                                    std::vector<uint8_t> bytes)
{
    comfoair_probe_pdo_row_t r = {};
    r.pdo = pdo;
    r.known = known;
    r.count = count;
    r.first_ms = first;
    r.last_ms = last;
    r.dlc_mask = dlc;
    r.node_mask = nodes;
    r.last_len = (uint8_t)bytes.size();
    for (size_t i = 0; i < bytes.size() && i < sizeof r.last; i++)
        r.last[i] = bytes[i];
    return r;
}

static const char HEAD[] =
    ",\"pdos\":{\"uptime_ms\":3912345,\"frames\":48211,\"distinct\":3,\"overflow\":1,\"rows\":[";

void test_no_summary_means_no_pdos_key(void)
{
    reset();
    Rows rows;
    rows.rows.push_back(row(16, true, 1, 0, 0, 2, 2, {1}));
    comfoair_probe_meta_t m = census_meta(rows);
    m.pdo_summary = nullptr;
    TEST_ASSERT_TRUE(render(&m).find("\"pdos\"") == std::string::npos);
    TEST_ASSERT_TRUE(render(nullptr).find("\"pdos\"") == std::string::npos);
    TEST_ASSERT_EQUAL_size_t(0, rows.calls);  // no summary: the rows are not even asked for
}

void test_zero_rows_render_an_empty_array_before_truncated(void)
{
    reset();
    Rows rows;
    comfoair_probe_meta_t m = census_meta(rows);
    expect_section(&m, std::string(HEAD) + "]}");
    m.pdo_rows = 5;
    m.pdo_row = nullptr;  // rows claimed but no way to fetch them: still a valid, empty array
    expect_section(&m, std::string(HEAD) + "]}");
}

void test_two_rows_render_exactly(void)
{
    reset();
    Rows rows;
    rows.rows.push_back(row(16, true, 412, 1830, 3911020, 1u << 1, 1ull << 1, {0x01}));
    rows.rows.push_back(row(290, false, 7, 2000, 3900000, (1u << 2) | (1u << 0),
                            (1ull << 48) | (1ull << 1), {0xAB, 0x0C}));
    comfoair_probe_meta_t m = census_meta(rows);
    expect_section(&m, std::string(HEAD) +
                           "{\"pdo\":16,\"known\":true,\"count\":412,\"first_ms\":1830,"
                           "\"last_ms\":3911020,\"dlc\":[1],\"nodes\":[1],\"last\":\"01\"},"
                           "{\"pdo\":290,\"known\":false,\"count\":7,\"first_ms\":2000,"
                           "\"last_ms\":3900000,\"dlc\":[0,2],\"nodes\":[1,48],\"last\":\"ab0c\"}"
                           "]}");
}

void test_empty_payload_top_bits_and_garbage(void)
{
    reset();
    Rows rows;
    // Length 0, the top node and the top length; dlc bits above 8 are not lengths and are
    // dropped; a last_len past the array renders the 8 bytes there are, never a read past them.
    rows.rows.push_back(row(1, false, 1, 0, 0, 1u << 0, 1ull << 0, {}));
    rows.rows.push_back(row(8191, false, 1, 0, 0, (1u << 8) | 0xFE00u, 1ull << 63,
                            {1, 2, 3, 4, 5, 6, 7, 8}));
    rows.rows[1].last_len = 200;
    comfoair_probe_meta_t m = census_meta(rows);
    expect_section(&m, std::string(HEAD) +
                           "{\"pdo\":1,\"known\":false,\"count\":1,\"first_ms\":0,\"last_ms\":0,"
                           "\"dlc\":[0],\"nodes\":[0],\"last\":\"\"},"
                           "{\"pdo\":8191,\"known\":false,\"count\":1,\"first_ms\":0,\"last_ms\":0,"
                           "\"dlc\":[8],\"nodes\":[63],\"last\":\"0102030405060708\"}"
                           "]}");
}

void test_a_row_that_cannot_be_copied_is_skipped(void)
{
    reset();
    Rows rows;
    rows.rows.push_back(row(16, true, 1, 0, 0, 1u << 1, 1ull << 1, {0x01}));
    rows.rows.push_back(row(17, true, 1, 0, 0, 1u << 1, 1ull << 1, {0x02}));
    rows.rows.push_back(row(18, true, 1, 0, 0, 1u << 1, 1ull << 1, {0x03}));
    rows.refuse = {1};
    comfoair_probe_meta_t m = census_meta(rows);
    expect_section(&m, std::string(HEAD) +
                           "{\"pdo\":16,\"known\":true,\"count\":1,\"first_ms\":0,\"last_ms\":0,"
                           "\"dlc\":[1],\"nodes\":[1],\"last\":\"01\"},"
                           "{\"pdo\":18,\"known\":true,\"count\":1,\"first_ms\":0,\"last_ms\":0,"
                           "\"dlc\":[1],\"nodes\":[1],\"last\":\"03\"}"
                           "]}");
    rows.refuse = {0};  // the first one refused: no leading comma
    std::string out = render(&m);
    TEST_ASSERT_TRUE(out.find("\"rows\":[{\"pdo\":17,") != std::string::npos);
}

// The worst-case table: COMFOAIR_PDO_CENSUS_MAX (256) rows, every field at its widest.
static void fill_worst(Rows &rows)
{
    for (uint16_t i = 0; i < 256; i++)
        rows.rows.push_back(row((uint16_t)(8191 - 255 + i), true, 0xFFFFFFFFu, 0xFFFFFFFFu,
                                0xFFFFFFFFu, 0x1FF, ~0ull, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                            0xFF, 0xFF}));
}

void test_a_failed_write_mid_rows_stops_the_render(void)
{
    reset();
    Rows rows;
    fill_worst(rows);
    comfoair_probe_meta_t m = census_meta(rows);
    comfoair_probe_meta_t bare = m;
    bare.pdo_summary = nullptr;
    Sink head;
    TEST_ASSERT_TRUE(comfoair_probe_render(&R, &bare, sink_write, &head));
    Sink s;
    s.fail_at = head.calls + 3;  // well inside the rows: each row is several hundred bytes
    rows.calls = 0;
    TEST_ASSERT_FALSE(comfoair_probe_render(&R, &m, sink_write, &s));
    TEST_ASSERT_EQUAL_size_t(s.fail_at, s.calls);  // not one write after the refusal
    TEST_ASSERT_TRUE(rows.calls < rows.rows.size());  // and the rows stopped being fetched
}

void test_every_census_piece_fits_and_the_pieces_join(void)
{
    reset();
    Rows rows;
    fill_worst(rows);
    comfoair_probe_meta_t m = census_meta(rows);
    // The u32 maximum everywhere: a counter or clock about to wrap must print whole, never signed.
    static const comfoair_probe_pdo_summary_t big = {0xFFFFFFFFu, 0xFFFFFFFFu, 256, 0};
    m.pdo_summary = &big;
    Sink s;
    TEST_ASSERT_TRUE(comfoair_probe_render(&R, &m, sink_write, &s));
    TEST_ASSERT_TRUE(s.out.find("\"uptime_ms\":4294967295,\"frames\":4294967295,") !=
                     std::string::npos);
    TEST_ASSERT_TRUE(s.out.find("\"count\":4294967295,\"first_ms\":4294967295,"
                                "\"last_ms\":4294967295,") != std::string::npos);
    TEST_ASSERT_TRUE(s.max_piece <= EMIT_PIECE);
    TEST_ASSERT_TRUE(s.calls > 100);
    TEST_ASSERT_EQUAL_STRING(TAIL, s.out.c_str() + s.out.size() - (sizeof TAIL - 1));
    size_t count = 0;
    for (size_t at = 0; (at = s.out.find("\"last\":\"ffffffffffffffff\"", at)) != std::string::npos;
         at++)
        count++;
    TEST_ASSERT_EQUAL_size_t(256, count);
    TEST_ASSERT_TRUE(s.out.find("\"nodes\":[0,1,2,") != std::string::npos);
    TEST_ASSERT_TRUE(s.out.find(",62,63],") != std::string::npos);
    TEST_ASSERT_TRUE(s.out.find("\"dlc\":[0,1,2,3,4,5,6,7,8]") != std::string::npos);
}

void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_summary_means_no_pdos_key);
    RUN_TEST(test_zero_rows_render_an_empty_array_before_truncated);
    RUN_TEST(test_two_rows_render_exactly);
    RUN_TEST(test_empty_payload_top_bits_and_garbage);
    RUN_TEST(test_a_row_that_cannot_be_copied_is_skipped);
    RUN_TEST(test_a_failed_write_mid_rows_stops_the_render);
    RUN_TEST(test_every_census_piece_fits_and_the_pieces_join);
    return UNITY_END();
}
