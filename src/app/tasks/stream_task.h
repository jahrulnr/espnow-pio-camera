#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "app/espnow/state_binary.h"

#ifdef __cplusplus
extern "C" {
#endif

void startStreamTask(QueueHandle_t frameQueue);

#ifdef __cplusplus
}
#endif
