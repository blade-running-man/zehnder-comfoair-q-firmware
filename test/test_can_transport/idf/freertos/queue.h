#pragma once
#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fake_queue *QueueHandle_t;

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size);
void          vQueueDelete(QueueHandle_t q);
BaseType_t    xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *higher_priority_woken);
BaseType_t    xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks);
BaseType_t    xQueueReceive(QueueHandle_t q, void *out, TickType_t ticks);
UBaseType_t   uxQueueMessagesWaiting(QueueHandle_t q);

#ifdef __cplusplus
}
#endif
