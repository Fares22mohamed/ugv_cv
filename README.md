# UGV flag vision: first working build

This project starts the **Computer Vision** work for the UGV brief. Your CAD
assembly already reserves space for the motors, battery, ESP32-CAM, main ESP32,
ultrasonic sensor, and cable holes. The assembly is a SolidWorks native file;
its geometry and dimensions have not been verified here.

## What the brief requires

The battery-powered UGV must use an ESP32 as its main controller, an ESP32-CAM
as its camera, and an ultrasonic sensor for distance. It must detect and
announce the competition-day flag color, keep the stationary flag centered
while approaching, and stop 50 cm from it. The total UGV mass must be at most
2 kg. The ESP32 is programmed using Arduino IDE. The color and flag size are
announced on competition day.

The code below is a **suggested implementation**. The brief does not mandate
HSV thresholds, UART, these pins, the named colors, or this motor command set.

## Folder map

- `prototype/vision.py`: laptop test. Processes an image, finds the largest
  region of the selected color, and reports its center. `--demo` creates
  sample flags so you can run it before hardware arrives.
- `esp32_cam_vision/`: Arduino IDE sketch for a common **AI Thinker ESP32-CAM**.
  Captures 160 x 120 RGB565 frames, looks for a selected color, and sends a
  compact serial result to the main ESP32.
- `esp32_main_bridge/`: Arduino IDE integration sketch for a generic ESP32.
  Reads camera results and ultrasonic distance, then prints the intended
  motion. `applyDrive()` is deliberately a handoff to the Control team: no
  motor driver or pinout has been supplied.
- `prototype/test_vision.py`: synthetic-image checks for the laptop detector.

## Start today, without hardware

Use Python 3 with Pillow (`python -m pip install pillow`). From this folder:

```powershell
python prototype/vision.py --demo
python prototype/vision.py --image prototype/demo/red_left.png --color red --output prototype/demo/red_left_result.png
python -m unittest discover -s prototype -p test_vision.py
```

On this computer, the Codex bundled Python already has Pillow. If `python`
opens the Microsoft Store instead of running, use this PowerShell command to
create the demo images:

```powershell
$py = "$env:USERPROFILE\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
& $py prototype/vision.py --demo
```

The demo prints JSON results and draws the detected bounding box. `error_pct`
is negative when the flag is left of the image center, positive when right,
and near zero when centered. The `area_permille` number describes how much of
the sampled image belongs to the candidate; it is **not** a distance reading.
The software accepts red, green, blue, yellow, orange, and purple as initial
presets. These are starting settings, not a promise to detect every material
or lighting condition. New colors need a tested threshold before the run.

## Suggested two-board connection, after parts arrive

The Arduino sketches assume this tentative 3.3 V UART connection:

| Signal | AI Thinker ESP32-CAM | Main ESP32 example |
|---|---|---|
| Camera result TX | GPIO 14 | RX GPIO 16 |
| Color setting RX | GPIO 13 | TX GPIO 17 |
| Ground | GND | GND |

Both boards need suitable, stable power from Electrical. **Do not connect a
5 V ultrasonic ECHO directly to a 3.3 V ESP32 GPIO**; Electrical must confirm
the sensor model and safe interface. GPIO 13/14 on the camera are shared with
the microSD interface, so this example does not use microSD. The exact pin map
must be checked against the purchased boards and other wiring before power-up.

On the main ESP32 example, ultrasonic TRIG=GPIO 25 and ECHO=GPIO 26. These
are editable examples, not pins specified by the brief. Mount the ultrasonic
sensor facing the flag and agree whether 50 cm is measured from the sensor
face or another vehicle reference point.

## Upload and test sequence

1. Electrical identifies both exact board models, power rails, sensor model,
   pin map, and motor driver. The ESP32-CAM sketch assumes AI Thinker pins;
   change the pin map if the purchased module differs.
2. In Arduino IDE, install the Espressif **esp32** board package. Open each
   sketch folder's `.ino` file and select the correct board and port.
3. Upload the camera sketch with a suitable USB-to-serial programmer. Check
   that camera initialization succeeds and that holding a colored object left,
   center, and right changes `error_pct` in the expected direction.
4. Upload the main sketch. With motors disconnected, verify that it prints
   `LEFT`, `RIGHT`, `FORWARD`, or `STOP` as expected. Send `RED`, `GREEN`, etc.
   to the main Serial Monitor to change the selected color; it forwards the
   setting to the camera board.
5. Electrical connects the ultrasonic sensor safely. Check measured distance
   against a tape measure around 50 cm. A missing/invalid reading commands
   `STOP` in this starter code.
6. Control fills in `applyDrive()` for the actual motor driver and tunes speed
   and steering on the assembled robot. Test first with wheels off the ground.
7. Agree how the detected color will be **announced** in competition. This
   starter prints it to the serial monitor only; an audible or visible output
   needs hardware and a team decision.

## Data sent from camera to main controller

One line per analyzed frame:

```text
V1,42,RED,1,-23,37
```

Fields: protocol version, sequence number, selected color, valid detection
(1/0), horizontal error in percent (-100 to +100), and candidate area in
parts per thousand (0 to 1000). The main controller sends `C,RED\n` to choose
a color. This UART format is a team proposal. Share it with Control before
integration. No flag or stale message means the main sketch commands `STOP`.
The ultrasonic reading alone decides whether the UGV has reached 50 cm.

## First acceptance checks

- Selected color can be changed without rewriting the detection algorithm.
- Flag position is correct on left, center, and right test images.
- A larger/smaller flag remains detectable without a fixed pixel-size target.
- A frame with no selected-color flag produces `valid=0`.
- Camera data older than 500 ms commands `STOP` on the main board.
- Missing ultrasonic echo and distances at/below 50 cm command `STOP`.
- On the assembled UGV, the team verifies announcement, centering, stopping
  distance, autonomy, and total mass at or below 2 kg.

## Details needed from teammates

| Team | Ask for |
|---|---|
| Electrical | Exact ESP32 and ESP32-CAM models; battery and power design; ultrasonic model and safe ECHO level; final pin/wiring diagram; motor driver model. |
| Control | Steering convention, accepted UART message, motor driver API, update rate, lost-target behavior, and who owns the 50 cm stopping logic. |
| Mechanical | CAD part files or a STEP export, camera height/angle, unobstructed camera view, ultrasonic face reference, and component masses. |

Once hardware is present, record successful and failed runs under different
flag colors, sizes, distances, and lighting. Tune the thresholds from those
observations and keep the settings with your test notes.

## Board documentation used for this starter

- Espressif [Arduino ESP32 camera pin map](https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/Camera/CameraWebServer/camera_pins.h)
- Espressif [ESP32 camera driver](https://github.com/espressif/esp32-camera/blob/master/README.md)
- Espressif [Arduino ESP32 UART guide](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/serial.html)
"# ugv_cv" 
"# ugv_cv" 
