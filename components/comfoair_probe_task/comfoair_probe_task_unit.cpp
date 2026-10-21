// The unit reads: who the ventilation unit is (NODE 0x01/01 0x04 0x06 0x08 0x0B 0x0D 0x14) and
// which errors it has active, for GET /api/system -- the full read -- and, every minute between two
// of those, the active errors alone, which also drive the unit_error / active_errors entities. See
// comfoair_probe_task.cpp for the component's contract notes.
//
// ON THE PROBE TASK, BETWEEN WALKS, and through probe_bus_ask() only. That is the whole safety
// argument: the requests are built by rmi_read.h's get_one()/get_active_errors() and nothing else,
// so this file cannot put a write or a >= 0x80 command other than `80 03 01` on the bus
// (comfoair-pdo-rmi.md §0 is why that matters), and because it shares the walk's task it never
// shares the reply queue with a walk in flight.
//
// The settings read-back (comfoair_probe_task_settings.cpp) rides on the same cycle: the schedule
// below (s_cadence, one schedule with two deadlines) is the one place that decides when the unit is
// asked again, so a second timer would be a second answer to that question.
//
// WRITERS: only the probe task writes s_unit, s_cadence and s_ok_tick -- the two reads below and
// probe_unit_from_walk_locked(), which the walk calls (on this task too). So the task READS s_unit
// without the lock and WRITES it under the lock; the HTTP task only ever copies it under the lock.
#include <cstring>

#include "comfoair_control.h"
#include "comfoair_probe_task_internal.h"
#include "comfoair_state.h"
#include "comfoair_unit_cadence.h"
#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "probe";

// The identity properties, in the order they are asked. The model is the one success depends on.
static const uint8_t PROPS[] = {0x04, 0x06, 0x08, 0x0B, 0x0D, 0x14};
#define PROP_COUNT (sizeof PROPS / sizeof PROPS[0])
#define PROP_FIRMWARE 0x06
#define PROP_MODEL 0x08
#define UNIT_NODE_UNIT 0x01     // RMI unit NODE
#define UNIT_NODE_SUBUNIT 0x01

static comfoair_sysinfo_unit_t s_unit;  // under the lock for writing and for other tasks
static TickType_t s_ok_tick;            // the last success; meaningful once identity_ever_read

// The schedule: comfoair_unit_cadence decides, in ticks, when the full read (10 s after boot, then
// 6 h, 60 s after an incomplete one) and the errors-only read (every 60 s) are due; the
// COMFOAIR_UNIT_* seconds carry the reasons. Its wrap-safe arithmetic is on uint32_t, which is
// exactly what makes it right for TickType_t -- hence the assert.
static comfoair_unit_cadence_t s_cadence;
static_assert(sizeof(TickType_t) == sizeof(uint32_t), "the cadence's tick arithmetic is 32-bit");
static_assert(COMFOAIR_SYSINFO_ERRORS_SUMMARY_MAX <= COMFOAIR_STATE_TEXT_MAX,
              "the error line must fit the state model's text");

void probe_unit_arm(void)
{
    comfoair_unit_cadence_arm(&s_cadence, xTaskGetTickCount(), configTICK_RATE_HZ);
}

TickType_t probe_unit_wait_ticks(void)
{
    uint32_t wait = 0;
    return comfoair_unit_cadence_due(&s_cadence, xTaskGetTickCount(), configTICK_RATE_HZ, &wait) ==
                   COMFOAIR_UNIT_READ_NONE
               ? wait
               : 0;
}

// The answer's bytes up to the first NUL, clamped so the NUL always fits. The unit pads its
// strings with NULs and the probe store keeps at most COMFOAIR_PROBE_VALUE_MAX of them, so a
// string is never trusted to be terminated.
static void take_text(char (&dst)[COMFOAIR_SYSINFO_TEXT_MAX], const uint8_t *data, size_t len)
{
    size_t n = 0;
    while (n < len && n < COMFOAIR_SYSINFO_TEXT_MAX - 1 && data[n] != 0)
        n++;
    memcpy(dst, data, n);
    dst[n] = '\0';
}

// One answered property into `unit`. Only DATA counts: an error or a silence keeps whatever the
// last read left there (comfoair_sysinfo.h, NO_ANSWER: "older values, if any, kept").
static void take_prop(comfoair_sysinfo_unit_t *unit, uint8_t prop, const uint8_t *data, size_t len)
{
    switch (prop) {
    case 0x04: take_text(unit->serial, data, len); break;
    case PROP_MODEL: take_text(unit->model, data, len); break;
    case 0x0B: take_text(unit->article, data, len); break;
    case 0x0D: take_text(unit->country, data, len); break;
    case 0x14: take_text(unit->name, data, len); break;
    case PROP_FIRMWARE:
        // UINT32 little-endian, the value comfoair_probe_version_text() decodes. A shorter
        // answer is not a version and is ignored rather than zero-padded into one.
        if (len >= 4) {
            unit->firmware_raw = (uint32_t)data[0] | (uint32_t)data[1] << 8 |
                                 (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
            unit->has_firmware = true;
        }
        break;
    default: break;
    }
}

static void take_errors(comfoair_sysinfo_unit_t *unit, comfoair_probe_answer_kind_t kind,
                        const uint8_t *data, size_t len)
{
    if (kind != COMFOAIR_PROBE_ANSWER_DATA) {
        unit->errors_state = COMFOAIR_SYSINFO_READ_NO_ANSWER;
        return;
    }
    // A short bitfield is zero-filled: a bit the unit did not send is an error it did not report.
    memset(unit->errors, 0, sizeof unit->errors);
    memcpy(unit->errors, data, len < sizeof unit->errors ? len : sizeof unit->errors);
    unit->errors_state = COMFOAIR_SYSINFO_READ_OK;
}

// The two error entities, from an ANSWERED read only: an unanswered one keeps what they showed (a
// single RMI timeout on a live bus must not flap a problem sensor; a dead bus greys them out
// through <prefix>/bus). OUTSIDE the probe lock -- the state model takes comfoair_lock(), and the
// two locks are never nested (the settings read keeps them apart the same way). Which ids raise
// unit_error is comfoair_sysinfo's call, by the unit's firmware numbering: informational ones such
// as a manual bypass are listed in the text but raise no problem. NOTHING IS PUBLISHED UNTIL THE
// FIRMWARE IS KNOWN: without it no id counts as informational, so a firmware read that timed out
// beside an answered error read would raise unit_error for a bypass the user set by hand, and hold
// it until the next full read (up to 6 h). The entities stay unknown instead.
static void publish_errors(const comfoair_sysinfo_unit_t &unit)
{
    if (unit.errors_state != COMFOAIR_SYSINFO_READ_OK || !unit.has_firmware)
        return;
    static char line[COMFOAIR_SYSINFO_ERRORS_SUMMARY_MAX + 1];  // probe task only
    if (comfoair_sysinfo_errors_summary(unit.errors, unit.has_firmware, unit.firmware_raw, line,
                                        sizeof line) == 0)
        return;
    const bool any = comfoair_sysinfo_errors_any(unit.errors, unit.has_firmware, unit.firmware_raw);
    if (!comfoair_state_set_unit_errors(any, line))
        return;
    // INFO on a change only: the read runs every minute and the log ring is GET /api/log.
    ESP_LOGI(TAG, "unit errors%s: %s", any ? " (problem)" : "", line);
    probe_notify_state_changed();
}

// UNDER THE LOCK, on the probe task: s_ok_tick is what the HTTP task's copy reads the age from.
// The identity only; the schedule is the caller's, because it also depends on the settings.
static void mark_identity_ok(comfoair_sysinfo_unit_t *unit)
{
    unit->identity_state     = COMFOAIR_SYSINFO_READ_OK;
    unit->identity_ever_read = true;
    s_ok_tick                = xTaskGetTickCount();
}

static void probe_unit_read(void)
{
    // Static, not on the stack: the task's 4096 bytes are sized for the walk (TASK_STACK in
    // comfoair_probe_task.cpp), and the read and the walk never run at once, so one copy is enough.
    static comfoair_sysinfo_unit_t next;
    next = s_unit;  // the task is the only writer, so this unlocked read is consistent

    // ON for the read. probe_bus_listen() is one flag shared with the walk; see the end.
    probe_lock();
    probe_bus_listen(true);
    probe_unlock();

    comfoair::rmi::ResponseReassembler reasm;
    comfoair_probe_outcome_t           out;
    bool                               model_ok = false;
    unsigned                           silent   = 0;
    for (size_t i = 0; i < PROP_COUNT; i++) {
        const comfoair_probe_request_t req = {COMFOAIR_PROBE_REQ_GET, COMFOAIR_CONTROL_UNIT_NODE,
                                              UNIT_NODE_UNIT, UNIT_NODE_SUBUNIT,
                                              COMFOAIR_PROBE_TYPE_VALUE, PROPS[i]};
        // another_counter 0: the CAN-id bits the command sender uses for the same unit.
        probe_bus_ask(&req, 0, reasm, &out);
        if (out.kind == COMFOAIR_PROBE_ANSWER_TIMEOUT)
            silent++;
        if (out.kind == COMFOAIR_PROBE_ANSWER_DATA) {
            // Copied now: out.data borrows `reasm`, which the next ask overwrites.
            take_prop(&next, PROPS[i], out.data, out.len);
            if (PROPS[i] == PROP_MODEL)
                model_ok = true;
        }
        vTaskDelay(pdMS_TO_TICKS(PROBE_GAP_MS));
    }
    const comfoair_probe_request_t errors = {COMFOAIR_PROBE_REQ_ACTIVE_ERRORS,
                                             COMFOAIR_CONTROL_UNIT_NODE, 0, 0, 0, 0};
    probe_bus_ask(&errors, 0, reasm, &out);
    take_errors(&next, out.kind, out.data, out.len);

    // Still listening, still outside the probe lock: it takes comfoair_lock() per apply, and never
    // holds the probe lock across its round trips any more than the reads above do.
    const bool settings_done = probe_settings_read(reasm);

    probe_lock();
    if (model_ok) {
        mark_identity_ok(&next);
    } else {
        // NO_ANSWER for any read without a model, not only for six silences: the model is what the
        // page leads with, and a stale "ok" over a unit that just refused it would be the lie.
        next.identity_state = COMFOAIR_SYSINFO_READ_NO_ANSWER;
    }
    // 6 h only when BOTH are in hand: identity_state keeps meaning "the model answered" for
    // GET /api/system, while the schedule needs the settings too. A settings read the sender's
    // queue postponed would otherwise leave the three selects unknown (or stale) for six hours
    // after a boot that happened to land on the owner's first command. The firmware word too, once:
    // publish_errors() waits for it, so a single timeout of it at boot would otherwise leave both
    // error entities unknown for six hours. It is sticky (next starts from s_unit), so a later
    // timeout of it does not bring the 60 s retry back.
    const bool complete = model_ok && settings_done && next.has_firmware;
    comfoair_unit_cadence_full_done(&s_cadence, xTaskGetTickCount(), configTICK_RATE_HZ, complete);
    s_unit = next;
    // OFF -- unless a start() landed during the read. start() turned listening on for its walk
    // under this same lock and the walk runs right after this returns; switching it off here would
    // turn that whole walk into timeouts and a FAILED/no_answer report. DO NOT drop the check.
    if (comfoair_probe_results()->state != COMFOAIR_PROBE_RUNNING)
        probe_bus_listen(false);
    probe_unlock();

    // A unit that stays silent -- or a settings row that keeps timing out beside a model that
    // answers -- would otherwise put a line a minute into the ring GET /api/log serves; the first
    // incomplete read says so, the repeats go to debug. Keyed on `complete`, the same thing the
    // schedule is, so every 60 s retry is demoted whichever half owes it.
    static bool incomplete_before;
    const esp_log_level_t level = !complete && incomplete_before ? ESP_LOG_DEBUG : ESP_LOG_INFO;
    incomplete_before           = !complete;
    ESP_LOG_LEVEL_LOCAL(level, TAG,
             "unit identity %s (%u of %u silent), errors %s, settings %s; next in %lu s",
             model_ok ? "read" : "not read", silent, (unsigned)PROP_COUNT,
             next.errors_state == COMFOAIR_SYSINFO_READ_OK ? "read" : "not read",
             settings_done ? "read" : "owed",
             (unsigned long)(complete ? COMFOAIR_UNIT_OK_S : COMFOAIR_UNIT_RETRY_S));
    publish_errors(next);
}

// `80 03 01` alone, between two full reads (comfoair_unit_cadence.h says why every minute). The
// answer goes straight into s_unit under the lock: only the errors change, so the full read's
// static copy of the whole snapshot is not needed. Serialised with the walk and the full read by
// running on the same task; listening and the sequence counter are handled exactly as there.
static void probe_errors_read(void)
{
    probe_lock();
    probe_bus_listen(true);
    probe_unlock();

    comfoair::rmi::ResponseReassembler reasm;
    comfoair_probe_outcome_t           out;
    const comfoair_probe_request_t     errors = {COMFOAIR_PROBE_REQ_ACTIVE_ERRORS,
                                                 COMFOAIR_CONTROL_UNIT_NODE, 0, 0, 0, 0};
    probe_bus_ask(&errors, 0, reasm, &out);

    probe_lock();
    // An unanswered read keeps the bitfield (take_errors() only flags it) -- and with it the
    // entities, which publish_errors() leaves alone for anything but READ_OK.
    take_errors(&s_unit, out.kind, out.data, out.len);  // out.data borrows reasm, still alive
    comfoair_unit_cadence_errors_done(&s_cadence, xTaskGetTickCount(), configTICK_RATE_HZ);
    // OFF unless a walk began meanwhile -- the same rule, and the same reason, as probe_unit_read().
    if (comfoair_probe_results()->state != COMFOAIR_PROBE_RUNNING)
        probe_bus_listen(false);
    probe_unlock();

    // Once per silence, like the full read: a unit that stops answering would otherwise put a line a
    // minute into the ring GET /api/log serves. The next minute is the retry.
    static bool silent_before;
    const bool  ok = s_unit.errors_state == COMFOAIR_SYSINFO_READ_OK;
    if (!ok) {
        ESP_LOG_LEVEL_LOCAL(silent_before ? ESP_LOG_DEBUG : ESP_LOG_WARN, TAG,
                            "unit errors not read (%s); keeping the last values",
                            out.kind == COMFOAIR_PROBE_ANSWER_TIMEOUT ? "no answer" : "refused");
    } else if (silent_before) {
        ESP_LOGI(TAG, "unit errors read again");
    }
    silent_before = !ok;

    publish_errors(s_unit);  // the task is the only writer, so this unlocked read is consistent
    // The walk's pause, so a walk start()ed meanwhile does not follow this request back to back.
    vTaskDelay(pdMS_TO_TICKS(PROBE_GAP_MS));
}

void probe_unit_run_due(void)
{
    switch (comfoair_unit_cadence_due(&s_cadence, xTaskGetTickCount(), configTICK_RATE_HZ, NULL)) {
    case COMFOAIR_UNIT_READ_FULL: probe_unit_read(); break;
    case COMFOAIR_UNIT_READ_ERRORS: probe_errors_read(); break;
    case COMFOAIR_UNIT_READ_NONE: break;
    }
}

void probe_unit_from_walk_locked(void)
{
    const comfoair_probe_results_t *r = comfoair_probe_results();
    // Only a walk OF THE UNIT describes the unit. A walk of another node can still carry node 0x01
    // entries (its extra-node identity reads), but its error bitfield is that other node's.
    if (r->plan.target_node != COMFOAIR_CONTROL_UNIT_NODE)
        return;

    bool model_ok = false;
    for (size_t i = 0; i < r->entry_count; i++) {
        const comfoair_probe_entry_t *e = &r->entries[i];
        if (e->node != COMFOAIR_CONTROL_UNIT_NODE || e->unit != UNIT_NODE_UNIT ||
            e->subunit != UNIT_NODE_SUBUNIT || e->value.kind != COMFOAIR_PROBE_ANSWER_DATA)
            continue;
        // value.len is as received and may exceed what the store kept (comfoair_probe.h).
        const size_t len =
            e->value.len < sizeof e->value.bytes ? e->value.len : sizeof e->value.bytes;
        take_prop(&s_unit, e->property, e->value.bytes, len);
        if (e->property == PROP_MODEL)
            model_ok = true;
    }
    if (r->errors_kind != COMFOAIR_PROBE_ANSWER_NONE) {
        const size_t len = r->errors_len < sizeof r->errors ? r->errors_len : sizeof r->errors;
        take_errors(&s_unit, r->errors_kind, r->errors, len);
    }
    // The identity only; THE DUE TIME IS LEFT ALONE. The timed cycle also carries the settings
    // read (the walk does not apply settings: its reads have no command-path guard), and its
    // "every 6 h" promise is one a walk must not stretch -- restarting the clock here pushed the
    // settings read to ~11 h. A redundant identity read costs seven frames. DO NOT call
    // comfoair_unit_cadence_full_done() here: a retry would postpone a read that is already due.
    // And the two error entities are not written here: this runs under the probe lock, and the
    // state model's lock is never taken inside it; the next minute's errors read catches up.
    if (model_ok)
        mark_identity_ok(&s_unit);
}

void probe_unit_copy_locked(comfoair_sysinfo_unit_t *out)
{
    *out = s_unit;
    if (!s_unit.identity_ever_read) {
        out->identity_age_ms = 0;
        return;
    }
    // 64-bit, clamped: pdTICKS_TO_MS truncates to TickType_t (projdefs.h:55), and an age is
    // allowed to grow for as long as the unit stays silent after its one good read.
    const uint64_t ms = (uint64_t)(TickType_t)(xTaskGetTickCount() - s_ok_tick) * 1000u /
                        configTICK_RATE_HZ;
    out->identity_age_ms = ms > UINT32_MAX ? UINT32_MAX : (uint32_t)ms;
}
