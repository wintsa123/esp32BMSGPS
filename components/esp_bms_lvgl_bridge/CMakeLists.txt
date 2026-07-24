set(_profile_driver_requires
    atanisoft__esp_lcd_ili9488
    atanisoft__esp_lcd_touch_xpt2046
    esp_lcd_st7796
    esp_lcd_touch_ft5x06
    esp_lcd_touch_gt1151
)
if(DEFINED ENV{ESP_BMS_PROFILE_FILE} AND NOT "$ENV{ESP_BMS_PROFILE_FILE}" STREQUAL "")
    get_filename_component(_profile_file "$ENV{ESP_BMS_PROFILE_FILE}" REALPATH)
    if(EXISTS "${_profile_file}")
        include("${_profile_file}")
        if(DEFINED ESP_BMS_PROFILE_DRIVER_REQUIRES)
            set(_profile_driver_requires ${ESP_BMS_PROFILE_DRIVER_REQUIRES})
        endif()
    endif()
endif()

idf_component_register(
    SRCS "esp_bms_lvgl_bridge.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_driver_spi esp_lcd esp_lcd_touch esp_lvgl_adapter lvgl nvs_flash ${_profile_driver_requires} esp_bms_lvgl_contract
)
