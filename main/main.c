/* Project entry point.
 *
 * Everything reusable lives in components/ -- that is the actual deliverable of this
 * template (PLAN §4). Bring-up order is app_core's business, so this file stays empty
 * enough that a new project can add its own code here without first having to
 * understand, or accidentally reorder, the startup sequence.
 */
#include "app_core.h"
#include "app_lora.h"
#include "app_ota.h"
#include "esp_err.h"
#include "esp_log.h"
#include "shed_io.h"

void app_main(void)
{
    ESP_ERROR_CHECK(app_core_start());

    /* A shed I/O failure is logged, not fatal: Wi-Fi and OTA keep running, so a bad
     * build or missing LoRaWAN keys can still be fixed over the air. */
    esp_err_t err = shed_io_start();
    if (err != ESP_OK) {
        ESP_LOGE("main", "shed I/O not started: %s", esp_err_to_name(err));
    }

    /* An OTA image stays on probation until the LoRaWAN session is up, so one that breaks
     * the radio or the LoRaWAN stack is reverted. A session restored from the persist
     * partition counts, so a gateway outage during an update does not revert a good image.
     * Without keys (ESP_ERR_INVALID_ARG) there is no session to expect, and holding the
     * image to one would revert the very update that adds them. */
    if (err != ESP_ERR_INVALID_ARG) {
        app_ota_set_project_selftest(app_lora_session_active);
    }
}
