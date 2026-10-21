// Host stand-in for the slice of FreeRTOS that comfoair_can.c and comfoair_can_api.c use.
//
// Backed by pthreads in fake_rtos.c, not by a cooperative scheduler: comfoair_can_rx_task() is an
// infinite loop blocking on portMAX_DELAY, and the only honest way to test "does the
// consumer keep up" is to let it actually run on its own thread.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int      BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;

#define pdTRUE   ((BaseType_t)1)
#define pdFALSE  ((BaseType_t)0)
#define pdPASS   pdTRUE
#define pdFAIL   pdFALSE

#define portMAX_DELAY ((TickType_t)0xffffffffU)
#define portTICK_PERIOD_MS 1u
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

// The production ISR returns `higher_priority_woken == pdTRUE` instead of calling this,
// so it is only here to keep an accidental direct call compiling.
#define portYIELD_FROM_ISR() do { } while (0)
