#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "esp_camera.h"

namespace vision {

enum Color { RED, ORANGE, YELLOW, GREEN, CYAN, BLUE, PURPLE };

inline const char* colorName(Color color) {
  static const char* names[] = {"RED", "ORANGE", "YELLOW", "GREEN", "CYAN", "BLUE", "PURPLE"};
  return names[static_cast<int>(color)];
}

inline bool parseColor(const char* text, Color& color) {
  for (int i = 0; i < 7; ++i) {
    if (strcmp(text, colorName(static_cast<Color>(i))) == 0) {
      color = static_cast<Color>(i);
      return true;
    }
  }
  return false;
}

inline bool matches(uint8_t red, uint8_t green, uint8_t blue, Color color) {
  uint8_t high = red > green ? red : green;
  high = high > blue ? high : blue;
  uint8_t low = red < green ? red : green;
  low = low < blue ? low : blue;
  int spread = static_cast<int>(high) - low;
  if (high < 46 || spread * 100 < high * 35 || spread == 0) return false;

  float hue;
  if (high == red) hue = 60.0f * (static_cast<int>(green) - blue) / spread;
  else if (high == green) hue = 120.0f + 60.0f * (static_cast<int>(blue) - red) / spread;
  else hue = 240.0f + 60.0f * (static_cast<int>(red) - green) / spread;
  if (hue < 0) hue += 360.0f;

  switch (color) {
    case RED: return hue >= 345 || hue <= 15;
    case ORANGE: return hue >= 16 && hue <= 44;
    case YELLOW: return hue >= 45 && hue <= 70;
    case GREEN: return hue >= 71 && hue <= 165;
    case CYAN: return hue >= 166 && hue <= 184;
    case BLUE: return hue >= 185 && hue <= 260;
    case PURPLE: return hue >= 261 && hue <= 344;
  }
  return false;
}

struct Result {
  bool valid;
  int errorPct;
  int areaPermille;
};

// Assumes a 160 x 120, high-byte-first RGB565 camera frame.
inline Result detectRGB565(const uint8_t* frame, size_t length, Color selectedColor) {
  constexpr int width = 80;
  constexpr int height = 60;
  constexpr int sourceWidth = 160;
  constexpr int minPixels = 20;
  static uint8_t mask[width * height];
  static uint16_t queue[width * height];
  if (!frame || length < sourceWidth * 120 * 2) return {false, 0, 0};

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      size_t index = static_cast<size_t>((y * 2) * sourceWidth + (x * 2)) * 2;
      uint16_t pixel = (static_cast<uint16_t>(frame[index]) << 8) | frame[index + 1];
      uint8_t red = ((pixel >> 11) & 31) * 255 / 31;
      uint8_t green = ((pixel >> 5) & 63) * 255 / 63;
      uint8_t blue = (pixel & 31) * 255 / 31;
      mask[y * width + x] = matches(red, green, blue, selectedColor) ? 1 : 0;
    }
  }

  int bestCount = 0;
  int bestSumX = 0;
  for (int start = 0; start < width * height; ++start) {
    if (mask[start] != 1) continue;
    int head = 0, tail = 0;
    queue[tail++] = start;
    mask[start] = 2;
    int count = 0, sumX = 0;
    while (head < tail) {
      int point = queue[head++];
      int x = point % width;
      int y = point / width;
      ++count;
      sumX += x;
      const int neighbors[4] = {point - width, point + width, point - 1, point + 1};
      for (int n = 0; n < 4; ++n) {
        int next = neighbors[n];
        if (next < 0 || next >= width * height || mask[next] != 1) continue;
        int nx = next % width;
        int ny = next / width;
        if ((nx > x ? nx - x : x - nx) + (ny > y ? ny - y : y - ny) != 1) continue;
        mask[next] = 2;
        queue[tail++] = next;
      }
    }
    if (count > bestCount) {
      bestCount = count;
      bestSumX = sumX;
    }
  }

  if (bestCount < minPixels) return {false, 0, 0};
  float centerX = static_cast<float>(bestSumX) / bestCount;
  int error = static_cast<int>((centerX - 39.5f) * 200.0f / 79.0f +
                               (centerX < 39.5f ? -0.5f : 0.5f));
  int area = (bestCount * 1000 + width * height / 2) / (width * height);
  return {true, error, area};
}

}  // namespace vision

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