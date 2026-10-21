// The read-back of the selects no PDO reports (COMFOAIR_RMI_READBACK: temperature_passive,
// humidity_comfort, humidity_protection -- TEMPHUMCONTROL 0x1D/01 properties 0x04/0x06/0x07), as
// part of the unit read in comfoair_probe_task_unit.cpp. See comfoair_probe_task.cpp for the
// component's contract notes.
//
// WHY READ AT ALL. The unit reports these three through no PDO, so without a read the state model
// holds only what THIS firmware last wrote -- and nothing at all after a boot. Persisting the last
// write in NVS was rejected: NVS knows only what this firmware sent, and is wrong after a change on
// the unit's own display, from ComfoConnect, after a unit reset, or once the ESP moves to another
// unit. The unit is the one holder of the truth, so the unit is asked.
//
// ON THE PROBE TASK, through probe_bus_ask() only, so the request is rmi_read.h's get_one() and
// can be nothing else: this file cannot put a write on the bus (comfoair-pdo-rmi.md §0).
//
// THE RACE IT GUARDS. HTTP and MQTT queue a write (comfoair_control_submit) BEFORE their optimistic
// comfoair_state_set_option(), and the sender may sleep between frames with up to eight commands
// queued. A GET put on the wire ahead of a queued SET returns the OLD value; applied blindly it
// would overwrite the owner's fresh choice for the next 6 h. The guard is on the command path:
// read only while the sender is idle, apply only if nothing was queued since -- decided under
// comfoair_lock(), which set_option takes too, so a write submitted after the check waits for this
// apply and lands on top of it. comfoair_readback.h says why a value compare-and-set is no guard.
#include <cstring>

#include "comfoair_control.h"
#include "comfoair_lock.h"
#include "comfoair_probe_task.h"
#include "comfoair_probe_task_internal.h"
#include "comfoair_readback.h"
#include "comfoair_state.h"
#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "probe";

// Set once, before comfoair_probe_task_init() creates the task that reads it (the header's
// contract), so task creation orders the write before every read and no atomic is needed.
static void (*s_observer)(void);

void comfoair_probe_task_set_observer(void (*on_state_changed)(void))
{
    s_observer = on_state_changed;
}

void probe_notify_state_changed(void)
{
    if (s_observer != NULL)
        s_observer();
}

// UNDER comfoair_lock(), taken here: the guard's second sample and the apply in ONE hold. Split
// them and a submit landing in the gap is overwritten by the old value this read brought back.
// Returns false when the guard refused; *changed says whether the state moved.
static bool apply_guarded(const comfoair_rmi_readback_t *row, uint8_t index,
                          const comfoair_control_stats_t *before, bool *changed)
{
    const comfoair_entity_t *e     = &COMFOAIR_ENTITIES[row->entity];
    const char              *label = e->options[index];

    comfoair_lock();
    comfoair_control_stats_t now;
    comfoair_control_get_stats(&now);
    const bool may =
        comfoair_readback_may_apply(before->queued, before->sent + before->failed, now.queued);
    if (may) {
        comfoair_value_t old;
        const bool had = comfoair_state_get(e->key, &old) && old.type == COMFOAIR_VALUE_TEXT &&
                         old.text != NULL;
        // By text, not by pointer: registry_generated.h's option arrays are `static const`, so
        // each translation unit holds its own copy, and the pointer comfoair_state stored is its
        // copy's, never this file's.
        if (comfoair_state_set_option(e->key, label))
            *changed = !had || strcmp(old.text, label) != 0;
    }
    comfoair_unlock();
    return may;
}

bool probe_settings_read(comfoair::rmi::ResponseReassembler &reasm)
{
    comfoair_control_stats_t before;
    comfoair_control_get_stats(&before);
    // Not even asked while a command is queued or on the wire: its SET may still be behind our GET.
    // Not done, so the unit read retries in 60 s rather than in 6 h.
    if (before.queued != before.sent + before.failed) {
        ESP_LOGD(TAG, "settings not read: %lu command(s) pending",
                 (unsigned long)(before.queued - before.sent - before.failed));
        return false;
    }

    // One WARN per row per boot for an error answer: it is final, and a unit that refuses the
    // property refuses it on every 6 h read -- the repeats would only fill GET /api/log.
    static bool error_logged[COMFOAIR_RMI_READBACK_COUNT];

    bool     all_final = true;
    bool     changed   = false;
    unsigned applied   = 0;
    for (size_t i = 0; i < COMFOAIR_RMI_READBACK_COUNT; i++) {
        const comfoair_rmi_readback_t *row = &COMFOAIR_RMI_READBACK[i];
        const char                    *key = COMFOAIR_ENTITIES[row->entity].key;
        const comfoair_probe_request_t req = {COMFOAIR_PROBE_REQ_GET, COMFOAIR_CONTROL_UNIT_NODE,
                                              row->unit, row->subunit,
                                              COMFOAIR_PROBE_TYPE_VALUE, row->property};
        vTaskDelay(pdMS_TO_TICKS(PROBE_GAP_MS));
        comfoair_probe_outcome_t out;
        // another_counter 0: the CAN-id bits the command sender uses for the same unit.
        probe_bus_ask(&req, 0, reasm, &out);

        if (out.kind == COMFOAIR_PROBE_ANSWER_TIMEOUT) {
            all_final = false;
            continue;
        }
        if (out.kind == COMFOAIR_PROBE_ANSWER_ERROR) {
            ESP_LOG_LEVEL_LOCAL(error_logged[i] ? ESP_LOG_DEBUG : ESP_LOG_WARN, TAG,
                                "setting %s: the unit answered error 0x%02x", key,
                                (unsigned)out.error);
            error_logged[i] = true;
            continue;
        }
        uint8_t index = 0;
        if (!comfoair_readback_decode(row, out.data, out.len, &index)) {
            // Final, not retried: the same property gives the same shape next minute too. The
            // state keeps what it had -- a guess at what the bytes meant would be shown as fact.
            ESP_LOGW(TAG, "setting %s: unexpected answer (%u bytes, first 0x%02x); not applied",
                     key, (unsigned)out.len, out.len > 0 ? (unsigned)out.data[0] : 0u);
            continue;
        }
        bool row_changed = false;
        if (!apply_guarded(row, index, &before, &row_changed)) {
            // A command was submitted during the read. Every later answer is suspect for the same
            // reason, so stop; the retry in 60 s finds the sender idle again.
            ESP_LOGD(TAG, "setting %s read but not applied: a command was submitted meanwhile",
                     key);
            all_final = false;
            break;
        }
        applied++;
        // INFO for a change only: an unchanged confirmation every 6 h (or every minute while
        // another row keeps timing out) is noise in the ring GET /api/log serves.
        ESP_LOG_LEVEL_LOCAL(row_changed ? ESP_LOG_INFO : ESP_LOG_DEBUG, TAG, "setting %s=%s", key,
                            COMFOAIR_ENTITIES[row->entity].options[index]);
        changed = changed || row_changed;
    }

    // Once per read, not per row: the observer wakes the WebSocket and MQTT publishers, which read
    // the whole model anyway.
    if (changed)
        probe_notify_state_changed();
    ESP_LOGD(TAG, "settings read: %u of %u applied%s", applied,
             (unsigned)COMFOAIR_RMI_READBACK_COUNT, all_final ? "" : ", retry owed");
    return all_final;
}
