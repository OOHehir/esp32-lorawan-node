# No default server: an image built against a placeholder would fail every later update
# check, and a board that installed it could only be recovered by cable. CONFIG_* is empty
# during the requirements pass, which the ENABLE guard skips.
if(CONFIG_APP_OTA_ENABLE AND "${CONFIG_APP_OTA_BASE_URL}" STREQUAL "")
    message(FATAL_ERROR "CONFIG_APP_OTA_BASE_URL is not set. Set it in sdkconfig.local, e.g.\n"
                        "    CONFIG_APP_OTA_BASE_URL=\"https://nas.lan:8443/fw\"")
endif()

# See components/app_led/CMakeLists.txt: SRCS may depend on CONFIG_*, requirements may not.
if(CONFIG_APP_OTA_ENABLE)
    set(srcs "app_ota.c" "manifest.c" "update_policy.c" "rollback_policy.c"
             "ota_record.c" "sha_hex.c" "image_check.c" "selftest.c")
else()
    set(srcs "app_ota_disabled.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES esp_event
    PRIV_REQUIRES app_cfg app_diag app_httpd app_led app_net app_trust
                  app_update esp_app_format esp_http_client esp_https_ota
                  esp_partition esp_timer espressif__cjson mbedtls nvs_flash
    )
