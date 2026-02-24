#include "stream_task.h"
#include "app/espnow/slave.h"
#include "esp_log.h"
#include <string.h>

#define TAG "StreamTask"
#define STREAM_TASK_STACK 6144
#define STREAM_TASK_PRIO  2
#define STREAM_POLL_MS 100

static void streamTask(void *pvParameters) {
    QueueHandle_t frameQueue = (QueueHandle_t)pvParameters;
    (void)frameQueue;
    while (1) {
        // Use SlaveNode API to pop and process next pending frame
        app::espnow::espnowSlave.processNextPendingFrame();
        vTaskDelay(pdMS_TO_TICKS(STREAM_POLL_MS));
    }
}

void startStreamTask(QueueHandle_t frameQueue) {
    xTaskCreatePinnedToCore(streamTask, TAG, STREAM_TASK_STACK, frameQueue, STREAM_TASK_PRIO, NULL, 1);
}
