#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// Example pin choices for a generic ESP32. Electrical must confirm them.
constexpr int kCameraRx = 16;
constexpr int kCameraTx = 17;
constexpr int kUltrasonicTrig = 25;
constexpr int kUltrasonicEcho = 26;
constexpr float kStopDistanceCm = 50.0f;
constexpr int kCenterTolerancePct = 12;
constexpr unsigned long kVisionTimeoutMs = 500;

HardwareSerial CameraLink(2);
char cameraLine[80] = {};
size_t cameraLineLength = 0;
bool cameraLineOverflow = false;
char consoleLine[24] = {};
size_t consoleLineLength = 0;
bool consoleLineOverflow = false;
char selectedColor[16] = "AUTO";
char lastAnnouncedColor[16] = {};
bool targetValid = false;
int targetErrorPct = 0;
unsigned long lastVisionAt = 0;
unsigned long lastDistanceAt = 0;
unsigned long lastColorSentAt = 0;
float distanceCm = -1;

bool supportedColor(const char* text) {
  const char* choices[] = {"RED", "ORANGE", "YELLOW", "GREEN", "CYAN", "BLUE", "PURPLE"};
  for (const char* choice : choices) {
    if (strcmp(text, choice) == 0) return true;
  }
  return false;
}

bool supportedSelection(const char* text) {
  return strcmp(text, "AUTO") == 0 || supportedColor(text);
}

void acceptCameraLine(const char* line) {
  if (strcmp(line, "NONE") == 0) {
    targetValid = false;
    lastVisionAt = millis();
    return;
  }

  int centerX = -1;
  int imageWidth = 0;
  char confidence[8] = {};
  char color[16] = {};
  int fields = sscanf(line, "FLAG,%d,%d,%7[^,],%15s",
                      &centerX, &imageWidth, confidence, color);
  if (fields != 4 || centerX < 0 || centerX >= 160 || imageWidth != 160 ||
      strcmp(confidence, "1.0") != 0 || !supportedColor(color)) {
    return;
  }

  // In AUTO mode accept whichever supported color the camera identifies.
  if (strcmp(selectedColor, "AUTO") != 0 && strcmp(color, selectedColor) != 0) {
    return;
  }

  targetValid = true;
  // Invert the camera mapping: centerX ~= 80 + errorPct * 0.8.
  targetErrorPct = static_cast<int>(lroundf((centerX - 80) * 1.25f));
  if (targetErrorPct < -100) targetErrorPct = -100;
  if (targetErrorPct > 100) targetErrorPct = 100;
  lastVisionAt = millis();

  if (strcmp(lastAnnouncedColor, color) != 0) {
    Serial.printf("FLAG COLOR: %s\n", color);
    strncpy(lastAnnouncedColor, color, sizeof(lastAnnouncedColor) - 1);
    lastAnnouncedColor[sizeof(lastAnnouncedColor) - 1] = '\0';
  }
}

void readCamera() {
  while (CameraLink.available()) {
    char character = static_cast<char>(CameraLink.read());
    if (character == '\n') {
      if (!cameraLineOverflow) {
        cameraLine[cameraLineLength] = '\0';
        acceptCameraLine(cameraLine);
      }
      cameraLineLength = 0;
      cameraLineOverflow = false;
    } else if (character != '\r') {
      if (!cameraLineOverflow && cameraLineLength < sizeof(cameraLine) - 1) {
        cameraLine[cameraLineLength++] = character;
      } else {
        cameraLineLength = 0;
        cameraLineOverflow = true;
      }
    }
  }
}

void readConsole() {
  while (Serial.available()) {
    char character = static_cast<char>(Serial.read());
    if (character == '\n') {
      if (!consoleLineOverflow) {
        consoleLine[consoleLineLength] = '\0';
        if (supportedSelection(consoleLine)) {
          strncpy(selectedColor, consoleLine, sizeof(selectedColor) - 1);
          selectedColor[sizeof(selectedColor) - 1] = '\0';
          targetValid = false;
          lastAnnouncedColor[0] = '\0';
          CameraLink.printf("C,%s\n", selectedColor);
          Serial.printf("Selected camera mode: %s\n", selectedColor);
        } else {
          Serial.println("Choose AUTO, RED, ORANGE, YELLOW, GREEN, CYAN, BLUE, or PURPLE");
        }
      }
      consoleLineLength = 0;
      consoleLineOverflow = false;
    } else if (character != '\r') {
      if (!consoleLineOverflow && consoleLineLength < sizeof(consoleLine) - 1) {
        consoleLine[consoleLineLength++] = character;
      } else {
        consoleLineLength = 0;
        consoleLineOverflow = true;
      }
    }
  }
}

float readDistanceCm() {
  digitalWrite(kUltrasonicTrig, LOW);
  delayMicroseconds(2);
  digitalWrite(kUltrasonicTrig, HIGH);
  delayMicroseconds(10);
  digitalWrite(kUltrasonicTrig, LOW);
  unsigned long echoUs = pulseIn(kUltrasonicEcho, HIGH, 30000);
  if (echoUs == 0) return -1;
  return echoUs / 58.0f;
}

const char* chooseMotion() {
  if (distanceCm < 0 || distanceCm <= kStopDistanceCm) return "STOP";
  if (lastVisionAt == 0 || millis() - lastVisionAt > kVisionTimeoutMs ||
      !targetValid) return "STOP";
  if (targetErrorPct < -kCenterTolerancePct) return "LEFT";
  if (targetErrorPct > kCenterTolerancePct) return "RIGHT";
  return "FORWARD";
}

void applyDrive(const char* motion) {
  // Control team: translate this intent into motor-driver commands and speed limits.
  // This starter does not actuate motors.
  static char previous[10] = {};
  if (strcmp(previous, motion) != 0) {
    Serial.printf("MOTION: %s, distance=%.1f cm, error=%d%%\n",
                  motion, distanceCm, targetErrorPct);
    strncpy(previous, motion, sizeof(previous) - 1);
    previous[sizeof(previous) - 1] = '\0';
  }
}

void setup() {
  Serial.begin(115200);
  CameraLink.begin(115200, SERIAL_8N1, kCameraRx, kCameraTx);
  pinMode(kUltrasonicTrig, OUTPUT);
  pinMode(kUltrasonicEcho, INPUT);
  digitalWrite(kUltrasonicTrig, LOW);
  Serial.println("Main ESP32 ready. Motors disabled until Control integration.");
  Serial.println("Selection: AUTO or one supported color. Motors remain a no-op.");
  CameraLink.println("C,AUTO");
}

void loop() {
  readCamera();
  readConsole();

  unsigned long now = millis();
  if (now - lastColorSentAt >= 2000) {
    CameraLink.printf("C,%s\n", selectedColor);
    lastColorSentAt = now;
  }
  if (now - lastDistanceAt >= 80) {
    distanceCm = readDistanceCm();
    lastDistanceAt = millis();
  }
  applyDrive(chooseMotion());
  delay(5);
}
