#include <Arduino.h>
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
char consoleLine[24] = {};
size_t consoleLineLength = 0;
char selectedColor[16] = "RED";
bool targetValid = false;
int targetErrorPct = 0;
unsigned long lastVisionAt = 0;
unsigned long lastDistanceAt = 0;
unsigned long lastColorSentAt = 0;
float distanceCm = -1;
bool colorAnnounced = false;

bool supportedColor(const char* text) {
  const char* choices[] = {"RED", "ORANGE", "YELLOW", "GREEN", "BLUE", "PURPLE"};
  for (const char* choice : choices) if (strcmp(text, choice) == 0) return true;
  return false;
}

void acceptCameraLine(const char* line) {
  unsigned long sequence;
  char color[16] = {};
  int valid, error, area;
  int fields = sscanf(line, "V1,%lu,%15[^,],%d,%d,%d", &sequence, color,
                      &valid, &error, &area);
  if (fields != 5 || strcmp(color, selectedColor) != 0 ||
      (valid != 0 && valid != 1) || error < -100 || error > 100 ||
      area < 0 || area > 1000) return;
  targetValid = valid == 1;
  targetErrorPct = error;
  lastVisionAt = millis();
  if (targetValid && !colorAnnounced) {
    Serial.printf("FLAG COLOR: %s\n", color);
    colorAnnounced = true;
  }
}

void readCamera() {
  while (CameraLink.available()) {
    char character = static_cast<char>(CameraLink.read());
    if (character == '\n') {
      cameraLine[cameraLineLength] = '\0';
      acceptCameraLine(cameraLine);
      cameraLineLength = 0;
    } else if (character != '\r' && cameraLineLength < sizeof(cameraLine) - 1) {
      cameraLine[cameraLineLength++] = character;
    } else if (character != '\r') {
      cameraLineLength = 0;
    }
  }
}

void readConsole() {
  while (Serial.available()) {
    char character = static_cast<char>(Serial.read());
    if (character == '\n') {
      consoleLine[consoleLineLength] = '\0';
      if (supportedColor(consoleLine)) {
        strcpy(selectedColor, consoleLine);
        targetValid = false;
        colorAnnounced = false;
        CameraLink.printf("C,%s\n", selectedColor);
        Serial.printf("Selected color: %s\n", selectedColor);
      } else {
        Serial.println("Choose RED, ORANGE, YELLOW, GREEN, BLUE, or PURPLE");
      }
      consoleLineLength = 0;
    } else if (character != '\r' && consoleLineLength < sizeof(consoleLine) - 1) {
      consoleLine[consoleLineLength++] = character;
    } else if (character != '\r') {
      consoleLineLength = 0;
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
    strcpy(previous, motion);
  }
}

void setup() {
  Serial.begin(115200);
  CameraLink.begin(115200, SERIAL_8N1, kCameraRx, kCameraTx);
  pinMode(kUltrasonicTrig, OUTPUT);
  pinMode(kUltrasonicEcho, INPUT);
  digitalWrite(kUltrasonicTrig, LOW);
  Serial.println("Main ESP32 ready. Motors disabled until Control integration.");
  CameraLink.printf("C,%s\n", selectedColor);
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
