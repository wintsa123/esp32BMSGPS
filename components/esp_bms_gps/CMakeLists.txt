idf_component_register(
    SRCS "esp_bms_gps.c" "esp_bms_gps_stream.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_bms_idf_runtime esp_bms_lvgl_ui esp_http_server esp_timer
)
