idf_component_register(
    SRCS "app_lora.cpp" "lora_wait.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES nvs_flash
    PRIV_REQUIRES app_cfg jgromes__radiolib esp_timer esp_driver_gpio)
