idf_component_register(
    SRCS "esp_bms_bms_ble.c"
    INCLUDE_DIRS "include"
    REQUIRES bt esp_bms_idf_runtime esp_bms_lvgl_ui esp_timer
)
