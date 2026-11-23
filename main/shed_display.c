#include "shed_display.h"

#include "sdkconfig.h"

#if CONFIG_SHED_DISPLAY_ENABLE

#include <stdbool.h>

#include "app_lora.h"
#include "app_net.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oled_fb.h"
#include "shed_io.h"
#include "shed_screen.h"

static const char *TAG = "shed_display";

#define SSD1306_ADDR 0x3C
#define POLL_MS      250
#define REDRAW_US    1000000

#if CONFIG_SHED_DISPLAY_FLIP
#define FLIP true
#else
#define FLIP false
#endif

static esp_lcd_panel_handle_t s_panel;
static esp_err_t s_lora_err;

static int64_t age_s(int64_t now_us, int64_t then_us)
{
    return then_us == 0 ? -1 : (now_us - then_us) / 1000000;
}

static shed_lora_state_t lora_state(void)
{
    switch (s_lora_err) {
    case ESP_OK:
        return app_lora_session_active() ? SHED_LORA_JOINED : SHED_LORA_JOINING;
    case ESP_ERR_INVALID_ARG:   return SHED_LORA_NO_KEYS;
    case ESP_ERR_NOT_SUPPORTED: return SHED_LORA_DISABLED;
    case ESP_FAIL:              return SHED_LORA_FAULT;
    default:                    return SHED_LORA_ERROR;
    }
}

static esp_err_t draw(int page)
{
    int64_t now = esp_timer_get_time();
    shed_io_state_t io;
    app_lora_stats_t lora;
    app_net_info_t net;
    shed_io_get_state(&io);
    app_lora_get_stats(&lora);
    app_net_get_info(&net);

    shed_screen_t s = {
        .lora = lora_state(),
        .uplink_age_s = age_s(now, lora.uplink_us),
        .downlink_age_s = age_s(now, lora.downlink_us),
        .downlink_rssi = (int)lora.downlink_rssi,
        .downlink_snr = lora.downlink_snr,
        .ip = net.ip,
        .wifi_rssi = net.rssi,
        .inputs = io.inputs,
        .outputs = io.outputs,
    };
    switch (net.state) {
    case APP_NET_STATE_UNPROVISIONED: s.wifi = SHED_WIFI_UNPROVISIONED; break;
    case APP_NET_STATE_CONNECTING:    s.wifi = SHED_WIFI_CONNECTING; break;
    case APP_NET_STATE_FALLBACK:      s.wifi = SHED_WIFI_SETUP_AP; break;
    case APP_NET_STATE_CONNECTED:     s.wifi = SHED_WIFI_CONNECTED; break;
    }

    shed_screen_lines_t lines;
    static oled_fb_t fb;   /* 1 KiB; kept off the task stack */
    shed_screen_format(&s, page, lines);
    for (int row = 0; row < OLED_ROWS; row++) {
        oled_fb_text(&fb, row, lines[row]);
    }
    return esp_lcd_panel_draw_bitmap(s_panel, 0, 0, OLED_W, OLED_H, fb.px);
}

static void display_task(void *arg)
{
    (void)arg;
    shed_io_state_t io;
    shed_io_get_state(&io);
    uint32_t changes = io.changes;
    int64_t activity = esp_timer_get_time();
    int64_t woke = activity;
    int64_t drawn = activity;
    int shown = 0;
    bool lit = true;
    esp_err_t last_err = ESP_OK;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        int64_t t = esp_timer_get_time();
        shed_io_get_state(&io);
        /* Any input or output change wakes the screen: the PRG button, a contact, or a
         * command from the network. */
        bool changed = io.changes != changes;
        changes = io.changes;
        if (changed) {
            activity = t;
        }

        bool want = shed_screen_lit(t, activity, CONFIG_SHED_DISPLAY_TIMEOUT_S);
        if (want && !lit) {
            woke = t;
        }
        int page = shed_screen_page(t, woke);
        esp_err_t err = ESP_OK;
        if (want && (changed || !lit || page != shown || t - drawn >= REDRAW_US)) {
            err = draw(page);
            drawn = t;
            shown = page;
        }
        if (err == ESP_OK && want != lit) {
            err = esp_lcd_panel_disp_on_off(s_panel, want);
            if (err == ESP_OK) {
                lit = want;
            }
        }
        if (err != ESP_OK && last_err == ESP_OK) {
            ESP_LOGW(TAG, "panel write failed: %s", esp_err_to_name(err));
        }
        last_err = err;
    }
}

static void vext(bool on)
{
#if CONFIG_SHED_DISPLAY_VEXT_GPIO >= 0
    gpio_set_level(CONFIG_SHED_DISPLAY_VEXT_GPIO, on ? 0 : 1);   /* active low */
#else
    (void)on;
#endif
}

esp_err_t shed_display_start(esp_err_t lora_err)
{
    s_lora_err = lora_err;
    i2c_master_bus_handle_t bus = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    esp_err_t err;

#if CONFIG_SHED_DISPLAY_VEXT_GPIO >= 0
    gpio_config_t vext_cfg = {.pin_bit_mask = 1ULL << CONFIG_SHED_DISPLAY_VEXT_GPIO,
                              .mode = GPIO_MODE_OUTPUT};
    if ((err = gpio_config(&vext_cfg)) != ESP_OK) {
        return err;
    }
    vext(true);
    vTaskDelay(pdMS_TO_TICKS(50));   /* let the panel's supply settle before reset */
#endif

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,
        .sda_io_num = CONFIG_SHED_DISPLAY_SDA_GPIO,
        .scl_io_num = CONFIG_SHED_DISPLAY_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if ((err = i2c_new_master_bus(&bus_cfg, &bus)) != ESP_OK) {
        goto fail;
    }

    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = SSD1306_ADDR,
        .scl_speed_hz = 400000,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    if ((err = esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io)) != ESP_OK) {
        goto fail;
    }

    esp_lcd_panel_ssd1306_config_t ssd_cfg = {.height = OLED_H};
    esp_lcd_panel_dev_config_t panel_cfg = {
        .bits_per_pixel = 1,
        .reset_gpio_num = CONFIG_SHED_DISPLAY_RST_GPIO,
        .vendor_config = &ssd_cfg,
    };
    if ((err = esp_lcd_new_panel_ssd1306(io, &panel_cfg, &s_panel)) != ESP_OK) {
        goto fail;
    }
    /* init is the first I2C traffic, so a missing panel fails here. */
    if ((err = esp_lcd_panel_reset(s_panel)) != ESP_OK ||
        (err = esp_lcd_panel_init(s_panel)) != ESP_OK ||
        (err = esp_lcd_panel_mirror(s_panel, FLIP, FLIP)) != ESP_OK ||
        (err = draw(0)) != ESP_OK ||
        (err = esp_lcd_panel_disp_on_off(s_panel, true)) != ESP_OK) {
        goto fail;
    }

    /* 4 KiB: newlib's float printf for the SNR line is stack-hungry. */
    if (xTaskCreate(display_task, "shed_display", 4096, NULL, 2, NULL) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    return ESP_OK;

fail:
    if (s_panel) {
        esp_lcd_panel_del(s_panel);
        s_panel = NULL;
    }
    if (io) {
        esp_lcd_panel_io_del(io);
    }
    if (bus) {
        i2c_del_master_bus(bus);
    }
#if CONFIG_SHED_DISPLAY_VEXT_GPIO >= 0
    vext(false);
    gpio_reset_pin(CONFIG_SHED_DISPLAY_VEXT_GPIO);
#endif
    return err;
}

#else  /* !CONFIG_SHED_DISPLAY_ENABLE */

esp_err_t shed_display_start(esp_err_t lora_err)
{
    (void)lora_err;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
