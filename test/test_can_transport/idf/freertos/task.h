#pragma once
#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fake_task *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack_depth, void *arg,
                       UBaseType_t priority, TaskHandle_t *created);
void       vTaskDelete(TaskHandle_t task);
void       vTaskDelay(TickType_t ticks);

#ifdef __cplusplus
}
#endif
