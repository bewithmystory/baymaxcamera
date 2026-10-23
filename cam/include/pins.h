#ifndef CAM_PINS_H
#define CAM_PINS_H

// =====================================================
// AI-Thinker ESP32-CAM + OV2640 Pin Definitions
// =====================================================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// On-board high-power Flash LED (shared with SD Data1 in 4-bit mode;
// free when SD_MMC is initialized in 1-bit mode)
#define FLASH_LED_PIN      4

// On-board small red status LED (active LOW on GPIO 33)
#define STATUS_LED_PIN    33

#endif // CAM_PINS_H