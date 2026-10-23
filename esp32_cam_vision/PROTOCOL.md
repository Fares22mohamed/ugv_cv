# ESP32-CAM UART and live-stream integration

## Camera UART contract

The ESP32-CAM sends a newline-terminated message for each processed frame over UART1 at **115200 baud, 8N1**:

- Valid target: `FLAG,centerX,160,1.0,COLOR\n`
- No target, invalid frame, or failed frame capture: `NONE\n`

Examples:

```text
FLAG,40,160,1.0,RED
FLAG,80,160,1.0,RED
FLAG,120,160,1.0,RED
NONE
```

`centerX` is clamped to 0–159. The camera estimates it with `round(80 + errorPct * 0.8)`; a flag on the left yields a smaller value than a flag on the right. The `1.0` field is a binary accepted/not-accepted marker, **not** a calibrated confidence probability.

The camera starts in **AUTO** mode, detecting the largest connected region in any supported preset color. The main controller can send `C,AUTO\n` to keep AUTO enabled or `C,COLOR\n` to lock the camera to one color. Supported colors are RED, ORANGE, YELLOW, GREEN, CYAN, BLUE, and PURPLE.

The main ESP32 bridge accepts this `FLAG` protocol, checks the selected color unless AUTO is active, and maps `centerX` back to its left/right steering error. `NONE` immediately invalidates the target. Missing, invalid, or stale vision data keeps the starter motion decision at STOP.

## Live video setup

The camera serves a browser page and a multipart JPEG stream:

- Main page: `http://CAMERA_IP/` (TCP port 80)
- Live stream: `http://CAMERA_IP:81/stream`
- Single photo: `http://CAMERA_IP/capture`

Before uploading, edit `WIFI_SSID` and `WIFI_PASSWORD` in `esp32_cam_vision.ino` to use a **2.4 GHz** Wi-Fi network. If connection fails, the camera starts the `ESP32-CAM` setup hotspot using the configured `AP_PASSWORD`; the default in this development sketch is `12345678`. Change that password before using the hotspot outside a controlled test.

Open the Serial Monitor at **115200 baud** and copy the IP printed after startup. Connect the laptop to the same Wi-Fi network and browse to that IP. If the fallback hotspot is active, connect the laptop to `ESP32-CAM` and browse to `http://192.168.4.1/`. Depending on the laptop and network, joining the hotspot may temporarily disconnect it from the internet.

The HTTP stream is for trusted local testing. It has no authentication or TLS; do not port-forward it or expose it directly to the public internet.

## Wiring

| Signal | ESP32-CAM (AI Thinker assumed) | Main Control ESP32 |
|---|---|---|
| Camera UART TX | GPIO14 | GPIO16 (RX) |
| Camera UART RX | GPIO13 | GPIO17 (TX) |
| Ground | GND | GND |

Cross TX to RX, not TX to TX. Both boards require suitable power and a common ground. GPIO13/14 share the camera board's microSD interface; do not use microSD while those pins are assigned to UART. Confirm the physical board pinout and wiring before power-up.

## Host-side contract tests

From the repository root, run:

```powershell
python -m unittest prototype.test_uart_protocol -v
```

These tests validate the documented message shape, coordinate conversion, accepted color names, newline handling, and rejection of the previous V1 format. They do not compile the Arduino sketches, verify real UART timing, validate camera pixel byte order, or replace physical-board tests.
