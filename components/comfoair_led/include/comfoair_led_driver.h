// The imperative half of the indication: the RMT channel, the WS2812 power gate, the heartbeat
// pin, the render task and the overlay slots. Everything here returns esp_err_t or touches a
// global; nothing here decides what the device looks like -- that is comfoair_led.h next door,
// which is pure and which is where every case in test/test_led_pattern reaches.
//
// TASK CONTEXT. comfoair_led_init() is called once from app_main and from nowhere else. The four
// setters below are safe from ANY task at any time: each is a short critical section over a state
// block the render task copies out under the same lock, so a caller is never blocked on a frame
// being sent, and a frame is never assembled from two different pushes. None of them may be called
// from an ISR -- taskENTER_CRITICAL is not the ISR variant -- and none of them needs to be: every
// caller in this tree (the button task, the OTA handler, the HTTP and MQTT paths) is a task.
//
// FAILURE. Losing the indication is never a reason to stop: "nothing reboots because a peer is
// absent" (AGENTS.md), and the same shape src/main.cpp
// already uses for a bus that will not come up (main.cpp:146-153). comfoair_led_init() returns
// ESP_OK for a board with no indicators at all, and drops the colour channel while keeping the
// heartbeat if the RMT channel cannot be created -- so a caller that gets an error has one correct
// response, which is to log it and carry on.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "comfoair_led.h"
#include "comfoair_phase.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Brings up whichever of the two indicators the board describes and starts the 25 ms render task.
// `board` is BORROWED AND MUST OUTLIVE THE DEVICE -- board_get() returns a pointer to static
// storage, which is the only thing ever passed here.
//
// A PARAMETER RATHER THAN A CALL TO board_get() INSIDE, the precedent being comfoair_can_start()
// (comfoair_can.h:33): the component then names no board and no pin, which is what AGENTS.md criterion 5
// asks for, and a second board can be handed in by a test that does not exist yet.
//
// Errors the caller must handle -- all of them by logging and continuing, none by returning:
//   ESP_ERR_INVALID_ARG   `board` is NULL.
//   ESP_ERR_INVALID_STATE already initialised. The second call changes nothing.
//   ESP_ERR_NO_MEM        the render task would not start. NOTHING IS INDICATED after this, and
//                         the device is otherwise unaffected -- see FAILURE above.
// ESP_OK is also the answer for a board with neither indicator (`status_led == GPIO_NUM_NC` and
// `rgb.present == false`): no pin is touched, no task is created, and that is a legitimate board
// rather than a fault. ESP_OK likewise covers "the colour channel did not
// come up but the heartbeat did"; the reason is logged at ERROR and the pixel stays dark.
esp_err_t comfoair_led_init(const board_t *board);

// The phase the device is in, as decided elsewhere. Cheap and idempotent: call it on every
// evaluation rather than only on a change. Until the first call the phase is COMFOAIR_PHASE_BOOT,
// which is exactly where comfoair_phase.h:44-47 says BOOT is actually observed.
void comfoair_led_set_phase(device_phase_t phase);

// Raise one overlay. `kind` selects the slot, so a second push of the same kind REPLACES the first
// and never stacks; the winner between different kinds is the fixed total order of
// comfoair_led_override_kind_t and never the order of the pushes.
//
// `arg` by kind:
//   OVR_OTA       percent complete, 0..100.
//   OVR_BUTTON    comfoair_led_button_arg(tier, tier_ms) -- NEVER PACKED BY HAND (comfoair_led.h).
//   OVR_IDENTIFY  ignored.
// OVR_NONE is not a thing to push and is ignored; clearing is comfoair_led_override_clear().
//
// The BUTTON slot's "when this tier began" is stamped here, and a BUTTON push whose `arg` REPEATS
// what the slot already holds does NOT restamp it: the tier has not changed, so the ramp must not
// restart. That is what makes a caller which pushes on every one of its own polls draw the same
// picture as one which pushes only on a change -- without it the ramp sits near zero for the whole
// hold, a defect no host case can see because the stamp is taken here and not in resolve(). Every
// other kind restamps on every push.
//
// IDENTIFY clears itself five seconds after its LAST push -- which is why it
// is not covered by the paragraph above. Every other kind stays up until its clear.
void comfoair_led_override_push(comfoair_led_override_kind_t kind, uint32_t arg);

// Drop one overlay. Clearing a slot that is not up is not an error and does nothing.
void comfoair_led_override_clear(comfoair_led_override_kind_t kind);

// The owner's settings. `ceiling` is the brightness cap 0..255, default 51. NOTHING CALLS THIS
// TODAY: no entity or setting feeds it, so the device runs at the default with quiet off.
//
// A CEILING OF ZERO IS AN OFF SWITCH AND IS OBEYED PHYSICALLY: the pixel is blanked, the data line
// is driven low and the power gate is dropped, in that order and with the settling waits that go
// with it. Raising the ceiling again brings all three back. ON A BOARD WITH NO GATE
// (`power_gate == GPIO_NUM_NC`) only the first of the three is possible, and a WS2812 holding
// (0,0,0) is dark but still drawing its quiescent current -- the promised darkness is guaranteed
// by the frame there, not by the rail. The heartbeat is untouched by any of it -- "I turned the
// light off" must never become "I cannot tell this from a brick", which is the whole return on
// giving the plain LED the heartbeat alone and the RGB all of the meaning.
void comfoair_led_policy_set(uint8_t ceiling, bool quiet);

#ifdef __cplusplus
}
#endif
