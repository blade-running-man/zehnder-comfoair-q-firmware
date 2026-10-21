#include "comfoair_lock.h"

#ifdef ESP_PLATFORM

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_mutex;

// Created on first use rather than in an init function, so no caller can forget to
// initialise it and no ordering constraint appears between components. The first take
// happens from app_main long before any task races for it, so the check-then-create is not
// itself a race in practice -- and making it one would require two tasks calling this
// before app_main has finished, which cannot happen.
static void ensure(void)
{
    if (s_mutex == NULL)
        s_mutex = xSemaphoreCreateRecursiveMutex();
}

void comfoair_lock(void)
{
    ensure();
    if (s_mutex != NULL)
        xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
}

void comfoair_unlock(void)
{
    if (s_mutex != NULL)
        xSemaphoreGiveRecursive(s_mutex);
}

#else

// Host tests are single-threaded. Nothing to serialise, and pulling in a threading library
// to prove that would be worse than saying so.
void comfoair_lock(void) {}
void comfoair_unlock(void) {}

#endif
