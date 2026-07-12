idf_component_register(
    SRCS "esp_bms_lvgl_ui.c" "controller_digits_72.c" "bluetoothon.c" "settings_zh_16.c" "settings_zh_13.c" "settings_zh_10.c" "${CMAKE_SOURCE_DIR}/wlanJZ.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl esp_bms_lvgl_contract
)
