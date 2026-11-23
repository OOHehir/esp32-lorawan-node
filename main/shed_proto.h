/* Wire format between the shed node and TTN. The TTN payload formatter in
 * ttn/formatter.js decodes and encodes exactly this; change both together.
 *
 * Uplink, port 1, 3 bytes:
 *   [0] inputs   bit0 PRG button, bit1 input 1, bit2 input 2   (1 = active)
 *   [1] outputs  bit0 output 1, bit1 output 2                  (1 = on)
 *   [2] reason   shed_reason_t
 *
 * Downlink, port 2, 2 or 3 bytes:
 *   [0] output   1 or 2
 *   [1] action   shed_action_t
 *   [2] seconds  pulse length, 1-255; only for SHED_ACTION_PULSE, default 1
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SHED_PORT_STATUS  1
#define SHED_PORT_COMMAND 2
#define SHED_STATUS_LEN   3
#define SHED_OUTPUTS      2

typedef enum {
    SHED_REASON_HEARTBEAT = 0,
    SHED_REASON_INPUT = 1,
    SHED_REASON_OUTPUT = 2,
    SHED_REASON_BOOT = 3,
} shed_reason_t;

typedef enum {
    SHED_ACTION_OFF = 0,
    SHED_ACTION_ON = 1,
    SHED_ACTION_TOGGLE = 2,
    SHED_ACTION_PULSE = 3,
} shed_action_t;

typedef struct {
    uint8_t output;        /* 1-based, as on the wire */
    shed_action_t action;
    uint8_t pulse_s;
} shed_cmd_t;

void shed_encode_status(uint8_t out[SHED_STATUS_LEN], uint8_t inputs, uint8_t outputs,
                        shed_reason_t reason);

/* False for anything malformed: wrong length, output out of range, unknown action,
 * or a zero-second pulse. *cmd is untouched then. */
bool shed_decode_command(const uint8_t *data, size_t len, shed_cmd_t *cmd);
