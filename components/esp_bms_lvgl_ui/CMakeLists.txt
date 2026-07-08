idf_component_register(
    SRCS "esp_bms_lvgl_ui.c" "bluetoothon.c" "${CMAKE_SOURCE_DIR}/wlanJZ.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl esp_bms_lvgl_contract
)
