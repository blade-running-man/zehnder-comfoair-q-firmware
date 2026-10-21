// FreeRTOS on pthreads, for the host tests only.
//
// Small on purpose: a queue with a condition variable and a task that is a thread. The
// one thing it does NOT simplify away is blocking -- comfoair_can_rx_task() waits on portMAX_DELAY
// forever, and a cooperative shim would have to change that loop to be testable, which
// would mean testing something other than the code that ships.

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

const char *esp_err_to_name(esp_err_t code)
{
    switch (code) {
    case ESP_OK:                return "ESP_OK";
    case ESP_FAIL:              return "ESP_FAIL";
    case ESP_ERR_NO_MEM:        return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG:   return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_NOT_SUPPORTED: return "ESP_ERR_NOT_SUPPORTED";
    case ESP_ERR_TIMEOUT:       return "ESP_ERR_TIMEOUT";
    default:                    return "ESP_ERR_UNKNOWN";
    }
}

// --- logging -------------------------------------------------------------------------

// Defined in fake_twai.c. A log from interrupt context is a finding, not a nuisance:
// esp_log takes a lock and formats, neither of which belongs in an ISR.
extern bool fake_twai_in_isr(void);

static unsigned s_log_calls_in_isr;
static unsigned s_live_queues;
static unsigned s_live_tasks;
static unsigned s_isr_blocking_calls;

unsigned fake_rtos_live_tasks(void) { return s_live_tasks; }
unsigned fake_rtos_live_queues(void) { return s_live_queues; }
unsigned fake_rtos_isr_blocking_calls(void) { return s_isr_blocking_calls; }

// Set while this thread is inside xQueueReceive and nowhere else. vTaskDelete on FreeRTOS
// stops a task at whatever instruction it happens to be executing, so a task torn down
// while it is anywhere ELSE -- inside the user's rx callback, say -- loses whatever that
// callback was holding. On the host pthread_cancel would do the same thing, but silently;
// counting it is what turns the hazard into an assertion.
static __thread int t_parked;

static unsigned s_tasks_killed_off_park;

unsigned fake_rtos_tasks_killed_off_park(void) { return s_tasks_killed_off_park; }


void fake_log_record(char level, const char *tag)
{
    (void)level;
    (void)tag;
    if (fake_twai_in_isr())
        s_log_calls_in_isr++;
}

void fake_log_sink(const char *fmt, ...) { (void)fmt; }

unsigned fake_log_calls_in_isr(void) { return s_log_calls_in_isr; }

void fake_log_reset(void)
{
    s_log_calls_in_isr      = 0;
    s_isr_blocking_calls    = 0;
    s_tasks_killed_off_park = 0;
}

// --- queue ---------------------------------------------------------------------------

struct fake_queue {
    pthread_mutex_t mtx;
    pthread_cond_t  not_empty;
    uint8_t        *slots;
    size_t          item_size;
    size_t          capacity;
    size_t          count;
    size_t          head;
};

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size)
{
    struct fake_queue *q = calloc(1, sizeof *q);
    if (q == NULL)
        return NULL;
    q->slots = calloc(length, item_size);
    if (q->slots == NULL) {
        free(q);
        return NULL;
    }
    q->item_size = item_size;
    q->capacity  = length;
    s_live_queues++;
    pthread_mutex_init(&q->mtx, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    return q;
}

void vQueueDelete(QueueHandle_t q)
{
    if (q == NULL)
        return;
    s_live_queues--;
    pthread_cond_destroy(&q->not_empty);
    pthread_mutex_destroy(&q->mtx);
    free(q->slots);
    free(q);
}

static BaseType_t queue_push(QueueHandle_t q, const void *item)
{
    BaseType_t ok = pdFALSE;
    pthread_mutex_lock(&q->mtx);
    if (q->count < q->capacity) {
        const size_t tail = (q->head + q->count) % q->capacity;
        memcpy(q->slots + tail * q->item_size, item, q->item_size);
        q->count++;
        ok = pdTRUE;
        pthread_cond_signal(&q->not_empty);
    }
    pthread_mutex_unlock(&q->mtx);
    return ok;
}

BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *higher_priority_woken)
{
    if (higher_priority_woken)
        *higher_priority_woken = pdFALSE;
    return queue_push(q, item);
}

// The blocking send. It is NOT interchangeable with xQueueSendFromISR, and the difference
// is the whole of comfoair_can.h's promise that the ISR "must not block": on FreeRTOS this
// call parks the caller until the queue drains, which inside an interrupt is a hang. The
// shim cannot reproduce the hang without hanging the suite, so it records the violation --
// that keeps "never blocks the interrupt" a checkable claim instead of a slogan.
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks)
{
    if (fake_twai_in_isr())
        s_isr_blocking_calls++;
    (void)ticks;
    return queue_push(q, item);
}

static void unlock_on_cancel(void *arg) { pthread_mutex_unlock((pthread_mutex_t *)arg); }

BaseType_t xQueueReceive(QueueHandle_t q, void *out, TickType_t ticks)
{
    BaseType_t ok = pdFALSE;
    t_parked = 1;
    pthread_mutex_lock(&q->mtx);
    // vTaskDelete() cancels the thread while it sits here; without this handler the
    // cancellation would unwind holding the mutex and the next test would deadlock.
    pthread_cleanup_push(unlock_on_cancel, &q->mtx);

    if (ticks == portMAX_DELAY) {
        while (q->count == 0)
            pthread_cond_wait(&q->not_empty, &q->mtx);
    } else if (q->count == 0 && ticks > 0) {
        struct timeval now;
        gettimeofday(&now, NULL);
        struct timespec until = {
            .tv_sec  = now.tv_sec + (time_t)(ticks / 1000u),
            .tv_nsec = now.tv_usec * 1000 + (long)(ticks % 1000u) * 1000000L,
        };
        if (until.tv_nsec >= 1000000000L) {
            until.tv_sec++;
            until.tv_nsec -= 1000000000L;
        }
        while (q->count == 0 && pthread_cond_timedwait(&q->not_empty, &q->mtx, &until) != ETIMEDOUT)
            ;
    }

    if (q->count > 0) {
        memcpy(out, q->slots + q->head * q->item_size, q->item_size);
        q->head = (q->head + 1) % q->capacity;
        q->count--;
        ok = pdTRUE;
    }

    pthread_cleanup_pop(0);
    pthread_mutex_unlock(&q->mtx);
    t_parked = 0;
    return ok;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q)
{
    pthread_mutex_lock(&q->mtx);
    const UBaseType_t n = (UBaseType_t)q->count;
    pthread_mutex_unlock(&q->mtx);
    return n;
}

// --- task ----------------------------------------------------------------------------

struct fake_task {
    pthread_t       thread;
    TaskFunction_t  fn;
    void           *arg;
    volatile int   *parked;   // points at the task thread's own t_parked
};

static __thread struct fake_task *t_self;

static void *task_trampoline(void *arg)
{
    struct fake_task *t = arg;
    t_self    = t;
    t->parked = &t_parked;
    t->fn(t->arg);
    return NULL;
}

BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack_depth, void *arg,
                       UBaseType_t priority, TaskHandle_t *created)
{
    (void)name;
    (void)stack_depth;
    (void)priority;

    struct fake_task *t = calloc(1, sizeof *t);
    if (t == NULL)
        return pdFAIL;
    t->fn  = fn;
    t->arg = arg;
    if (pthread_create(&t->thread, NULL, task_trampoline, t) != 0) {
        free(t);
        return pdFAIL;
    }
    s_live_tasks++;
    if (created)
        *created = t;
    return pdPASS;
}

// vTaskDelete on FreeRTOS reclaims the task wherever it happens to be. pthread_cancel is
// the closest equivalent, and the join is what makes a test's teardown deterministic --
// without it the next test could still be racing the previous consumer.
void vTaskDelete(TaskHandle_t task)
{
    // vTaskDelete(NULL) means "delete the calling task" and is how a FreeRTOS task function
    // is allowed to end -- returning from one is a fatal error there. A task that leaves on
    // its own was never killed at an arbitrary instruction, so it is not counted as a
    // mid-work teardown.
    if (task == NULL) {
        if (t_self != NULL) {
            s_live_tasks--;
            pthread_detach(pthread_self());
            free(t_self);
            t_self = NULL;
        }
        pthread_exit(NULL);
    }
    // NULL `parked` means the thread has not reached its first statement yet, which is not
    // a mid-work kill; only a running task observed outside xQueueReceive is.
    if (task->parked != NULL && *task->parked == 0)
        s_tasks_killed_off_park++;
    pthread_cancel(task->thread);
    pthread_join(task->thread, NULL);
    s_live_tasks--;
    free(task);
}

void vTaskDelay(TickType_t ticks)
{
    struct timespec ts = {.tv_sec = ticks / 1000u, .tv_nsec = (long)(ticks % 1000u) * 1000000L};
    nanosleep(&ts, NULL);
}
