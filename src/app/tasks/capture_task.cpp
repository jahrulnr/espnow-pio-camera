#include "capture_task.h"
#include "app/espnow/slave.h"
#include "esp_log.h"
#include "esp_log.h"

#define TAG "CaptureTask"
#define CAPTURE_TASK_STACK 4096
#define CAPTURE_TASK_PRIO  2
#define CAPTURE_INTERVAL_MS 200

static void captureTask(void *pvParameters) {
    QueueHandle_t frameQueue = (QueueHandle_t)pvParameters;
    while (1) {
            // Use SlaveNode API to perform capture and enqueue safely
            app::espnow::espnowSlave.triggerCaptureIfNeeded();
        vTaskDelay(pdMS_TO_TICKS(CAPTURE_INTERVAL_MS));
    }
}

void startCaptureTask(QueueHandle_t frameQueue) {
    xTaskCreatePinnedToCore(captureTask, TAG, CAPTURE_TASK_STACK, frameQueue, CAPTURE_TASK_PRIO, NULL, 1);
}
