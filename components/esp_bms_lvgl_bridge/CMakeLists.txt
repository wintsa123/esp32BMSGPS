set(_profile_driver_requires)
if(DEFINED ENV{ESP_BMS_PROFILE_FILE} AND NOT "$ENV{ESP_BMS_PROFILE_FILE}" STREQUAL "")
    get_filename_component(_profile_file "$ENV{ESP_BMS_PROFILE_FILE}" REALPATH)
    if(EXISTS "${_profile_file}")
        include("${_profile_file}")
        if(DEFINED ESP_BMS_PROFILE_DRIVER_REQUIRES)
            set(_profile_driver_requires ${ESP_BMS_PROFILE_DRIVER_REQUIRES})
        endif()
    endif()
endif()

set(_profile_panel_driver "ST7789")
set(_profile_touch_driver "NONE")
if(DEFINED ESP_BMS_PROFILE_DISPLAY_DRIVER)
    set(_profile_panel_driver "${ESP_BMS_PROFILE_DISPLAY_DRIVER}")
endif()
if(DEFINED ESP_BMS_PROFILE_TOUCH_DRIVER)
    set(_profile_touch_driver "${ESP_BMS_PROFILE_TOUCH_DRIVER}")
endif()

idf_component_register(
    SRCS "esp_bms_lvgl_bridge.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_driver_spi esp_lcd esp_lcd_touch esp_lvgl_adapter esp_new_jpeg esp_timer lvgl nvs_flash ${_profile_driver_requires} esp_bms_lvgl_contract
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ESP_BMS_PROFILE_PANEL_ST7796=$<STREQUAL:${_profile_panel_driver},ST7796>
    ESP_BMS_PROFILE_PANEL_ILI9488=$<STREQUAL:${_profile_panel_driver},ILI9488>
    ESP_BMS_PROFILE_PANEL_ILI9341=$<STREQUAL:${_profile_panel_driver},ILI9341>
    ESP_BMS_PROFILE_PANEL_GC9A01=$<STREQUAL:${_profile_panel_driver},GC9A01>
    ESP_BMS_PROFILE_TOUCH_XPT2046=$<STREQUAL:${_profile_touch_driver},XPT2046>
    ESP_BMS_PROFILE_TOUCH_FT5X06=$<STREQUAL:${_profile_touch_driver},FT5X06>
    ESP_BMS_PROFILE_TOUCH_GT1151=$<STREQUAL:${_profile_touch_driver},GT1151>
    ESP_BMS_PROFILE_TOUCH_GT911=$<STREQUAL:${_profile_touch_driver},GT911>
    ESP_BMS_PROFILE_TOUCH_CST816S=$<STREQUAL:${_profile_touch_driver},CST816S>
)
