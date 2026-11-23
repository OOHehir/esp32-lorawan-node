/* LoRaWAN Class C link on RadioLib. See app_lora.h for the contract. */
#include "app_lora.h"

#include <stdio.h>
#include <string.h>

#include <RadioLib.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "app_cfg.h"
#include "lora_wait.h"
#include "nvs.h"

static const char *TAG = "app_lora";

#if CONFIG_APP_LORA_ENABLE

namespace {

constexpr const char *NVS_NS = "lora";
constexpr const char *NVS_KEY_PERSIST = "persist";
constexpr const char *NVS_KEY_SESSION = "session";
/* How often to check the radio for a Class C downlink. The SX1276 holds a received
 * frame until it is read, so this sets latency, not whether frames are lost. */
constexpr TickType_t RX_POLL_TICKS = pdMS_TO_TICKS(20);
constexpr int JOIN_RETRY_MIN_S = 30;

struct Uplink {
    uint8_t port;
    uint8_t len;
    bool confirmed;
    uint8_t data[APP_LORA_MAX_PAYLOAD];
};

QueueHandle_t s_queue;
app_lora_rx_cb_t s_on_downlink;
volatile bool s_session_active;

EspHal *s_hal;
SX1276 *s_radio;
LoRaWANNode *s_node;

bool parse_hex(const char *hex, uint8_t *out, size_t len)
{
    if (strlen(hex) != len * 2) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned v;
        if (sscanf(hex + 2 * i, "%2x", &v) != 1) {
            return false;
        }
        out[i] = (uint8_t)v;
    }
    return true;
}

uint64_t to_u64(const uint8_t b[8])
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v = (v << 8) | b[i];
    }
    return v;
}

/* RadioLib calls these after a join and after every uplink. The persistence buffer
 * holds the DevNonce counter; losing it makes the network reject every later join as a
 * replay, so a failed write is logged loudly rather than ignored. */
void nvs_store(const char *key, uint8_t *buf, size_t len)
{
    nvs_handle_t h;
    esp_err_t err = app_cfg_persist_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, key, buf, len);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving %s to NVS failed: %s", key, esp_err_to_name(err));
    }
}

/* An absent or wrong-sized blob leaves the buffer zeroed; RadioLib's signature check
 * then rejects it and the node starts fresh. */
void nvs_restore(const char *key, uint8_t *buf, size_t len)
{
    memset(buf, 0, len);
    nvs_handle_t h;
    if (app_cfg_persist_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t stored = len;
    if (nvs_get_blob(h, key, buf, &stored) != ESP_OK || stored != len) {
        memset(buf, 0, len);
    }
    nvs_close(h);
}

void store_persist(uint8_t *buf, size_t len) { nvs_store(NVS_KEY_PERSIST, buf, len); }
void restore_persist(uint8_t *buf, size_t len) { nvs_restore(NVS_KEY_PERSIST, buf, len); }
void store_session(uint8_t *buf, size_t len) { nvs_store(NVS_KEY_SESSION, buf, len); }
void restore_session(uint8_t *buf, size_t len) { nvs_restore(NVS_KEY_SESSION, buf, len); }

/* Without a sleep callback RadioLib spins through every wait, including the 5 s
 * before a join-accept, which starves the idle task and trips the task watchdog. The wait
 * itself, and why it must end on DIO0, is in lora_wait.h. */
int64_t io_now_us(void *) { return esp_timer_get_time(); }
bool io_irq(void *) { return gpio_get_level((gpio_num_t)CONFIG_APP_LORA_PIN_DIO0) != 0; }
void io_sleep_tick(void *) { vTaskDelay(1); }

void lora_sleep(RadioLibTime_t ms)
{
    static const lora_wait_io_t io = {io_now_us, io_irq, io_sleep_tick, nullptr};
    lora_wait(&io, (uint32_t)ms, 2);
}

void deliver(const uint8_t *data, size_t len, const LoRaWANEvent_t &ev)
{
    /* Port 0 carries MAC commands only, which RadioLib has already handled. */
    if (ev.fPort == 0 || !s_on_downlink) {
        return;
    }
    ESP_LOGI(TAG, "downlink: port %u, %u bytes, fcnt %lu", ev.fPort, (unsigned)len,
             (unsigned long)ev.fCnt);
    s_on_downlink(ev.fPort, data, len);
}

void join()
{
    int wait_s = JOIN_RETRY_MIN_S;
    for (;;) {
        int16_t state = s_node->activateOTAA();
        if (state == RADIOLIB_LORAWAN_NEW_SESSION || state == RADIOLIB_LORAWAN_SESSION_RESTORED) {
            ESP_LOGI(TAG, "%s", state == RADIOLIB_LORAWAN_NEW_SESSION ? "joined" : "session restored");
            return;
        }
        ESP_LOGW(TAG, "join failed (%d), retrying in %d s", state, wait_s);
        vTaskDelay(pdMS_TO_TICKS(wait_s * 1000));
        wait_s = wait_s * 2 > CONFIG_APP_LORA_JOIN_RETRY_MAX_S ? CONFIG_APP_LORA_JOIN_RETRY_MAX_S
                                                               : wait_s * 2;
    }
}

void send(const Uplink &up)
{
    uint8_t down[256];
    size_t down_len = 0;
    LoRaWANEvent_t ev_up = {}, ev_down = {};
    int16_t state = s_node->sendReceive(up.data, up.len, up.port, down, &down_len, up.confirmed,
                                        &ev_up, &ev_down);
    if (state < RADIOLIB_ERR_NONE) {
        ESP_LOGW(TAG, "uplink on port %u failed: %d", up.port, state);
        return;
    }
    ESP_LOGI(TAG, "uplink sent: port %u, fcnt %lu%s", up.port, (unsigned long)ev_up.fCnt,
             up.confirmed ? (state > 0 ? ", confirmed" : ", NOT confirmed") : "");
    if (state > 0) {
        deliver(down, down_len, ev_down);
    }
}

void lora_task(void *)
{
    s_node->setCallbackStorePersistence(store_persist);
    s_node->setCallbackRestorePersistence(restore_persist);
    s_node->setCallbackStoreSession(store_session);
    s_node->setCallbackRestoreSession(restore_session);
    int16_t state = s_node->loadBuffers();
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGI(TAG, "no usable saved session (%d); will join", state);
    }

    join();
    s_session_active = true;

    state = s_node->setClass(RADIOLIB_LORAWAN_CLASS_C);
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "switching to Class C failed: %d; downlinks only after uplinks", state);
    }

    Uplink up;
    uint8_t down[256];
    for (;;) {
        if (xQueueReceive(s_queue, &up, RX_POLL_TICKS) == pdTRUE) {
            send(up);
            continue;
        }
        size_t down_len = 0;
        LoRaWANEvent_t ev = {};
        if (s_node->getDownlinkClassC(down, &down_len, &ev) > 0) {
            deliver(down, down_len, ev);
        }
    }
}

}  // namespace

extern "C" esp_err_t app_lora_start(app_lora_rx_cb_t on_downlink)
{
    uint8_t dev_eui[8], join_eui[8], app_key[16];
    if (!parse_hex(CONFIG_APP_LORA_DEV_EUI, dev_eui, sizeof dev_eui) ||
        !parse_hex(CONFIG_APP_LORA_JOIN_EUI, join_eui, sizeof join_eui) ||
        !parse_hex(CONFIG_APP_LORA_APP_KEY, app_key, sizeof app_key)) {
        ESP_LOGE(TAG, "DevEUI, JoinEUI or AppKey missing or not hex; set them in sdkconfig.local");
        return ESP_ERR_INVALID_ARG;
    }
    /* The two EUIs are what the TTN console asks for when registering a board. */
    ESP_LOGI(TAG, "DevEUI %s, JoinEUI %s", CONFIG_APP_LORA_DEV_EUI, CONFIG_APP_LORA_JOIN_EUI);

    /* The DevNonce counter lives in app_cfg's persist partition, which provisioning and
     * factory reset leave alone. Without it a join counter can roll back, so refuse to run. */
    nvs_handle_t probe;
    esp_err_t err = app_cfg_persist_open(NVS_NS, NVS_READWRITE, &probe);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "persist partition unavailable (%s); flash the current partition table by cable",
                 esp_err_to_name(err));
        return err;
    }
    nvs_close(probe);

    s_on_downlink = on_downlink;
    s_queue = xQueueCreate(8, sizeof(Uplink));
    if (!s_queue) {
        return ESP_ERR_NO_MEM;
    }

    s_hal = new EspHal(CONFIG_APP_LORA_PIN_SCK, CONFIG_APP_LORA_PIN_MISO, CONFIG_APP_LORA_PIN_MOSI);
    s_radio = new SX1276(new Module(s_hal, CONFIG_APP_LORA_PIN_NSS, CONFIG_APP_LORA_PIN_DIO0,
                                    CONFIG_APP_LORA_PIN_RST, CONFIG_APP_LORA_PIN_DIO1));
    ConfigLoRa_t cfg = {};
    cfg.frequency = RADIOLIB_UNIT_MEGA(868);   /* Hz; placeholder, LoRaWAN sets every channel */
    int16_t state = s_radio->begin(cfg);
    if (state != RADIOLIB_ERR_NONE) {
        /* -2 (chip not found) points at the pins; other codes at the configuration. */
        ESP_LOGE(TAG, "SX1276 init failed: %d", state);
        return ESP_FAIL;
    }

    s_node = new LoRaWANNode(s_radio, &EU868);
    s_node->setSleepFunction(lora_sleep);
    /* LoRaWAN 1.0.4 has a single root key, so the NwkKey argument is NULL. */
    state = s_node->beginOTAA(to_u64(join_eui), to_u64(dev_eui), nullptr, app_key);
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "beginOTAA failed: %d", state);
        return ESP_FAIL;
    }

    /* Core 1 and priority 10 keep Wi-Fi (core 0, priority 23) from delaying the wake-up
     * for a receive window. */
    if (xTaskCreatePinnedToCore(lora_task, "app_lora", 8192, nullptr, 10, nullptr, 1) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

extern "C" esp_err_t app_lora_send(uint8_t port, const uint8_t *data, size_t len, bool confirmed)
{
    if (len > APP_LORA_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!s_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    Uplink up = {};
    up.port = port;
    up.len = (uint8_t)len;
    up.confirmed = confirmed;
    memcpy(up.data, data, len);
    return xQueueSend(s_queue, &up, 0) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM;
}

extern "C" bool app_lora_session_active(void)
{
    return s_session_active;
}

#else  /* !CONFIG_APP_LORA_ENABLE */

extern "C" esp_err_t app_lora_start(app_lora_rx_cb_t) { return ESP_ERR_NOT_SUPPORTED; }
extern "C" esp_err_t app_lora_send(uint8_t, const uint8_t *, size_t, bool) { return ESP_ERR_NOT_SUPPORTED; }
extern "C" bool app_lora_session_active(void) { return false; }

#endif
