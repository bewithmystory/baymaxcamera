#ifndef CAM_CONFIG_H
#define CAM_CONFIG_H

#include <Arduino.h>

// =====================================================
// Radio & SoftAP Configuration
// =====================================================
// Channel 6 or 11 to avoid ESP-NOW channel 1 interference
static const char *CAM_AP_SSID     = "DOOM_CAM";
static const char *CAM_AP_PASSWORD = "doomcam88";
static const int   CAM_AP_CHANNEL  = 6;           // Changed from 1 to 6
static const int   CAM_MAX_CLIENTS = 4;

// =====================================================
// Camera Quality & Performance Tuning
// =====================================================
// Dynamic quality: adjusts based on RSSI/heap/FPS
static const bool    DYNAMIC_QUALITY_ENABLED = true;
static const int     JPEG_QUALITY_MIN = 8;        // Best quality (lower = better)
static const int     JPEG_QUALITY_MAX = 20;       // Lower quality for speed
static const int     JPEG_QUALITY_DEFAULT = 12;   // Starting quality
static const int     TARGET_FPS_MIN = 10;         // Minimum acceptable FPS
static const int     TARGET_FPS_MAX = 25;         // Maximum target FPS

// =====================================================
// Recording Configuration
// =====================================================
static const uint32_t RECORD_FRAME_INTERVAL_MS = 100;  // ~10 FPS MJPEG recording (was 125ms ~8 FPS)
static const size_t   MAX_HAND_JPEG_BYTES      = 80000; // Increased from 60KB to 80KB for better quality
static const size_t   MAX_GALLERY_ITEMS        = 12;    // Max lines for /media/list

// =====================================================
// HTTP Server Optimization
// =====================================================
static const bool    HTTP_KEEP_ALIVE_ENABLED = true;
static const uint32_t HTTP_STREAM_TIMEOUT_MS = 5000;

// =====================================================
// Latency Tracking
// =====================================================
static const bool    LATENCY_TRACKING_ENABLED = true;

// Flash LED active level
#define FLASH_ON_LEVEL  HIGH
#define FLASH_OFF_LEVEL LOW

#endif // CAM_CONFIG_H