#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "app/camera/camera_capture.h"

#ifdef __cplusplus
extern "C" {
#endif

void startCaptureTask(QueueHandle_t frameQueue);

#ifdef __cplusplus
}
#endif
