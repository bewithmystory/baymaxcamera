#ifndef CAM_CONFIG_H
#define CAM_CONFIG_H

#include <Arduino.h>

// =====================================================
// Radio & SoftAP Configuration (Must use Channel 1)
// =====================================================
static const char *CAM_AP_SSID     = "DOOM_CAM";
static const char *CAM_AP_PASSWORD = "doomcam88";
static const int   CAM_AP_CHANNEL  = 1;
static const int   CAM_MAX_CLIENTS = 4;

// =====================================================
// Recording & Buffer Limits (HAND Contract)
// =====================================================
static const uint32_t RECORD_FRAME_INTERVAL_MS = 125;   // ~8 FPS MJPEG recording
static const size_t   MAX_HAND_JPEG_BYTES      = 60000; // HAND buffer ceiling
static const size_t   MAX_GALLERY_ITEMS        = 12;    // Max lines for /media/list

// Flash LED active level
#define FLASH_ON_LEVEL  HIGH
#define FLASH_OFF_LEVEL LOW

#endif // CAM_CONFIG_H
