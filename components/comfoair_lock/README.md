# comfoair_lock — the one recursive mutex guarding the state model

> This is documentation, not code — re-read the source before relying on a detail.

The entity values and change marks in `comfoair_state` are written by the CAN receive task
(PDOs), by the HTTP server and `comfoair_mqtt` (optimistic select/number sets) and by
`app_main` (the boot reset), and read by the REST/WebSocket renderer and the MQTT publisher.
Without a lock, an HTTP handler rendering `/api/state` while a frame arrives is a real race.
This component is that lock: two functions, one process-wide recursive FreeRTOS mutex on the
device, and nothing at all on the host so the state model stays host-testable.

## Responsibility

Owns:
- The single process-wide lock around the state model (`comfoair_lock()` / `comfoair_unlock()`).
- Lazy creation of the underlying mutex on first use (no init function, no init ordering).
- The host build's no-op variant (the `#else` branch of `comfoair_lock.c`).

Does NOT do:
- Decide what is guarded or how long a critical section lasts — that is the callers'
  (`comfoair_state/comfoair_state.cpp`, `comfoair_api/comfoair_api.cpp`).
- Guard the configuration document, the provisioning machine or the radio — `comfoair_net`
  has its own two recursive mutexes in `comfoair_net.c`.
- Timeouts, try-lock, ISR-safe locking or error reporting — none exist.
- Any thread-safety on the host — host suites are single-threaded by design.

## Public API

Header: `include/comfoair_lock.h` (C, `extern "C"`-wrapped for the C++ callers).

| Symbol | Contract |
| --- | --- |
| `void comfoair_lock(void)` | Take the state lock. Task context only (FreeRTOS mutex; never from an ISR). Blocks with `portMAX_DELAY` — there is no timeout and no failure return. Recursive: the same task may take it again while holding it, and must call `comfoair_unlock()` once per take. Safe before any initialisation: creates the mutex on first call. Host: does nothing. |
| `void comfoair_unlock(void)` | Release one level of the lock taken by the calling task. Must be called by the task that took it (recursive-mutex rule). No return value. Host: does nothing. |

No types, constants or `esp_err_t` values. If mutex creation ever fails (out of heap),
both calls silently degrade to no-ops — there is nothing for a caller to check.

## Implementation

- `comfoair_lock.c` — the whole component; `#ifdef ESP_PLATFORM` picks the FreeRTOS
  implementation, `#else` the empty host stubs.
- `include/comfoair_lock.h` — the two prototypes and the rationale for recursion and the
  host no-op.
- `CMakeLists.txt` — `idf_component_register(SRCS "comfoair_lock.c" INCLUDE_DIRS "include")`;
  no `REQUIRES`/`PRIV_REQUIRES` (FreeRTOS is always available to IDF components).

Flow on the device: a static `SemaphoreHandle_t s_mutex` starts `NULL`. `comfoair_lock()`
calls `ensure()`, which creates it with `xSemaphoreCreateRecursiveMutex()` if still `NULL`,
then `xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY)`. `comfoair_unlock()` gives it back
with `xSemaphoreGiveRecursive()` if it exists.

Invariants:
- The first take happens on `app_main` before any other task exists: `src/main.cpp` calls
  `comfoair_state_reset()` (which locks) right after `nvs_init()`, before the CAN bus,
  network, HTTP or MQTT tasks start. That is what makes the unsynchronised check-then-create
  in `ensure()` safe.
- Nesting is real, not hypothetical: `comfoair_api.cpp` `render_object()` and
  `comfoair_api_render_value()` hold the lock for a whole document and call
  `comfoair_state_get()` / `comfoair_state_changed()` per entity, each of which locks again.
- Critical sections must stay short and non-blocking: the CAN receive task waits on this
  lock with no timeout, so anything slow done under it stalls PDO ingestion.

DO NOT:
- Replace the recursive mutex with a plain mutex or binary semaphore. The renderer's nested
  take would deadlock on the device, and the host suites would never notice (the host
  variant is a no-op).
- Drop the lock in the renderer "because `comfoair_state_get()` already locks". One take per
  entity would let a CAN frame land between two of them and produce a document mixing
  values from before and after it — a snapshot of a moment that never existed.
- Add a FreeRTOS or pthread dependency to the host branch. The state model is tested
  natively precisely because it is the same code the device runs; pulling a framework into
  it ends that, and the host suites have nothing to serialise.
- Call it from an ISR, or hold it across network I/O, logging to a slow sink, or any wait.
- Add an explicit `comfoair_lock_init()` that callers must remember; lazy creation removes
  the ordering constraint between components.

## Dependencies

Requires: nothing declared; on the device it includes `freertos/FreeRTOS.h` and
`freertos/semphr.h`.

Depended on by (CMake `REQUIRES`):
- `comfoair_state` — every getter, setter, change-mark accessor, `comfoair_state_apply_pdo()`
  and `comfoair_state_reset()` brackets its body with the lock.
- `comfoair_api` — the full-state/delta renderer and the single-value renderer hold it for
  the whole document.

Everything else (`src/main.cpp`, `comfoair_http`, `comfoair_mqtt`) reaches the lock only
through those two components. `[env:native]` in `platformio.ini` adds
`-I components/comfoair_lock/include`, and the library finder compiles `comfoair_lock.c`
(host branch) into any suite that includes `comfoair_state.h` or `comfoair_api.h`.

## Tests

No suite targets this component directly. It is compiled — as the no-op host branch — into
`test/test_state`, `test/test_api` and `test/test_ha_cleanup`, which exercise its callers.

Not covered anywhere on the host: the FreeRTOS branch itself, recursion depth, the
nested-take behaviour of the renderer, and any cross-task race (the host is
single-threaded). Concurrency correctness of the state model rests on the device build and
on reading the call sites.

## Notes

- **Why one global lock, not per-entity or reader/writer locks.** The state is a few
  hundred bytes updated by short memcpy-sized operations; a single mutex is cheap, and it is
  the only shape that gives the renderer a consistent snapshot across all entities without a
  multi-lock ordering protocol.
- **Why a mutex rather than a critical section / spinlock.** The renderer holds it while
  formatting an entire JSON document; disabling interrupts or spinning for that long would
  be far worse than blocking. A FreeRTOS mutex also gives priority inheritance, so a
  low-priority renderer holding it cannot indefinitely starve the CAN task behind a
  mid-priority task.
- **Why lazy creation is accepted as race-free.** Two tasks would have to call
  `comfoair_lock()` concurrently before `app_main` has reached `comfoair_state_reset()`,
  and no such task exists at that point. If boot order ever changes so that a task touching
  the state starts earlier, this assumption must be re-checked.
- **Silent degradation on allocation failure.** If `xSemaphoreCreateRecursiveMutex()`
  returns `NULL`, every call retries creation and otherwise does nothing — the state model
  runs unsynchronised rather than the firmware stopping. On a fresh boot with the heap
  untouched this does not happen in practice.
