#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace vision {

enum Color { RED, ORANGE, YELLOW, GREEN, CYAN, BLUE, PURPLE };

inline const char* colorName(Color color) {
  static const char* names[] = {"RED", "ORANGE", "YELLOW", "GREEN", "CYAN", "BLUE", "PURPLE"};
  const int index = static_cast<int>(color);
  return (index >= 0 && index < 7) ? names[index] : "UNKNOWN";
}

inline bool parseColor(const char* text, Color& color) {
  if (!text) return false;
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

// Classify a pixel: returns color index 0..6, or -1 when no preset matches.
// In automatic mode every supported color is checked; otherwise only the
// color selected by the main controller is checked.
inline int classify(uint8_t red, uint8_t green, uint8_t blue,
                    bool autoMode, Color lockedColor) {
  if (!autoMode) return matches(red, green, blue, lockedColor)
                         ? static_cast<int>(lockedColor) : -1;
  for (int index = 0; index < 7; ++index) {
    if (matches(red, green, blue, static_cast<Color>(index))) return index;
  }
  return -1;
}

struct AnyResult {
  bool valid;
  Color color;
  int errorPct;
  int areaPermille;
};

// Finds the largest connected region in a 160 x 120 RGB565 frame, sampling
// every second pixel in each direction to keep processing lightweight.
inline AnyResult detectAnyRGB565(const uint8_t* frame, size_t length,
                                 bool autoMode, Color lockedColor) {
  constexpr int width = 80;
  constexpr int height = 60;
  constexpr int sourceWidth = 160;
  constexpr int minPixels = 40;
  static uint8_t mask[width * height];  // 0 = none, 1..7 = color + 1, 0xFF = visited
  static uint16_t queue[width * height];
  if (!frame || length < sourceWidth * 120 * 2) return {false, RED, 0, 0};

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      size_t index = static_cast<size_t>((y * 2) * sourceWidth + (x * 2)) * 2;
      uint16_t pixel = (static_cast<uint16_t>(frame[index]) << 8) | frame[index + 1];
      uint8_t red = ((pixel >> 11) & 31) * 255 / 31;
      uint8_t green = ((pixel >> 5) & 63) * 255 / 63;
      uint8_t blue = (pixel & 31) * 255 / 31;
      int colorIndex = classify(red, green, blue, autoMode, lockedColor);
      mask[y * width + x] = (colorIndex < 0) ? 0 : static_cast<uint8_t>(colorIndex + 1);
    }
  }

  int bestCount = 0;
  int bestSumX = 0;
  Color bestColor = RED;
  for (int start = 0; start < width * height; ++start) {
    uint8_t label = mask[start];
    if (label == 0 || label == 0xFF) continue;

    int head = 0, tail = 0;
    queue[tail++] = static_cast<uint16_t>(start);
    mask[start] = 0xFF;
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
        if (next < 0 || next >= width * height || mask[next] != label) continue;
        int nx = next % width;
        int ny = next / width;
        if ((nx > x ? nx - x : x - nx) + (ny > y ? ny - y : y - ny) != 1) continue;
        mask[next] = 0xFF;
        queue[tail++] = static_cast<uint16_t>(next);
      }
    }

    if (count > bestCount) {
      bestCount = count;
      bestSumX = sumX;
      bestColor = static_cast<Color>(label - 1);
    }
  }

  if (bestCount < minPixels) return {false, RED, 0, 0};
  float centerX = static_cast<float>(bestSumX) / bestCount;
  int error = static_cast<int>((centerX - 39.5f) * 200.0f / 79.0f +
                               (centerX < 39.5f ? -0.5f : 0.5f));
  int area = (bestCount * 1000 + width * height / 2) / (width * height);
  return {true, bestColor, error, area};
}

}  // namespace vision
