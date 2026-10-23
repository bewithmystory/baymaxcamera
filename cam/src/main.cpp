/*
 * DOOM WORLD - ESP32-CAM Firmware (V4)
 * Board: AI-Thinker ESP32-CAM + OV2640
 * SoftAP: DOOM_CAM (Channel 1, IP: 192.168.4.1)
 *
 * Supported HTTP Endpoints:
 *   GET /capture                            -> Live JPEG snapshot for TFT preview
 *   GET /photo                              -> Save still JPEG to microSD
 *   GET /record?state=1|0                   -> Start/stop MJPEG video recording
 *   GET /flash?state=1|0                    -> Turn on-board GPIO 4 Flash LED ON/OFF
 *   GET /zoom?value=0..100                  -> Optional zoom telemetry endpoint
 *   GET /status                             -> System, SD, flash, and recording status
 *   GET /media/list?type=photo|video        -> List up to 12 newest saved basenames
 *   GET /media/photo?name=<basename>        -> Fetch saved JPEG photo
 *   GET /media/video?name=<basename>        -> Fetch complete concatenated-JPEG MJPEG recording
 *   GET /media/video/frame?name=<bname>&frame=<idx> -> Fetch 0-based frame from MJPEG
 */

#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include "FS.h"
#include "SD_MMC.h"

#include "pins.h"
#include "config.h"

WebServer server(80);

// ------------ Runtime State ------------
bool sdReady = false;
bool recording = false;
bool flashState = false;
int  zoomValue = 0;

File recordingFile;
String activeRecordingName = "";
uint32_t photoCounter = 0;
uint32_t recordingCounter = 0;
uint32_t recordedFrames = 0;
uint32_t lastFrameWriteMs = 0;

// =====================================================
// FLASH LED CONTROL (GPIO 4)
// =====================================================

void setFlashLed(bool on) {
  flashState = on;
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, on ? FLASH_ON_LEVEL : FLASH_OFF_LEVEL);
}

// =====================================================
// FILENAME VALIDATION & NEXT-PATH HELPERS
// =====================================================

// Strictly validates that a filename is a bare basename matching expected
// photo/video extensions and contains no path traversal ('/', '\', '..').
bool isValidMediaBasename(const String &name, bool isVideo) {
  if (name.length() < 5 || name.length() > 40) return false;
  if (name.indexOf('/') >= 0 || name.indexOf('\\') >= 0 || name.indexOf("..") >= 0) {
    return false;
  }
  for (size_t i = 0; i < name.length(); i++) {
    char c = name.charAt(i);
    bool ok = (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              (c == '_') || (c == '-') || (c == '.');
    if (!ok) return false;
  }
  if (isVideo) {
    return name.endsWith(".mjpeg") || name.endsWith(".MJPEG");
  } else {
    return name.endsWith(".jpg") || name.endsWith(".JPG");
  }
}

void syncCountersFromSD() {
  if (!sdReady) return;
  File root = SD_MMC.open("/");
  if (!root || !root.isDirectory()) return;

  File entry = root.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String fname = String(entry.name());
      int slash = fname.lastIndexOf('/');
      if (slash >= 0) fname = fname.substring(slash + 1);

      unsigned long num = 0;
      if (sscanf(fname.c_str(), "photo_%lu.jpg", &num) == 1) {
        if (num > photoCounter) photoCounter = (uint32_t)num;
      } else if (sscanf(fname.c_str(), "doom_%lu.mjpeg", &num) == 1) {
        if (num > recordingCounter) recordingCounter = (uint32_t)num;
      }
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();
}

String nextPhotoPath() {
  char path[32];
  do {
    ++photoCounter;
    snprintf(path, sizeof(path), "/photo_%04lu.jpg", (unsigned long)photoCounter);
  } while (sdReady && SD_MMC.exists(path) && photoCounter < 999999UL);
  return String(path);
}

String nextRecordingPath() {
  char path[40];
  do {
    ++recordingCounter;
    snprintf(path, sizeof(path), "/doom_%04lu.mjpeg", (unsigned long)recordingCounter);
  } while (sdReady && SD_MMC.exists(path) && recordingCounter < 999999UL);
  return String(path);
}

// =====================================================
// CAMERA INITIALIZATION
// =====================================================

bool initCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    Serial.println("Camera memory: PSRAM");
    config.frame_size   = FRAMESIZE_QVGA; // 320x240 (fits HAND 60KB buffer)
    config.jpeg_quality = 16;
    config.fb_count     = 2;
    config.fb_location  = CAMERA_FB_IN_PSRAM;
    config.grab_mode    = CAMERA_GRAB_LATEST;
  } else {
    Serial.println("WARNING: PSRAM unavailable. Using DRAM QQVGA mode.");
    config.frame_size   = FRAMESIZE_QQVGA; // 160x120
    config.jpeg_quality = 14;
    config.fb_count     = 1;
    config.fb_location  = CAMERA_FB_IN_DRAM;
    config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor) {
    sensor->set_brightness(sensor, 0);
    sensor->set_contrast(sensor, 0);
    sensor->set_saturation(sensor, 0);
  }

  return true;
}

// =====================================================
// HTTP HELPERS
// =====================================================

void sendText(int statusCode, const String &message) {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(statusCode, "text/plain; charset=utf-8", message);
}

void sendJpegBuffer(const uint8_t *buf, size_t len) {
  WiFiClient client = server.client();
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.setContentLength(len);
  server.send(200, "image/jpeg", "");
  if (client.connected()) {
    client.write(buf, len);
  }
}

// =====================================================
// RECORDING ENGINE
// =====================================================

void stopRecording() {
  if (recordingFile) {
    recordingFile.flush();
    recordingFile.close();
  }
  recording = false;
  activeRecordingName = "";
  Serial.printf("Recording stopped. Frames saved: %lu\n", (unsigned long)recordedFrames);
}

void writeRecordingFrame() {
  if (!recording || !recordingFile) return;

  uint32_t now = millis();
  if (lastFrameWriteMs != 0 && (uint32_t)(now - lastFrameWriteMs) < RECORD_FRAME_INTERVAL_MS) {
    return;
  }
  lastFrameWriteMs = now;

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return;

  if (fb->format == PIXFORMAT_JPEG && fb->len > 0 && fb->len <= MAX_HAND_JPEG_BYTES) {
    size_t written = recordingFile.write(fb->buf, fb->len);
    size_t expected = fb->len;
    esp_camera_fb_return(fb);

    if (written == expected) {
      ++recordedFrames;
      if ((recordedFrames % 10U) == 0U) {
        recordingFile.flush();
      }
    } else {
      Serial.println("SD write error; stopping recording");
      stopRecording();
    }
  } else {
    esp_camera_fb_return(fb);
  }
}

// =====================================================
// CORE CAMERA ENDPOINTS (/capture, /photo, /record, /flash)
// =====================================================

void handleCapture() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    sendText(503, "Camera capture failed");
    return;
  }
  if (fb->format != PIXFORMAT_JPEG || fb->len == 0 || fb->len > MAX_HAND_JPEG_BYTES) {
    esp_camera_fb_return(fb);
    sendText(500, "Invalid JPEG frame size");
    return;
  }

  sendJpegBuffer(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void handlePhoto() {
  if (!sdReady) {
    sendText(503, "SD card not ready");
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    sendText(503, "Camera capture failed");
    return;
  }
  if (fb->format != PIXFORMAT_JPEG || fb->len == 0 ||
      fb->len > MAX_HAND_JPEG_BYTES) {
    esp_camera_fb_return(fb);
    sendText(413, "Photo exceeds HAND 60KB buffer");
    return;
  }

  String path = nextPhotoPath();
  File file = SD_MMC.open(path.c_str(), FILE_WRITE);
  if (!file) {
    esp_camera_fb_return(fb);
    sendText(500, "Could not create photo file");
    return;
  }

  size_t expected = fb->len;
  size_t written = file.write(fb->buf, expected);
  file.close();
  esp_camera_fb_return(fb);

  // Re-assert GPIO 4 flash state in case SD_MMC touched pin state
  setFlashLed(flashState);

  if (written != expected) {
    SD_MMC.remove(path.c_str());
    sendText(500, "Photo write incomplete");
    return;
  }

  sendText(200, "PHOTO_SAVED " + path + " " + String((unsigned long)written) + " bytes");
  Serial.printf("Saved photo: %s (%u bytes)\n", path.c_str(), (unsigned)written);
}

void handleRecord() {
  if (!server.hasArg("state")) {
    sendText(400, "Use /record?state=1 or /record?state=0");
    return;
  }

  String state = server.arg("state");
  if (state == "1") {
    if (!sdReady) {
      sendText(503, "SD card not ready");
      return;
    }
    if (recording) {
      sendText(200, "RECORDING_ALREADY_ACTIVE");
      return;
    }

    String path = nextRecordingPath();
    recordingFile = SD_MMC.open(path.c_str(), FILE_WRITE);
    if (!recordingFile) {
      sendText(500, "Could not create recording file");
      return;
    }

    activeRecordingName = path.substring(1); // Basename without leading '/'
    recordedFrames = 0;
    lastFrameWriteMs = 0;
    recording = true;
    setFlashLed(flashState);
    sendText(200, "RECORDING_STARTED " + path + " format=MJPEG");
    Serial.printf("Recording started: %s\n", path.c_str());
  } else if (state == "0") {
    bool wasRecording = recording;
    stopRecording();
    setFlashLed(flashState);
    sendText(200, wasRecording ? "RECORDING_STOPPED" : "RECORDING_NOT_ACTIVE");
  } else {
    sendText(400, "state must be 1 or 0");
  }
}

void handleFlash() {
  if (!server.hasArg("state")) {
    sendText(400, "Use /flash?state=1 or /flash?state=0");
    return;
  }

  String state = server.arg("state");
  if (state == "1") {
    setFlashLed(true);
    sendText(200, "FLASH_ON");
    Serial.println("Camera Flash LED: ON");
  } else if (state == "0") {
    setFlashLed(false);
    sendText(200, "FLASH_OFF");
    Serial.println("Camera Flash LED: OFF");
  } else {
    sendText(400, "state must be 1 or 0");
  }
}

void handleZoom() {
  if (server.hasArg("value")) {
    zoomValue = constrain(server.arg("value").toInt(), 0, 100);
  }
  sendText(200, "ZOOM=" + String(zoomValue));
}

void handleStatus() {
  String msg = "camera=ready\nsd=";
  msg += sdReady ? "ready" : "not_ready";
  msg += "\nrecording=";
  msg += recording ? "yes" : "no";
  msg += "\nflash=";
  msg += flashState ? "on" : "off";
  msg += "\nframes=";
  msg += String((unsigned long)recordedFrames);
  msg += "\nap_ip=";
  msg += WiFi.softAPIP().toString();
  sendText(200, msg);
}

// =====================================================
// SD GALLERY ENDPOINTS (/media/list, /media/photo, /media/video/frame)
// =====================================================

void handleMediaList() {
  if (!sdReady) {
    sendText(503, "SD card not ready");
    return;
  }
  if (!server.hasArg("type")) {
    sendText(400, "Missing type=photo or type=video");
    return;
  }

  String type = server.arg("type");
  bool wantVideo = (type == "video");
  if (!wantVideo && type != "photo") {
    sendText(400, "type must be photo or video");
    return;
  }

  File root = SD_MMC.open("/");
  if (!root || !root.isDirectory()) {
    sendText(500, "SD root open failed");
    return;
  }

  // Keep the newest matching filenames by their monotonically increasing IDs.
  String items[MAX_GALLERY_ITEMS];
  unsigned long itemNumbers[MAX_GALLERY_ITEMS] = {};
  size_t count = 0;

  File entry = root.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String fname = String(entry.name());
      int slash = fname.lastIndexOf('/');
      if (slash >= 0) fname = fname.substring(slash + 1);

      // Skip the actively recording file so the gallery only reads closed files
      if (isValidMediaBasename(fname, wantVideo) && fname != activeRecordingName &&
          (!wantVideo || entry.size() > 0)) {
        const size_t numberOffset = wantVideo ? 5 : 6;
        const unsigned long number = strtoul(fname.c_str() + numberOffset, nullptr, 10);
        size_t insertAt = 0;
        while (insertAt < count && itemNumbers[insertAt] > number) {
          ++insertAt;
        }
        if (insertAt < MAX_GALLERY_ITEMS) {
          const size_t newCount = count < MAX_GALLERY_ITEMS ? count + 1 : count;
          for (size_t i = newCount - 1; i > insertAt; --i) {
            items[i] = items[i - 1];
            itemNumbers[i] = itemNumbers[i - 1];
          }
          items[insertAt] = fname;
          itemNumbers[insertAt] = number;
          count = newCount;
        }
      }
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();
  setFlashLed(flashState);

  // Output newest-first, one basename per line.
  String body = "";
  for (size_t i = 0; i < count; ++i) {
    body += items[i] + "\n";
  }
  sendText(200, body);
}

void handleMediaPhoto() {
  if (!sdReady) {
    sendText(503, "SD card not ready");
    return;
  }
  if (!server.hasArg("name")) {
    sendText(400, "Missing name parameter");
    return;
  }

  String name = server.arg("name");
  if (!isValidMediaBasename(name, false)) {
    sendText(400, "Invalid photo filename");
    return;
  }

  String path = "/" + name;
  if (!SD_MMC.exists(path.c_str())) {
    sendText(404, "Photo not found");
    return;
  }

  File file = SD_MMC.open(path.c_str(), FILE_READ);
  if (!file) {
    sendText(500, "Could not open photo");
    return;
  }

  const size_t fileSize = file.size();
  if (fileSize == 0) {
    file.close();
    sendText(500, "Photo file is empty");
    return;
  }
  if (fileSize > MAX_HAND_JPEG_BYTES) {
    file.close();
    sendText(413, "Photo exceeds HAND 60KB buffer");
    return;
  }

  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.streamFile(file, "image/jpeg");
  file.close();
  setFlashLed(flashState);
}

void handleMediaVideo() {
  if (!sdReady) {
    sendText(503, "SD card not ready");
    return;
  }
  if (!server.hasArg("name")) {
    sendText(400, "Missing name parameter");
    return;
  }

  String name = server.arg("name");
  if (!isValidMediaBasename(name, true)) {
    sendText(400, "Invalid video filename");
    return;
  }
  if (name == activeRecordingName) {
    sendText(409, "Recording is still active");
    return;
  }

  String path = "/" + name;
  File file = SD_MMC.open(path.c_str(), FILE_READ);
  if (!file) {
    sendText(404, "Video not found");
    return;
  }
  if (file.size() == 0) {
    file.close();
    sendText(500, "Video file is empty");
    return;
  }

  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.streamFile(file, "video/x-motion-jpeg");
  file.close();
  setFlashLed(flashState);
}

// Scans a concatenated-JPEG (.mjpeg) file for the Nth JPEG frame (0-based)
// by locating SOI (0xFF 0xD8) and EOI (0xFF 0xD9) markers.
void handleMediaVideoFrame() {
  if (!sdReady) {
    sendText(503, "SD card not ready");
    return;
  }
  if (!server.hasArg("name") || !server.hasArg("frame")) {
    sendText(400, "Missing name or frame parameter");
    return;
  }

  String name = server.arg("name");
  int targetFrame = server.arg("frame").toInt();
  if (!isValidMediaBasename(name, true) || targetFrame < 0) {
    sendText(400, "Invalid video filename or frame index");
    return;
  }

  String path = "/" + name;
  if (!SD_MMC.exists(path.c_str())) {
    sendText(404, "Video not found");
    return;
  }

  File file = SD_MMC.open(path.c_str(), FILE_READ);
  if (!file) {
    sendText(500, "Could not open video");
    return;
  }

  const size_t CHUNK_SIZE = 2048;
  uint8_t *chunk = (uint8_t *)malloc(CHUNK_SIZE);
  if (!chunk) {
    file.close();
    sendText(500, "Out of memory");
    return;
  }

  int currentFrame = 0;
  bool inFrame = false;
  size_t frameStartOffset = 0;
  size_t frameLength = 0;
  bool foundTarget = false;
  uint8_t prevByte = 0;
  size_t filePos = 0;

  while (file.available()) {
    int n = file.read(chunk, CHUNK_SIZE);
    if (n <= 0) break;

    for (int i = 0; i < n; i++) {
      uint8_t b = chunk[i];
      size_t absPos = filePos + (size_t)i;

      if (!inFrame) {
        if (prevByte == 0xFF && b == 0xD8 && absPos >= 1) {
          inFrame = true;
          frameStartOffset = absPos - 1;
        }
      } else {
        if (prevByte == 0xFF && b == 0xD9) {
          size_t endOffset = absPos + 1;
          frameLength = endOffset - frameStartOffset;
          inFrame = false;

          if (currentFrame == targetFrame) {
            foundTarget = true;
            break;
          }
          currentFrame++;
        }
      }
      prevByte = b;
    }

    if (foundTarget) break;
    filePos += (size_t)n;
  }

  free(chunk);

  if (!foundTarget) {
    file.close();
    setFlashLed(flashState);
    sendText(404, "Frame past end of video");
    return;
  }

  if (frameLength == 0 || frameLength > MAX_HAND_JPEG_BYTES) {
    file.close();
    setFlashLed(flashState);
    sendText(500, "Video frame exceeds HAND 60KB buffer");
    return;
  }

  uint8_t *jpg = (uint8_t *)ps_malloc(frameLength);
  if (!jpg) jpg = (uint8_t *)malloc(frameLength);
  if (!jpg) {
    file.close();
    setFlashLed(flashState);
    sendText(500, "Out of memory");
    return;
  }

  file.seek(frameStartOffset);
  size_t got = file.read(jpg, frameLength);
  file.close();
  setFlashLed(flashState);

  if (got == frameLength) {
    sendJpegBuffer(jpg, frameLength);
  } else {
    sendText(500, "Frame read error");
  }
  free(jpg);
}

void handleNotFound() {
  sendText(404, "CAMERA ENDPOINT NOT FOUND");
}

// =====================================================
// SETUP & MAIN LOOP
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== DOOM CAM V4 STARTING ===");

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, HIGH); // Status LED OFF initially
  setFlashLed(false);                 // Ensure Flash LED is OFF at boot

  if (!initCamera()) {
    Serial.println("FATAL: Camera initialization failed!");
    while (true) {
      digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
      delay(300);
    }
  }
  Serial.println("Camera sensor ready.");

  // Mount microSD in 1-bit mode so GPIO 4 remains dedicated to the Flash LED
  if (SD_MMC.begin("/sdcard", true)) {
    if (SD_MMC.cardType() != CARD_NONE) {
      sdReady = true;
      syncCountersFromSD();
      Serial.printf("SD_MMC (1-bit mode) ready. Next photo: %lu, Next video: %lu\n",
                    (unsigned long)(photoCounter + 1),
                    (unsigned long)(recordingCounter + 1));
    } else {
      Serial.println("No SD card inserted. Live preview only.");
    }
  } else {
    Serial.println("SD_MMC mount failed. Live preview only.");
  }

  // Re-assert Flash LED OFF after SD_MMC initialization
  setFlashLed(false);

  // Start SoftAP locked to Channel 1 for ESP-NOW coexistence
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  bool apOK = WiFi.softAP(CAM_AP_SSID, CAM_AP_PASSWORD, CAM_AP_CHANNEL, false, CAM_MAX_CLIENTS);
  if (!apOK) {
    Serial.println("FATAL: SoftAP start failed!");
    while (true) {
      delay(1000);
    }
  }

  Serial.printf("SoftAP: %s | Channel: %d | IP: %s\n",
                CAM_AP_SSID, CAM_AP_CHANNEL, WiFi.softAPIP().toString().c_str());

  // Register HTTP endpoints
  server.on("/capture",            HTTP_GET, handleCapture);
  server.on("/photo",              HTTP_GET, handlePhoto);
  server.on("/record",             HTTP_GET, handleRecord);
  server.on("/flash",              HTTP_GET, handleFlash);
  server.on("/zoom",               HTTP_GET, handleZoom);
  server.on("/status",             HTTP_GET, handleStatus);
  server.on("/media/list",         HTTP_GET, handleMediaList);
  server.on("/media/photo",        HTTP_GET, handleMediaPhoto);
  server.on("/media/video",        HTTP_GET, handleMediaVideo);
  server.on("/media/video/frame",  HTTP_GET, handleMediaVideoFrame);
  server.onNotFound(handleNotFound);

  server.begin();
  digitalWrite(STATUS_LED_PIN, LOW); // Turn status LED ON when ready
  Serial.println("HTTP Server ready.");
}

void loop() {
  server.handleClient();
  writeRecordingFrame();
  delay(1);
}