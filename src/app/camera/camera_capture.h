#pragma once

#include <Arduino.h>
#include <esp_camera.h>

namespace app::camera {

struct CaptureFrame {
  uint32_t frameId = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint8_t format = 0;
  camera_fb_t* fb = nullptr;
};

class CameraCapture {
 public:
  bool begin();
  bool capture(CaptureFrame& out);
  void release(CaptureFrame& frame);
  bool isReady() const { return started; }

 private:
  bool started = false;
  uint32_t nextFrameId = 1;
};

extern CameraCapture cameraCapture;

}  // namespace app::camera
