# Copilot instructions for this repository

## Project shape (ESP32-CAM / PlatformIO)
- This is a PlatformIO + Arduino project for ESP32-CAM (`esp32cam` environment in `platformio.ini`).
- Runtime entrypoint is `src/main.cpp`:
  - `init()` disables timer WDT panic and initializes NVS.
  - `setup()` mounts LittleFS, enables PSRAM allocator (if available), and starts ESP-NOW slave.
  - `loop()` runs `espnowSlave.loop()` continuously.
- Current scope is camera slave-side preparation; master integration is out of scope for now.

## Core modules and boundaries
- `src/core/wdt.h`: low-level watchdog handling (`esp_panic_handler_disable_timg_wdts`) and task-WDT reconfigure.
- `src/core/nvs.h`: NVS init.
- `src/app/espnow/slave.cpp`: ESP-NOW channel scan/lock, master handshake, state sending.
- `src/app/camera/camera_capture.cpp`: ESP32-CAM initialization and JPEG frame capture.
- `src/app/espnow/state_binary.h`: binary wire structs for camera metadata/chunks.

## Board/config conventions
- Device constants are in `include/app_config.h`.
- Camera pin mapping is configured via `CAM_PIN_*` macros.
- Keep secrets in `include/secret.h` (gitignored by convention in sibling projects).

## Coding patterns to follow here
- Prefer ESP-IDF logging macros (`ESP_LOGI/W/E/D`) over `Serial.print`.
- Keep ESP-NOW wire structs backward-compatible once consumed by master.
- Keep runtime lightweight in callbacks; avoid heavy work inside RX callback.

## Developer workflows
- Build: `platformio run --environment esp32cam`
- Upload: `platformio run --environment esp32cam --target upload`
- Serial monitor: `platformio device monitor --environment esp32cam`

## Notes for AI agents
- Validate changes by building `esp32cam` environment.
- Do not edit generated VS Code files manually.
