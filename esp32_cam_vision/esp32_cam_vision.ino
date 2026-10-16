#include <Arduino.h>
#include "esp_camera.h"
#include "vision_core.h"

// AI Thinker ESP32-CAM pin map. Verify the exact board before uploading.
constexpr int kPwdn = 32, kReset = -1, kXclk = 0;
constexpr int kSiod = 26, kSioc = 27;
constexpr int kY2 = 5, kY3 = 18, kY4 = 19, kY5 = 21;
constexpr int kY6 = 36, kY7 = 39, kY8 = 34, kY9 = 35;
constexpr int kVsync = 25, kHref = 23, kPclk = 22;

// Example UART pins: SD card cannot be used at the same time.
constexpr int kLinkRx = 13;
constexpr int kLinkTx = 14;
HardwareSerial Link(1);
vision::Color selectedColor = vision::RED;
unsigned long sequenceNumber = 0;
char command[24] = {};
size_t commandLength = 0;

void acceptCommand(const char* line) {
  if (line[0] != 'C' || line[1] != ',') return;
  vision::Color next;
  if (vision::parseColor(line + 2, next)) {
    selectedColor = next;
    Serial.printf("Selected color: %s\n", vision::colorName(selectedColor));
  }
}

void readCommands() {
  while (Link.available()) {
    char character = static_cast<char>(Link.read());
    if (character == '\n') {
      command[commandLength] = '\0';
      acceptCommand(command);
      commandLength = 0;
    } else if (character != '\r' && commandLength < sizeof(command) - 1) {
      command[commandLength++] = character;
    } else if (character != '\r') {
      commandLength = 0;
    }
  }
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

  esp_err_t status = esp_camera_init(&config);
  if (status != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", status);
    while (true) delay(1000);
  }
  Serial.println("Camera ready. Waiting for selected-color commands.");
}

void loop() {
  readCommands();
  camera_fb_t* frame = esp_camera_fb_get();
  if (!frame) {
    Serial.println("Frame capture failed");
    delay(100);
    return;
  }

  vision::Result result = {false, 0, 0};
  if (frame->format == PIXFORMAT_RGB565 && frame->width == 160 && frame->height == 120) {
    result = vision::detectRGB565(frame->buf, frame->len, selectedColor);
  }
  esp_camera_fb_return(frame);

  ++sequenceNumber;
  Link.printf("V1,%lu,%s,%d,%d,%d\n", sequenceNumber,
              vision::colorName(selectedColor), result.valid ? 1 : 0,
              result.errorPct, result.areaPermille);
  Serial.printf("%s: valid=%d, error=%d%%, area=%d/1000\n",
                vision::colorName(selectedColor), result.valid ? 1 : 0,
                result.errorPct, result.areaPermille);
  delay(100);
}
