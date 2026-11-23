idf_component_register(
    SRCS "main.c" "shed_io.c" "shed_proto.c"
    INCLUDE_DIRS ""
    REQUIRES app_core app_lora app_ota esp_driver_gpio esp_timer)
