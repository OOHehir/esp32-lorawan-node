/* LoRaWAN Class C link: joins over OTAA, keeps the session in NVS, and exchanges small
 * frames with the application server.
 *
 * One task owns the radio. Callers queue uplinks with app_lora_send() and receive
 * downlinks through the handler passed to app_lora_start(), which runs on that task:
 * keep it short and do not call app_lora_send() expecting it to go out before the
 * handler returns.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_LORA_MAX_PAYLOAD 51   /* safe for every EU868 data rate down to DR0 */

typedef void (*app_lora_rx_cb_t)(uint8_t port, const uint8_t *data, size_t len);

/* Starts the LoRaWAN task. ESP_ERR_INVALID_ARG if the DevEUI or AppKey is unset or not
 * hex; the task is not started in that case. Needs app_cfg initialised (app_core_start()). */
esp_err_t app_lora_start(app_lora_rx_cb_t on_downlink);

/* Queues an uplink. ESP_ERR_INVALID_SIZE above APP_LORA_MAX_PAYLOAD, ESP_ERR_NO_MEM if
 * the queue is full. The frame is sent once a session exists. */
esp_err_t app_lora_send(uint8_t port, const uint8_t *data, size_t len, bool confirmed);

/* True once a session is active, whether newly joined or restored from NVS. */
bool app_lora_session_active(void);

#ifdef __cplusplus
}
#endif
