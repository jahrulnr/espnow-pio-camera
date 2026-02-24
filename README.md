# ESP-NOW Cam Slave (ESP32-CAM)

Node slave ESP32-CAM untuk jaringan ESP-NOW. Fokus tahap ini adalah menyiapkan pondasi slave kamera; integrasi parser di sisi master dilakukan belakangan.

## Status Saat Ini

Project sudah menyiapkan:
- Channel scan + lock ke beacon master (`PIO_MASTER_V1`).
- Handshake identity (`IdentityState`) saat link pertama.
- Heartbeat response (`SlaveAliveState`).
- Capture frame JPEG dari kamera ESP32-CAM.
- Pengiriman frame sebagai state biner chunked:
  - `CameraMetaState`
  - `CameraChunkState`

## Struktur Utama

- `src/main.cpp` — bootstrap runtime.
- `src/app/espnow/slave.cpp` — logic slave ESP-NOW (scan, lock, send state, stream chunk).
- `src/app/espnow/state_binary.h` — wire format biner untuk camera slave.
- `src/app/camera/camera_capture.cpp` — init/capture kamera (`esp_camera`).
- `include/app_config.h` — konfigurasi device, pin kamera, interval frame.

## Konfigurasi

Edit `include/app_config.h` sesuai board:
- Device: `DEVICE_NAME`
- Interval/limit kirim: `CAM_FRAME_INTERVAL_MS`, `CAM_MAX_FRAME_BYTES`, `CAM_CHUNK_DELAY_MS`
- Quality: `CAM_JPEG_QUALITY`
- Pin map AI Thinker ESP32-CAM: `CAM_PIN_*`

## Build & Upload

```bash
platformio run -e esp32cam
platformio run -e esp32cam -t upload --upload-port /dev/ttyUSB0
platformio device monitor -e esp32cam --port /dev/ttyUSB0
```

## Catatan

- Slave ini sudah bisa kirim data kamera via ESP-NOW, tetapi master saat ini belum punya handler `CameraMeta/CameraChunk`.
- Tahap berikutnya adalah menambahkan reassembly + storage/forward pipeline di master.

## Related repositories

- ESP-NOW Master (gateway): https://github.com/jahrulnr/espnow-pio-master.git
- ESP-NOW Weather (slave/proxy client): https://github.com/jahrulnr/espnow-pio-weather.git
- ESP-NOW Cam (this repo): https://github.com/jahrulnr/espnow-pio-camera.git
