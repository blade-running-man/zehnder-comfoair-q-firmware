// The device half of the indication: two pins, one RMT channel, one task.
//
// WRAPPED IN #ifdef ESP_PLATFORM ENTIRELY, a build fact rather than taste: the host test
// build compiles every .c in this directory (components/comfoair_config/CMakeLists.txt says the
// same of comfoair_config_nvs.c), so without the guard test/test_led_pattern would go looking for
// led_strip.h on a laptop. Everything a host case CAN see was pushed next door into
// comfoair_led_pattern.c; the rest is unobservable without a board and is checked by the
// hardware scenarios in docs/hardware-verification/07-indication-and-button.md.
//
// NOT ONE GPIO NUMBER APPEARS BELOW (AGENTS.md criterion 5): the pins, the polarity of the mono LED, the
// byte order of the pixel and the gate's settling time all arrive through the board_t handed to
// comfoair_led_init(), which is what makes the last two -- both unverified guesses -- a one-line
// edit in one board file when the bench finally settles them.
#ifdef ESP_PLATFORM

#include <string.h>
#include "comfoair_led_driver.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "led_strip_rmt.h"

static const char *TAG = "comfoair_led";

// Below the CAN tasks (10 and 9, comfoair_can_api.c:102-106) and below MQTT and the push channel
// (4), because a light must never delay the bus.
#define RENDER_PRIO   3
#define RENDER_STACK  3072
// IDENTIFY: "which of these boxes is mine", five seconds, self-clearing. A duration of this
// component's own overlay, not a gesture threshold -- the five and fifteen seconds
// comfoair_config.h:196-199 owns still appear nowhere in comfoair_led.
#define IDENTIFY_MS 5000
// One frame of one pixel is 24 bits x 1.25 us plus a reset gap of >= 280 us, rounded up to a whole
// millisecond and then to a scheduler tick: the wait between "the blanking frame went to the RMT"
// and "the data line may stop being data".
#define BLANK_LATCH_MS 1

// One slot per kind, indexed BY the kind: a second push of the same kind replaces the first and two
// kinds can never share a slot. Slot 0 is OVR_NONE and stays empty for ever, which costs twelve
// bytes and lets comfoair_led_override_strongest() -- the pure function the host suite pins the
// priority with -- be handed this array exactly as it stands, with no repacking to get it wrong.
#define SLOT_COUNT ((uint8_t)COMFOAIR_LED_OVR_BUTTON + 1u)

// Written in comfoair_led_init() before the render task exists, read everywhere after it.
static const board_t     *s_board;  // also the "already initialised" flag
static bool               s_has_mono, s_rgb_usable;  // rgb_usable: present AND the RMT came up
static led_strip_handle_t s_strip;
static bool               s_gate_up;
// The last pixel that reached the wire, and whether it is known. ONLINE's dark-plus-flash makes
// this more than an optimisation, both ways: ONLINE is dark for 9750 ms of every 10 000, so
// without it the RMT would run 40 times a second for ever; and BECAUSE the resolver floors a lit
// frame's brightness at 1, the flash never compares equal to the dark around it, so the pixel is
// still rewritten twice every ten seconds and a latched corruption still gets corrected. DO NOT
// drop that floor and keep this: they are one decision.
static uint8_t s_last[3];
static bool    s_last_valid;

// Everything a caller may change from another task. A spinlock and not a mutex: the critical
// sections are a struct copy long, cannot fail, need no allocation, and a caller pushing an overlay
// never waits for a frame to finish going out on the wire.
static portMUX_TYPE           s_lock   = portMUX_INITIALIZER_UNLOCKED;
static device_phase_t         s_phase  = COMFOAIR_PHASE_BOOT;
static comfoair_led_policy_t  s_policy = {COMFOAIR_LED_CEILING_DEFAULT, false};
static comfoair_led_override_t s_slots[SLOT_COUNT];

// Uptime in milliseconds, the same clock and expression as comfoair_net.c:104: resolve()'s now_ms
// and comfoair_led_override_t's since_ms must be one clock, or the ramp measures a hold against a
// stopwatch that started elsewhere.
static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

// Rounded UP and never zero, which pdMS_TO_TICKS() is neither. THE PROJECT RUNS AT
// CONFIG_FREERTOS_HZ=100 (sdkconfig.m5stack-nanoc6): the grid is 10 ms and 25 ms is not on it, so
// pdMS_TO_TICKS(25) is 20 ms, the loop wakes early, the rest of the wait is 5 ms, and
// pdMS_TO_TICKS(5) is 0 -- a yield, not a sleep. The task would then spin at priority 3 for a
// twentieth of every second: a busy loop reported by nothing.
static TickType_t ticks_at_least(uint32_t ms) {
    TickType_t t = (TickType_t)((ms * (uint64_t)configTICK_RATE_HZ + 999u) / 1000u);
    return t ? t : 1;
}

// LEVEL FIRST, THEN DIRECTION. The other way round the pin is an
// output for as long as it takes to reach the next line, driving whatever the reset value was.
static void drive(gpio_num_t pin, uint32_t level) {
    gpio_set_level(pin, level);
    (void)gpio_set_direction(pin, GPIO_MODE_OUTPUT);
}

// THE PERMUTATION ITSELF IS NOT HERE, and the reason is that it was, for one review cycle: a
// switch over six orders that no host case could reach, while hardware scenario HV-LED-01 carried
// a second copy of it as a diagnostic table -- and three of that table's six rows were the inverse
// permutation. board_rgb_positions() is pure and test/test_board_rgb_order checks it against that
// table, so this function is only the translation into the library's struct, and the only one:
// the library does the reordering on the wire.
static led_color_component_format_t format_of(board_rgb_order_t order) {
    uint8_t r, g, b;
    board_rgb_positions(order, &r, &g, &b);
    led_color_component_format_t fmt = {
        .format = {.r_pos = r, .g_pos = g, .b_pos = b, .w_pos = 3, .reserved = 0,
                   .bytes_per_color = 1, .num_components = 3}};
    return fmt;
}

// Claims the RMT channel and puts a KNOWN frame on the wire before anything else does. That
// blanking frame goes out before any pattern and is not decoration: a WS2812's latch is undefined
// after power is applied, so without it "the pixel shows something random" and "the palette is
// wrong" become one observation.
static esp_err_t strip_create(void) {
    led_strip_config_t cfg = {
        .strip_gpio_num         = s_board->rgb.data,
        .max_leds               = s_board->rgb.count,
        .led_model              = LED_MODEL_WS2812,
        .color_component_format = format_of(s_board->rgb.color_order),
        .flags                  = {.invert_out = 0},
    };
    // Zeroes are the library's defaults: 10 MHz, its block size, no DMA -- one pixel is 24 bits,
    // so DMA would cost a channel for nothing. Spelled out rather than left implicit.
    led_strip_rmt_config_t rmt = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 0,
        .mem_block_symbols = 0,
        .flags             = {.with_dma = 0},
    };
    const esp_err_t err = led_strip_new_rmt_device(&cfg, &rmt, &s_strip);
    if (err != ESP_OK) {
        s_strip = NULL;  // the library promises nothing about the handle on a failed create
        return err;
    }
    s_last_valid = false;
    return led_strip_clear(s_strip);
}

// The owner's off switch, and THE ONLY TIME THE GATE MOVES AFTER INIT. Driven
// by the CEILING and never by the frame being dark: ONLINE is dark for 97.5 % of every ten seconds
// and cutting power there would cost a settling wait on every flash, so the ceiling -- the one
// input meaning "off" rather than "dark" -- is what moves it. THE DATA PIN IS NEVER ABOVE THE
// GATE, and the order below enforces that: dropping the gate with the data line still driven high
// pushes current into an unpowered chip through its input protection diode, and the pixel glows
// dimly in some colour on a device that believes itself dark -- intermittently, by history, never
// on the bench.
static void rgb_power(bool on) {
    if (on == s_gate_up || s_board->rgb.power_gate == GPIO_NUM_NC) return;
    if (on) {
        gpio_set_level(s_board->rgb.power_gate, 1);
        s_gate_up = true;
        if (s_board->rgb.power_gate_settle_ms)
            vTaskDelay(ticks_at_least(s_board->rgb.power_gate_settle_ms));
        const esp_err_t err = strip_create();
        if (err == ESP_OK) return;
        // The gate went up and the channel did not, so the chip now has power, an UNDEFINED latch
        // and nothing able to write it -- it would glow in whatever colour that latch happens to
        // hold, for ever, on a device that believes itself dark. Fall through and take the power
        // back: the ONE state in which "dark" is guaranteed rather than hoped for.
        ESP_LOGE(TAG, "RGB did not return when the ceiling rose: %s", esp_err_to_name(err));
        s_rgb_usable = false;
    }
    if (s_strip) {
        (void)led_strip_clear(s_strip);              // (0,0,0) while the chip still has power
        vTaskDelay(ticks_at_least(BLANK_LATCH_MS));  // and latched before data stops being data
        (void)led_strip_del(s_strip);                // the RMT gives the pin back
        s_strip = NULL;
    }
    drive(s_board->rgb.data, 0);
    gpio_set_level(s_board->rgb.power_gate, 0);
    s_gate_up    = false;
    s_last_valid = false;
}

static void apply_colour(const comfoair_led_frame_t *f, uint8_t owner_ceiling) {
    if (!s_rgb_usable) return;
    rgb_power(owner_ceiling > 0);
    if (!s_gate_up || !s_strip) return;

    const uint8_t px[3] = {comfoair_led_scale(f->r, f->brightness),
                           comfoair_led_scale(f->g, f->brightness),
                           comfoair_led_scale(f->b, f->brightness)};
    if (s_last_valid && memcmp(px, s_last, sizeof(px)) == 0) return;

    esp_err_t err = ESP_OK;
    for (uint8_t i = 0; i < s_board->rgb.count && err == ESP_OK; i++)
        err = led_strip_set_pixel(s_strip, i, px[0], px[1], px[2]);
    if (err == ESP_OK) err = led_strip_refresh(s_strip);
    if (err != ESP_OK) {
        // DO NOT remember a frame that may not have gone out: the next tick would compare equal to
        // it, skip the send, and the pixel would sit on whatever the failed transfer left until
        // the picture happened to change -- the exact failure that looks like a working device.
        s_last_valid = false;
        return;
    }
    memcpy(s_last, px, sizeof(px));
    s_last_valid = true;
}

static void render(uint32_t t) {
    device_phase_t          phase;
    comfoair_led_policy_t   policy;
    comfoair_led_override_t slots[SLOT_COUNT];

    taskENTER_CRITICAL(&s_lock);
    // IDENTIFY retires itself here rather than on a timer of its own: this
    // task is awake 40 times a second anyway, and an expiry inside the same lock as its push cannot
    // fall between the halves of one frame. Modular subtraction, wrap-safe like the ramp's.
    if (s_slots[COMFOAIR_LED_OVR_IDENTIFY].kind == COMFOAIR_LED_OVR_IDENTIFY &&
        (uint32_t)(t - s_slots[COMFOAIR_LED_OVR_IDENTIFY].since_ms) >= IDENTIFY_MS)
        s_slots[COMFOAIR_LED_OVR_IDENTIFY].kind = COMFOAIR_LED_OVR_NONE;
    phase  = s_phase;
    policy = s_policy;
    memcpy(slots, s_slots, sizeof(slots));
    taskEXIT_CRITICAL(&s_lock);

    // caps is built from what CAME UP, not from what the board claims: a failed RMT channel leaves
    // has_rgb false, the resolver empties the colour half of the frame, and the heartbeat carries
    // on. Nothing reboots because a peer is absent (AGENTS.md), at the scale of one component.
    const comfoair_led_caps_t caps = {s_has_mono, s_rgb_usable};
    const comfoair_led_frame_t f =
        comfoair_led_resolve(phase, comfoair_led_override_strongest(slots, SLOT_COUNT), caps,
                             policy, t);

    // Written every tick rather than on a change: one register write costs less than the state
    // that would have to be kept right across init, the gate and the failures. The inequality IS
    // the inversion -- both operands are bool, so it is exclusive-or spelled in C.
    if (s_has_mono)
        gpio_set_level(s_board->status_led, f.mono_on != s_board->status_led_inverted);
    apply_colour(&f, policy.ceiling);
}

// The deadline advances on the exact COMFOAIR_LED_TICK_MS grid while the SLEEP is whole scheduler
// ticks rounded up, so at 100 Hz the real sample times are 0, 30, 50, 80, 100 ms ... -- and both
// durations argued as TICK COUNTS keep their count, because both of their edges (250 ms for the
// flash, 100 ms for the pip) are sample times. Exact in ticks, not on a wall clock: the pip
// measures 100 ms give or take one scheduler tick, which is the tolerance hardware scenario
// HV-LED-06 asks for. DO NOT render from `next`: a frame drawn from an intended time is drawn from
// a time that did not happen.
static void render_task(void *arg) {
    (void)arg;
    uint32_t next = now_ms();
    for (;;) {
        render(now_ms());
        next += COMFOAIR_LED_TICK_MS;
        const int32_t left = (int32_t)(next - now_ms());
        // A WHOLE PERIOD behind means a tick was genuinely lost -- a long critical section
        // elsewhere, a flash write -- and resyncing beats running a burst of frames for time that
        // has already passed. DO NOT resync on ANY lateness: the sleep rounds UP, so the deadline
        // is a few milliseconds behind the clock on nearly every pass BY DESIGN. Resyncing there
        // makes every period 30 ms and the 25 ms grid -- the thing the two exact durations are
        // argued against -- mean nothing, while every test still passes.
        if (left < -(int32_t)COMFOAIR_LED_TICK_MS) next = now_ms();
        vTaskDelay(left > 0 ? ticks_at_least((uint32_t)left) : 1);
    }
}

esp_err_t comfoair_led_init(const board_t *board) {
    if (!board) return ESP_ERR_INVALID_ARG;
    if (s_board) return ESP_ERR_INVALID_STATE;

    s_board    = board;
    s_has_mono = board->status_led != GPIO_NUM_NC;

    // A board with neither indicator is a legitimate board and not a fault: the Olimex ESP32-EVB
    // documents no LED hardware at all. Failing here would stop a device that
    // works, which is the style src/main.cpp:141-143 exists to forbid.
    if (!s_has_mono && !board->rgb.present) {
        ESP_LOGI(TAG, "board %s has no indicators; nothing to drive", board->name);
        return ESP_OK;
    }

    if (s_has_mono) drive(board->status_led, board->status_led_inverted ? 1 : 0);

    if (board->rgb.present) {
        // The data line goes low first, BEFORE the gate goes up, so that the chip is never
        // powered while its data line is at an unknown level.
        drive(board->rgb.data, 0);
        if (board->rgb.power_gate != GPIO_NUM_NC) {
            drive(board->rgb.power_gate, 1);
            if (board->rgb.power_gate_settle_ms)
                vTaskDelay(ticks_at_least(board->rgb.power_gate_settle_ms));
        }
        s_gate_up       = true;  // true for a gateless board too: its rail is simply always on
        const esp_err_t err = strip_create();
        s_rgb_usable        = err == ESP_OK;
        // Logged, powered back down and survived: the colour channel carries the meaning, so this
        // is the loudest line here -- and still not a reason to stop, because a device with a
        // heartbeat is a device an owner can reach. It is not retried; a reboot is the recovery.
        if (!s_rgb_usable) {
            ESP_LOGE(TAG, "RGB channel did not start: %s -- continuing without colour",
                     esp_err_to_name(err));
            rgb_power(false);
        }
    }

    // No handle is kept: nothing in this tree stops the indication, and a handle nobody reads is
    // state that can go stale.
    if (xTaskCreate(render_task, "comfoair_led", RENDER_STACK, NULL, RENDER_PRIO, NULL) != pdPASS) {
        // The gate goes back down with it. Nothing would ever write the pixel again, so leaving it
        // powered means the blanking frame's darkness holds only until the first glitch -- and the
        // pin the mono LED sits on keeps whatever "off" level it was just given.
        ESP_LOGE(TAG, "render task did not start -- nothing will be indicated");
        rgb_power(false);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "indication up: heartbeat %s, colour %s", s_has_mono ? "yes" : "no",
             s_rgb_usable ? "yes" : "no");
    return ESP_OK;
}

void comfoair_led_set_phase(device_phase_t phase) {
    taskENTER_CRITICAL(&s_lock);
    s_phase = phase;
    taskEXIT_CRITICAL(&s_lock);
}

void comfoair_led_override_push(comfoair_led_override_kind_t kind, uint32_t arg) {
    if (kind <= COMFOAIR_LED_OVR_NONE || kind >= (comfoair_led_override_kind_t)SLOT_COUNT) return;
    const uint32_t t = now_ms();
    taskENTER_CRITICAL(&s_lock);
    comfoair_led_override_t *slot = &s_slots[kind];
    // THE EXCEPTION IS BUTTON'S ALONE, and it has to be. For BUTTON since_ms means "when the
    // current TIER began" (comfoair_led.h), so a caller re-pushing the same tier on every poll of
    // its own must not restart the ramp -- it would sit near zero for the whole hold, and no host
    // case can see it because the stamp is taken here and resolve() only reads it. For IDENTIFY
    // since_ms is the five-second deadline instead, so a repeat push MUST move it or "self-clears
    // five seconds after the last push" would silently mean "after the first".
    if (kind != COMFOAIR_LED_OVR_BUTTON || slot->kind != kind || slot->arg != arg)
        slot->since_ms = t;
    slot->kind = kind;
    slot->arg  = arg;
    taskEXIT_CRITICAL(&s_lock);
}

void comfoair_led_override_clear(comfoair_led_override_kind_t kind) {
    if (kind <= COMFOAIR_LED_OVR_NONE || kind >= (comfoair_led_override_kind_t)SLOT_COUNT) return;
    taskENTER_CRITICAL(&s_lock);
    s_slots[kind].kind = COMFOAIR_LED_OVR_NONE;
    taskEXIT_CRITICAL(&s_lock);
}

void comfoair_led_policy_set(uint8_t ceiling, bool quiet) {
    taskENTER_CRITICAL(&s_lock);
    s_policy.ceiling = ceiling;
    s_policy.quiet   = quiet;
    taskEXIT_CRITICAL(&s_lock);
}
#endif  // ESP_PLATFORM
