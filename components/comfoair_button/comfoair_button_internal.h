// The seam between the two halves of the polling task, and it exists because they are two
// responsibilities and one 350-line ceiling: comfoair_button_task.c is the pin and the loop, and
// comfoair_button_reset.c is everything that happens after a release has decided to destroy
// something. NOT IN include/ ON PURPOSE -- nothing outside this component may call either of these,
// and a header in include/ is an invitation to.
#pragma once

#ifdef ESP_PLATFORM

#include <stdbool.h>
#include <stdint.h>

#include "comfoair_button.h"

// One reading of the pin, already in the button's own terms: true means a finger is on it. The
// board and its polarity live in comfoair_button_task.c, which is the only file that ever sees
// them; the reset half needs this to gate the reboot and must not learn the pin to get it.
bool comfoair_button_pin_pressed(void);

// One poll period of sleep. Shared so the reboot gate and the sampler wait on the same grid.
void comfoair_button_wait_a_poll(void);

// Carry out an erasing tier: erase, say what went, confirm to the owner, and restart -- but only
// once the pin reads released, so the reboot never meets a held download strap. RETURNS ONLY IF
// THE ERASE FAILED (the device is then left running and reachable so the log ring can be read) or
// if the caller passed a tier that erases nothing. Blocks for at least the confirming animation,
// and for as long as the owner keeps the button down after that.
void comfoair_button_erase_and_restart(comfoair_gesture_tier_t tier);

#endif  // ESP_PLATFORM
