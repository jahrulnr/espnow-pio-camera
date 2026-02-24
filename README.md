# ESP-NOW Camera Slave (ESP32-CAM)

An ESP32-CAM based ESP-NOW slave node. This project provides a foundation for capturing JPEG frames and sending them via ESP-NOW in chunked binary messages.

Status
------

- Channel scan and master lock are implemented.
- Identity handshake (`IdentityState`) and heartbeat (`SlaveAliveState`) are supported.
- Camera capture (JPEG) and chunked transmission (`CameraMetaState`, `CameraChunkState`) are implemented.
- The master repository includes handlers for `CameraMeta` and `CameraChunk` to enable end-to-end integration.

Main files
----------

- `src/main.cpp` — application bootstrap
- `src/app/espnow/slave.cpp` — ESP-NOW slave logic (scan, lock, send state, stream chunks)
- `src/app/espnow/state_binary.h` — camera wire-format definitions
- `src/app/camera/camera_capture.cpp` — camera initialization and capture
- `include/app_config.h` — device configuration and camera pin mapping

Configuration
-------------

Edit `include/app_config.h` to set:

- `DEVICE_NAME`
- Capture and transfer parameters: `CAM_FRAME_INTERVAL_MS`, `CAM_MAX_FRAME_BYTES`, `CAM_CHUNK_DELAY_MS`
- JPEG quality: `CAM_JPEG_QUALITY`
- Camera pin mapping for AI-Thinker ESP32-CAM: `CAM_PIN_*`

Build & upload
--------------

```bash
platformio run -e esp32cam
platformio run -e esp32cam -t upload --upload-port /dev/ttyUSB0
platformio device monitor -e esp32cam --port /dev/ttyUSB0
```

Notes / Next steps
------------------

- The current focus is improving reassembly performance, storage/forward reliability, and error handling on both the slave and master sides.

Related repositories
--------------------

- ESP-NOW Master (gateway): https://github.com/jahrulnr/espnow-pio-master.git
- ESP-NOW Weather (slave/proxy client): https://github.com/jahrulnr/espnow-pio-weather.git
- ESP-NOW Cam (this repo): https://github.com/jahrulnr/espnow-pio-camera.git
