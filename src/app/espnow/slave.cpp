#include "slave.h"

#include "state_binary.h"
#include "app/camera/camera_capture.h"

#include <WiFi.h>
#include <app_config.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <cstdlib>
#include <cstring>

namespace app::espnow {

static constexpr const char* TAG = "espnow_cam";
static volatile bool gAwaitingSendCb = false;
static volatile bool gSendDone = false;
static volatile esp_now_send_status_t gLastSendStatus = ESP_NOW_SEND_FAIL;

static uint16_t computeChecksum16(const uint8_t* data, size_t length) {
  if (data == nullptr || length == 0) {
    return 0;
  }

  uint32_t sum = 0;
  for (size_t index = 0; index < length; ++index) {
    sum += data[index];
  }
  return static_cast<uint16_t>(sum & 0xFFFF);
}

static bool sendStateBinaryAndWait(const void* payload, size_t payloadSize, uint32_t timeoutMs) {
  if (payload == nullptr || payloadSize == 0) {
    return false;
  }

  gSendDone = false;
  gLastSendStatus = ESP_NOW_SEND_FAIL;
  gAwaitingSendCb = true;

  if (!espnowSlave.sendStateBinary(payload, payloadSize)) {
    gAwaitingSendCb = false;
    return false;
  }

  const uint32_t startMs = millis();
  while (gAwaitingSendCb && (millis() - startMs) < timeoutMs) {
    delayMicroseconds(100);
  }

  if (!gSendDone) {
    gAwaitingSendCb = false;
    return false;
  }

  return gLastSendStatus == ESP_NOW_SEND_SUCCESS;
}

SlaveNode* SlaveNode::activeInstance = nullptr;
SlaveNode espnowSlave;

bool SlaveNode::begin(uint8_t channel) {
  if (started) {
    return true;
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (channel > 0) {
    const esp_err_t channelErr = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (channelErr != ESP_OK) {
      ESP_LOGW(TAG, "Failed to set channel %u: %s", channel, esp_err_to_name(channelErr));
    }
    scanChannel = channel;
  }

  const esp_err_t initErr = esp_now_init();
  if (initErr != ESP_OK) {
    ESP_LOGE(TAG, "esp_now_init failed: %s", esp_err_to_name(initErr));
    return false;
  }

  if (!app::camera::cameraCapture.begin()) {
    ESP_LOGE(TAG, "Camera init failed");
    return false;
  }

  activeInstance = this;
  esp_now_register_send_cb(SlaveNode::onSendStatic);
  esp_now_register_recv_cb(SlaveNode::onReceiveStatic);

  frameQueue = xQueueCreate(FRAME_QUEUE_DEPTH, sizeof(PendingFrame*));
  if (frameQueue == nullptr) {
    ESP_LOGE(TAG, "Failed to create frame queue");
    return false;
  }

  started = true;
  lastHelloMs = millis();
  lastScanMs = millis();
  lastMasterSeenMs = 0;
  lastFrameMs = millis();

  // Task start moved to main.cpp layer

  ESP_LOGI(TAG, "ESP-NOW cam slave ready");
  return true;
}

void SlaveNode::loop() {
  if (!started) {
    return;
  }

  const uint32_t now = millis();
  bool timeout = false;
  portENTER_CRITICAL(&stateMux);
  if (masterKnown && lastMasterSeenMs > 0 && (now - lastMasterSeenMs > MASTER_TIMEOUT_MS)) {
    masterKnown = false;
    memset(masterMac, 0, sizeof(masterMac));
    timeout = true;
  }
  portEXIT_CRITICAL(&stateMux);

  if (timeout) {
    flushFrameQueue();
    ESP_LOGW(TAG, "Master timeout, back to scan mode");
  }

  if (!masterKnown && (now - lastScanMs >= CHANNEL_SCAN_INTERVAL_MS)) {
    scanNextChannel();
    lastScanMs = now;
  }

  if (masterKnown && (now - lastHelloMs >= 7000)) {
    static const char hello[] = "cam-online";
    sendToMaster(PacketType::HELLO, hello, sizeof(hello) - 1);
    lastHelloMs = now;
  }
}

bool SlaveNode::matchesMasterBeacon(const uint8_t* payload, uint8_t payloadSize) const {
  if (payload == nullptr || payloadSize != MASTER_BEACON_ID_LEN) {
    return false;
  }

  return memcmp(payload, MASTER_BEACON_ID, MASTER_BEACON_ID_LEN) == 0;
}

void SlaveNode::scanNextChannel() {
  uint8_t nextChannel = scanChannel;
  if (nextChannel < MIN_SCAN_CHANNEL || nextChannel >= MAX_SCAN_CHANNEL) {
    nextChannel = MIN_SCAN_CHANNEL;
  } else {
    nextChannel++;
  }

  const esp_err_t err = esp_wifi_set_channel(nextChannel, WIFI_SECOND_CHAN_NONE);
  if (err == ESP_OK) {
    scanChannel = nextChannel;
    ESP_LOGD(TAG, "Scanning channel %u", scanChannel);
  }
}

bool SlaveNode::addMasterPeer(const uint8_t mac[6]) {
  if (mac == nullptr) {
    return false;
  }

  if (!esp_now_is_peer_exist(mac)) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.ifidx = WIFI_IF_STA;
    peer.channel = 0;
    peer.encrypt = false;

    const esp_err_t addErr = esp_now_add_peer(&peer);
    if (addErr != ESP_OK) {
      ESP_LOGW(TAG, "Add master peer failed: %s", esp_err_to_name(addErr));
      return false;
    }
  }

  portENTER_CRITICAL(&stateMux);
  memcpy(masterMac, mac, 6);
  masterKnown = true;
  portEXIT_CRITICAL(&stateMux);
  return true;
}

void SlaveNode::onMasterLinked() {
  portENTER_CRITICAL(&stateMux);
  streamEnabled = true;
  forceCapturePending = false;
  portEXIT_CRITICAL(&stateMux);

  app::espnow::state_binary::IdentityState identity = {};
  app::espnow::state_binary::initHeader(identity.header, app::espnow::state_binary::Type::Identity);
  strncpy(identity.id, DEVICE_NAME, sizeof(identity.id) - 1);
  identity.id[sizeof(identity.id) - 1] = '\0';

  if (sendStateBinary(&identity, sizeof(identity))) {
    ESP_LOGI(TAG, "Master linked, identity sent: %s", DEVICE_NAME);
  }

  app::espnow::state_binary::FeaturesState features = {};
  app::espnow::state_binary::initHeader(features.header, app::espnow::state_binary::Type::Features);
  features.contractVersion = 1;
  features.featureBits = static_cast<uint32_t>(app::espnow::state_binary::FeatureIdentity)
                       | static_cast<uint32_t>(app::espnow::state_binary::FeatureCameraJpeg)
                       | static_cast<uint32_t>(app::espnow::state_binary::FeatureCameraStream);

  if (sendStateBinary(&features, sizeof(features))) {
    ESP_LOGI(TAG, "Master linked, features sent: 0x%08lX", static_cast<unsigned long>(features.featureBits));
  }
}

bool SlaveNode::enqueueCapturedFrame() {
  if (!masterKnown) {
    return false;
  }

  const uint32_t now = millis();
  bool shouldCapture = false;

  portENTER_CRITICAL(&stateMux);
  if (forceCapturePending) {
    forceCapturePending = false;
    shouldCapture = true;
  } else if (streamEnabled && (now - lastFrameMs >= CAM_FRAME_INTERVAL_MS)) {
    shouldCapture = true;
  }
  portEXIT_CRITICAL(&stateMux);

  if (!shouldCapture) {
    return false;
  }

  app::camera::CaptureFrame frame;
  if (!app::camera::cameraCapture.capture(frame)) {
    portENTER_CRITICAL(&stateMux);
    lastFrameMs = now;
    portEXIT_CRITICAL(&stateMux);
    return false;
  }

  if (frame.fb == nullptr || frame.fb->buf == nullptr) {
    app::camera::cameraCapture.release(frame);
    portENTER_CRITICAL(&stateMux);
    lastFrameMs = now;
    portEXIT_CRITICAL(&stateMux);
    ESP_LOGW(TAG, "Skip frame id=%lu invalid buffer", static_cast<unsigned long>(frame.frameId));
    return false;
  }

  if (frame.fb->format != PIXFORMAT_JPEG) {
    ESP_LOGW(TAG,
             "Skip frame id=%lu unexpected fb format=%u (expect JPEG)",
             static_cast<unsigned long>(frame.frameId),
             static_cast<unsigned>(frame.fb->format));
    app::camera::cameraCapture.release(frame);
    portENTER_CRITICAL(&stateMux);
    lastFrameMs = now;
    portEXIT_CRITICAL(&stateMux);
    return false;
  }

  const uint8_t* jpegPayload = frame.fb->buf;
  const size_t availableBytes = frame.fb->len;

  if (availableBytes == 0 || jpegPayload == nullptr) {
    app::camera::cameraCapture.release(frame);
    portENTER_CRITICAL(&stateMux);
    lastFrameMs = now;
    portEXIT_CRITICAL(&stateMux);
    ESP_LOGW(TAG, "Skip frame id=%lu empty jpeg", static_cast<unsigned long>(frame.frameId));
    return false;
  }

  if (availableBytes > CAM_MAX_FRAME_BYTES) {
    ESP_LOGW(TAG,
             "Skip frame id=%lu size=%u exceeds max=%u",
             static_cast<unsigned long>(frame.frameId),
             static_cast<unsigned>(availableBytes),
             static_cast<unsigned>(CAM_MAX_FRAME_BYTES));
    app::camera::cameraCapture.release(frame);
    portENTER_CRITICAL(&stateMux);
    lastFrameMs = now;
    portEXIT_CRITICAL(&stateMux);
    return false;
  }

  auto* pending = new PendingFrame();
  if (pending == nullptr) {
    app::camera::cameraCapture.release(frame);
    portENTER_CRITICAL(&stateMux);
    lastFrameMs = now;
    portEXIT_CRITICAL(&stateMux);
    ESP_LOGW(TAG, "Skip frame id=%lu alloc pending failed", static_cast<unsigned long>(frame.frameId));
    return false;
  }

  pending->bytes = static_cast<uint8_t*>(malloc(availableBytes));
  if (pending->bytes == nullptr) {
    releasePendingFrame(pending);
    app::camera::cameraCapture.release(frame);
    portENTER_CRITICAL(&stateMux);
    lastFrameMs = now;
    portEXIT_CRITICAL(&stateMux);
    ESP_LOGW(TAG, "Skip frame id=%lu alloc payload failed", static_cast<unsigned long>(frame.frameId));
    return false;
  }

  memcpy(pending->bytes, jpegPayload, availableBytes);
  pending->frameId = frame.frameId;
  pending->width = frame.width;
  pending->height = frame.height;
  pending->format = frame.format;
  pending->size = availableBytes;

  app::camera::cameraCapture.release(frame);

  portENTER_CRITICAL(&stateMux);
  lastFrameMs = now;
  portEXIT_CRITICAL(&stateMux);

  if (frameQueue == nullptr) {
    releasePendingFrame(pending);
    return false;
  }

  if (xQueueSend(frameQueue, &pending, 0) == pdPASS) {
    return true;
  }

  PendingFrame* stale = nullptr;
  if (xQueueReceive(frameQueue, &stale, 0) == pdPASS) {
    releasePendingFrame(stale);
  }

  if (xQueueSend(frameQueue, &pending, 0) != pdPASS) {
    ESP_LOGW(TAG, "Drop frame id=%lu queue full", static_cast<unsigned long>(pending->frameId));
    releasePendingFrame(pending);
    return false;
  }

  return true;
}

bool SlaveNode::sendFrameFromQueue(const PendingFrame& frame) {
  if (!masterKnown || frame.bytes == nullptr || frame.size == 0) {
    return false;
  }

  const size_t payloadBytes = frame.size;
  const uint16_t totalChunks = static_cast<uint16_t>(
      (payloadBytes + app::espnow::state_binary::kCameraChunkDataBytes - 1) /
      app::espnow::state_binary::kCameraChunkDataBytes);

  app::espnow::state_binary::CameraMetaState meta = {};
  app::espnow::state_binary::initHeader(meta.header, app::espnow::state_binary::Type::CameraMeta);
  meta.frameId = frame.frameId;
  meta.totalBytes = static_cast<uint32_t>(payloadBytes);
  meta.totalChunks = totalChunks;
  meta.width = frame.width;
  meta.height = frame.height;
  meta.format = frame.format;
  meta.quality = CAM_JPEG_QUALITY;

  if (!sendStateBinaryAndWait(&meta, sizeof(meta), CAM_SEND_ACK_TIMEOUT_MS)) {
    ESP_LOGW(TAG, "Failed sending meta for frame=%lu", static_cast<unsigned long>(frame.frameId));
    return false;
  }

  for (uint16_t chunkIndex = 0; chunkIndex < totalChunks; ++chunkIndex) {
    app::espnow::state_binary::CameraChunkState chunk = {};
    app::espnow::state_binary::initHeader(chunk.header, app::espnow::state_binary::Type::CameraChunk);
    chunk.frameId = frame.frameId;
    chunk.idx = static_cast<uint16_t>(chunkIndex + 1);
    chunk.total = totalChunks;

    const size_t offset = static_cast<size_t>(chunkIndex) * app::espnow::state_binary::kCameraChunkDataBytes;
    const size_t remaining = payloadBytes - offset;
    const size_t currentSize = remaining > app::espnow::state_binary::kCameraChunkDataBytes
                                   ? app::espnow::state_binary::kCameraChunkDataBytes
                                   : remaining;
    chunk.dataLen = static_cast<uint8_t>(currentSize);
    memcpy(chunk.data, frame.bytes + offset, currentSize);

    bool sent = false;
    for (uint8_t attempt = 0; attempt < CAM_CHUNK_RETRY_COUNT; ++attempt) {
      if (sendStateBinaryAndWait(&chunk, sizeof(chunk), CAM_SEND_ACK_TIMEOUT_MS)) {
        sent = true;
        break;
      }

      ESP_LOGW(TAG,
               "Failed sending chunk %u/%u for frame=%lu (attempt=%u)",
               static_cast<unsigned>(chunk.idx),
               static_cast<unsigned>(chunk.total),
               static_cast<unsigned long>(frame.frameId),
               static_cast<unsigned>(attempt + 1));
      delay(CAM_CHUNK_DELAY_MS);
    }

    if (!sent) {
      ESP_LOGW(TAG,
               "Frame transfer aborted id=%lu chunks=%u",
               static_cast<unsigned long>(frame.frameId),
               static_cast<unsigned>(totalChunks));
      return false;
    }
  }

  app::espnow::state_binary::CameraFrameEndState frameEnd = {};
  app::espnow::state_binary::initHeader(frameEnd.header, app::espnow::state_binary::Type::CameraFrameEnd);
  frameEnd.frameId = frame.frameId;
  frameEnd.totalBytes = static_cast<uint32_t>(payloadBytes);
  frameEnd.totalChunks = totalChunks;
  frameEnd.reserved = computeChecksum16(frame.bytes, payloadBytes);

  bool frameEndSent = false;
  for (uint8_t attempt = 0; attempt < CAM_CHUNK_RETRY_COUNT; ++attempt) {
    if (sendStateBinaryAndWait(&frameEnd, sizeof(frameEnd), CAM_SEND_ACK_TIMEOUT_MS)) {
      frameEndSent = true;
      break;
    }
    delay(CAM_CHUNK_DELAY_MS);
  }

  if (!frameEndSent) {
    ESP_LOGW(TAG, "Failed sending frame-end id=%lu", static_cast<unsigned long>(frame.frameId));
    return false;
  }

  ESP_LOGI(TAG,
           "Frame sent id=%lu size=%u chunks=%u",
           static_cast<unsigned long>(frame.frameId),
           static_cast<unsigned>(payloadBytes),
           static_cast<unsigned>(totalChunks));
  return true;
}

bool SlaveNode::getMasterMacSnapshot(uint8_t outMac[6]) {
  if (outMac == nullptr) {
    return false;
  }

  bool known = false;
  portENTER_CRITICAL(&stateMux);
  known = masterKnown;
  if (known) {
    memcpy(outMac, masterMac, 6);
  }
  portEXIT_CRITICAL(&stateMux);
  return known;
}

void SlaveNode::releasePendingFrame(PendingFrame* frame) {
  if (frame == nullptr) {
    return;
  }

  if (frame->bytes != nullptr) {
    free(frame->bytes);
    frame->bytes = nullptr;
  }

  delete frame;
}

void SlaveNode::flushFrameQueue() {
  if (frameQueue == nullptr) {
    return;
  }

  PendingFrame* frame = nullptr;
  while (xQueueReceive(frameQueue, &frame, 0) == pdPASS) {
    releasePendingFrame(frame);
  }
}

bool SlaveNode::triggerCaptureIfNeeded() {
  return enqueueCapturedFrame();
}

bool SlaveNode::processNextPendingFrame() {
  if (frameQueue == nullptr) {
    return false;
  }

  PendingFrame* frame = nullptr;
  if (xQueueReceive(frameQueue, &frame, pdMS_TO_TICKS(STREAM_QUEUE_WAIT_MS)) != pdPASS) {
    return false;
  }

  if (frame == nullptr) {
    return false;
  }

  bool sent = false;
  if (masterKnown) {
    sent = sendFrameFromQueue(*frame);
  }

  releasePendingFrame(frame);
  return sent;
}

// Task logic moved to separate modules (see capture_task.cpp, stream_task.cpp)

void SlaveNode::handleCommandPayload(const uint8_t* payload, uint8_t payloadSize) {
  if (payload == nullptr || payloadSize == 0) {
    return;
  }

  if (app::espnow::state_binary::hasTypeAndSize(payload,
                                                payloadSize,
                                                app::espnow::state_binary::Type::IdentityReq,
                                                sizeof(app::espnow::state_binary::IdentityReqCommand))) {
    onMasterLinked();
    return;
  }

  if (!app::espnow::state_binary::hasTypeAndSize(payload,
                                                  payloadSize,
                                                  app::espnow::state_binary::Type::CameraControl,
                                                  sizeof(app::espnow::state_binary::CameraControlCommand))) {
    return;
  }

  const auto* command = reinterpret_cast<const app::espnow::state_binary::CameraControlCommand*>(payload);
  const auto action = static_cast<app::espnow::state_binary::CameraControlAction>(command->action);

  switch (action) {
    case app::espnow::state_binary::CameraControlAction::CaptureOnce:
      forceCapturePending = true;
      ESP_LOGI(TAG, "Command: capture once");
      break;
    case app::espnow::state_binary::CameraControlAction::SetStreaming:
      streamEnabled = command->value != 0;
      ESP_LOGI(TAG, "Command: streaming=%s", streamEnabled ? "on" : "off");
      break;
    default:
      ESP_LOGW(TAG, "Unknown camera command action=%u", static_cast<unsigned>(command->action));
      break;
  }
}

bool SlaveNode::sendToMaster(PacketType type, const void* payload, size_t payloadSize) {
  uint8_t targetMac[6] = {0};
  if (!started || !getMasterMacSnapshot(targetMac)) {
    return false;
  }

  if (txBusy) {
    return false;
  }

  txFrame = {};
  txFrame.header.version = PROTOCOL_VERSION;
  txFrame.header.type = static_cast<uint8_t>(type);
  txFrame.header.sequence = sequence++;
  txFrame.header.timestampMs = millis();

  txFrame.payloadSize = payloadSize > MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE : payloadSize;
  if (txFrame.payloadSize > 0 && payload != nullptr) {
    memcpy(txFrame.payload, payload, txFrame.payloadSize);
  }

  const size_t bytes = sizeof(txFrame.header) + sizeof(txFrame.payloadSize) + txFrame.payloadSize;
  txBusy = true;
  const esp_err_t sendErr = esp_now_send(targetMac, reinterpret_cast<const uint8_t*>(&txFrame), bytes);
  if (sendErr != ESP_OK) {
    txBusy = false;
    ESP_LOGW(TAG, "Send failed: %s", esp_err_to_name(sendErr));
    return false;
  }

  return true;
}

bool SlaveNode::sendStateBinary(const void* payload, size_t payloadSize) {
  return sendToMaster(PacketType::STATE, payload, payloadSize);
}

void SlaveNode::onSendStatic(const esp_now_send_info_t* txInfo, esp_now_send_status_t status) {
  if (!activeInstance) {
    return;
  }

  activeInstance->txBusy = false;

  if (gAwaitingSendCb) {
    gLastSendStatus = status;
    gSendDone = true;
    gAwaitingSendCb = false;
  }

  ESP_LOGD(TAG, "TX status=%s", status == ESP_NOW_SEND_SUCCESS ? "ok" : "fail");
}

void SlaveNode::onReceiveStatic(const esp_now_recv_info_t* recvInfo, const uint8_t* data, int len) {
  if (!activeInstance || recvInfo == nullptr || data == nullptr || len <= 0) {
    return;
  }

  if (len < static_cast<int>(sizeof(PacketHeader) + sizeof(uint8_t))) {
    return;
  }

  const auto* header = reinterpret_cast<const PacketHeader*>(data);
  const auto payloadSize = *(data + sizeof(PacketHeader));
  const auto* payload = data + sizeof(PacketHeader) + sizeof(uint8_t);
  const size_t expectedLen = sizeof(PacketHeader) + sizeof(uint8_t) + payloadSize;
  if (payloadSize > MAX_PAYLOAD_SIZE || expectedLen > static_cast<size_t>(len)) {
    return;
  }

  const PacketType type = static_cast<PacketType>(header->type);
  uint8_t knownMasterMac[6] = {0};
  const bool hasKnownMaster = activeInstance->getMasterMacSnapshot(knownMasterMac);
  const bool fromKnownMaster = hasKnownMaster &&
                               (memcmp(knownMasterMac, recvInfo->src_addr, 6) == 0);
  const bool validBeacon = activeInstance->matchesMasterBeacon(payload, payloadSize);

  if (!fromKnownMaster) {
    if ((type != PacketType::HELLO && type != PacketType::HEARTBEAT) || !validBeacon) {
      return;
    }

    if (!activeInstance->addMasterPeer(recvInfo->src_addr)) {
      return;
    }

    ESP_LOGI(TAG, "Master locked %02X:%02X:%02X:%02X:%02X:%02X",
             recvInfo->src_addr[0], recvInfo->src_addr[1], recvInfo->src_addr[2],
             recvInfo->src_addr[3], recvInfo->src_addr[4], recvInfo->src_addr[5]);
    activeInstance->onMasterLinked();
  }

  if ((type == PacketType::HELLO || type == PacketType::HEARTBEAT) && validBeacon) {
    portENTER_CRITICAL(&activeInstance->stateMux);
    activeInstance->lastMasterSeenMs = millis();
    activeInstance->scanChannel = WiFi.channel();
    portEXIT_CRITICAL(&activeInstance->stateMux);
  }

  switch (type) {
    case PacketType::HEARTBEAT: {
      app::espnow::state_binary::SlaveAliveState alive = {};
      app::espnow::state_binary::initHeader(alive.header, app::espnow::state_binary::Type::SlaveAlive);
      activeInstance->sendStateBinary(&alive, sizeof(alive));
      break;
    }
    case PacketType::COMMAND:
      if (payloadSize > 0) {
        activeInstance->handleCommandPayload(payload, payloadSize);
      }
      break;
    default:
      break;
  }
}

}  // namespace app::espnow
