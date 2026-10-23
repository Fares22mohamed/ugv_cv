# UGV flag vision: first working build

This project starts the **Computer Vision** work for the UGV brief. The CAD assembly reserves space for the motors, battery, ESP32-CAM, main ESP32, ultrasonic sensor, and cable holes. The assembly is a SolidWorks native file; its geometry and dimensions have not been verified here.

## What the brief requires

The battery-powered UGV must use an ESP32 as its main controller, an ESP32-CAM as its camera, and an ultrasonic sensor for distance. It must detect and announce the competition-day flag color, keep the stationary flag centered while approaching, and stop 50 cm from it. The total UGV mass must be at most 2 kg. The ESP32 is programmed using Arduino IDE. The color and flag size are announced on competition day.

The code below is a **suggested implementation**. The brief does not mandate HSV thresholds, UART, these pins, the named colors, or this motor command set.

## Folder map

- `prototype/vision.py`: laptop test. Processes an image, finds the largest region of the selected color, and reports its center. `--demo` creates sample flags so you can run it before hardware arrives.
- `esp32_cam_vision/`: Arduino IDE sketch for a common **AI Thinker ESP32-CAM**. Captures 160 x 120 RGB565 frames, detects the largest region in the selected color (or any supported color in AUTO mode), sends a compact serial result to the main ESP32, and provides a browser-based live MJPEG preview over Wi-Fi.
- `esp32_main_bridge/`: Arduino IDE integration sketch for a generic ESP32. Reads camera results and ultrasonic distance, then prints the intended motion. `applyDrive()` is deliberately a handoff to the Control team: no motor driver or pinout has been supplied.
- `prototype/test_vision.py`: synthetic-image checks for the laptop detector.
- `prototype/test_uart_protocol.py`: host-side checks for the camera UART message contract.

## Start today, without hardware

Use Python 3 with Pillow (`python -m pip install pillow`). From this folder:

```powershell
python prototype/vision.py --demo
python prototype/vision.py --image prototype/demo/red_left.png --color red --output prototype/demo/red_left_result.png
python -m unittest discover -s prototype -p test_vision.py
python -m unittest prototype.test_uart_protocol -v
```

On this computer, the Codex bundled Python already has Pillow. If `python` opens the Microsoft Store instead of running, use this PowerShell command to create the demo images:

```powershell
$py = "$env:USERPROFILE\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
& $py prototype/vision.py --demo
```

The demo prints JSON results and draws the detected bounding box. `error_pct` is negative when the flag is left of the image center, positive when right, and near zero when centered. The `area_permille` number describes how much of the sampled image belongs to the candidate; it is **not** a distance reading. The laptop prototype and onboard camera share the initial color presets, but thresholds remain starting settings rather than a promise to detect every material or lighting condition.

## Suggested two-board connection, after parts arrive

The Arduino sketches assume this tentative 3.3 V UART connection:

| Signal | AI Thinker ESP32-CAM | Main ESP32 example |
|---|---|---|
| Camera result TX | GPIO 14 | RX GPIO 16 |
| Color setting RX | GPIO 13 | TX GPIO 17 |
| Ground | GND | GND |

Both boards need suitable, stable power from Electrical. **Do not connect a 5 V ultrasonic ECHO directly to a 3.3 V ESP32 GPIO**; Electrical must confirm the sensor model and safe interface. GPIO 13/14 on the camera are shared with the microSD interface, so this example does not use microSD. The exact pin map must be checked against the purchased boards and other wiring before power-up.

On the main ESP32 example, ultrasonic TRIG=GPIO 25 and ECHO=GPIO 26. These are editable examples, not pins specified by the brief. Mount the ultrasonic sensor facing the flag and agree whether 50 cm is measured from the sensor face or another vehicle reference point.

## ESP32-CAM live video

The camera firmware provides a browser page on TCP port 80 and an MJPEG stream on TCP port 81.

1. Open `esp32_cam_vision/esp32_cam_vision.ino` in Arduino IDE and edit `WIFI_SSID` and `WIFI_PASSWORD` for a 2.4 GHz network.
2. Select the matching AI Thinker ESP32-CAM board configuration and the COM port for the USB programmer, then upload.
3. Open Serial Monitor at 115200 baud. Copy the IP address printed by the sketch.
4. Connect the Windows laptop to the same Wi-Fi network and browse to `http://CAMERA_IP/`, replacing `CAMERA_IP` with the printed address.
5. If router Wi-Fi cannot be joined, the camera creates an `ESP32-CAM` hotspot. Connect the laptop to it and open `http://192.168.4.1/`. The development default hotspot password is `12345678`; change it before any shared or field deployment.

The camera stream uses plain HTTP without authentication or TLS and is intended for trusted local testing only. Do not expose its ports directly to the public internet. The live JPEG preview and color detector use the same camera frame but serve different purposes: the preview is for people, and the detector sends its compact UART result to the main controller.

## Upload and test sequence

1. Electrical identifies both exact board models, power rails, sensor model, pin map, and motor driver. The ESP32-CAM sketch assumes AI Thinker pins; change the pin map if the purchased module differs.
2. In Arduino IDE, install the Espressif **esp32** board package. Open each sketch folder's `.ino` file and select the correct board and port.
3. Upload the camera sketch with a suitable USB-to-serial programmer. Check that camera initialization succeeds, the live page opens, and holding a supported colored object left, center, and right changes the detected horizontal error in the expected direction.
4. Upload the main sketch. With motors disconnected, verify that it prints `LEFT`, `RIGHT`, `FORWARD`, or `STOP` as expected. Send `AUTO`, `RED`, `GREEN`, etc. to the main Serial Monitor to select any-color detection or lock to one color.
5. Electrical connects the ultrasonic sensor safely. Check measured distance against a tape measure around 50 cm. A missing/invalid reading commands `STOP` in this starter code.
6. Control fills in `applyDrive()` for the actual motor driver and tunes speed and steering on the assembled robot. Test first with wheels off the ground.
7. Agree how the detected color will be **announced** in competition. This starter prints it to the serial monitor only; an audible or visible output needs hardware and a team decision.

## Data sent from camera to main controller

One newline-terminated message is sent over UART1 at 115200 baud, 8N1 for each processed frame:

```text
FLAG,42,160,1.0,RED
NONE
```

For `FLAG`, the fields are message type, horizontal center pixel (0–159), image width (160), binary acceptance marker (`1.0`, not a probability), and detected color. `NONE` means that the camera did not accept a target in that frame. The main controller sends a newline-terminated `C,AUTO` command to choose automatic color detection or `C,RED` to lock one color. The bridge validates incoming messages and the selected color; no flag, stale data, a missing ultrasonic echo, or a measured distance at/below 50 cm commands `STOP`.

## First acceptance checks

- Selected color can be changed without rewriting the detection algorithm.
- AUTO mode detects supported colors; locked mode only detects the selected color.
- Flag position is correct on left, center, and right test images.
- A larger/smaller flag remains detectable without a fixed pixel-size target.
- A frame with no selected-color flag produces `NONE` and stops the starter motion decision.
- Camera data older than 500 ms commands `STOP` on the main board.
- Missing ultrasonic echo and distances at/below 50 cm command `STOP`.
- On the assembled UGV, the team verifies announcement, centering, stopping distance, autonomy, and total mass at or below 2 kg.

## Details needed from teammates

| Team | Ask for |
|---|---|
| Electrical | Exact ESP32 and ESP32-CAM models; battery and power design; ultrasonic model and safe ECHO level; final pin/wiring diagram; motor driver model. |
| Control | Steering convention, accepted UART message, motor driver API, update rate, lost-target behavior, and who owns the 50 cm stopping logic. |
| Mechanical | CAD part files or a STEP export, camera height/angle, unobstructed camera view, ultrasonic face reference, and component masses. |

Once hardware is present, record successful and failed runs under different flag colors, sizes, distances, and lighting. Tune the thresholds from those observations and keep the settings with your test notes.

## Board documentation used for this starter

- Espressif [Arduino ESP32 camera pin map](https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/Camera/CameraWebServer/camera_pins.h)
- Espressif [ESP32 camera driver](https://github.com/espressif/esp32-camera/blob/master/README.md)
- Espressif [Arduino ESP32 UART guide](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/serial.html)
