// One lock, guarding the state model.
//
// It exists because phase 4 introduces the first reader. Until now the CAN task was the
// only code touching the state, so the absence of a lock was safe by accident rather than
// by design; an HTTP handler reading /api/state while a frame arrives is a genuine race.
//
// Two properties are load-bearing:
//
//  * RECURSIVE. A renderer takes the lock for the whole document so the snapshot is
//    consistent, and then calls comfoair_state_get() per entity, which takes it again. A
//    plain mutex would deadlock on the second take.
//  * A NO-OP ON THE HOST. The point of testing the state model natively is that it is the
//    same code the device runs (AGENTS.md), and a FreeRTOS dependency would end that.
//    The host tests are single-threaded, so there is nothing to serialise there.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Safe before any initialisation: the lock is created on first use, and on the host both
// calls do nothing at all.
void comfoair_lock(void);
void comfoair_unlock(void);

#ifdef __cplusplus
}
#endif
