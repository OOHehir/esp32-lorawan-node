#include "shed_proto.h"

void shed_encode_status(uint8_t out[SHED_STATUS_LEN], uint8_t inputs, uint8_t outputs,
                        shed_reason_t reason)
{
    out[0] = inputs & 0x07;
    out[1] = outputs & 0x03;
    out[2] = (uint8_t)reason;
}

bool shed_decode_command(const uint8_t *data, size_t len, shed_cmd_t *cmd)
{
    if (len < 2 || len > 3) {
        return false;
    }
    if (data[0] < 1 || data[0] > SHED_OUTPUTS || data[1] > SHED_ACTION_PULSE) {
        return false;
    }
    uint8_t pulse_s = 1;
    if (len == 3) {
        if (data[1] != SHED_ACTION_PULSE || data[2] == 0) {
            return false;
        }
        pulse_s = data[2];
    }
    cmd->output = data[0];
    cmd->action = (shed_action_t)data[1];
    cmd->pulse_s = pulse_s;
    return true;
}
