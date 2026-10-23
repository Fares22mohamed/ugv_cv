# ESP32-CAM live preview and color detection

This sketch adds an HTTP live preview while retaining onboard color detection and the UART result sent to the main ESP32.

## Before uploading

1. Confirm the board is the AI Thinker ESP32-CAM model. The pin map in the sketch is for that model.
2. Install Espressif's `esp32` board package in Arduino IDE.
3. Open `esp32_cam_vision.ino`.
4. Replace `WIFI_SSID` and `WIFI_PASSWORD` with your 2.4 GHz Wi-Fi credentials. If you want to test without a router, the sketch falls back to its setup hotspot.
5. Change the development default `AP_PASSWORD` of `12345678` before deploying outside a controlled test.
6. Select the correct AI Thinker ESP32-CAM board option and the USB programmer's COM port, then upload.

## Open the live video on Windows

1. Open **Tools → Serial Monitor** and select **115200 baud**.
2. Wait for the camera to initialize. The sketch prints the web-page IP address.
3. Connect the laptop to the same Wi-Fi network as the camera.
4. Open `http://CAMERA_IP/` in a browser, replacing `CAMERA_IP` with the printed IP. The image should update automatically.
5. If the router connection failed, connect the laptop to the `ESP32-CAM` Wi-Fi hotspot and open `http://192.168.4.1/`.

The single-photo link opens `/capture`; the MJPEG endpoint is `http://CAMERA_IP:81/stream`.

## Detection and UART

The camera starts in AUTO mode and can detect red, orange, yellow, green, cyan, blue, or purple by the current preset thresholds. The main board can send `C,AUTO\n` or `C,RED\n` (or another supported color) over UART1 at 115200 baud, 8N1. Camera TX GPIO14 connects to main-board RX GPIO16; camera RX GPIO13 connects to main-board TX GPIO17; connect both grounds.

The camera sends `FLAG,centerX,160,1.0,COLOR\n` for an accepted target, or `NONE\n` when there is no target. See [PROTOCOL.md](PROTOCOL.md) for the message contract and caveats.

## Network safety and limitations

The page and stream use HTTP without login or encryption. Keep them on a trusted local network; do not expose them directly to the public internet. The JPEG buffer is capped at 40 KiB per frame, so a frame that compresses to a larger JPEG is skipped for the browser while detection continues. The preview is intended for setup/debugging, not security-sensitive video transport.

The code change still requires an Arduino compile and a real-board test. Host-side protocol tests do not validate camera wiring, frame format, PSRAM availability, Wi-Fi performance, or UART electrical connections.
