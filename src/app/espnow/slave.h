#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "protocol.h"

namespace app::espnow {

class SlaveNode {
 public:
  // Public task-friendly API
  QueueHandle_t getFrameQueue() const { return frameQueue; }
  bool triggerCaptureIfNeeded();
  bool processNextPendingFrame();

  bool begin(uint8_t channel = DEFAULT_CHANNEL);
  void loop();

  bool sendToMaster(PacketType type, const void* payload, size_t payloadSize);
  bool sendStateBinary(const void* payload, size_t payloadSize);
  bool isMasterLinked() const { return masterKnown; }

 private:
  struct PendingFrame {
    uint32_t frameId = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t format = 0;
    size_t size = 0;
    uint8_t* bytes = nullptr;
  };

  static constexpr uint8_t MIN_SCAN_CHANNEL = 1;
  static constexpr uint8_t MAX_SCAN_CHANNEL = 13;
  static constexpr uint32_t CHANNEL_SCAN_INTERVAL_MS = 300;
  static constexpr uint32_t MASTER_TIMEOUT_MS = 12000;
  static constexpr uint8_t FRAME_QUEUE_DEPTH = 2;
  static constexpr uint32_t CAPTURE_IDLE_DELAY_MS = 8;
  static constexpr uint32_t CAPTURE_OFFLINE_DELAY_MS = 120;
  static constexpr uint32_t STREAM_QUEUE_WAIT_MS = 100;

  static SlaveNode* activeInstance;

  volatile bool started = false;
  volatile bool masterKnown = false;
  uint8_t masterMac[6] = {0};
  uint16_t sequence = 0;
  uint8_t scanChannel = MIN_SCAN_CHANNEL;
  uint32_t lastHelloMs = 0;
  uint32_t lastScanMs = 0;
  uint32_t lastMasterSeenMs = 0;
  uint32_t lastFrameMs = 0;
  volatile bool streamEnabled = true;
  volatile bool forceCapturePending = false;
  volatile bool txBusy = false;
  Frame txFrame = {};
  QueueHandle_t frameQueue = nullptr;
  TaskHandle_t captureTaskHandle = nullptr;
  TaskHandle_t streamTaskHandle = nullptr;
  portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;

  bool matchesMasterBeacon(const uint8_t* payload, uint8_t payloadSize) const;
  void scanNextChannel();
  bool addMasterPeer(const uint8_t mac[6]);
  void onMasterLinked();
  bool enqueueCapturedFrame();
  bool sendFrameFromQueue(const PendingFrame& frame);
  bool getMasterMacSnapshot(uint8_t outMac[6]);
  void releasePendingFrame(PendingFrame* frame);
  void flushFrameQueue();
  void handleCommandPayload(const uint8_t* payload, uint8_t payloadSize);
  // Task logic moved to separate modules (see capture_task.h, stream_task.h)

  static void onSendStatic(const esp_now_send_info_t* txInfo, esp_now_send_status_t status);
  static void onReceiveStatic(const esp_now_recv_info_t* recvInfo, const uint8_t* data, int len);
};

extern SlaveNode espnowSlave;

}  // namespace app::espnow
