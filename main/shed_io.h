/* The shed's inputs and outputs, bridged to LoRaWAN through app_lora. */
#pragma once

#include <stdint.h>

#include "esp_err.h"

/* Configures the GPIOs, starts app_lora and the input-polling task, and queues a
 * confirmed boot status. Returns app_lora_start()'s error if the link cannot start; the
 * outputs are configured (off) either way. */
esp_err_t shed_io_start(void);

typedef struct {
    uint8_t inputs;        /* debounced; bit0 button, bit1 input 1, bit2 input 2 */
    uint8_t outputs;       /* bit0 output 1, bit1 output 2 */
    uint32_t changes;      /* counts input and output changes, so a reader polling slower
                              than a button tap still sees one happened */
} shed_io_state_t;

void shed_io_get_state(shed_io_state_t *out);
