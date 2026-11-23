/* The shed's inputs and outputs, bridged to LoRaWAN through app_lora. */
#pragma once

#include "esp_err.h"

/* Configures the GPIOs, starts app_lora and the input-polling task, and queues a
 * confirmed boot status. Returns app_lora_start()'s error if the link cannot start; the
 * outputs are configured (off) either way. */
esp_err_t shed_io_start(void);
