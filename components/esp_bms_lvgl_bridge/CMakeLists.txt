idf_component_register(
    SRCS "esp_bms_lvgl_bridge.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_driver_spi esp_lcd esp_lcd_touch esp_lvgl_adapter lvgl nvs_flash atanisoft__esp_lcd_ili9488 atanisoft__esp_lcd_touch_xpt2046 esp_lcd_st7796 esp_lcd_touch_ft5x06 esp_lcd_touch_gt1151 esp_bms_lvgl_contract
)
