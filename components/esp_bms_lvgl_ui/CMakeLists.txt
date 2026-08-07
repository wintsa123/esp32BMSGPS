if(NOT DEFINED ESP_BMS_FEATURE_DASHBOARD_S1000RR)
    set(ESP_BMS_FEATURE_DASHBOARD_S1000RR 1)
endif()
if(NOT DEFINED ESP_BMS_FEATURE_DASHBOARD_CONTROLLER)
    set(ESP_BMS_FEATURE_DASHBOARD_CONTROLLER 1)
endif()
if(NOT DEFINED ESP_BMS_FEATURE_DASHBOARD_FIREBLADE)
    set(ESP_BMS_FEATURE_DASHBOARD_FIREBLADE 1)
endif()
foreach(feature BMS CONTROLLER GPS NETWORK)
    if(NOT DEFINED ESP_BMS_FEATURE_${feature})
        set(ESP_BMS_FEATURE_${feature} 1)
    endif()
endforeach()
if(NOT DEFINED ESP_BMS_FEATURE_AUDIO)
    set(ESP_BMS_FEATURE_AUDIO 1)
endif()
if(NOT DEFINED ESP_BMS_FEATURE_CAST)
    set(ESP_BMS_FEATURE_CAST 1)
endif()
if(NOT DEFINED ESP_BMS_FEATURE_BLE_MEDIA_HID)
    set(ESP_BMS_FEATURE_BLE_MEDIA_HID 0)
endif()
if(NOT DEFINED ESP_BMS_FEATURE_CLASSIC_MEDIA_HID)
    set(ESP_BMS_FEATURE_CLASSIC_MEDIA_HID 0)
endif()

set(ESP_BMS_LVGL_UI_SRCS
    "esp_bms_lvgl_ui.c" "bluetoothon.c" "settings_zh_16.c" "settings_zh_13.c"
    "settings_zh_10.c" "wlanJZ.c"
    "ui_bms_dashboard.c" "ui_quick_panel.c" "ui_settings.c"
    "ui_settings_pickers.c" "ui_settings_system.c" "ui_dashboard_pages.c"
    "ui_dashboard_controller.c" "ui_dashboard_fireblade.c" "ui_dashboard_speed.c"
    "ui_screen.c" "ui_boot_ota.c" "ui_simulator.c"
)
if(IDF_TARGET STREQUAL "esp32s3")
    list(APPEND ESP_BMS_LVGL_UI_SRCS "settings_zh_18.c")
endif()
if(ESP_BMS_FEATURE_DASHBOARD_CONTROLLER)
    list(APPEND ESP_BMS_LVGL_UI_SRCS "controller_digits_72.c")
endif()
if(ESP_BMS_FEATURE_DASHBOARD_FIREBLADE)
    list(APPEND ESP_BMS_LVGL_UI_SRCS
        "fireblade_digits_64.c" "fireblade_info_digits_12.c"
        "fireblade_info_units_8.c" "fireblade_scale_digits_14.c"
    )
endif()
if(ESP_BMS_FEATURE_BLE_MEDIA_HID OR ESP_BMS_FEATURE_CLASSIC_MEDIA_HID)
    list(APPEND ESP_BMS_LVGL_UI_SRCS "media_zh_13.c")
endif()

set(ESP_BMS_LVGL_UI_EMBED_FILES)
if(IDF_TARGET STREQUAL "esp32" AND ESP_BMS_FEATURE_DASHBOARD_S1000RR)
    list(APPEND ESP_BMS_LVGL_UI_EMBED_FILES "speed_dashboard_static_landscape.rgb565")
endif()

idf_component_register(
    SRCS ${ESP_BMS_LVGL_UI_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES lvgl esp_bms_lvgl_contract esp_hw_support esp_timer
    EMBED_FILES ${ESP_BMS_LVGL_UI_EMBED_FILES}
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ESP_BMS_FEATURE_DASHBOARD_S1000RR=${ESP_BMS_FEATURE_DASHBOARD_S1000RR}
    ESP_BMS_FEATURE_DASHBOARD_CONTROLLER=${ESP_BMS_FEATURE_DASHBOARD_CONTROLLER}
    ESP_BMS_FEATURE_DASHBOARD_FIREBLADE=${ESP_BMS_FEATURE_DASHBOARD_FIREBLADE}
    ESP_BMS_FEATURE_BMS=${ESP_BMS_FEATURE_BMS}
    ESP_BMS_FEATURE_CONTROLLER=${ESP_BMS_FEATURE_CONTROLLER}
    ESP_BMS_FEATURE_GPS=${ESP_BMS_FEATURE_GPS}
    ESP_BMS_FEATURE_NETWORK=${ESP_BMS_FEATURE_NETWORK}
    ESP_BMS_FEATURE_AUDIO=${ESP_BMS_FEATURE_AUDIO}
    ESP_BMS_FEATURE_CAST=${ESP_BMS_FEATURE_CAST}
    ESP_BMS_FEATURE_BLE_MEDIA_HID=${ESP_BMS_FEATURE_BLE_MEDIA_HID}
    ESP_BMS_FEATURE_CLASSIC_MEDIA_HID=${ESP_BMS_FEATURE_CLASSIC_MEDIA_HID}
)
