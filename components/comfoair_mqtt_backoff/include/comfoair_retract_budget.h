// The ceiling on a retraction, expressed as the CONDITION of the loop it bounds.
//
// WHY IT IS NOT A `budget_spent()` PREDICATE BESIDE THE LOOP, which is the shorter and more
// obvious shape and was the shape until 2026-09-07. A predicate is read by an `if` inside the
// body, and an `if` can be deleted while the loop it guarded goes on looking correct. That is not
// hypothetical here: comfoair_mqtt is esp-mqtt, FreeRTOS and a client handle, so it is not built
// in [env:native] at all, and removing the whole ceiling left every host case green. Written as
// the loop's own condition there is nothing left to delete -- taking the call out takes the `for`
// with it -- and the arithmetic lands in a component a host suite can call
// (test/test_retract_budget).
//
// NO CLOCK AND NO FreeRTOS, for the same reason as the rest of this component: the caller owns
// xTaskGetTickCount() and passes the DIFFERENCE it has already taken. That is what makes the wrap
// expressible as a value rather than as a comment -- at CONFIG_FREERTOS_HZ = 100
// (sdkconfig.m5stack-nanoc6:2497) a 32-bit TickType_t turns over about every 497 days, and
// `(uint32_t)(now - started)` is correct across that turn while `now >= started + budget` is false
// for the 497 days after it.
//
// IT IS A CEILING AND NEVER A CONDITION TO SATISFY (AGENTS.md). The only thing
// it can do is end a sweep early, leaving retained topics behind on a broker this device is being
// taken away from. Nothing here retries, reboots or reports a terminal state.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Whether the retraction loop should run its `index`-th pass.
 *
 * `index`  how many passes have been entered so far, 0 on the first.
 * `total`  comfoair_ha_cleanup_count() -- 0 is legitimate and means "enter nothing".
 * `elapsed_ticks`  the caller's own unsigned difference, `(uint32_t)(now - started)`.
 * `budget_ticks`   pdMS_TO_TICKS(RETRACT_BUDGET_MS). 0 means nothing is published at all.
 *
 * True only while there is work left AND the budget has not been reached. The budget is spent ON
 * the tick it equals, not after it: a `>` here would be a ceiling one tick too high, which is
 * harmless, and the same slip written the other way round is a ceiling that never stops, which is
 * the twelve-second publisher stall this function exists to bound. Both are pinned.
 *
 * PURE. No clock, no globals, no allocation; safe from any task.
 */
bool comfoair_retract_continue(size_t index, size_t total, uint32_t elapsed_ticks,
                               uint32_t budget_ticks);

#ifdef __cplusplus
}
#endif
