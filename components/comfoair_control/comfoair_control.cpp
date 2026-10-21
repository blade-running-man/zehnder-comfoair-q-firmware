// See comfoair_control.h for why a command is queued rather than sent from the handler.
#include "comfoair_control.h"

#include <string.h>

#include "comfoair_can.h"
#include "comfoair_poll.h"
#include "rmi_frame.h"
#include "comfoair_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "ctrl";

// Eight commands. A person pressing buttons produces one every second or so; eight is a burst
// nobody makes by hand, and a queue longer than the bus can drain would apply commands minutes
// after they were asked for -- which on a ventilation unit is worse than refusing them.
#define QUEUE_LEN 8

// An 8-byte extended-id frame is about 2.5 ms at 50 kbit/s; this is the driver's patience,
// It bounds how long the sender task waits on a unit that is not acknowledging.
#define TX_TIMEOUT_MS 200

// How long after a command its read-back request goes out (comfoair_command_readback_pdo()).
// Long enough for the unit to have applied the timer change before it answers, short enough that
// the switch settles while the person is still looking. Not measured on the unit: HV-MQTT-31
// records the actual delay between the command and the answering frame.
#define READBACK_DELAY_MS 1500

static QueueHandle_t     s_queue;
static TaskHandle_t      s_task;
static SemaphoreHandle_t s_lock;

static comfoair_command_durations_t s_durations;
static comfoair_control_stats_t     s_stats;

// Two bits on the wire (rmi_frame.h, command_can_id). Fresh per RMI FRAME -- the chunks of ONE
// frame share it, two frames of one command do not (see send_frame() for why). Shared with the
// probe; see comfoair_control_next_sequence() in the header.
//
// A SPINLOCK, NOT s_lock. The probe can ask for a number whether or not this sender started, and
// s_lock only exists once comfoair_control_start() has run; a portMUX is initialised statically.
// DO NOT move this under s_lock: a probe on a device whose sender failed to start would take a
// NULL mutex.
static uint8_t     s_sequence;
static portMUX_TYPE s_sequence_mux = portMUX_INITIALIZER_UNLOCKED;

static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

// --- storage ----------------------------------------------------------------------------------

// Every read is REPORTED, never checked into a panic. A preference that will not load is a
// preference, and a device that refuses to boot over one has no way back over the air, because
// the bad value survives the reboot.
static void load_u16(nvs_handle_t handle, const char *key, uint16_t *out)
{
    uint16_t value = 0;
    if (nvs_get_u16(handle, key, &value) == ESP_OK)
        *out = value;
}

static void load_durations(void)
{
    // The table's defaults first, so every path below leaves a usable set.
    comfoair_command_durations_defaults(&s_durations);

    nvs_handle_t handle;
    // READONLY: opening NVS_READWRITE creates the namespace, and this runs at the first boot of
    // every device.
    if (nvs_open(COMFOAIR_CONFIG_NS_APP_NAME, NVS_READONLY, &handle) != ESP_OK)
        return;

    load_u16(handle, COMFOAIR_CONFIG_KEY_BOOST_MINUTES, &s_durations.boost_minutes);
    load_u16(handle, COMFOAIR_CONFIG_KEY_BYPASS_MINUTES, &s_durations.bypass_minutes);
    load_u16(handle, COMFOAIR_CONFIG_KEY_BALANCE_MINUTES, &s_durations.balance_minutes);
    load_u16(handle, COMFOAIR_CONFIG_KEY_AWAY_MINUTES, &s_durations.away_minutes);

    // u8 and normalised, never a bool straight out of flash: a _Bool holding a bit pattern that is
    // neither 0 nor 1 is not a value a C program may read at all, and flash can hand back 0xff.
    // Same rule and same reason as comfoair_config_nvs.c's load_bool().
    uint8_t manual = 0;
    if (nvs_get_u8(handle, COMFOAIR_CONFIG_KEY_PERMANENT_MANUAL, &manual) == ESP_OK)
        s_durations.permanent_manual = manual != 0;

    nvs_close(handle);
}

static esp_err_t store_u16(const char *key, uint16_t value)
{
    nvs_handle_t handle;
    esp_err_t    err = nvs_open(COMFOAIR_CONFIG_NS_APP_NAME, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;
    err = nvs_set_u16(handle, key, value);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    // The KEY name, never a value -- although a duration is not a secret, the rule is one rule.
    if (err != ESP_OK)
        ESP_LOGW(TAG, "could not store %s: %s", key, esp_err_to_name(err));
    return err;
}

void comfoair_control_durations(comfoair_command_durations_t *out)
{
    if (out == NULL)
        return;
    if (s_lock == NULL) {
        // Asked before start(). The table's defaults are the right answer: they are what the
        // registry advertises, so a command built from them is one the API described.
        comfoair_command_durations_defaults(out);
        return;
    }
    lock();
    *out = s_durations;
    unlock();
}

esp_err_t comfoair_control_store_duration(const char *key, uint16_t minutes)
{
    if (key == NULL)
        return ESP_ERR_INVALID_ARG;
    if (s_lock == NULL)
        return ESP_ERR_INVALID_STATE;

    // The RANGE was checked by comfoair_command against the registry before this was reached.
    // Checking it again here with a second set of numbers is how the two come to disagree.
    const char *nvs_key = NULL;
    lock();
    if (strcmp(key, "boost_duration") == 0) {
        s_durations.boost_minutes = minutes;
        nvs_key                   = COMFOAIR_CONFIG_KEY_BOOST_MINUTES;
    } else if (strcmp(key, "bypass_duration") == 0) {
        s_durations.bypass_minutes = minutes;
        nvs_key                    = COMFOAIR_CONFIG_KEY_BYPASS_MINUTES;
    } else if (strcmp(key, "balance_duration") == 0) {
        s_durations.balance_minutes = minutes;
        nvs_key                     = COMFOAIR_CONFIG_KEY_BALANCE_MINUTES;
    } else if (strcmp(key, "away_duration") == 0) {
        s_durations.away_minutes = minutes;
        nvs_key                  = COMFOAIR_CONFIG_KEY_AWAY_MINUTES;
    }
    unlock();

    if (nvs_key == NULL)
        return ESP_ERR_NOT_FOUND;
    // Persisting can fail on a full or damaged NVS, and the value is ALREADY in effect. That is
    // the honest outcome to report: the change happened and will not survive a reboot. Undoing it
    // to match the flash would be a device that ignores what the owner just did.
    return store_u16(nvs_key, minutes);
}

esp_err_t comfoair_control_set_permanent_manual(bool permanent)
{
    if (s_lock == NULL)
        return ESP_ERR_INVALID_STATE;
    lock();
    s_durations.permanent_manual = permanent;
    unlock();

    nvs_handle_t handle;
    esp_err_t    err = nvs_open(COMFOAIR_CONFIG_NS_APP_NAME, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;
    err = nvs_set_u8(handle, COMFOAIR_CONFIG_KEY_PERMANENT_MANUAL, permanent ? 1 : 0);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK)
        ESP_LOGW(TAG, "could not store %s: %s", COMFOAIR_CONFIG_KEY_PERMANENT_MANUAL,
                 esp_err_to_name(err));
    return err;
}

// --- the wire ------------------------------------------------------------------------------

// One RMI frame onto the bus, chunked if it does not fit a CAN frame.
//
// THE ID AND THE CHUNKING ARE rmi_frame.h's, CALLED not copied. An earlier version of this
// function assembled the id by hand and rewrote the chunk loop, with a comment above it claiming
// it did not -- the bytes happened to be identical, so nothing was broken and the guarantee was:
// the DO NOTs and the 48 tests that carry the reasoning for each constant live in that header, and
// a correction there would silently not have reached the wire. Counter-review found it.
//
// THE SEQUENCE NUMBER IS PER FRAME, not per command, so each RMI message of a multi-frame command
// is distinguishable; sharing one across the two 13-byte messages that a level change sends puts
// two back-to-back multi-frame messages on a byte-identical CAN id, which removes the one field that
// exists to keep them apart (rmi_response.h:56-63).
static bool send_frame(const comfoair_command_frame_t *frame, uint8_t sequence)
{
    comfoair::rmi::Frame rmi(frame->bytes, frame->bytes + frame->len);
    const uint32_t       can_id = comfoair::rmi::command_can_id(
        COMFOAIR_CONTROL_LOCAL_NODE, COMFOAIR_CONTROL_UNIT_NODE, 0,
        comfoair::rmi::needs_multi_frame(rmi), false, true, sequence);

    // EVERY chunk is attempted, even after one is not acknowledged: abandoning the rest leaves a
    // truncated multi-frame message on the bus, and a receiver reassembling it has half a command
    // under a key that the next message will reuse.
    bool all = true;
    for (const comfoair::rmi::Frame &chunk : comfoair::rmi::split_into_can_frames(rmi))
        if (comfoair_can_transmit(can_id, chunk.data(), chunk.size(), false, TX_TIMEOUT_MS) != ESP_OK)
            all = false;
    return all;
}

uint8_t comfoair_control_next_sequence(void)
{
    taskENTER_CRITICAL(&s_sequence_mux);
    s_sequence             = (uint8_t)((s_sequence + 1) & 0x03);
    const uint8_t sequence = s_sequence;
    taskEXIT_CRITICAL(&s_sequence_mux);
    return sequence;
}

static void control_task(void *arg)
{
    (void)arg;
    for (;;) {
        comfoair_command_t command;
        if (xQueueReceive(s_queue, &command, portMAX_DELAY) != pdTRUE)
            continue;

        bool all = true;
        for (uint8_t i = 0; i < command.count; i++) {
            // The delay belongs to the FRAME, not to the command: the "Auto" preset releases boost
            // and away immediately and the manual mode a second later, and sending the third early
            // is what leaves the unit in manual.
            if (command.frames[i].delay_ms > 0)
                vTaskDelay(pdMS_TO_TICKS(command.frames[i].delay_ms));
            // Two bits, fresh for each RMI frame -- see send_frame().
            const uint8_t sequence = comfoair_control_next_sequence();

            if (!send_frame(&command.frames[i], sequence)) {
                all = false;
                // The rest is STILL SENT. A balance-mode command is two frames and the second is
                // what releases the other subunit's timer -- abandoning it because the first was
                // not acknowledged leaves the unit in an undefined state with both timers set. A
                // bus nobody answers on drops both, which is the same outcome either
                // way; a bus that dropped one frame gets the other.
                ESP_LOGW(TAG, "frame %u of %u was not acknowledged", (unsigned)(i + 1),
                         (unsigned)command.count);
            }
        }

        // Even after a frame that was not acknowledged: the read-back shows what the unit actually
        // did, which is exactly what is in doubt then. One request, handed to the poll task --
        // this call returns at once and never touches the bus from here.
        const int32_t readback = comfoair_command_readback_pdo(&command);
        if (readback >= 0)
            comfoair_poll_request_pdo((uint16_t)readback, READBACK_DELAY_MS);

        lock();
        if (all)
            s_stats.sent++;
        else
            s_stats.failed++;
        unlock();
    }
}

esp_err_t comfoair_control_submit(const comfoair_command_t *command)
{
    if (command == NULL)
        return ESP_ERR_INVALID_ARG;
    if (s_queue == NULL)
        return ESP_ERR_INVALID_STATE;
    // Nothing to send is a success ONLY for a duration: writing one is a local change and produces
    // no frames at all (comfoair_command.h, stores_duration). Anything else with no frames is a
    // builder that dropped them, and answering ESP_OK to it is a silent no-op that looks like a
    // successful send.
    if (command->count == 0)
        return command->stores_duration ? ESP_OK : ESP_ERR_INVALID_ARG;

    // NEVER BLOCKS. This is called from the HTTP task, and a wait here is the web interface
    // stopping -- which is the whole reason the queue exists.
    // Counted BEFORE the hop: the sender task can drain and increment `sent` between the send and
    // the increment, which made a reader briefly see more sent than queued.
    lock();
    s_stats.queued++;
    unlock();
    if (xQueueSend(s_queue, command, 0) != pdTRUE) {
        lock();
        s_stats.queued--;
        s_stats.dropped++;
        unlock();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void comfoair_control_get_stats(comfoair_control_stats_t *out)
{
    if (out == NULL)
        return;
    const comfoair_control_stats_t empty = {0};
    if (s_lock == NULL) {
        *out = empty;
        return;
    }
    lock();
    *out = s_stats;
    unlock();
}

esp_err_t comfoair_control_start(void)
{
    if (s_task != NULL)
        return ESP_ERR_INVALID_STATE;

    s_lock  = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(QUEUE_LEN, sizeof(comfoair_command_t));
    if (s_lock == NULL || s_queue == NULL) {
        ESP_LOGE(TAG, "out of memory starting the command sender");
        return ESP_ERR_NO_MEM;
    }

    load_durations();

    // 3072: the task formats log lines and calls into the TWAI driver. It does no JSON and holds
    // no document, so it needs less than the provisioning task next door.
    if (xTaskCreate(control_task, "comfoair_ctrl", 3072, NULL, 4, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "command sender did not start; the unit cannot be controlled");
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "commands ready: boost %u min, bypass %u min, balance %u min, away %u min, "
                  "permanent manual %s",
             (unsigned)s_durations.boost_minutes, (unsigned)s_durations.bypass_minutes,
             (unsigned)s_durations.balance_minutes, (unsigned)s_durations.away_minutes,
             s_durations.permanent_manual ? "on" : "off");
    return ESP_OK;
}
