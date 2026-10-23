# DOOM Camera V4

Firmware for the AI-Thinker ESP32-CAM with OV2640 camera and microSD card.
It provides a Wi-Fi access point and HTTP API so the DOOM HAND controller can
preview the camera, save photos, record MJPEG video, and browse saved media.

## Hardware and project files

- `cam/` — ESP32-CAM PlatformIO project.
- `cam/src/main.cpp` — camera setup, SD-card operations, HTTP routes, and
  recording logic.
- `cam/include/config.h` — access-point settings, HAND-compatible JPEG and
  gallery limits, and recording frame interval. Keep network credentials in
  this source configuration; do not copy them into shared documentation.
- `cam/include/pins.h` — AI-Thinker camera, flash LED, and status LED pin map.
- `DOOM_CONTROLLER_CONTEXT.md` — cross-device radio and controller integration
  contract.

The HAND controller firmware is a separate project. Its gallery client uses
the camera's list, photo, and video-frame endpoints. The HAND photo-preview
screen was also updated to retain and redraw a loaded photo when the gallery
UI refreshes.

## Build and upload

Install PlatformIO, connect the ESP32-CAM over its USB-to-serial adapter, then
run:

```sh
cd cam
pio run
pio run -t upload --upload-port <serial-port>
```

The configured serial upload speed is 115200 baud. Select the port belonging
to the ESP32-CAM, not the HAND controller. The successful camera build and
firmware binary are under `cam/.pio/build/esp32cam/`.

After boot, the camera starts its access point and HTTP server. The HAND joins
that access point and uses the camera at `192.168.4.1`. Keep the camera AP,
HAND Wi-Fi, and ESP-NOW on channel 1 for coexistence; see
`DOOM_CONTROLLER_CONTEXT.md` for the complete system contract.

## HTTP API

Use `http://192.168.4.1` as the base URL after connecting to the camera
network. The AP credentials are defined in `cam/include/config.h`.

| Request | Purpose |
| --- | --- |
| `GET /status` | Camera, SD-card, recording, flash, and AP status. |
| `GET /capture` | Current JPEG frame for the live HAND preview. |
| `GET /photo` | Capture and save a still JPEG to the SD card. |
| `GET /record?state=1` | Start recording concatenated JPEG frames as MJPEG. |
| `GET /record?state=0` | Stop and close the current recording. |
| `GET /flash?state=1` or `state=0` | Turn the camera flash LED on or off. |
| `GET /zoom?value=0..100` | Accept zoom telemetry; does not optically zoom the camera. |
| `GET /media/list?type=photo` | List up to 12 newest saved photo basenames. |
| `GET /media/list?type=video` | List up to 12 newest, non-empty, closed recording basenames. |
| `GET /media/photo?name=photo_0001.jpg` | Fetch a saved JPEG for the HAND gallery. |
| `GET /media/video/frame?name=doom_0001.mjpeg&frame=0` | Fetch a single zero-based JPEG frame for HAND playback. |
| `GET /media/video?name=doom_0001.mjpeg` | Download the complete MJPEG recording. |

Use the exact basenames returned by `/media/list`; do not prepend a directory
path. Photos are named `photo_####.jpg`; recordings are named
`doom_####.mjpeg`. A recording must be stopped before it appears in the video
gallery. MJPEG is a sequence of JPEG frames, not an MP4 file; the HAND gallery
plays it by requesting frames in order.

## HAND compatibility and recent fixes

- Camera stills and individual video frames must be no larger than 60,000
  bytes because that is the HAND JPEG buffer limit.
- The camera uses QVGA with a lower JPEG quality setting when PSRAM is present
  to help keep frames within that limit. Oversized still captures are rejected
  instead of saving files the HAND cannot display.
- Recording samples frames at about 8 FPS. Frames above the HAND size limit
  are skipped.
- The SD card mounts in 1-bit mode so GPIO 4 remains available for the flash
  LED.
- Gallery listings are returned newest-first and exclude an active recording
  and empty video files.
- The HAND retains a loaded photo for gallery redraws, preventing the preview
  from going black after a UI refresh.

## Basic troubleshooting

- If `/status` reports `sd=not_ready`, check that a supported microSD card is
  inserted and seated before the camera boots.
- If the photo or video list is empty, capture a new photo or make and stop a
  recording, then reload the list.
- If a photo cannot be displayed on the HAND, it may exceed the 60,000-byte
  buffer limit; retake it after confirming the updated camera firmware is
  installed.
- If the HAND cannot reach the camera, verify it is connected to the camera
  access point and that the system remains on channel 1.
