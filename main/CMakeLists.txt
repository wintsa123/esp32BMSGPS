idf_component_register(
    SRCS "idf_main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_bms_idf_runtime esp_bms_lvgl_bridge esp_bms_lvgl_ui lvgl esp_lvgl_adapter
)
