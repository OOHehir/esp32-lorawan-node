#include "test.h"
#include "shed_proto.h"

static void status_packs_inputs_outputs_and_reason(void)
{
    uint8_t buf[SHED_STATUS_LEN];
    shed_encode_status(buf, 0x05, 0x02, SHED_REASON_INPUT);
    CHECK_INT(buf[0], 0x05);
    CHECK_INT(buf[1], 0x02);
    CHECK_INT(buf[2], SHED_REASON_INPUT);
}

static void status_masks_bits_that_do_not_exist(void)
{
    uint8_t buf[SHED_STATUS_LEN];
    shed_encode_status(buf, 0xff, 0xff, SHED_REASON_HEARTBEAT);
    CHECK_INT(buf[0], 0x07);
    CHECK_INT(buf[1], 0x03);
}

static void two_byte_command_decodes_with_one_second_pulse(void)
{
    shed_cmd_t cmd;
    const uint8_t in[] = {2, SHED_ACTION_ON};
    CHECK(shed_decode_command(in, sizeof in, &cmd), "valid command rejected");
    CHECK_INT(cmd.output, 2);
    CHECK_INT(cmd.action, SHED_ACTION_ON);
    CHECK_INT(cmd.pulse_s, 1);
}

static void pulse_takes_its_length_from_the_third_byte(void)
{
    shed_cmd_t cmd;
    const uint8_t in[] = {1, SHED_ACTION_PULSE, 30};
    CHECK(shed_decode_command(in, sizeof in, &cmd), "valid pulse rejected");
    CHECK_INT(cmd.pulse_s, 30);
}

static void malformed_commands_are_rejected(void)
{
    shed_cmd_t cmd = {.output = 9};
    const uint8_t output_zero[] = {0, SHED_ACTION_ON};
    const uint8_t output_three[] = {3, SHED_ACTION_ON};
    const uint8_t unknown_action[] = {1, 4};
    const uint8_t zero_pulse[] = {1, SHED_ACTION_PULSE, 0};
    const uint8_t length_on_non_pulse[] = {1, SHED_ACTION_ON, 5};
    const uint8_t too_long[] = {1, SHED_ACTION_PULSE, 5, 0};

    CHECK(!shed_decode_command(output_zero, 1, &cmd), "1-byte command accepted");
    CHECK(!shed_decode_command(output_zero, sizeof output_zero, &cmd), "output 0 accepted");
    CHECK(!shed_decode_command(output_three, sizeof output_three, &cmd), "output 3 accepted");
    CHECK(!shed_decode_command(unknown_action, sizeof unknown_action, &cmd), "action 4 accepted");
    CHECK(!shed_decode_command(zero_pulse, sizeof zero_pulse, &cmd), "0 s pulse accepted");
    CHECK(!shed_decode_command(length_on_non_pulse, sizeof length_on_non_pulse, &cmd),
          "length on a non-pulse accepted");
    CHECK(!shed_decode_command(too_long, sizeof too_long, &cmd), "4-byte command accepted");
    CHECK_INT(cmd.output, 9);
}

void test_shed_proto(void)
{
    SUITE("shed_proto");
    status_packs_inputs_outputs_and_reason();
    status_masks_bits_that_do_not_exist();
    two_byte_command_decodes_with_one_second_pulse();
    pulse_takes_its_length_from_the_third_byte();
    malformed_commands_are_rejected();
}
