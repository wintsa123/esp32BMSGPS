idf_component_register(
    SRCS "esp_bms_display_service.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_bms_lvgl_bridge esp_bms_lvgl_ui esp_timer freertos lvgl
)
