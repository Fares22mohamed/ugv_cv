#include <Arduino.h>
#include <math.h>
#include "esp_camera.h"
#include "vision_core.h"

// Compile-time default target. Change to vision::ORANGE, vision::YELLOW,
// vision::GREEN, vision::BLUE, or vision::PURPLE as needed before uploading.
#ifndef TARGET_COLOR
#define TARGET_COLOR vision::RED
#endif

// AI Thinker ESP32-CAM pin map. Keep this mapping for the AI Thinker board.
constexpr int kPwdn = 32, kReset = -1, kXclk = 0;
constexpr int kSiod = 26, kSioc = 27;
constexpr int kY2 = 5, kY3 = 18, kY4 = 19, kY5 = 21;
constexpr int kY6 = 36, kY7 = 39, kY8 = 34, kY9 = 35;
constexpr int kVsync = 25, kHref = 23, kPclk = 22;

// UART1 to main Control ESP32. GPIO13/14 are shared with microSD; do not
// use the microSD interface while these UART pins are assigned.
constexpr int kLinkRx = 13;
constexpr int kLinkTx = 14;
constexpr int kImageWidth = 160;
constexpr unsigned long kFrameIntervalMs = 100;

HardwareSerial Link(1);
vision::Color selectedColor = TARGET_COLOR;
char command[24] = {};
size_t commandLength = 0;
bool commandOverflow = false;

void sendNone() {
  // Protocol output only: no debug text is ever written to this UART.
  Link.println("NONE");
}

void acceptCommand(const char* line) {
  if (line[0] != 'C' || line[1] != ',') return;
  vision::Color next;
  if (vision::parseColor(line + 2, next)) {
    selectedColor = next;
    Serial.printf("Selected target color: %s\n", vision::colorName(selectedColor));
  }
}

void readCommands() {
  while (Link.available()) {
    const char character = static_cast<char>(Link.read());
    if (character == '\n') {
      if (!commandOverflow) {
        command[commandLength] = '\0';
        acceptCommand(command);
      }
      commandLength = 0;
      commandOverflow = false;
    } else if (character != '\r') {
      if (commandLength < sizeof(command) - 1) {
        command[commandLength++] = character;
      } else {
        // Discard the entire oversized command through its newline.
        commandLength = 0;
        commandOverflow = true;
      }
    }
  }
}

int centerPixelFromError(int errorPct) {
  // Existing detector: negative means left; positive means right.
  long centerX = lroundf(80.0f + static_cast<float>(errorPct) * 0.8f);
  if (centerX < 0) centerX = 0;
  if (centerX >= kImageWidth) centerX = kImageWidth - 1;
  return static_cast<int>(centerX);
}

void sendResult(const vision::Result& result) {
  if (!result.valid) {
    sendNone();
    return;
  }

  const int centerX = centerPixelFromError(result.errorPct);
  // The detector provides a thresholded valid/invalid result, not calibrated
  // probability. 1.0 is a binary acceptance marker required by the protocol.
  Link.printf("FLAG,%d,%d,1.0,%s\n",
              centerX, kImageWidth, vision::colorName(selectedColor));
}

void setup() {
  Serial.begin(115200);
  Link.begin(115200, SERIAL_8N1, kLinkRx, kLinkTx);

  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = kY2;
  config.pin_d1 = kY3;
  config.pin_d2 = kY4;
  config.pin_d3 = kY5;
  config.pin_d4 = kY6;
  config.pin_d5 = kY7;
  config.pin_d6 = kY8;
  config.pin_d7 = kY9;
  config.pin_xclk = kXclk;
  config.pin_pclk = kPclk;
  config.pin_vsync = kVsync;
  config.pin_href = kHref;
  config.pin_sccb_sda = kSiod;
  config.pin_sccb_scl = kSioc;
  config.pin_pwdn = kPwdn;
  config.pin_reset = kReset;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size = FRAMESIZE_QQVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  const esp_err_t status = esp_camera_init(&config);
  if (status != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x; no valid target messages will be sent.\n",
                  status);
    // Leave the camera unavailable and rely on the main controller's stale-data
    // timeout to stop motion. Never fabricate a FLAG on initialization failure.
    while (true) delay(1000);
  }

  Serial.printf("Camera ready. Default target color: %s\n",
                vision::colorName(selectedColor));
  Serial.println("UART protocol: FLAG,centerX,160,1.0,COLOR or NONE; 115200 8N1.");
  Serial.println("Confidence 1.0 means detector accepted the candidate; it is not a probability.");
}

void loop() {
  readCommands();

  camera_fb_t* frame = esp_camera_fb_get();
  if (!frame) {
    Serial.println("Frame capture failed; sending NONE.");
    sendNone();
    delay(kFrameIntervalMs);
    return;
  }

  vision::Result result = {false, 0, 0};
  if (frame->format == PIXFORMAT_RGB565 &&
      frame->width == kImageWidth &&
      frame->height == 120 &&
      frame->len >= static_cast<size_t>(kImageWidth * 120 * 2)) {
    result = vision::detectRGB565(frame->buf, frame->len, selectedColor);
  } else {
    Serial.printf("Invalid frame: format=%d width=%d height=%d length=%u; sending NONE.\n",
                  static_cast<int>(frame->format), frame->width, frame->height,
                  static_cast<unsigned int>(frame->len));
  }

  esp_camera_fb_return(frame);
  sendResult(result);

  Serial.printf("%s: valid=%d, error=%d%%, area=%d/1000\n",
                vision::colorName(selectedColor), result.valid ? 1 : 0,
                result.errorPct, result.areaPermille);
  delay(kFrameIntervalMs);
}
