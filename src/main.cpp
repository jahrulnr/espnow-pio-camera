#include <Arduino.h>
#include <LittleFS.h>

#include "core/wdt.h"
#include "core/nvs.h"
#include "app/espnow/slave.h"
#include "app/tasks/capture_task.h"
#include "app/tasks/stream_task.h"

void init() {
  esp_panic_handler_disable_timg_wdts();
  nvs_init();
}

void setup() {
	Serial.begin(115200);
  LittleFS.begin(true);

  #if BOARD_HAS_PSRAM
  heap_caps_malloc_extmem_enable(0);
  #endif

  app::espnow::espnowSlave.begin(app::espnow::DEFAULT_CHANNEL);
  // Start capture and stream tasks after queue is ready
  startCaptureTask(app::espnow::espnowSlave.getFrameQueue());
  startStreamTask(app::espnow::espnowSlave.getFrameQueue());
}

void loop() {
  app::espnow::espnowSlave.loop();
  delay(10);
}
