# DOOM Controller System Context

Use this document as shared project context when building or modifying any
DOOM controller firmware (HAND, HEAD, BODY, or ESP32-CAM). The components are
separate firmware projects, but their radio, packet, mode, camera, and wiring
interfaces must remain compatible.

## System overview

- **HAND:** ESP32-S3 controller with a 320x240 ST7789 display, rotary encoder,
  joystick and switch, zoom potentiometer, buzzer, and HEAD/BODY indicator LEDs.
  It sends joystick/mode packets and auxiliary commands.
- **HEAD:** Separate ESP32 receiver/controller. Its documented hardware
  includes two servos, ultrasonic distance sensor, MPU6500, laser, head LED,
  buzzer, and switched power for the camera.
- **BODY:** Separate ESP32 receiver/controller for the drive motors.
- **ESP32-CAM:** Separate AI-Thinker camera with OV2640, microSD, and a Wi-Fi
  access point named `DOOM_CAM`.
- HEAD, BODY, HAND, and the camera must interoperate. Do not assume a firmware
  project is present in this workspace unless its source files are available.

## Radio and network contract

- `shared/protocol.h` is the shared source of truth for `DOOM_MAGIC`,
  `WIFI_CHANNEL`, auxiliary target IDs, and packet layouts.
- `WIFI_CHANNEL` is **1**. The ESP32-CAM AP, HAND Wi-Fi station, and ESP-NOW
  receivers must all use channel 1.
- HAND has one Wi-Fi radio. It joins the camera AP and uses ESP-NOW on that
  same channel, allowing camera video and control packets concurrently.
- HEAD and BODY selection is a mode field in every joystick packet, not a
  separate radio channel: `0 = HEAD`, `1 = BODY`. The HAND joystick switch
  toggles the mode; its LEDs and main-menu mode tile indicate the selection.
- Do not place HEAD and BODY on different channels or implement channel
  hopping as a substitute. That would disrupt the camera connection and leave
  one receiver unable to hear control packets at a time.
- Credentials are maintained in the local `hand/include/config.h`; do not
  copy credentials into documentation, logs, commits, or other firmware.

## ESP-NOW wire protocol

Definitions are in `shared/protocol.h`; receiver firmware must use the exact
packed field order and sizes from that header.

- `V3ControlPacket`, `type = 3`: `magic`, `type`, `mode`, signed joystick `x`
  and `y` in `-100..100`, `zoom` in `0..100`, and 32-bit `sequence`.
- HAND sends this packet every 50 ms, including while viewing other screens or
  receiving camera frames.
- `V3AuxCommand`, `type = 4`: `magic`, `type`, `target`, `state`, `value`, and
  32-bit `sequence`. The `value` byte is required by the current layout; it
  carries servo angles (`0..180`) or volume (`0..100`).
- Auxiliary targets: `AUX_CAMERA_POWER = 1`, `AUX_HEAD_LED = 2`,
  `AUX_LASER = 3`, `AUX_SERVO_1 = 4`, `AUX_SERVO_2 = 5`, and `AUX_VOLUME = 6`.
- Older receivers expecting a shorter auxiliary packet are incompatible and
  must be updated against the shared header before these controls can work.
- `SensorPacket` is the receiver-to-HAND telemetry layout: 32-bit sequence,
  float distance in centimeters, and one-byte valid flag.
- To display live heading on DIS, update the HEAD to produce heading telemetry,
  extend the shared receiver packet contract and the HAND receive/display code
  together. The referenced MPU6500 is gyro-only (no magnetometer); integrated
  yaw will drift and must not be presented as a stable absolute compass
  bearing without calibration or an absolute heading sensor.

## HAND inputs and behavior

Pin assignments are in `hand/include/pins.h`. The display theme and timeout
are in `hand/include/theme.h` and `hand/include/config.h`.

- Rotary encoder rotates the selected UI control. Encoder click activates the
  current selection or the visible BACK control on screens that have one.
- Joystick axes drive whichever mode is selected by the joystick switch.
- Joystick-switch click toggles HEAD/BODY mode. Rotary selection does not
  include mode buttons.
- Camera controls are power, BACK, flash, and capture/record. A single capture
  click takes a photo after the double-click interval; a double click toggles
  recording. The display reports capture/recording progress and errors, plus
  measured preview FPS and zoom-potentiometer percentage.
- Camera power and flash controls use red fill for their current on-state and
  orange fill for off. The flash label reads `FLASH ON` while active and uses
  a brief lighter-orange pending fill while waiting for the HTTP response.
- The selectable RFID/SD tile opens HAND gallery screen 8 for saved photos
  and MJPEG recordings. Gallery menu, list, preview, and video-frame client
  logic are implemented on HAND; the current camera server does not expose
  the required endpoints, so HAND reports that the API is unavailable until
  camera firmware is updated. The Iron Man screensaver is screen 9.
- DIS distance is live only when the HEAD sends a valid `SensorPacket` defined
  in `shared/protocol.h`. That packet has distance but no heading. The DIS
  compass graphic is currently static; the HAND does not use an IMU or derive
  a heading.
- The Navigation page is currently static artwork, with no GPS, map,
  destination, or navigation data source. The AI/J.A.R.V.I.S. page is also
  artwork only, with no microphone or AI service wired up.
- Action controls indicate active laser, servo control, head LED, and buzzer
  with red fills. The buzzer control is an on/off toggle sent using
  `AUX_VOLUME` (`state` on/off and `value` 50 when on, 0 when off); it is not a
  rotary-adjustable volume control.
- Camera HTTP work runs in a background FreeRTOS task so the joystick control
  loop continues while camera requests or snapshots are processed.
- The Iron Man screensaver is screen 9. It appears only while the main menu
  (screen 2) is open, after 10 seconds without rotary movement or click.
  Joystick input does not dismiss it. Rotary encoder movement or click wakes
  the screensaver.
- UI palette is orange-on-black RGB565. Keep asset colors and selection
  outlines consistent with the theme.
- AI/microphone artwork is display-only; no AI or microphone service is wired
  to the HAND controller.
- Navigation artwork is display-only; no navigation/location source is wired
  to the HAND controller.

## Camera HTTP interface

The HAND connects to `DOOM_CAM` and uses the camera at `192.168.4.1`.
Endpoint constants are in `hand/include/config.h`:

- `/capture` retrieves a JPEG snapshot for the live preview.
- `/photo` requests a still photo.
- `/record?state=1` starts recording; `state=0` stops it.
- `/flash?state=1` turns flash on; `state=0` turns it off.
- `/zoom?value=` is defined but is not currently sent by HAND.
- The current camera firmware source only has live capture, save-photo,
  record, flash, and status endpoints. It does not yet list or read saved files.

The camera AP channel must remain channel 1. The HEAD auxiliary camera-power
command controls camera power through the HEAD node; the camera's HTTP flash
endpoint is controlled directly over Wi-Fi.

### Planned SD gallery API (HAND client contract)

The HAND gallery client expects these endpoints. The camera firmware project
must implement them for gallery support:

- `GET /media/list?type=photo` or `type=video`: HTTP 200, plain text, one
  basename per line, newest/oldest ordering defined consistently by the camera.
  Photo names should match the camera's saved JPEGs; video names should match
  saved MJPEG recordings. Return no more than 12 entries for the current HAND
  UI. Never return directory paths.
- `GET /media/photo?name=<basename>`: HTTP 200 with `Content-Type:
  image/jpeg` and a correct `Content-Length` for a saved photo.
- `GET /media/video/frame?name=<basename>&frame=<zero-based-index>`: HTTP 200
  with one JPEG frame from the concatenated-JPEG MJPEG recording, or HTTP 404
  when the requested frame is past the end.
- Return HTTP 404 for missing endpoints so the HAND can show
  `CAMERA GALLERY API NOT AVAILABLE`; use other error codes for SD/read errors.
- Camera firmware must validate names against the expected photo/video
  filename patterns and never concatenate untrusted path input into an SD path.
- Bound each returned JPEG to 60,000 bytes (HAND buffer size). Set content
  length so the HAND's HTTP client can read each frame safely.
- The HAND requests video frames sequentially at a maximum of about 8 frames
  per second. This is a preview/playback rate, not the original recording
  rate.

The above endpoints are a planned integration contract, not currently
implemented by the available camera source. The HAND client currently treats
HTTP 404 as “gallery API unavailable”. Build the camera firmware against this
document and verify the routes before expecting the gallery to show files.

## Build and integration rules

- HAND PlatformIO project: `hand/`; shared protocol include: `shared/`.
- Build HAND from its project directory with `pio run`; upload with
  `pio run -t upload`.
- Build HEAD, BODY, and camera separately using their own project
  configurations and boards. The gallery endpoints are a camera-side
  requirement, not implemented by the HAND build.
- Before testing cross-device functions, confirm each receiver implements
  `shared/protocol.h`'s current packet layouts, target IDs, and channel.
- Treat this file as integration context, not as a replacement for the source
  headers. When behavior changes, update the source and this document together.
- Validate camera controls, ESP-NOW reception, motor/servo safety, and the
  physical display on hardware; a successful compile alone does not prove
  cross-device operation.
