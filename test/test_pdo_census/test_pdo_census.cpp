// The PDO census against hand-fed frames.
//
// The census is one static instance (≈14 KB, too big for a test's stack and exactly how the
// device keeps it). Records are looked up by scanning rec[0, count) for their pdo, NOT through the
// index: the index is what is under test, so a lookup that went through it would agree with
// whatever it got wrong. The index is exercised by re-recording a PDO and asserting that the
// existing record was updated rather than a second one appended.
#include <unity.h>

#include <cstring>

#include "comfoair_pdo_census.h"

namespace {

comfoair_pdo_census_t census;

const comfoair_pdo_census_record_t *find(uint16_t pdo)
{
    for (uint32_t i = 0; i < census.count; i++) {
        if (census.rec[i].pdo == pdo) return &census.rec[i];
    }
    return nullptr;
}

void feed(uint16_t pdo, uint8_t node, const uint8_t *data, size_t len, uint32_t now)
{
    comfoair_pdo_census_record(&census, pdo, node, data, len, now);
}

void feed1(uint16_t pdo, uint8_t byte, uint32_t now)  // one payload byte, always from node 1
{
    feed(pdo, 1, &byte, 1, now);
}

}  // namespace

void setUp(void)
{
    std::memset(&census, 0xA5, sizeof census);  // reset() must not lean on a zeroed .bss
    comfoair_pdo_census_reset(&census);
}

void tearDown(void) {}

void test_reset_leaves_nothing(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, census.count);
    TEST_ASSERT_EQUAL_UINT32(0, census.frames);
    TEST_ASSERT_EQUAL_UINT32(0, census.distinct);
    uint16_t out[4];
    TEST_ASSERT_EQUAL(0, comfoair_pdo_census_order(&census, census.count, out, 4));
    feed1(16, 0x01, 5);  // a PDO heard before the reset garbage would not be "first sight"
    TEST_ASSERT_EQUAL_UINT32(1, census.distinct);
    TEST_ASSERT_EQUAL_UINT32(1, census.count);
}

void test_the_first_frame_creates_one_record(void)
{
    const uint8_t data[] = {0xde, 0xad, 0xbe, 0xef};
    feed(65, 0x0d, data, sizeof data, 1830);
    TEST_ASSERT_EQUAL_UINT32(1, census.count);
    TEST_ASSERT_EQUAL_UINT32(1, census.frames);
    TEST_ASSERT_EQUAL_UINT32(1, census.distinct);
    const comfoair_pdo_census_record_t *r = find(65);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_UINT32(1, r->count);
    TEST_ASSERT_EQUAL_UINT32(1830, r->first_ms);
    TEST_ASSERT_EQUAL_UINT32(1830, r->last_ms);
    TEST_ASSERT_EQUAL_HEX16(1u << 4, r->dlc_mask);
    TEST_ASSERT_TRUE(r->node_mask == (1ull << 0x0d));
    TEST_ASSERT_EQUAL(4, r->last_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, r->last, 4);
}

void test_repeated_frames_update_and_keep_first_ms(void)
{
    const uint8_t a[] = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint8_t b[] = {0x99, 0x98};
    feed(117, 1, a, 8, 100);
    feed(117, 63, b, 2, 250);
    feed(117, 1, b, 2, 400);
    TEST_ASSERT_EQUAL_UINT32(1, census.count);
    TEST_ASSERT_EQUAL_UINT32(3, census.frames);
    TEST_ASSERT_EQUAL_UINT32(1, census.distinct);
    const comfoair_pdo_census_record_t *r = find(117);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_UINT32(3, r->count);
    TEST_ASSERT_EQUAL_UINT32(100, r->first_ms);
    TEST_ASSERT_EQUAL_UINT32(400, r->last_ms);
    TEST_ASSERT_EQUAL_HEX16((1u << 8) | (1u << 2), r->dlc_mask);
    TEST_ASSERT_TRUE(r->node_mask == ((1ull << 1) | (1ull << 63)));
    TEST_ASSERT_EQUAL(2, r->last_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(b, r->last, 2);
    const uint8_t zeros[6] = {};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zeros, r->last + 2, 6);  // a shorter frame leaves no stale tail
}

void test_past_the_ceiling_pdos_are_counted_but_get_no_record(void)
{
    for (uint16_t p = 0; p < 300; p++) feed1(static_cast<uint16_t>(p * 7), 0x01, p);
    TEST_ASSERT_EQUAL_UINT32(COMFOAIR_PDO_CENSUS_MAX, census.count);
    TEST_ASSERT_EQUAL_UINT32(300, census.distinct);
    TEST_ASSERT_EQUAL_UINT32(300, census.frames);
    TEST_ASSERT_EQUAL_UINT32(44, census.distinct - census.count);  // what the caller calls overflow
    TEST_ASSERT_NOT_NULL(find(255 * 7));
    TEST_ASSERT_NULL(find(256 * 7));

    feed1(256 * 7, 0x02, 999);  // heard again: a frame, not a new PDO, and still no row
    TEST_ASSERT_EQUAL_UINT32(COMFOAIR_PDO_CENSUS_MAX, census.count);
    TEST_ASSERT_EQUAL_UINT32(300, census.distinct);
    TEST_ASSERT_EQUAL_UINT32(301, census.frames);
    TEST_ASSERT_NULL(find(256 * 7));

    feed1(3 * 7, 0x02, 1000);  // a recorded PDO is still found and updated past the ceiling
    TEST_ASSERT_EQUAL_UINT32(2, find(3 * 7)->count);
    TEST_ASSERT_EQUAL_UINT32(COMFOAIR_PDO_CENSUS_MAX, census.count);
}

void test_out_of_range_frames_change_nothing(void)
{
    const uint8_t nine[9] = {};
    feed(8192, 1, nine, 1, 1);
    feed(0xffff, 1, nine, 1, 1);
    feed(16, 64, nine, 1, 1);
    feed(16, 0xff, nine, 1, 1);
    feed(16, 1, nine, 9, 1);
    feed(16, 1, nullptr, 1, 1);
    TEST_ASSERT_EQUAL_UINT32(0, census.count);
    TEST_ASSERT_EQUAL_UINT32(0, census.frames);
    TEST_ASSERT_EQUAL_UINT32(0, census.distinct);

    feed(8191, 63, nine, 8, 2);  // the edges themselves are in range
    TEST_ASSERT_EQUAL_UINT32(1, census.count);
    TEST_ASSERT_TRUE(find(8191)->node_mask == (1ull << 63));
}

void test_a_zero_length_frame_records_dlc_bit_0(void)
{
    const uint8_t a[] = {7, 7, 7};
    feed(290, 5, a, 3, 10);
    feed(290, 5, nullptr, 0, 20);
    const comfoair_pdo_census_record_t *r = find(290);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_HEX16((1u << 0) | (1u << 3), r->dlc_mask);
    TEST_ASSERT_EQUAL(0, r->last_len);
    const uint8_t zeros[8] = {};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zeros, r->last, 8);
    TEST_ASSERT_EQUAL_UINT32(20, r->last_ms);
}

void test_order_sorts_by_pdo_and_respects_cap(void)
{
    feed1(4201, 0, 1);
    feed1(16, 0, 2);
    feed1(1024, 0, 3);
    feed1(337, 0, 4);
    uint16_t out[8];
    std::memset(out, 0xff, sizeof out);
    TEST_ASSERT_EQUAL(4, comfoair_pdo_census_order(&census, census.count, out, 8));
    const uint16_t want[] = {1, 3, 2, 0};  // indices of 16, 337, 1024, 4201
    TEST_ASSERT_EQUAL_UINT16_ARRAY(want, out, 4);
    TEST_ASSERT_EQUAL_HEX16(0xffff, out[4]);

    std::memset(out, 0xff, sizeof out);
    TEST_ASSERT_EQUAL(2, comfoair_pdo_census_order(&census, census.count, out, 2));
    TEST_ASSERT_EQUAL_UINT16_ARRAY(want, out, 2);  // the two lowest PDOs, not the first two rows
    TEST_ASSERT_EQUAL_HEX16(0xffff, out[2]);

    // A snapshot older than the census sees only the records it covered.
    TEST_ASSERT_EQUAL(2, comfoair_pdo_census_order(&census, 2, out, 8));
    TEST_ASSERT_EQUAL_UINT16(1, out[0]);
    TEST_ASSERT_EQUAL_UINT16(0, out[1]);
    TEST_ASSERT_EQUAL(0, comfoair_pdo_census_order(&census, census.count, out, 0));
}

void test_colliding_pdos_are_each_found(void)
{
    // All ≡ 16 (mod 512), plus 511/1023/1535 which collide on the last slot and wrap to slot 0.
    const uint16_t pdos[] = {16, 528, 1040, 4112, 511, 1023, 1535, 17};
    const size_t n = sizeof pdos / sizeof pdos[0];
    const uint8_t b = 0x42;
    for (size_t i = 0; i < n; i++) feed(pdos[i], 1, &b, 1, static_cast<uint32_t>(i));
    for (size_t i = 0; i < n; i++) feed(pdos[i], static_cast<uint8_t>(2 + i), &b, 1, 100 + i);
    TEST_ASSERT_EQUAL_UINT32(n, census.count);
    TEST_ASSERT_EQUAL_UINT32(n, census.distinct);
    TEST_ASSERT_EQUAL_UINT32(2 * n, census.frames);
    for (size_t i = 0; i < n; i++) {
        const comfoair_pdo_census_record_t *r = find(pdos[i]);
        TEST_ASSERT_NOT_NULL(r);
        TEST_ASSERT_EQUAL_UINT32(2, r->count);
        TEST_ASSERT_EQUAL_UINT32(i, r->first_ms);
        TEST_ASSERT_EQUAL_UINT32(100 + i, r->last_ms);
        TEST_ASSERT_TRUE(r->node_mask == ((1ull << 1) | (1ull << (2 + i))));
    }
}

void test_a_full_census_still_finds_every_pdo(void)
{
    // Clustered on purpose: 256 PDOs over only 32 home slots make long probe runs.
    uint16_t pdos[COMFOAIR_PDO_CENSUS_MAX];
    for (int i = 0; i < COMFOAIR_PDO_CENSUS_MAX; i++)
        pdos[i] = static_cast<uint16_t>((i % 32) + 512 * (i / 32) + 480);
    for (int i = 0; i < COMFOAIR_PDO_CENSUS_MAX; i++) feed1(pdos[i], 1, 1);
    TEST_ASSERT_EQUAL_UINT32(COMFOAIR_PDO_CENSUS_MAX, census.count);
    for (int round = 0; round < 3; round++)
        for (int i = 0; i < COMFOAIR_PDO_CENSUS_MAX; i++) feed1(pdos[i], 2, 2);
    TEST_ASSERT_EQUAL_UINT32(COMFOAIR_PDO_CENSUS_MAX, census.count);
    TEST_ASSERT_EQUAL_UINT32(COMFOAIR_PDO_CENSUS_MAX, census.distinct);
    for (int i = 0; i < COMFOAIR_PDO_CENSUS_MAX; i++) {
        const comfoair_pdo_census_record_t *r = find(pdos[i]);
        TEST_ASSERT_NOT_NULL(r);
        TEST_ASSERT_EQUAL_UINT32(4, r->count);
    }
    // A new PDO homed at slot 480 (5088 = 480 + 9 * 512) with the index full: its miss walks the
    // whole 256-slot run to the empty slot past it. Counted, no row, and the probe ends.
    feed1(5088, 1, 3);
    TEST_ASSERT_EQUAL_UINT32(COMFOAIR_PDO_CENSUS_MAX + 1, census.distinct);
    feed1(5088, 1, 4);
    TEST_ASSERT_EQUAL_UINT32(COMFOAIR_PDO_CENSUS_MAX * 4 + 2, census.frames);

    uint16_t out[COMFOAIR_PDO_CENSUS_MAX];
    TEST_ASSERT_EQUAL(COMFOAIR_PDO_CENSUS_MAX,
                      comfoair_pdo_census_order(&census, census.count, out, COMFOAIR_PDO_CENSUS_MAX));
    for (int i = 1; i < COMFOAIR_PDO_CENSUS_MAX; i++)
        TEST_ASSERT_TRUE(census.rec[out[i - 1]].pdo < census.rec[out[i]].pdo);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_reset_leaves_nothing);
    RUN_TEST(test_the_first_frame_creates_one_record);
    RUN_TEST(test_repeated_frames_update_and_keep_first_ms);
    RUN_TEST(test_past_the_ceiling_pdos_are_counted_but_get_no_record);
    RUN_TEST(test_out_of_range_frames_change_nothing);
    RUN_TEST(test_a_zero_length_frame_records_dlc_bit_0);
    RUN_TEST(test_order_sorts_by_pdo_and_respects_cap);
    RUN_TEST(test_colliding_pdos_are_each_found);
    RUN_TEST(test_a_full_census_still_finds_every_pdo);
    return UNITY_END();
}
