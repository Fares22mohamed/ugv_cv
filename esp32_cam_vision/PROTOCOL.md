# ESP32-CAM UART protocol integration

## Camera firmware contract

The ESP32-CAM sends one newline-terminated message per processed frame over UART1 at **115200 baud, 8N1**:

- Valid target: `FLAG,centerX,160,1.0,COLOR\n`
- No target, invalid frame, or failed frame capture: `NONE\n`

Examples:

```text
FLAG,40,160,1.0,RED
FLAG,80,160,1.0,RED
FLAG,120,160,1.0,RED
NONE
```

`centerX` is clamped to 0–159. The preserved detector's negative horizontal error means left, approximately zero means center, and positive means right. Firmware conversion is `round(80.0 + errorPct * 0.8)`.

The detector does **not** produce a calibrated confidence probability. The required `1.0` field is a binary acceptance marker used only when the existing detector reports `valid == true`.

## Target color

The default is RED. In `esp32_cam_vision.ino`, change:

```cpp
#define TARGET_COLOR vision::RED
```

to one of:

- `vision::ORANGE`
- `vision::YELLOW`
- `vision::GREEN`
- `vision::BLUE`
- `vision::PURPLE`

Then compile and upload the camera sketch. The camera tracks the one configured target color; it does not automatically classify arbitrary colors. The optional `C,COLOR\n` UART command remains supported, but startup does not depend on receiving one.

## Wiring

| Signal | ESP32-CAM (AI Thinker assumed) | Main Control ESP32 |
|---|---|---|
| Camera UART TX | GPIO14 | GPIO16 (RX) |
| Camera UART RX (optional color command) | GPIO13 | GPIO17 (TX) |
| Ground | GND | GND |

Cross TX to RX. Never connect TX to TX. Both boards require suitable power and a common ground. GPIO13/14 share the camera board's microSD interface; do not use microSD while they are assigned to UART. Verify the actual board pinout before wiring.

## Important integration blocker: Control parser mismatch

The current `esp32_main_bridge/esp32_main_bridge.ino` on `main` parses the old format:

```text
V1,sequence,color,valid,errorPct,areaPermille
```

It does **not** parse `FLAG,centerX,160,1.0,COLOR`. Per the scoped request, this change intentionally does not modify the Control sketch. Therefore, this branch updates the camera transmitter but is **not end-to-end compatible with the currently committed Control receiver** until the Control team updates its parser or explicitly approves another protocol. Do not claim integrated operation or connect this protocol to motion control until that mismatch is resolved and tested.

## Host-side contract tests

From the repository root, run:

```powershell
python -m unittest prototype.test_uart_protocol -v
```

These tests validate the specified protocol shape, coordinate conversion, color names, newline handling, and rejection of the old V1 format. They do not compile the Arduino sketch, test real UART timing, validate camera pixel byte order, or replace physical-board testing.
