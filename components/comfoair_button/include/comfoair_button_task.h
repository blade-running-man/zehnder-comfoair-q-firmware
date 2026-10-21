// The imperative half of the gesture: one pin, one 20 ms poll, and one of the two esp_restart()
// calls in this firmware (OTA's restart into a new image is the other). Nothing here decides what a hold MEANS -- that is comfoair_button.h next door, which is
// pure and where every case in test/test_button_gesture reaches.
//
// The split is the same one comfoair_led draws between comfoair_led.h and comfoair_led_driver.h,
// and it is drawn in the same place for the same reason: the decider must compile in [env:native]
// with no ESP-IDF header anywhere on its include path, so the pin, the inversion, the clock and the
// FreeRTOS task live here and the thresholds live there.
//
// TASK CONTEXT. comfoair_button_start() is called once, from app_main, after the network and the
// indication are up. It creates one task at priority 4 with a 2560-byte stack; that task owns the
// button state, samples the pin, pushes the LED overlay and executes the outcome. Nothing in this
// header is safe to call from an ISR and nothing needs to be. There is no stop(): a component that
// can be stopped is a component somebody stops in the middle of a fifteen-second hold. The task
// therefore runs for the life of the device.
//
// WHAT THE TASK CAN DO TO THE DEVICE, spelled out because it is not reversible. A hold of five
// seconds erases the Wi-Fi pair and the UI password; fifteen erases every namespace, the broker
// settings among them, and clears the log ring; both then reboot. A short press erases nothing and
// only opens the setup window. Every outcome is committed by the RELEASE, never by reaching a
// threshold, which is what keeps the download strap (the same pin) clear of all three.
//
// FAILURE. comfoair_button_start() failing is not a reason to stop the device (nothing reboots
// because a peer is absent: AGENTS.md; and the shape
// src/main.cpp already uses for a bus that will not come up): it means the factory reset by button
// is unavailable until the next boot and the only way back is USB. That is worth an ESP_LOGE and
// nothing more -- a device that refuses to ventilate a house because a button could not be read is
// a worse failure than the one it is reporting.
#pragma once

#include "board.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Starts the poller. `board` is BORROWED AND MUST OUTLIVE THE DEVICE -- board_get() returns static
// storage, which is the only thing ever passed here.
//
// A PARAMETER RATHER THAN A CALL TO board_get() INSIDE, the precedent being comfoair_can_start()
// (comfoair_can.h:33) and comfoair_led_init(): this component then names no board and no pin, which
// is what AGENTS.md criterion 5 asks for, and the day a board arrives with the button on another pin and
// the other polarity, the board file changes and neither this file nor the decider does.
//
// Errors the caller must handle -- all of them by logging and continuing, none by returning:
//   ESP_ERR_INVALID_ARG    `board` is NULL.
//   ESP_ERR_NOT_SUPPORTED  the board describes no button (`button == GPIO_NUM_NC`). A legitimate
//                          board, not a fault: it has no gesture and no factory reset.
//   ESP_ERR_INVALID_STATE  already started. The second call changes nothing.
//   ESP_ERR_NO_MEM         the task would not start. NO GESTURE IS READ after this.
//   anything gpio_config() returns, unchanged.
//
// `on_setup_requested` is what a press shorter than a second does -- comfoair_net_open_setup_window
// is the only thing ever passed. A POINTER AND NOT A CALL, because PlatformIO's dependency finder
// does not read #ifdef: including comfoair_net.h from this component would drag the radio into
// every host suite that includes the decider next door, and that is measured rather than feared --
// it turned test/test_button_gesture red with "esp_event.h file not found". It is invoked from the
// button task, at most once per gesture, and MAY BLOCK. NULL is legal: a short press then only
// writes a log line, which is what a board with no network layer would want.
esp_err_t comfoair_button_start(const board_t *board, void (*on_setup_requested)(void));

#ifdef __cplusplus
}
#endif
