#include "comfoair_poll_plan.h"

#include <stddef.h>

// Signed distance from `now` to `due`: negative or zero means due. Wrap-safe for spans < 2^31 ms.
static int32_t until(uint32_t due, uint32_t now) { return (int32_t)(due - now); }

void comfoair_poll_plan_init(comfoair_poll_plan_t *plan, const uint16_t *pdos, size_t count,
                             uint32_t interval_ms, uint32_t now_ms)
{
    plan->pdos             = pdos;
    plan->count            = count;
    plan->interval_ms      = interval_ms;
    plan->next_sweep_ms    = now_ms;
    plan->sweep_index      = count;
    plan->sweep_requested  = false;
    plan->readback_pending = false;
    plan->readback_pdo     = 0;
    plan->readback_due_ms  = 0;
}

void comfoair_poll_plan_request_sweep(comfoair_poll_plan_t *plan) { plan->sweep_requested = true; }

bool comfoair_poll_plan_request_readback(comfoair_poll_plan_t *plan, uint16_t pdo,
                                         uint32_t now_ms, uint32_t delay_ms)
{
    if (plan->readback_pending && plan->readback_pdo != pdo)
        return false;
    // Re-arming moves the one request to after the LATEST command. DO NOT keep the earlier
    // deadline "so it is not postponed": a request that lands between two commands shows the
    // state after the first, and nothing would ask again after the second.
    plan->readback_pending = true;
    plan->readback_pdo     = pdo;
    plan->readback_due_ms  = now_ms + delay_ms;
    return true;
}

comfoair_poll_step_t comfoair_poll_plan_next(comfoair_poll_plan_t *plan, uint32_t now_ms)
{
    comfoair_poll_step_t step = {false, 0, 0};

    // The read-back first: it is one frame, and it is what somebody is looking at right now.
    if (plan->readback_pending && until(plan->readback_due_ms, now_ms) <= 0) {
        plan->readback_pending = false;
        step.send              = true;
        step.pdo               = plan->readback_pdo;
        return step;
    }

    if (plan->sweep_index >= plan->count &&
        (plan->sweep_requested || until(plan->next_sweep_ms, now_ms) <= 0)) {
        // Counted from the START, so the period is the interval and not interval + sweep length.
        // A request made during this sweep sets the flag again and gets one more sweep after it.
        plan->sweep_requested = false;
        plan->sweep_index     = 0;
        plan->next_sweep_ms   = now_ms + plan->interval_ms;
    }

    if (plan->sweep_index < plan->count) {
        step.send = true;
        step.pdo  = plan->pdos[plan->sweep_index++];
        return step;
    }

    int32_t wait = until(plan->next_sweep_ms, now_ms);
    if (plan->readback_pending && until(plan->readback_due_ms, now_ms) < wait)
        wait = until(plan->readback_due_ms, now_ms);
    step.wait_ms = wait > 0 ? (uint32_t)wait : 0;
    return step;
}
