idf_component_register(
    SRCS "main.c" "shed_io.c" "shed_proto.c" "shed_display.c" "shed_screen.c" "oled_fb.c"
         "font5x7.c"
    INCLUDE_DIRS ""
    REQUIRES app_core app_lora app_net app_ota esp_driver_gpio esp_driver_i2c
             esp_lcd esp_timer)
