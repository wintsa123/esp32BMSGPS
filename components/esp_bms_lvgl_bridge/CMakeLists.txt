idf_component_register(
    SRCS "esp_bms_lvgl_bridge.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_lcd esp_lcd_touch esp_lvgl_adapter lvgl atanisoft__esp_lcd_touch_xpt2046 esp_bms_lvgl_contract
)
