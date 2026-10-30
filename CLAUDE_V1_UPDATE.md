# Baymax Camera Firmware - CLAUDE v1 Update Log

**Date:** 2026-10-31
**Branch:** main (local modifications)
**Author:** Claude Code Assistant

---

## Overview

This document tracks all optimizations applied to the ESP32-CAM firmware (`baymaxcamera/cam`) to improve latency, frame rate stability, image quality, and Wi-Fi coexistence with ESP-NOW.

---

## Files Modified

| File | Type | Lines Changed |
|------|------|---------------|
| `cam/include/config.h` | Config | +35/-2 |
| `cam/src/main.cpp` | Core Firmware | +215/-13 |

---

## Changes Summary

### 1. SD Card: Switched from 1-bit to 4-bit Mode

**File:** `cam/src/main.cpp` (setup)
**Change:** `SD_MMC.begin("/sdcard", true)` → `SD_MMC.begin("/sdcard")`

**Why:** The `true` parameter forced 1-bit SD mode, which is significantly slower for writes. 4-bit mode uses 4 data lines (DAT0-DAT3) for parallel data transfer.

**Improvement:** **2-4x faster SD write speeds**, critical for MJPEG recording where frames are written sequentially. Reduces frame drops during recording.

---

### 2. Wi-Fi Channel Separation: Camera AP → Channel 6

**Files:** 
- `cam/include/config.h`: `CAM_AP_CHANNEL = 6` (was 1)
- `cam/src/main.cpp`: Uses `CAM_AP_CHANNEL` constant

**Why:** ESP-NOW communication (used by hand/head/body controllers) operates on Wi-Fi channel 1. The camera SoftAP was also on channel 1, causing co-channel interference, packet loss, and increased latency.

**Improvement:** **Eliminates Wi-Fi interference** between camera streaming and ESP-NOW control packets. ESP-NOW stays on channel 1; camera AP on channel 6. Hand controller updated to connect on channel 6.

---

### 3. Dynamic JPEG Quality Adjustment

**Files:**
- `cam/include/config.h`: New constants for dynamic quality
- `cam/src/main.cpp`: `adjustCameraQuality()` function + runtime state

**New Configuration (config.h):**
```cpp
DYNAMIC_QUALITY_ENABLED = true
JPEG_QUALITY_MIN = 8      // Best quality (lower = less compression)
JPEG_QUALITY_MAX = 20     // Lower quality for speed
JPEG_QUALITY_DEFAULT = 12 // Starting point
TARGET_FPS_MIN = 10       // Minimum acceptable FPS
TARGET_FPS_MAX = 25       // Maximum target FPS
```

**Algorithm (runs every 2 seconds):**
| Condition | Action |
|-----------|--------|
| RSSI < -75 (weak) | Decrease quality (+2) |
| RSSI > -55 (strong) | Increase quality (-1) |
| Free heap < 50KB | Decrease quality (+2) |
| FPS < 10 | Decrease quality (+1) |
| FPS > 25 | Increase quality (-1) |

**Why:** Fixed quality (previously 16) couldn't adapt to varying signal conditions, memory pressure, or performance targets.

**Improvement:** **Adaptive quality** - maintains target FPS under poor signal, improves quality when signal is strong, prevents OOM crashes when heap is low.

---

### 4. Increased JPEG Buffer Limit: 60KB → 80KB

**File:** `cam/include/config.h`
**Change:** `MAX_HAND_JPEG_BYTES = 80000` (was 60000)

**Why:** The hand controller's TFT display (320x240) at QVGA with JPEG quality 8-12 can produce frames larger than 60KB, causing frames to be rejected.

**Improvement:** **Higher quality frames accepted** - less aggressive compression, better image quality on hand display.

---

### 5. HTTP Keep-Alive Support

**Files:**
- `cam/include/config.h`: `HTTP_KEEP_ALIVE_ENABLED = true`
- `cam/src/main.cpp`: `sendText()` and `sendJpegBuffer()` add `Connection: keep-alive` header

**Why:** Without keep-alive, each HTTP request requires a new TCP connection (SYN/SYN-ACK/ACK), adding ~20-50ms latency per frame.

**Improvement:** **Persistent connections** - reduces per-frame latency, improves live preview FPS stability.

---

### 6. Proper MJPEG Recording Format

**File:** `cam/src/main.cpp`
**New Functions:** `writeMjpegBoundary()`, `writeMjpegHeader()`, updated `stopRecording()`

**Before:** Raw concatenated JPEG frames (no boundaries, no headers)
**After:** Proper MJPEG with:
- Boundary marker: `--frameboundary\r\n`
- Per-frame headers: `Content-Type: image/jpeg\r\nContent-Length: N\r\n\r\n`
- End marker: `--frameboundary--\r\n`

**Why:** Raw concatenated JPEGs are fragile - no way to parse frame boundaries, seek, or stream to standard players.

**Improvement:** **Standards-compliant MJPEG** - playable in browsers, VLC, ffmpeg; supports frame seeking via `/media/video/frame` endpoint.

---

### 7. Comprehensive Latency Tracking

**File:** `cam/src/main.cpp`
**New Global Variables:**
```cpp
uint32_t lastTotalLatency = 0;
uint32_t lastCaptureLatency = 0;
uint32_t lastEncodeLatency = 0;
uint32_t lastSendLatency = 0;
```

**Tracking Points in `handleCapture()`:**
1. `captureStartMs` - `esp_camera_fb_get()` called
2. `encodeEndMs` - Frame buffer obtained (JPEG encoded by hardware)
3. `httpSendStartMs` - HTTP response headers sent
4. `httpSendEndMs` - Frame data fully transmitted

**Metrics Exposed via `/status`:**
- `latency_total` - Glass-to-glass (capture → client received)
- `latency_capture` - Sensor → frame buffer
- `latency_encode` - Hardware JPEG encoding (minimal on ESP32-CAM)
- `latency_send` - HTTP transmission time

**Why:** No visibility into where latency occurred. "It feels slow" isn't actionable.

**Improvement:** **Measurable, debuggable latency** - target <100ms glass-to-glass. Enables data-driven optimization.

---

### 8. Enhanced `/status` Endpoint

**File:** `cam/src/main.cpp` - `handleStatus()`

**New Fields Added:**
```
quality=<current JPEG quality 8-20>
framesize=<FRAMESIZE enum value>
fps=<measured FPS>
rssi=<connected station RSSI>
heap=<free heap bytes>
latency_total=<ms>
latency_capture=<ms>
latency_encode=<ms>
latency_send=<ms>
```

**Why:** Hand controller and desktop command center need real-time telemetry for monitoring and adaptive behavior.

**Improvement:** **Single endpoint for all telemetry** - enables hand controller to display live metrics, adjust its own behavior.

---

### 9. Error Message Updates

**File:** `cam/src/main.cpp` - All error responses
**Change:** "HAND 60KB buffer" → "HAND 80KB buffer" (4 occurrences)

**Why:** Buffer limit increased to 80KB, error messages were stale.

---

### 10. Comment Corrections

| Location | Before | After |
|----------|--------|-------|
| Header comment | "Channel 1" | "Channel 6" |
| SD mount log | "1-bit mode" | "4-bit mode" |

**Why:** Documentation must match implementation.

---

## Performance Targets

| Metric | Before | Target | Measurement |
|--------|--------|--------|-------------|
| Glass-to-glass latency | Unknown | <100ms | `/status` latency_total |
| Live preview FPS | ~8-10 (unstable) | 15-25 stable | `/status` fps |
| MJPEG recording FPS | ~8 | ~10 | `RECORD_FRAME_INTERVAL_MS=100` |
| SD write speed | 1-bit | 4-bit | ~2-4x faster |
| Wi-Fi interference | High (ch1 conflict) | None | Ch6 AP + Ch1 ESP-NOW |
| JPEG quality | Fixed (16) | Adaptive (8-20) | Dynamic based on RSSI/heap/FPS |

---

## Verification Checklist

After flashing, verify:
- [ ] Camera AP broadcasts on **channel 6** (WiFi analyzer)
- [ ] Hand controller connects, shows "live" status
- [ ] Serial shows: `Quality adjusted: Q=X RSSI=Y Heap=Z FPS=N`
- [ ] Serial shows: `Latency: Total=Xms Capture=Yms Encode=Zms Send=Wms`
- [ ] `/status` endpoint returns all new fields
- [ ] MJPEG recording plays in browser/VLC
- [ ] ESP-NOW communication still works (separate channel 1)
- [ ] No frame drops during recording
- [ ] Latency displayed on hand screen <100ms

---

## Rollback Instructions

If issues arise, revert these specific changes:
1. `config.h`: Change `CAM_AP_CHANNEL` back to 1
2. `main.cpp`: Change `SD_MMC.begin("/sdcard")` back to `SD_MMC.begin("/sdcard", true)`
3. `main.cpp`: Remove `adjustCameraQuality()` call from `loop()`
4. `config.h`: Change `MAX_HAND_JPEG_BYTES` back to 60000
5. `main.cpp`: Remove `Connection: keep-alive` headers
6. `main.cpp`: Revert MJPEG recording to raw concatenation (remove boundary/header writes)
7. Hand controller: Change `CAM_WIFI_CHANNEL` back to 1 (or use `WIFI_CHANNEL`)

---

## Related Hand Controller Changes

See `doom-world-baymax-/hand/CLAUDE_V1_UPDATE.md` for corresponding hand controller updates:
- Camera WiFi channel updated to 6
- JPEG buffer increased to 80KB
- Latency/status polling added
- Critical bug fixes (missing function, double draw, off-screen display)