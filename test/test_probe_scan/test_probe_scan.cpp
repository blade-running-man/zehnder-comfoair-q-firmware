// The RMI walk against a fake ventilation unit.
//
// The fake is a table of what exists: units with a subunit count, and properties with a value,
// a range and a step (or the error each answers). Everything not in the table answers the way the
// real unit does per the RMI error codes (comfoair-pdo-rmi.md §3) -- 12 for a unit it lacks, 13
// past its last subunit, 14 for a property it lacks -- and a node with no units at all is silence. The driver is exactly
// what comfoair_probe_task does: `while (next(&r)) record(answer(r))`, and every request is logged,
// so a test asserts the SHAPE of the walk (what went on the wire), not only the report.
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

#include "comfoair_probe.h"

namespace {

using Bytes = std::vector<uint8_t>;
constexpr int DATA = 0;     // an *_err of 0 means "answers with data"
constexpr int SILENT = -1;  // and -1 means "never answers"

struct Unit {
    uint8_t node, unit, subunits;
};
struct Prop {
    uint8_t node, unit, sub, prop;
    int value_err;
    Bytes value;
    int range_err;
    Bytes range;
    int step_err;
    Bytes step;
};

struct Fake {
    std::vector<Unit> units;
    std::vector<Prop> props;
    std::vector<comfoair_probe_request_t> log;
    // Overrides the table for one request when it returns true: scripted silences and retries.
    std::function<bool(const comfoair_probe_request_t &, int *err)> script;
    Bytes buf;  // the outcome's `data` is borrowed for the record() call only, so it lives here

    comfoair_probe_outcome_t reply(int err, const Bytes &data)
    {
        comfoair_probe_outcome_t o{};
        if (err == SILENT) {
            o.kind = COMFOAIR_PROBE_ANSWER_TIMEOUT;
            return o;
        }
        buf = err == DATA ? data : Bytes{static_cast<uint8_t>(err)};
        o.kind = err == DATA ? COMFOAIR_PROBE_ANSWER_DATA : COMFOAIR_PROBE_ANSWER_ERROR;
        o.error = static_cast<uint8_t>(err == DATA ? 0 : err);
        o.data = buf.data();
        o.len = buf.size();
        return o;
    }

    comfoair_probe_outcome_t answer(const comfoair_probe_request_t &r)
    {
        int err = 0;
        if (script && script(r, &err))
            return reply(err, Bytes{});
        if (r.kind == COMFOAIR_PROBE_REQ_ACTIVE_ERRORS) {
            Bytes bits(32, 0);
            bits[2] = 0x10;
            return reply(DATA, bits);
        }
        bool node_known = false;
        const Unit *u = nullptr;
        for (const Unit &x : units) {
            node_known |= x.node == r.node;
            if (x.node == r.node && x.unit == r.unit)
                u = &x;
        }
        if (!node_known)
            return reply(SILENT, {});
        if (!u)
            return reply(COMFOAIR_PROBE_ERR_UNKNOWN_UNIT, {});
        if (r.subunit > u->subunits)
            return reply(COMFOAIR_PROBE_ERR_UNKNOWN_SUBUNIT, {});
        for (const Prop &p : props) {
            if (p.node != r.node || p.unit != r.unit || p.sub != r.subunit || p.prop != r.property)
                continue;
            if (r.type == COMFOAIR_PROBE_TYPE_RANGE)
                return reply(p.range_err, p.range);
            if (r.type == COMFOAIR_PROBE_TYPE_STEP)
                return reply(p.step_err, p.step);
            return reply(p.value_err, p.value);
        }
        return reply(COMFOAIR_PROBE_ERR_UNKNOWN_PROPERTY, {});
    }

    // Bounded, so a walk that never ends fails the test instead of hanging it.
    void run(size_t limit = 100000)
    {
        comfoair_probe_request_t r;
        while (comfoair_probe_next(&r)) {
            log.push_back(r);
            TEST_ASSERT_TRUE_MESSAGE(log.size() < limit, "the walk does not end");
            comfoair_probe_outcome_t o = answer(r);
            comfoair_probe_record(&o);
        }
    }

    size_t count(std::function<bool(const comfoair_probe_request_t &)> pred) const
    {
        size_t n = 0;
        for (const auto &r : log)
            n += pred(r) ? 1 : 0;
        return n;
    }
};

Prop value(uint8_t unit, uint8_t sub, uint8_t prop, Bytes v, uint8_t node = 0x01)
{
    return Prop{node, unit, sub, prop, DATA, v, COMFOAIR_PROBE_ERR_NO_RANGE, {}, SILENT, {}};
}
Prop ranged(uint8_t unit, uint8_t sub, uint8_t prop, Bytes v, Bytes range, Bytes step)
{
    return Prop{0x01, unit, sub, prop, DATA, v, DATA, range, DATA, step};
}
Prop failing(uint8_t unit, uint8_t sub, uint8_t prop, int err)
{
    return Prop{0x01, unit, sub, prop, err, {}, SILENT, {}, SILENT, {}};
}

constexpr uint8_t U_NODE = 0x01, U_ERROR = 0x03, U_SCHEDULE = 0x15, U_VENT = 0x1E, U_NODECFG = 0x20;

// A ComfoAir Q as its documented property table describes it, trimmed: NODE with holes in
// 1..0x14, VENTILATIONCONFIG with a hole at 0x10 and ranged airflow presets, a 10-subunit
// SCHEDULE, NODECONFIGURATION with a property that is not gettable, an ERROR unit that has no
// subunits. Every other unit is absent.
Fake realistic()
{
    Fake f;
    f.units = {{1, U_NODE, 1}, {1, U_ERROR, 0}, {1, U_SCHEDULE, 10}, {1, U_VENT, 1}, {1, U_NODECFG, 1}};
    for (uint8_t p = 0x01; p <= 0x14; p++)
        if (p != 0x07 && p != 0x09 && p != 0x0E)
            f.props.push_back(value(U_NODE, 1, p, {p, 0x00}));
    f.props[0].value = Bytes{'S', 'I', 'T', '1', '2', '3', 0};
    for (uint8_t p = 0x01; p <= 0x18; p++)
        if (p != 0x10)
            f.props.push_back(ranged(U_VENT, 1, p, {static_cast<uint8_t>(p * 10), 0x00},
                                     {0x4B, 0x00, 0xC2, 0x01}, {0x01, 0x00}));
    for (uint8_t s = 1; s <= 10; s++)
        for (uint8_t p = 1; p <= 3; p++)
            f.props.push_back(value(U_SCHEDULE, s, p, {s, p}));
    f.props.push_back(value(U_NODECFG, 1, 0x01, {0x01}));
    f.props.push_back(failing(U_NODECFG, 1, 0x03, COMFOAIR_PROBE_ERR_NOT_GETTABLE));
    return f;
}

const comfoair_probe_entry_t *entry(uint8_t node, uint8_t unit, uint8_t sub, uint8_t prop)
{
    const comfoair_probe_results_t *res = comfoair_probe_results();
    for (size_t i = 0; i < res->entry_count; i++) {
        const comfoair_probe_entry_t &e = res->entries[i];
        if (e.node == node && e.unit == unit && e.subunit == sub && e.property == prop)
            return &e;
    }
    return nullptr;
}

const comfoair_probe_unit_t *row(uint8_t node, uint8_t unit)
{
    const comfoair_probe_results_t *res = comfoair_probe_results();
    for (size_t i = 0; i < res->unit_count; i++)
        if (res->units[i].node == node && res->units[i].unit == unit)
            return &res->units[i];
    return nullptr;
}

bool is(const comfoair_probe_request_t &r, uint8_t unit, uint8_t sub, uint8_t type, uint8_t prop)
{
    return r.kind == COMFOAIR_PROBE_REQ_GET && r.unit == unit && r.subunit == sub && r.type == type &&
           r.property == prop;
}

}  // namespace

void setUp(void) { comfoair_probe_begin(nullptr); }
void tearDown(void) {}

// --- the realistic walk --------------------------------------------------------------------

void test_a_full_default_walk_maps_the_fake_unit(void)
{
    Fake f = realistic();
    f.run();
    const comfoair_probe_results_t *res = comfoair_probe_results();
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_DONE, res->state);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_FAILURE_NONE, res->failure);
    TEST_ASSERT_EQUAL(0x2F, res->unit_count);
    TEST_ASSERT_FALSE(res->truncated);

    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_UNIT_PRESENT, row(1, U_NODE)->status);
    TEST_ASSERT_EQUAL(1, row(1, U_NODE)->subunits);
    TEST_ASSERT_EQUAL(63 - 17, row(1, U_NODE)->absent_properties);
    TEST_ASSERT_NULL(entry(1, U_NODE, 1, 0x07));
    TEST_ASSERT_EQUAL(7, entry(1, U_NODE, 1, 0x01)->value.len);
    TEST_ASSERT_EQUAL_MEMORY("SIT123", entry(1, U_NODE, 1, 0x01)->value.bytes, 7);

    // The 13 from subunit 11 ends SCHEDULE: one request to 11, none past it.
    TEST_ASSERT_EQUAL(10, row(1, U_SCHEDULE)->subunits);
    TEST_ASSERT_EQUAL(1, f.count([](const comfoair_probe_request_t &r) { return r.unit == U_SCHEDULE && r.subunit == 11; }));
    TEST_ASSERT_EQUAL(0, f.count([](const comfoair_probe_request_t &r) { return r.unit == U_SCHEDULE && r.subunit > 11; }));
    TEST_ASSERT_EQUAL_HEX8(10, entry(1, U_SCHEDULE, 10, 3)->value.bytes[0]);
    TEST_ASSERT_EQUAL(10 * 60, row(1, U_SCHEDULE)->absent_properties);
    TEST_ASSERT_EQUAL(63 - 23, row(1, U_VENT)->absent_properties);
    TEST_ASSERT_NULL(entry(1, U_VENT, 1, 0x10));

    // 13 on the discovery read: the unit exists, it has no subunit 1. One request, no more.
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_UNIT_PRESENT, row(1, U_ERROR)->status);
    TEST_ASSERT_EQUAL(0, row(1, U_ERROR)->subunits);
    TEST_ASSERT_EQUAL(1, f.count([](const comfoair_probe_request_t &r) { return r.unit == U_ERROR; }));

    // An absent unit costs exactly its discovery read.
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_UNIT_ABSENT, row(1, 0x02)->status);
    TEST_ASSERT_EQUAL(0, row(1, 0x02)->subunits);
    TEST_ASSERT_EQUAL(1, f.count([](const comfoair_probe_request_t &r) { return r.unit == 0x02; }));

    const comfoair_probe_entry_t *ng = entry(1, U_NODECFG, 1, 0x03);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_ANSWER_ERROR, ng->value.kind);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_ERR_NOT_GETTABLE, ng->value.error);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_ANSWER_NONE, ng->range.kind);
    TEST_ASSERT_EQUAL(0, f.count([](const comfoair_probe_request_t &r) {
                          return r.unit == U_NODECFG && r.property == 3 && r.type != COMFOAIR_PROBE_TYPE_VALUE;
                      }));

    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_ANSWER_DATA, res->errors_kind);
    TEST_ASSERT_EQUAL(32, res->errors_len);
    TEST_ASSERT_EQUAL_HEX8(0x10, res->errors[2]);

    // The order of magnitude the owner waits for. Pinned exactly: 42 absent units x 1, NODE
    // 63 + 17 ranges + 1 subunit probe, ERROR 1, SCHEDULE 10 x 63 + 30 ranges + 1, VENT
    // 63 + 23 ranges + 23 steps + 1, NODECONFIG 63 + 1 range + 1, active errors 1.
    printf("full default walk against the fake: %u requests\n", (unsigned)res->progress.requests);
    TEST_ASSERT_EQUAL(961, f.log.size());
    TEST_ASSERT_EQUAL(961, res->progress.requests);
    TEST_ASSERT_EQUAL(0, res->progress.timeouts);
}

void test_range_and_step_land_on_their_own_property_or_not_at_all(void)
{
    Fake f = realistic();
    f.run();
    const comfoair_probe_entry_t *e = entry(1, U_VENT, 1, 0x04);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_ANSWER_DATA, e->range.kind);
    TEST_ASSERT_EQUAL(4, e->range.len);
    const uint8_t range[] = {0x4B, 0x00, 0xC2, 0x01};
    TEST_ASSERT_EQUAL_MEMORY(range, e->range.bytes, 4);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_ANSWER_DATA, e->step.kind);
    TEST_ASSERT_EQUAL(2, e->step.len);
    TEST_ASSERT_EQUAL_HEX8(0x01, e->step.bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(40, e->value.bytes[0]);
    // The value, its range and its step go out back to back, before the next property.
    for (size_t i = 0; i + 3 < f.log.size(); i++)
        if (is(f.log[i], U_VENT, 1, COMFOAIR_PROBE_TYPE_VALUE, 4)) {
            TEST_ASSERT_TRUE(is(f.log[i + 1], U_VENT, 1, COMFOAIR_PROBE_TYPE_RANGE, 4));
            TEST_ASSERT_TRUE(is(f.log[i + 2], U_VENT, 1, COMFOAIR_PROBE_TYPE_STEP, 4));
            TEST_ASSERT_TRUE(is(f.log[i + 3], U_VENT, 1, COMFOAIR_PROBE_TYPE_VALUE, 5));
        }
    // "No range" (15) is kept on the range, and no step is asked for it.
    e = entry(1, U_NODE, 1, 0x02);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_ANSWER_ERROR, e->range.kind);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_ERR_NO_RANGE, e->range.error);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_ANSWER_NONE, e->step.kind);
    TEST_ASSERT_EQUAL(0, f.count([](const comfoair_probe_request_t &r) {
                          return r.unit == U_NODE && r.type == COMFOAIR_PROBE_TYPE_STEP;
                      }));
}

void test_the_walk_only_reads_and_asks_for_errors_once(void)
{
    Fake f = realistic();
    f.props.push_back(value(1, 1, 1, {1}, 0x30));
    f.units.push_back({0x30, 1, 1});
    comfoair_probe_plan_t p = COMFOAIR_PROBE_DEFAULT_PLAN();
    p.extra_nodes[0] = 0x30;
    p.extra_node_count = 1;
    comfoair_probe_begin(&p);
    f.run();
    size_t errors_at = 0, errors = 0;
    for (size_t i = 0; i < f.log.size(); i++) {
        const comfoair_probe_request_t &r = f.log[i];
        if (r.kind == COMFOAIR_PROBE_REQ_ACTIVE_ERRORS) {
            errors++;
            errors_at = i;
            TEST_ASSERT_EQUAL_HEX8(0x01, r.node);
            continue;
        }
        TEST_ASSERT_EQUAL(COMFOAIR_PROBE_REQ_GET, r.kind);
        TEST_ASSERT_TRUE(r.type == COMFOAIR_PROBE_TYPE_VALUE || r.type == COMFOAIR_PROBE_TYPE_RANGE ||
                         r.type == COMFOAIR_PROBE_TYPE_STEP);
        TEST_ASSERT_TRUE(r.node == 0x01 || r.node == 0x30);
    }
    TEST_ASSERT_EQUAL(1, errors);
    // After every target read, before every other node's.
    for (size_t i = 0; i < f.log.size(); i++)
        TEST_ASSERT_TRUE(i == errors_at || (i < errors_at) == (f.log[i].node == 0x01));
}

void test_another_node_gets_its_identity_read_and_nothing_else(void)
{
    Fake f;
    f.units = {{0x01, U_NODE, 1}, {0x30, U_NODE, 4}};
    f.props.push_back(value(U_NODE, 1, 1, {1}));
    for (uint8_t p = 1; p <= 0x3F; p++)
        f.props.push_back(Prop{0x30, U_NODE, 1, p, DATA, {p}, DATA, {0, 9}, DATA, {1}});
    comfoair_probe_plan_t p = COMFOAIR_PROBE_DEFAULT_PLAN();
    p.max_unit = 1;
    const uint8_t extra[] = {0x01, 0x2A, 0x30, 0x30, 0x31, 0x00};
    memcpy(p.extra_nodes, extra, sizeof extra);
    p.extra_node_count = sizeof extra;
    comfoair_probe_begin(&p);
    f.run();
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_DONE, comfoair_probe_results()->state);
    TEST_ASSERT_EQUAL(0x14, f.count([](const comfoair_probe_request_t &r) { return r.node == 0x30; }));
    uint8_t want = 1;
    for (const auto &r : f.log)
        if (r.node == 0x30)
            TEST_ASSERT_TRUE(is(r, U_NODE, 1, COMFOAIR_PROBE_TYPE_VALUE, want++));
    TEST_ASSERT_EQUAL(1, row(0x30, 1)->subunits);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_UNIT_PRESENT, row(0x30, 1)->status);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_ANSWER_NONE, entry(0x30, 1, 1, 0x05)->range.kind);
    TEST_ASSERT_NULL(entry(0x30, 1, 1, 0x15));
    // A silent node is a TIMEOUT row after its retry; ourselves (0x2A) and 0 are never asked.
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_UNIT_TIMEOUT, row(0x31, 1)->status);
    TEST_ASSERT_EQUAL(2, f.count([](const comfoair_probe_request_t &r) { return r.node == 0x31; }));
    TEST_ASSERT_EQUAL(0, f.count([](const comfoair_probe_request_t &r) { return r.node == 0x2A || r.node == 0; }));
    // The target is not walked a second time, and a duplicate is not read twice.
    TEST_ASSERT_EQUAL(3, comfoair_probe_results()->unit_count);
}

// --- handing out and taking back ------------------------------------------------------------

static void record_kind(comfoair_probe_answer_kind_t kind)
{
    comfoair_probe_outcome_t o{};
    o.kind = kind;
    comfoair_probe_record(&o);
}

void test_a_timeout_with_a_retry_left_hands_out_the_same_request(void)
{
    comfoair_probe_request_t a, b, c, d;
    const comfoair_probe_results_t *res = comfoair_probe_results();
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_RUNNING, res->state);
    TEST_ASSERT_TRUE(comfoair_probe_next(&a));
    TEST_ASSERT_TRUE(comfoair_probe_next(&b));  // no record() between: not a new handout
    TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);
    TEST_ASSERT_EQUAL(1, res->progress.requests);
    record_kind(COMFOAIR_PROBE_ANSWER_TIMEOUT);
    TEST_ASSERT_TRUE(comfoair_probe_next(&c));
    TEST_ASSERT_EQUAL_MEMORY(&a, &c, sizeof a);
    TEST_ASSERT_EQUAL(2, res->progress.requests);
    TEST_ASSERT_EQUAL(1, res->progress.timeouts);
    record_kind(COMFOAIR_PROBE_ANSWER_TIMEOUT);  // the retry is spent: the answer stands
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_UNIT_TIMEOUT, row(1, 1)->status);
    TEST_ASSERT_TRUE(comfoair_probe_next(&d));
    TEST_ASSERT_TRUE(is(d, 0x02, 1, COMFOAIR_PROBE_TYPE_VALUE, 1));
    TEST_ASSERT_EQUAL(0x02, res->progress.unit);
    TEST_ASSERT_EQUAL(1, res->progress.property);
}

void test_a_stray_or_empty_outcome_does_not_advance_the_walk(void)
{
    comfoair_probe_request_t r;
    record_kind(COMFOAIR_PROBE_ANSWER_DATA);  // nothing outstanding yet
    comfoair_probe_record(nullptr);
    TEST_ASSERT_EQUAL(0, comfoair_probe_results()->unit_count);
    TEST_ASSERT_TRUE(comfoair_probe_next(&r));
    TEST_ASSERT_TRUE(is(r, 1, 1, COMFOAIR_PROBE_TYPE_VALUE, 1));
    record_kind(COMFOAIR_PROBE_ANSWER_NONE);
    TEST_ASSERT_TRUE(comfoair_probe_next(&r));
    TEST_ASSERT_TRUE(is(r, 1, 1, COMFOAIR_PROBE_TYPE_VALUE, 1));
    TEST_ASSERT_EQUAL(1, comfoair_probe_results()->progress.requests);
    record_kind(COMFOAIR_PROBE_ANSWER_ERROR);  // error 0: "some other error", unit present
    record_kind(COMFOAIR_PROBE_ANSWER_ERROR);  // late duplicate: ignored
    TEST_ASSERT_TRUE(comfoair_probe_next(&r));
    TEST_ASSERT_TRUE(is(r, 1, 1, COMFOAIR_PROBE_TYPE_VALUE, 2));
}

static Fake one_unit(uint8_t subunits)
{
    Fake f;
    f.units = {{1, U_NODE, subunits}};
    for (uint8_t s = 1; s <= subunits; s++)
        for (uint8_t p = 1; p <= 0x3F; p++)
            f.props.push_back(value(U_NODE, s, p, {p}));
    return f;
}

static comfoair_probe_plan_t only_unit_1()
{
    comfoair_probe_plan_t p = COMFOAIR_PROBE_DEFAULT_PLAN();
    p.max_unit = 1;
    return p;
}

void test_14_on_the_discovery_read_is_a_present_unit_missing_property_1(void)
{
    Fake f = one_unit(1);
    f.props.erase(f.props.begin());  // no property 1
    comfoair_probe_plan_t p = only_unit_1();
    comfoair_probe_begin(&p);
    f.run();
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_UNIT_PRESENT, row(1, 1)->status);
    TEST_ASSERT_EQUAL(1, row(1, 1)->subunits);
    TEST_ASSERT_EQUAL(1, row(1, 1)->absent_properties);
    TEST_ASSERT_TRUE(is(f.log[1], 1, 1, COMFOAIR_PROBE_TYPE_VALUE, 2));
    TEST_ASSERT_NULL(entry(1, 1, 1, 1));
}

void test_long_answers_keep_a_prefix_and_their_true_length(void)
{
    Fake f;
    f.units = {{1, U_NODE, 1}};
    Bytes long60(60, 0xAB), long300(300, 0xCD), range12(12, 0xEF);
    long60[39] = 0x39;
    f.props = {value(U_NODE, 1, 1, long60), value(U_NODE, 1, 2, long300),
               ranged(U_NODE, 1, 3, {1}, range12, {1, 2, 3, 4, 5, 6, 7, 8, 9}),
               failing(U_NODE, 1, 4, COMFOAIR_PROBE_ERR_NOT_GETTABLE)};
    comfoair_probe_plan_t p = only_unit_1();
    comfoair_probe_begin(&p);
    f.run();
    TEST_ASSERT_EQUAL(60, entry(1, 1, 1, 1)->value.len);
    TEST_ASSERT_EQUAL_HEX8(0x39, entry(1, 1, 1, 1)->value.bytes[COMFOAIR_PROBE_VALUE_MAX - 1]);
    TEST_ASSERT_EQUAL(255, entry(1, 1, 1, 2)->value.len);
    TEST_ASSERT_EQUAL(12, entry(1, 1, 1, 3)->range.len);
    TEST_ASSERT_EQUAL_HEX8(0xEF, entry(1, 1, 1, 3)->range.bytes[COMFOAIR_PROBE_AUX_MAX - 1]);
    TEST_ASSERT_EQUAL(9, entry(1, 1, 1, 3)->step.len);
    TEST_ASSERT_EQUAL(1, entry(1, 1, 1, 4)->value.len);  // an error keeps its raw payload
    TEST_ASSERT_EQUAL_HEX8(COMFOAIR_PROBE_ERR_NOT_GETTABLE, entry(1, 1, 1, 4)->value.bytes[0]);
}

void test_begin_wipes_the_previous_report_and_null_is_the_default_plan(void)
{
    Fake f = realistic();
    f.run();
    comfoair_probe_begin(nullptr);
    const comfoair_probe_results_t *res = comfoair_probe_results();
    comfoair_probe_plan_t d = COMFOAIR_PROBE_DEFAULT_PLAN();
    TEST_ASSERT_EQUAL_MEMORY(&d, &res->plan, sizeof d);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_RUNNING, res->state);
    TEST_ASSERT_EQUAL(0, res->unit_count);
    TEST_ASSERT_EQUAL(0, res->entry_count);
    TEST_ASSERT_EQUAL(0, res->progress.requests);
    TEST_ASSERT_EQUAL(COMFOAIR_PROBE_ANSWER_NONE, res->errors_kind);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_full_default_walk_maps_the_fake_unit);
    RUN_TEST(test_range_and_step_land_on_their_own_property_or_not_at_all);
    RUN_TEST(test_the_walk_only_reads_and_asks_for_errors_once);
    RUN_TEST(test_another_node_gets_its_identity_read_and_nothing_else);
    RUN_TEST(test_a_timeout_with_a_retry_left_hands_out_the_same_request);
    RUN_TEST(test_a_stray_or_empty_outcome_does_not_advance_the_walk);
    RUN_TEST(test_14_on_the_discovery_read_is_a_present_unit_missing_property_1);
    RUN_TEST(test_long_answers_keep_a_prefix_and_their_true_length);
    RUN_TEST(test_begin_wipes_the_previous_report_and_null_is_the_default_plan);
    return UNITY_END();
}
