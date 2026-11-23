#include "shed_io.h"

#include <stdbool.h>

#include "app_lora.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "shed_proto.h"

static const char *TAG = "shed_io";

#define POLL_MS 20
/* An input must read the same for this many polls (60 ms) before it counts as changed,
 * which absorbs contact bounce on a door switch. */
#define DEBOUNCE_POLLS 3

static const int s_in_gpio[3] = {CONFIG_SHED_BUTTON_GPIO, CONFIG_SHED_IN1_GPIO, CONFIG_SHED_IN2_GPIO};
static const int s_out_gpio[SHED_OUTPUTS] = {CONFIG_SHED_OUT1_GPIO, CONFIG_SHED_OUT2_GPIO};

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_outputs;        /* bit n = output n+1 on */
static volatile uint8_t s_inputs; /* debounced; bit n = input n active */
static esp_timer_handle_t s_pulse_timer[SHED_OUTPUTS];
static uint32_t s_changes;        /* under s_lock */

static void send_status(shed_reason_t reason, bool confirmed)
{
    uint8_t buf[SHED_STATUS_LEN];
    taskENTER_CRITICAL(&s_lock);
    uint8_t outputs = s_outputs;
    taskEXIT_CRITICAL(&s_lock);
    shed_encode_status(buf, s_inputs, outputs, reason);
    esp_err_t err = app_lora_send(SHED_PORT_STATUS, buf, sizeof buf, confirmed);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "status (reason %d) not queued: %s", reason, esp_err_to_name(err));
    }
}

static void set_output(int idx, bool on)
{
    taskENTER_CRITICAL(&s_lock);
    s_outputs = on ? (s_outputs | (1u << idx)) : (s_outputs & ~(1u << idx));
    s_changes++;
    taskEXIT_CRITICAL(&s_lock);
    if (s_out_gpio[idx] >= 0) {
        gpio_set_level(s_out_gpio[idx], on == CONFIG_SHED_OUT_ACTIVE_HIGH);
    }
}

static void pulse_end(void *arg)
{
    int idx = (int)(intptr_t)arg;
    set_output(idx, false);
    ESP_LOGI(TAG, "output %d pulse ended", idx + 1);
    send_status(SHED_REASON_OUTPUT, false);
}

/* Runs on the app_lora task. */
static void on_downlink(uint8_t port, const uint8_t *data, size_t len)
{
    shed_cmd_t cmd;
    if (port != SHED_PORT_COMMAND || !shed_decode_command(data, len, &cmd)) {
        ESP_LOGW(TAG, "ignored downlink: port %u, %u bytes", port, (unsigned)len);
        return;
    }
    int idx = cmd.output - 1;
    if (s_out_gpio[idx] < 0) {
        ESP_LOGW(TAG, "output %u is not wired (GPIO -1)", cmd.output);
        return;
    }
    esp_timer_stop(s_pulse_timer[idx]);   /* a new command cancels a running pulse */
    switch (cmd.action) {
    case SHED_ACTION_OFF:
        set_output(idx, false);
        break;
    case SHED_ACTION_ON:
        set_output(idx, true);
        break;
    case SHED_ACTION_TOGGLE:
        set_output(idx, !(s_outputs & (1u << idx)));
        break;
    case SHED_ACTION_PULSE:
        set_output(idx, true);
        esp_timer_start_once(s_pulse_timer[idx], (uint64_t)cmd.pulse_s * 1000000);
        break;
    }
    ESP_LOGI(TAG, "output %u: action %d", cmd.output, cmd.action);
    send_status(SHED_REASON_OUTPUT, false);
}

static uint8_t read_inputs(void)
{
    uint8_t bits = 0;
    for (int i = 0; i < 3; i++) {
        if (s_in_gpio[i] >= 0 && gpio_get_level(s_in_gpio[i]) == 0) {
            bits |= 1u << i;
        }
    }
    return bits;
}

static void io_task(void *arg)
{
    (void)arg;
    uint8_t candidate = s_inputs;
    int stable = 0;
    int64_t last_heartbeat = esp_timer_get_time();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        uint8_t now = read_inputs();
        if (now != candidate) {
            candidate = now;
            stable = 0;
        } else if (now != s_inputs && ++stable >= DEBOUNCE_POLLS) {
            ESP_LOGI(TAG, "inputs 0x%02x -> 0x%02x", s_inputs, now);
            taskENTER_CRITICAL(&s_lock);
            s_inputs = now;
            s_changes++;
            taskEXIT_CRITICAL(&s_lock);
            send_status(SHED_REASON_INPUT, false);
        }
        if (esp_timer_get_time() - last_heartbeat >= (int64_t)CONFIG_APP_LORA_HEARTBEAT_S * 1000000) {
            last_heartbeat = esp_timer_get_time();
            send_status(SHED_REASON_HEARTBEAT, false);
        }
    }
}

void shed_io_get_state(shed_io_state_t *out)
{
    taskENTER_CRITICAL(&s_lock);
    out->inputs = s_inputs;
    out->outputs = s_outputs;
    out->changes = s_changes;
    taskEXIT_CRITICAL(&s_lock);
}

esp_err_t shed_io_start(void)
{
    for (int i = 0; i < SHED_OUTPUTS; i++) {
        const esp_timer_create_args_t args = {
            .callback = pulse_end, .arg = (void *)(intptr_t)i, .name = "shed_pulse"};
        ESP_ERROR_CHECK(esp_timer_create(&args, &s_pulse_timer[i]));
        if (s_out_gpio[i] < 0) {
            continue;
        }
        gpio_config_t out = {.pin_bit_mask = 1ULL << s_out_gpio[i], .mode = GPIO_MODE_OUTPUT};
        ESP_ERROR_CHECK(gpio_config(&out));
        set_output(i, false);
    }
    for (int i = 0; i < 3; i++) {
        if (s_in_gpio[i] < 0) {
            continue;
        }
        gpio_config_t in = {.pin_bit_mask = 1ULL << s_in_gpio[i], .mode = GPIO_MODE_INPUT,
                            .pull_up_en = GPIO_PULLUP_ENABLE};
        ESP_ERROR_CHECK(gpio_config(&in));
    }
    s_inputs = read_inputs();

    esp_err_t err = app_lora_start(on_downlink);
    if (err != ESP_OK) {
        return err;
    }
    /* Confirmed, so the first downlink after a join also tells the network the node is
     * listening; it is the only confirmed uplink, as TTN's fair-use policy allows about ten
     * downlinks a day and every confirmation spends one. */
    send_status(SHED_REASON_BOOT, true);
    if (xTaskCreate(io_task, "shed_io", 3072, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
