#include "camera_capture.h"

#include <app_config.h>
#include <esp_log.h>

namespace app::camera {

static constexpr const char* TAG = "cam_capture";
CameraCapture cameraCapture;

bool CameraCapture::begin() {
  if (started) {
    return true;
  }

  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAM_PIN_Y2;
  config.pin_d1 = CAM_PIN_Y3;
  config.pin_d2 = CAM_PIN_Y4;
  config.pin_d3 = CAM_PIN_Y5;
  config.pin_d4 = CAM_PIN_Y6;
  config.pin_d5 = CAM_PIN_Y7;
  config.pin_d6 = CAM_PIN_Y8;
  config.pin_d7 = CAM_PIN_Y9;
  config.pin_xclk = CAM_PIN_XCLK;
  config.pin_pclk = CAM_PIN_PCLK;
  config.pin_vsync = CAM_PIN_VSYNC;
  config.pin_href = CAM_PIN_HREF;
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;
  config.pin_pwdn = CAM_PIN_PWDN;
  config.pin_reset = CAM_PIN_RESET;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = CAM_JPEG_QUALITY;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = CAM_JPEG_QUALITY;
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }

  const esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(err));
    return false;
  }

  started = true;
  ESP_LOGI(TAG, "Camera ready");
  return true;
}

bool CameraCapture::capture(CaptureFrame& out) {
  out = CaptureFrame{};

  if (!started) {
    return false;
  }

  camera_fb_t* frameBuffer = esp_camera_fb_get();
  if (frameBuffer == nullptr) {
    ESP_LOGW(TAG, "Capture failed: fb null");
    return false;
  }

  out.frameId = nextFrameId++;
  out.width = static_cast<uint16_t>(frameBuffer->width);
  out.height = static_cast<uint16_t>(frameBuffer->height);
  out.format = static_cast<uint8_t>(frameBuffer->format);
  out.fb = frameBuffer;
  return true;
}

void CameraCapture::release(CaptureFrame& frame) {
  if (frame.fb != nullptr) {
    esp_camera_fb_return(frame.fb);
  }
  frame = CaptureFrame{};
}

}  // namespace app::camera
