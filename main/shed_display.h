/* Status screen on the board's SSD1306 OLED. */
#pragma once

#include "esp_err.h"

/* Powers the panel and starts the task that draws it. lora_err is what shed_io_start()
 * returned, so the screen can say why LoRaWAN is off. Returns ESP_ERR_NOT_SUPPORTED when
 * the display is disabled in menuconfig, or the I2C error when no panel answers. */
esp_err_t shed_display_start(esp_err_t lora_err);
