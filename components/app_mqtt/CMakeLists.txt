# No default server: an image built against a placeholder would fail every later update
# check, and a board that installed it could only be recovered by cable. CONFIG_* is empty
# during the requirements pass, which the ENABLE guard skips.
if(CONFIG_APP_MQTT_ENABLE AND "${CONFIG_APP_MQTT_BROKER_URI}" STREQUAL "")
    message(FATAL_ERROR "CONFIG_APP_MQTT_BROKER_URI is not set. Set it in sdkconfig.local, e.g.\n"
                        "    CONFIG_APP_MQTT_BROKER_URI=\"mqtts://nas.lan:8883\"")
endif()

# See components/app_led/CMakeLists.txt: SRCS may depend on CONFIG_*, requirements may not.
if(CONFIG_APP_MQTT_ENABLE)
    set(srcs "app_mqtt.c" "mqtt_cmd.c")
else()
    set(srcs "app_mqtt_disabled.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    PRIV_REQUIRES app_cfg app_diag app_led app_net app_ota app_trust
                  esp_event esp_timer espressif__mqtt)
