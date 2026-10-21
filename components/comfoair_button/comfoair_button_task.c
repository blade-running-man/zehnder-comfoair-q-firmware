// The device half of the gesture: samples the pin, feeds the decider, draws the tier and carries
// out what a release decided. See comfoair_button_task.h for the contract and comfoair_button.h for
// what a hold means -- not one threshold appears below.
//
// WRAPPED IN #ifdef ESP_PLATFORM ENTIRELY, a build fact rather than taste: the host test build
// compiles every .c in this directory (comfoair_led_driver.c:3-8 and comfoair_config_nvs.c:4-10 say
// the same of themselves), so without the guard test/test_button_gesture would go looking for
// driver/gpio.h on a laptop -- and that suite exists to prove the decider needs no such thing.
//
// NOT ONE GPIO NUMBER APPEARS BELOW (AGENTS.md criterion 5). The pin and its polarity arrive in the
// board_t handed to comfoair_button_start(); the inversion is applied ONCE, at the sample, which is
// the arrangement comfoair_button.h promises on the decider's behalf.
#ifdef ESP_PLATFORM

#include "comfoair_button_task.h"

#include "comfoair_button_internal.h"
#include "comfoair_led_driver.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "comfoair_btn";

// The poll period, the debounce and the flash length are ALL in comfoair_button.h now, next to the
// decider that is measured against them (they moved there when this file needed the debounce to
// answer "was that gap a release"). 50 Hz satisfies three constraints at once, the first being
// the one it was chosen for: samples must arrive more often than the 60 ms debounce -- 20 ms puts
// three inside it; CONFIG_FREERTOS_HZ is 100 (sdkconfig.m5stack-nanoc6:2497), so the delay is
// exactly two scheduler ticks and vTaskDelay rounds by nothing; and one sample period of threshold
// error on five seconds is 0,4 %.
//
// DO NOT raise it to save power. At 30 ms a single missed wake-up is already 60 ms of silence, and
// 60 ms of silence is a release -- comfoair_button_sample() will end the press rather than guess,
// so the cost is the owner's gesture, every time the task is late.

// Priority 4, and the ladder it is chosen against is in this tree: CAN rx = 10 and CAN bus = 9
// (comfoair_can_api.c:102,106) must never be starved; net = 5, poll = 5; mqtt, ws and control = 4.
// The button sits with the things that answer a person and below everything that must not miss a
// frame. The price of a lost sample is one of the two hundred and fifty in a five-second hold.
#define POLL_MS    COMFOAIR_BUTTON_POLL_MS

#define TASK_PRIO  4
// 2560 bytes, and it is the SMALLEST STACK IN THIS TREE while being the only one from which an NVS
// erase is called -- so the number is a guess until a board says otherwise, and it is a guess made
// visible rather than left in a header: the task logs its own high-water mark immediately before
// the restart, which is the one moment the deepest call (comfoair_config_nvs_reset, mbedtls-free
// but flash-bound) has already returned. Hardware scenario HV-BTN-07 reads that line. Nothing
// else in this task allocates, formats or recurses.
#define TASK_STACK 2560



static const board_t *s_board;
static TaskHandle_t   s_task;
static void (*s_on_setup_requested)(void);

static uint32_t now_ms(void)
{
    // The same clock comfoair_provision and comfoair_led measure against, and it has to be: the
    // tier's since_ms goes straight into an overlay the render task reads (comfoair_led_driver.c),
    // and two clocks there would draw a hold against a stranger's stopwatch.
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// The one place the board's polarity is applied. Everything above this line is in the button's own
// terms -- true means a finger is on it (comfoair_button.h:103-104).
bool comfoair_button_pin_pressed(void)
{
    const int raw = gpio_get_level(s_board->button);
    return s_board->button_inverted ? (raw == 0) : (raw != 0);
}

void comfoair_button_wait_a_poll(void) { vTaskDelay(pdMS_TO_TICKS(POLL_MS)); }

// --- what the owner sees ------------------------------------------------------------------

// The two rules of comfoair_button.h are NOT applied here any more: comfoair_button_plan_draw()
// applies them, in the pure half, where test/test_button_poller can reach them. What is left in
// this file is the four lines that turn a plan into a call -- and that is the whole point of the
// split, because the version of these four lines that decided as well as acted survived every
// mutation the tree could aim at it.
static void draw(const comfoair_button_out_t *out, uint32_t t, comfoair_button_flash_t *flash)
{
    const comfoair_button_draw_t plan = comfoair_button_plan_draw(out, t, flash);
    if (plan.clear)
        comfoair_led_override_clear(COMFOAIR_LED_OVR_BUTTON);
    if (plan.push)
        comfoair_led_override_push(COMFOAIR_LED_OVR_BUTTON, plan.arg);
    // out->tier_since_ms IS DELIBERATELY UNUSED. comfoair_led.h says both halves of the overlay go
    // over, but the driver stamps since_ms itself inside comfoair_led_override_push() -- it has to,
    // because that stamp is what it refuses to move when an arg repeats -- so there is no parameter
    // to hand this to. The two clocks agree anyway: both are esp_timer_get_time()/1000.
}

static void execute(comfoair_gesture_tier_t gesture)
{
    switch (gesture) {
        case COMFOAIR_GESTURE_SETUP:
            // Erases nothing and reboots nothing. It reaches the provisioning machine through the
            // radio's own queue rather than touching it -- comfoair_prov_t has one writer, the task
            // inside comfoair_net -- and through a function pointer rather than a call, for the
            // build reason set out at comfoair_button_start(). The confirmation the owner gets is
            // not this light: it is `comfoair-XXXX` appearing in the Wi-Fi list within a couple of
            // seconds -- a check the owner can make without looking at the LED at all.
            ESP_LOGI(TAG, "setup window requested by button");
            if (s_on_setup_requested != NULL)
                s_on_setup_requested();
            break;
        case COMFOAIR_GESTURE_SOFT:
        case COMFOAIR_GESTURE_HARD:
            // Next door, and it returns only if the erase did not take -- see
            // comfoair_button_internal.h. Everything destructive is in that file and nothing in
            // this one, which is what makes this loop readable as a loop.
            comfoair_button_erase_and_restart(gesture);
            break;
        case COMFOAIR_GESTURE_NONE:
        case COMFOAIR_GESTURE_COUNT:
            break;  // no release decided anything; the common case, every sample of every hold
    }
}

// --- the loop ------------------------------------------------------------------------------

static void button_task(void *arg)
{
    (void)arg;

    comfoair_button_t state;
    comfoair_button_init(&state);

    comfoair_button_flash_t flash = {false, 0};

    for (;;) {
        const uint32_t t   = now_ms();
        const bool     hit = comfoair_button_pin_pressed();

        // comfoair_button_sample() and NOT comfoair_button_step(): the wrapper is what turns a
        // poll this task missed into the release it actually was. Without it two separate presses
        // either side of one stalled second are a single six-second hold, and the owner's network
        // and password are erased by the scheduler. A write to NVS from a web handler is enough to
        // cause that stall, so it is not a thought experiment (comfoair_button.h).
        const comfoair_button_out_t out = comfoair_button_sample(&state, hit, t);

        // The drawing first and the outcome second, and the order is load-bearing: the sample that
        // commits a gesture is also the sample whose tier goes to NONE (comfoair_button.h:74-79),
        // so this is what takes the hold's colour off the pixel before confirm() puts its own
        // flashes there. The other way round, the confirmation would be overwritten by a clear.
        draw(&out, t, &flash);
        execute(out.gesture);

        // A fixed delay rather than vTaskDelayUntil, deliberately. A missed deadline here must
        // cost ONE late sample, not a burst of catch-up samples with timestamps the decider will
        // read as a hold that never happened -- and the decider is already built to read a gap as
        // a release rather than to guess (comfoair_button.h:112-117).
        comfoair_button_wait_a_poll();
    }
}

// `on_setup_requested` IS A FUNCTION POINTER AND NOT A CALL TO comfoair_net_open_setup_window(),
// and the reason is a build fact rather than a taste in coupling: PlatformIO's dependency finder
// does not evaluate the preprocessor, so an #include of comfoair_net.h from this file -- inside
// #ifdef ESP_PLATFORM or not -- pulls comfoair_net into every host suite that includes
// comfoair_button.h, and comfoair_net cannot compile off the device. Measured, not assumed: it
// turned test/test_button_gesture red with "esp_event.h file not found". A callback run on the
// button task is also the shape the contract below wants from the other direction, and the
// wiring stays visible in src/main.cpp where the rest of it is.
//
// Called from the button task, never from an ISR, at most once per gesture, and it MAY BLOCK.
// NULL is legal and means a short press only writes a log line.
esp_err_t comfoair_button_start(const board_t *board, void (*on_setup_requested)(void))
{
    if (board == NULL)
        return ESP_ERR_INVALID_ARG;
    if (board->button == GPIO_NUM_NC)
        // A board with no button is a board with no factory reset, not a fault. The caller logs it
        // once so that "hold the button" is never the advice given for such a device.
        return ESP_ERR_NOT_SUPPORTED;
    if (s_task != NULL)
        return ESP_ERR_INVALID_STATE;

    s_board              = board;
    s_on_setup_requested = on_setup_requested;

    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << (unsigned)board->button,
        .mode         = GPIO_MODE_INPUT,
        // Derived from the polarity, not from the board: a button that reads low when pressed is
        // one whose idle level something has to hold high. The debounce is reasoned for a bare
        // input with a pull-up and no RC filter, which this resistor guarantees, and it is
        // harmless where an external one exists.
        .pull_up_en   = board->button_inverted ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = board->button_inverted ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        // NO INTERRUPT, rejected deliberately: an ISR on a strap pin is more
        // mechanism than a poll, and a poll is exactly what makes the gesture a pure function of a
        // sequence of samples.
        .intr_type    = GPIO_INTR_DISABLE,
    };
    const esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK)
        return err;

    // The board's own warning, READ AND PUT IN THE RING (comfoair_log_install() is already up), not
    // merely relied upon in a comment. board_t.button_is_download_strap is the field every argument
    // in this component cites; until this line nothing in the firmware actually looked at it, which
    // is the same defect as a guard that checks something other than what it promises. An owner
    // reading GET /api/log after a device came up "dead" finds the explanation here.
    if (board->button_is_download_strap)
        ESP_LOGW(TAG, "the button is also the download strap: hold it AFTER boot, never through "
                      "power-on -- the ROM latches this pin at reset and the firmware never runs");

    if (xTaskCreate(button_task, "comfoair_btn", TASK_STACK, NULL, TASK_PRIO, &s_task) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

#endif  // ESP_PLATFORM
