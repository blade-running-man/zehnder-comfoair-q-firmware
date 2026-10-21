// The walk's bounds: what stops it asking, and what never may.
//
// The fake unit here is a RULE, not a table: one function says, per request, DATA, silence or an
// error code, and a DATA value is the property number. Each test states the misbehaviour it
// models in one line. Owned by this suite alone -- DO NOT reach for test_probe_scan's table fake:
// once two suites share a fake, "it grew a feature for suite B" quietly weakens suite A.
//
// The request counts are pinned exactly: each bound exists to make a walk shorter, and the count
// is the thing it makes shorter. A count that drifts is a bound that moved.
#include <unity.h>

#include <cstdio>
#include <functional>
#include <vector>

#include "comfoair_probe.h"

namespace {

constexpr int DATA = 0;
constexpr int SILENT = -1;
using Req = comfoair_probe_request_t;
using Rule = std::function<int(const Req &)>;

struct Walk {
    std::vector<Req> log;

    // Bounded, so a walk that never ends fails the test instead of hanging it.
    void run(const Rule &rule, size_t limit = 100000)
    {
        Req r;
        uint8_t byte = 0;
        while (comfoair_probe_next(&r)) {
            log.push_back(r);
            TEST_ASSERT_TRUE_MESSAGE(log.size() < limit, "the walk does not end");
            int err = rule(r);
            comfoair_probe_outcome_t o{};
            o.kind = err == SILENT ? COMFOAIR_PROBE_ANSWER_TIMEOUT
                     : err == DATA ? COMFOAIR_PROBE_ANSWER_DATA
                                   : COMFOAIR_PROBE_ANSWER_ERROR;
            if (err != SILENT) {
                byte = static_cast<uint8_t>(err == DATA ? r.property : err);
                o.error = static_cast<uint8_t>(err == DATA ? 0 : err);
                o.data = &byte;
                o.len = 1;
            }
            comfoair_probe_record(&o);
        }
    }

    size_t count(const std::function<bool(const Req &)> &pred) const
    {
        size_t n = 0;
        for (const Req &r : log)
            n += pred(r) ? 1 : 0;
        return n;
    }
};

// Unit 1 of node 1 with `subunits` subunits whose every property answers with data and has no
// range (15); every other unit is absent. The base most rules below bend in one place.
int tree(const Req &r, uint8_t subunits)
{
    if (r.kind == COMFOAIR_PROBE_REQ_ACTIVE_ERRORS)
        return DATA;
    if (r.unit != 1)
        return COMFOAIR_PROBE_ERR_UNKNOWN_UNIT;
    if (r.subunit > subunits)
        return COMFOAIR_PROBE_ERR_UNKNOWN_SUBUNIT;
    return r.type == COMFOAIR_PROBE_TYPE_VALUE ? DATA : COMFOAIR_PROBE_ERR_NO_RANGE;
}

const comfoair_probe_entry_t *entry(uint8_t unit, uint8_t sub, uint8_t prop)
{
    const comfoair_probe_results_t *res = comfoair_probe_results();
    for (size_t i = 0; i < res->entry_count; i++) {
        const comfoair_probe_entry_t &e = res->entries[i];
        if (e.node == 1 && e.unit == unit && e.subunit == sub && e.property == prop)
            return &e;
    }
    return nullptr;
}

const comfoair_probe_unit_t *row(uint8_t unit)
{
    const comfoair_probe_results_t *res = comfoair_probe_results();
    for (size_t i = 0; i < res->unit_count; i++)
        if (res->units[i].node == 1 && res->units[i].unit == unit)
            return &res->units[i];
    return nullptr;
}

void begin_max_unit(uint8_t max_unit)
{
    comfoair_probe_plan_t p = COMFOAIR_PROBE_DEFAULT_PLAN();
    p.max_unit = max_unit;
    comfoair_probe_begin(&p);
}

bool value_at(const Req &r, uint8_t sub, uint8_t prop)
{
    return r.type == COMFOAIR_PROBE_TYPE_VALUE && r.subunit == sub && r.property == prop;
}

}  // namespace

void setUp(void) { comfoair_probe_begin(nullptr); }
void tearDown(void) {}

// --- silence ---------------------------------------------------------------------------------

void test_five_silent_answers_first_mean_no_answer(void)
{
    Walk w;
    w.run([](const Req &) { return SILENT; });
    const comfoair_probe_results_t *res = comfoair_probe_results();
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_FAILED, res->state);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_FAILURE_NO_ANSWER, res->failure);
    TEST_ASSERT_EQUAL(10, w.log.size());  // five final answers, each after one retry
    TEST_ASSERT_EQUAL(10, res->progress.timeouts);
    TEST_ASSERT_EQUAL(5, res->unit_count);
    Req r;
    TEST_ASSERT_FALSE(comfoair_probe_next(&r));
}

void test_once_anything_answered_the_walk_never_aborts(void)
{
    Walk w;  // units 1..4 silent, 5..0x15 absent, the rest and the error list silent again
    w.run([](const Req &r) {
        bool quiet = r.kind == COMFOAIR_PROBE_REQ_ACTIVE_ERRORS || r.unit < 0x05 || r.unit > 0x15;
        return quiet ? SILENT : COMFOAIR_PROBE_ERR_UNKNOWN_UNIT;
    });
    const comfoair_probe_results_t *res = comfoair_probe_results();
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_DONE, res->state);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_UNIT_ABSENT, row(0x05)->status);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_UNIT_TIMEOUT, row(0x2F)->status);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_ANSWER_TIMEOUT, res->errors_kind);
}

void test_three_silent_properties_in_a_row_end_the_subunit(void)
{
    Walk w;  // subunit 1: 2..3 silent, 4 answers (the run resets), 5.. silent
    begin_max_unit(1);
    w.run([](const Req &r) {
        bool quiet = r.subunit == 1 && (r.property == 2 || r.property == 3 || r.property >= 5);
        return quiet ? SILENT : tree(r, 2);
    });
    TEST_ASSERT_EQUAL(2, w.count([](const Req &r) { return value_at(r, 1, 7); }));
    TEST_ASSERT_EQUAL(0, w.count([](const Req &r) { return value_at(r, 1, 8); }));
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_ANSWER_TIMEOUT, entry(1, 1, 6)->value.kind);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_ANSWER_DATA, entry(1, 1, 4)->value.kind);
    TEST_ASSERT_EQUAL(0x3F, w.count([](const Req &r) { return r.subunit == 2 && r.type == COMFOAIR_PROBE_TYPE_VALUE; }));
    TEST_ASSERT_EQUAL(2, row(1)->subunits);
}

void test_silent_ranges_do_not_end_the_subunit(void)
{
    Walk w;
    begin_max_unit(1);
    w.run([](const Req &r) { return r.type == COMFOAIR_PROBE_TYPE_RANGE ? SILENT : tree(r, 1); });
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_DONE, comfoair_probe_results()->state);
    TEST_ASSERT_EQUAL(0x3F, w.count([](const Req &r) { return r.type == COMFOAIR_PROBE_TYPE_VALUE && r.subunit == 1; }));
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_ANSWER_TIMEOUT, entry(1, 1, 0x3F)->range.kind);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_ANSWER_NONE, entry(1, 1, 0x3F)->step.kind);
    TEST_ASSERT_EQUAL(0, w.count([](const Req &r) { return r.type == COMFOAIR_PROBE_TYPE_STEP; }));
}

void test_a_silent_first_property_of_a_later_subunit_ends_the_subunits(void)
{
    Walk w;
    begin_max_unit(1);
    w.run([](const Req &r) { return r.subunit == 2 && r.property == 1 ? SILENT : tree(r, 3); });
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_ANSWER_TIMEOUT, entry(1, 2, 1)->value.kind);
    TEST_ASSERT_EQUAL(2, w.count([](const Req &r) { return r.subunit == 2; }));
    TEST_ASSERT_EQUAL(0, w.count([](const Req &r) { return r.subunit == 3; }));
    TEST_ASSERT_EQUAL(1, row(1)->subunits);  // "how many answered": the silent one did not
}

// --- codes that end a subunit ------------------------------------------------------------------

void test_13_or_12_mid_subunit_ends_that_subunit_only(void)
{
    Walk w;
    begin_max_unit(1);
    w.run([](const Req &r) {
        if (r.property != 5 || r.type != COMFOAIR_PROBE_TYPE_VALUE)
            return tree(r, 2);
        return r.subunit == 1 ? COMFOAIR_PROBE_ERR_UNKNOWN_SUBUNIT : COMFOAIR_PROBE_ERR_UNKNOWN_UNIT;
    });
    TEST_ASSERT_EQUAL(0, w.count([](const Req &r) { return r.property == 6; }));
    TEST_ASSERT_EQUAL(2, row(1)->subunits);
    TEST_ASSERT_NULL(entry(1, 1, 5));  // a 13 is an answer about the tree, not a property
    // A 12 after the unit said it exists contradicts the discovery read: kept, so it is seen.
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_ERR_UNKNOWN_UNIT, entry(1, 2, 5)->value.error);
    TEST_ASSERT_EQUAL(1, w.count([](const Req &r) { return r.subunit == 3; }));
}

// A unit that answers 14, not 13, for a subunit it does not have would otherwise cost
// 63 requests for each of subunits 3..16.
void test_a_later_subunit_answering_only_14_ends_the_subunits(void)
{
    Walk w;
    begin_max_unit(1);
    w.run([](const Req &r) {
        return r.subunit > 2 && r.type == COMFOAIR_PROBE_TYPE_VALUE ? COMFOAIR_PROBE_ERR_UNKNOWN_PROPERTY : tree(r, 16);
    });
    TEST_ASSERT_EQUAL(2 * (63 + 63) + 63 + 1, w.log.size());
    TEST_ASSERT_EQUAL(0, w.count([](const Req &r) { return r.subunit > 3; }));
    TEST_ASSERT_EQUAL(2, row(1)->subunits);  // subunit 3 evidently is not one
    TEST_ASSERT_EQUAL(63, row(1)->absent_properties);
}

// Subunit 1 is exempt: the unit row already proves the unit exists, and its subunit 1 answering
// only 14 says nothing about subunit 2.
void test_subunit_1_answering_only_14_does_not_end_the_subunits(void)
{
    Walk w;
    begin_max_unit(1);
    w.run([](const Req &r) {
        return r.kind == COMFOAIR_PROBE_REQ_GET ? COMFOAIR_PROBE_ERR_UNKNOWN_PROPERTY : DATA;
    });
    TEST_ASSERT_EQUAL(63 + 63 + 1, w.log.size());
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_UNIT_PRESENT, row(1)->status);
    TEST_ASSERT_EQUAL(1, row(1)->subunits);
    TEST_ASSERT_EQUAL(0, w.count([](const Req &r) { return r.subunit == 3; }));
}

// No DATA at all is the wider rule: a subunit that only says "not gettable" is kept (it answered,
// so it counts), but it is no evidence that subunit 3 exists.
void test_a_later_subunit_with_no_data_ends_the_subunits(void)
{
    Walk w;
    begin_max_unit(1);
    w.run([](const Req &r) {
        return r.subunit == 2 && r.type == COMFOAIR_PROBE_TYPE_VALUE ? COMFOAIR_PROBE_ERR_NOT_GETTABLE : tree(r, 16);
    });
    TEST_ASSERT_EQUAL((63 + 63) + 63 + 1, w.log.size());
    TEST_ASSERT_EQUAL(2, row(1)->subunits);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_ERR_NOT_GETTABLE, entry(1, 2, 0x3F)->value.error);
    TEST_ASSERT_EQUAL(0, w.count([](const Req &r) { return r.subunit == 3; }));
}

void test_eight_11s_in_a_row_end_a_subunit_and_an_answer_resets_the_run(void)
{
    Walk w;  // subunit 1: 1..7 answer 11, 8 answers, 9.. answer 11 again; subunit 2 all 11
    begin_max_unit(1);
    w.run([](const Req &r) {
        bool known = r.subunit == 1 && r.property == 8;
        return r.kind == COMFOAIR_PROBE_REQ_GET && !known ? COMFOAIR_PROBE_ERR_UNKNOWN_COMMAND : tree(r, 16);
    });
    TEST_ASSERT_EQUAL(16, w.count([](const Req &r) { return r.subunit == 1 && r.type == COMFOAIR_PROBE_TYPE_VALUE; }));
    TEST_ASSERT_EQUAL(8, w.count([](const Req &r) { return r.subunit == 2; }));
    TEST_ASSERT_EQUAL(0, w.count([](const Req &r) { return r.subunit == 3; }));
    TEST_ASSERT_EQUAL(16 + 1 + 8 + 1, w.log.size());  // + the range of property 8, + error list
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_ERR_UNKNOWN_COMMAND, entry(1, 1, 16)->value.error);
}

// The risk the bounds close: a unit that does not know GET ONE at all. Unbounded, every present
// unit would cost 16 x 63 requests -- ~47k over the default plan, hours on the bus.
void test_a_unit_that_answers_11_to_everything_costs_16_requests_per_unit(void)
{
    Walk w;
    w.run([](const Req &) { return COMFOAIR_PROBE_ERR_UNKNOWN_COMMAND; });
    const comfoair_probe_results_t *res = comfoair_probe_results();
    printf("default walk against a unit answering 11 to everything: %u requests\n", (unsigned)res->progress.requests);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_DONE, res->state);
    TEST_ASSERT_EQUAL(0x2F * 16 + 1, w.log.size());
    TEST_ASSERT_EQUAL(2, row(0x2F)->subunits);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_ANSWER_ERROR, res->errors_kind);
}

// --- storage ---------------------------------------------------------------------------------

void test_a_full_report_keeps_walking_and_says_truncated(void)
{
    Walk w;  // unit 1: 16 x 63 = 1008 properties, the store holds 400; unit 2: only property 7
    begin_max_unit(2);
    w.run([](const Req &r) {
        if (r.unit != 2)
            return tree(r, 16);
        if (r.type != COMFOAIR_PROBE_TYPE_VALUE)
            return COMFOAIR_PROBE_ERR_NO_RANGE;
        return r.subunit > 1 ? COMFOAIR_PROBE_ERR_UNKNOWN_SUBUNIT
               : r.property == 7 ? DATA
                                 : COMFOAIR_PROBE_ERR_UNKNOWN_PROPERTY;
    });
    const comfoair_probe_results_t *res = comfoair_probe_results();
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_DONE, res->state);
    TEST_ASSERT_TRUE(res->truncated);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_MAX_ENTRIES, res->entry_count);
    TEST_ASSERT_EQUAL(62, row(0x02)->absent_properties);  // counting goes on past the limit
    // The walk's shape does not depend on the store: every value still gets its range asked.
    TEST_ASSERT_EQUAL(1009, w.count([](const Req &r) { return r.type == COMFOAIR_PROBE_TYPE_RANGE; }));
    TEST_ASSERT_EQUAL(7, res->entries[COMFOAIR_PROBE_MAX_ENTRIES - 1].subunit);  // 6 x 63 + 22
    TEST_ASSERT_EQUAL(22, res->entries[COMFOAIR_PROBE_MAX_ENTRIES - 1].property);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_five_silent_answers_first_mean_no_answer);
    RUN_TEST(test_once_anything_answered_the_walk_never_aborts);
    RUN_TEST(test_three_silent_properties_in_a_row_end_the_subunit);
    RUN_TEST(test_silent_ranges_do_not_end_the_subunit);
    RUN_TEST(test_a_silent_first_property_of_a_later_subunit_ends_the_subunits);
    RUN_TEST(test_13_or_12_mid_subunit_ends_that_subunit_only);
    RUN_TEST(test_a_later_subunit_answering_only_14_ends_the_subunits);
    RUN_TEST(test_subunit_1_answering_only_14_does_not_end_the_subunits);
    RUN_TEST(test_a_later_subunit_with_no_data_ends_the_subunits);
    RUN_TEST(test_eight_11s_in_a_row_end_a_subunit_and_an_answer_resets_the_run);
    RUN_TEST(test_a_unit_that_answers_11_to_everything_costs_16_requests_per_unit);
    RUN_TEST(test_a_full_report_keeps_walking_and_says_truncated);
    return UNITY_END();
}
