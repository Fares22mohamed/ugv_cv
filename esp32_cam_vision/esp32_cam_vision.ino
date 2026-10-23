#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include "img_converters.h"
#include <WiFi.h>
#include "vision_core.h"

// ================= WI-FI SETTINGS =================
// Use a 2.4 GHz Wi-Fi network. Replace these before joining your router.
const char* WIFI_SSID     = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// If router connection fails, the camera creates this setup hotspot.
// Change AP_PASSWORD before using the device outside a trusted test area.
const char* AP_SSID       = "ESP32-CAM";
const char* AP_PASSWORD   = "12345678";
// ===================================================

// AI Thinker ESP32-CAM pin map. Verify the purchased board is AI Thinker.
constexpr int kPwdn = 32, kReset = -1, kXclk = 0;
constexpr int kSiod = 26, kSioc = 27;
constexpr int kY2 = 5, kY3 = 18, kY4 = 19, kY5 = 21;
constexpr int kY6 = 36, kY7 = 39, kY8 = 34, kY9 = 35;
constexpr int kVsync = 25, kHref = 23, kPclk = 22;

// UART1 to main Control ESP32. GPIO13/14 share pins with microSD.
constexpr int kLinkRx = 13;
constexpr int kLinkTx = 14;
constexpr int kImageWidth = 160;
constexpr int kImageHeight = 120;
constexpr unsigned long kFrameIntervalMs = 100;
constexpr unsigned long kDebugIntervalMs = 500;
constexpr size_t kMaxJpeg = 40 * 1024;

HardwareSerial Link(1);
vision::Color selectedColor = vision::RED;
bool autoMode = true;  // C,AUTO enables all colors; C,COLOR locks to one color.
char command[24] = {};
size_t commandLength = 0;
bool commandOverflow = false;

// Shared latest JPEG frame used by the HTTP handlers.
httpd_handle_t pageServer = nullptr;    // TCP port 80
httpd_handle_t streamServer = nullptr;  // TCP port 81
uint8_t* latestJpeg = nullptr;
size_t latestLen = 0;
uint32_t latestId = 0;
SemaphoreHandle_t jpegMutex = nullptr;

uint8_t* allocBuf(size_t size) {
  uint8_t* buffer = psramFound() ? static_cast<uint8_t*>(ps_malloc(size)) : nullptr;
  return buffer ? buffer : static_cast<uint8_t*>(malloc(size));
}

// ---------------- Wi-Fi ----------------
void scanWifi() {
  Serial.println("\nScanning Wi-Fi networks...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int count = WiFi.scanNetworks();
  if (count <= 0) {
    Serial.println("No Wi-Fi networks found during scan.");
    return;
  }
  for (int i = 0; i < count; ++i) {
    Serial.printf("  %2d: %-24s %4d dBm  ch%d\n",
                  i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
  }
  WiFi.scanDelete();
}

bool connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to Wi-Fi network \"%s\"", WIFI_SSID);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries++ < 40) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

String currentIP() {
  return (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP().toString()
                                     : WiFi.localIP().toString();
}

// ---------------- Web server ----------------
static const char INDEX_HTML[] = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ESP32-CAM Live View</title>
  <style>
    body { margin:0; padding:24px 12px; background:#111827; color:#e5e7eb;
           font-family:Arial,sans-serif; text-align:center; }
    main { max-width:900px; margin:auto; }
    h1 { font-size:1.6rem; margin-bottom:8px; }
    p { color:#cbd5e1; }
    img { display:block; width:100%; max-width:800px; margin:18px auto;
          border:2px solid #475569; border-radius:8px; background:#020617; }
    a { color:#7dd3fc; }
    .hint { font-size:.9rem; }
  </style>
</head>
<body>
  <main>
    <h1>ESP32-CAM Live View</h1>
    <p>The live image refreshes automatically.</p>
    <img id="video" alt="Waiting for camera stream...">
    <p><a href="/capture">Take a single photo</a></p>
    <p class="hint">Keep this page on the same local network as the ESP32-CAM.</p>
  </main>
  <script>
    document.getElementById('video').src =
      location.protocol + '//' + location.hostname + ':81/stream';
  </script>
</body>
</html>
)HTML";

static esp_err_t indexHandler(httpd_req_t* request) {
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  return httpd_resp_send(request, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t captureHandler(httpd_req_t* request) {
  if (!jpegMutex || !latestJpeg) {
    httpd_resp_set_status(request, "503 Service Unavailable");
    return httpd_resp_sendstr(request, "JPEG frame storage is unavailable.");
  }

  uint8_t* buffer = allocBuf(kMaxJpeg);
  if (!buffer) {
    httpd_resp_send_500(request);
    return ESP_FAIL;
  }

  size_t length = 0;
  if (xSemaphoreTake(jpegMutex, pdMS_TO_TICKS(500))) {
    length = latestLen;
    if (length > 0) memcpy(buffer, latestJpeg, length);
    xSemaphoreGive(jpegMutex);
  }

  esp_err_t result;
  if (length > 0) {
    httpd_resp_set_type(request, "image/jpeg");
    result = httpd_resp_send(request, reinterpret_cast<const char*>(buffer), length);
  } else {
    httpd_resp_set_status(request, "503 Service Unavailable");
    result = httpd_resp_sendstr(request, "No camera frame is available yet.");
  }
  free(buffer);
  return result;
}

static esp_err_t streamHandler(httpd_req_t* request) {
  if (!jpegMutex || !latestJpeg) {
    httpd_resp_send_500(request);
    return ESP_FAIL;
  }

  uint8_t* buffer = allocBuf(kMaxJpeg);
  if (!buffer) {
    httpd_resp_send_500(request);
    return ESP_FAIL;
  }

  httpd_resp_set_type(request, "multipart/x-mixed-replace;boundary=frame");
  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");

  uint32_t lastId = 0;
  esp_err_t result = ESP_OK;
  char header[96];
  while (result == ESP_OK) {
    size_t length = 0;
    uint32_t id = 0;
    if (xSemaphoreTake(jpegMutex, pdMS_TO_TICKS(200))) {
      id = latestId;
      length = latestLen;
      if (id != lastId && length > 0 && length <= kMaxJpeg) {
        memcpy(buffer, latestJpeg, length);
      } else {
        length = 0;
      }
      xSemaphoreGive(jpegMutex);
    }

    if (id == lastId || length == 0) {
      delay(10);
      continue;
    }

    lastId = id;
    int headerLength = snprintf(
      header, sizeof(header),
      "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
      static_cast<unsigned>(length));
    result = httpd_resp_send_chunk(request, header, headerLength);
    if (result == ESP_OK) {
      result = httpd_resp_send_chunk(request, reinterpret_cast<const char*>(buffer), length);
    }
    if (result == ESP_OK) result = httpd_resp_send_chunk(request, "\r\n", 2);
  }

  free(buffer);
  return result;
}

void startServers() {
  if (!jpegMutex || !latestJpeg) {
    Serial.println("Web stream disabled: JPEG buffer or mutex allocation failed.");
    return;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  httpd_uri_t indexUri = {"/", HTTP_GET, indexHandler, nullptr};
  httpd_uri_t captureUri = {"/capture", HTTP_GET, captureHandler, nullptr};

  if (httpd_start(&pageServer, &config) == ESP_OK) {
    httpd_register_uri_handler(pageServer, &indexUri);
    httpd_register_uri_handler(pageServer, &captureUri);
    Serial.println("Web page server started on port 80.");
  } else {
    pageServer = nullptr;
    Serial.println("Could not start the web page server on port 80.");
  }

  config.server_port = 81;
  config.ctrl_port += 1;
  httpd_uri_t streamUri = {"/stream", HTTP_GET, streamHandler, nullptr};
  if (httpd_start(&streamServer, &config) == ESP_OK) {
    httpd_register_uri_handler(streamServer, &streamUri);
    Serial.println("MJPEG stream server started on port 81.");
  } else {
    streamServer = nullptr;
    Serial.println("Could not start the MJPEG stream server on port 81.");
  }
}

// Convert each RGB565 frame to JPEG for the browser. The detector still uses
// the original RGB565 pixels, so streaming does not change detector inputs.
void publishFrame(camera_fb_t* frame) {
  if (!frame || !jpegMutex || !latestJpeg) return;

  uint8_t* jpeg = nullptr;
  size_t jpegLength = 0;
  if (!frame2jpg(frame, 80, &jpeg, &jpegLength) || !jpeg) {
    if (jpeg) free(jpeg);
    return;
  }

  if (jpegLength > 0 && jpegLength <= kMaxJpeg &&
      xSemaphoreTake(jpegMutex, pdMS_TO_TICKS(20))) {
    memcpy(latestJpeg, jpeg, jpegLength);
    latestLen = jpegLength;
    ++latestId;
    xSemaphoreGive(jpegMutex);
  }
  free(jpeg);
}

// ---------------- UART control link ----------------
void sendNone() {
  // Only protocol messages go over the UART; debug text stays on USB Serial.
  Link.println("NONE");
}

void acceptCommand(const char* line) {
  if (!line || line[0] != 'C' || line[1] != ',') return;

  if (strcmp(line + 2, "AUTO") == 0) {
    autoMode = true;
    Serial.println("Detection mode: AUTO (all supported colors)");
    return;
  }

  vision::Color next;
  if (vision::parseColor(line + 2, next)) {
    selectedColor = next;
    autoMode = false;
    Serial.printf("Detection mode: locked to %s\n", vision::colorName(selectedColor));
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
      if (!commandOverflow && commandLength < sizeof(command) - 1) {
        command[commandLength++] = character;
      } else {
        commandLength = 0;
        commandOverflow = true;  // discard the rest of this line
      }
    }
  }
}

int centerPixelFromError(int errorPct) {
  long centerX = lroundf(80.0f + static_cast<float>(errorPct) * 0.8f);
  if (centerX < 0) centerX = 0;
  if (centerX >= kImageWidth) centerX = kImageWidth - 1;
  return static_cast<int>(centerX);
}

void sendResult(const vision::AnyResult& result) {
  if (!result.valid) {
    sendNone();
    return;
  }

  // The 1.0 field is a binary acceptance marker, not a calibrated probability.
  Link.printf("FLAG,%d,%d,1.0,%s\n",
              centerPixelFromError(result.errorPct), kImageWidth,
              vision::colorName(result.color));
}

// ---------------- Arduino setup/loop ----------------
void setup() {
  Serial.begin(115200);
  delay(300);
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
    while (true) delay(1000);
  }

  jpegMutex = xSemaphoreCreateMutex();
  latestJpeg = allocBuf(kMaxJpeg);
  if (!jpegMutex || !latestJpeg) {
    Serial.println("Could not allocate JPEG streaming resources; detection will continue without web preview.");
  }

  scanWifi();
  if (connectWifi()) {
    Serial.println("Wi-Fi connected.");
  } else {
    Serial.println("Router Wi-Fi connection failed; starting the ESP32-CAM setup hotspot.");
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
      Serial.println("Could not start the setup hotspot.");
    } else {
      Serial.printf("Connect to Wi-Fi \"%s\" using the configured AP password.\n", AP_SSID);
    }
  }

  startServers();

  Serial.println("--------------------------------------");
  Serial.print("Web page: http://");
  Serial.println(currentIP());
  Serial.print("Stream:   http://");
  Serial.print(currentIP());
  Serial.println(":81/stream");
  Serial.println("--------------------------------------");
  Serial.println("Camera ready. Detection mode: AUTO.");
  Serial.println("UART: FLAG,centerX,160,1.0,COLOR or NONE; 115200 baud, 8N1.");
}

void loop() {
  static unsigned long lastDebug = 0;
  static unsigned long lastIp = 0;
  readCommands();

  camera_fb_t* frame = esp_camera_fb_get();
  if (!frame) {
    Serial.println("Frame capture failed; sending NONE.");
    sendNone();
    delay(kFrameIntervalMs);
    return;
  }

  vision::AnyResult result = {false, vision::RED, 0, 0};
  if (frame->format == PIXFORMAT_RGB565 &&
      frame->width == kImageWidth &&
      frame->height == kImageHeight &&
      frame->len >= static_cast<size_t>(kImageWidth * kImageHeight * 2)) {
    result = vision::detectAnyRGB565(frame->buf, frame->len, autoMode, selectedColor);
    publishFrame(frame);
  } else {
    Serial.printf("Invalid frame: format=%d width=%d height=%d length=%u; sending NONE.\n",
                  static_cast<int>(frame->format), frame->width, frame->height,
                  static_cast<unsigned int>(frame->len));
  }

  esp_camera_fb_return(frame);
  sendResult(result);

  if (millis() - lastDebug >= kDebugIntervalMs) {
    lastDebug = millis();
    if (result.valid) {
      Serial.printf("DETECTED: %s  error=%d%%  area=%d/1000\n",
                    vision::colorName(result.color), result.errorPct, result.areaPermille);
    } else {
      Serial.println("DETECTED: NONE");
    }
  }

  if (millis() - lastIp >= 5000) {
    lastIp = millis();
    Serial.print("IP: ");
    Serial.println(currentIP());
  }
  delay(kFrameIntervalMs);
}
