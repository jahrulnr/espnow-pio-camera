#pragma once

#define DEVICE_NAME "pio-cam"

#define CAM_FRAME_INTERVAL_MS 50
#define CAM_MAX_FRAME_BYTES 32768
#define CAM_CHUNK_DELAY_MS 2
#define CAM_CHUNK_RETRY_COUNT 2
#define CAM_SEND_ACK_TIMEOUT_MS 80
#define CAM_JPEG_QUALITY 11

#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27

#define CAM_PIN_Y9 35
#define CAM_PIN_Y8 34
#define CAM_PIN_Y7 39
#define CAM_PIN_Y6 36
#define CAM_PIN_Y5 21
#define CAM_PIN_Y4 19
#define CAM_PIN_Y3 18
#define CAM_PIN_Y2 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22
