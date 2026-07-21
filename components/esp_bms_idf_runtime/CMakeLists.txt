if(CMAKE_BUILD_EARLY_EXPANSION AND DEFINED ENV{ESP_BMS_PROFILE_FILE})
    include("$ENV{ESP_BMS_PROFILE_FILE}")
endif()

set(ESP_BMS_RUNTIME_REQUIRES
    app_update bt driver esp_adc esp_bms_lvgl_bridge esp_bms_lvgl_ui esp_event
    esp_fardriver_protocol esp_http_server esp_hw_support esp_netif esp_timer
    esp_wifi nvs_flash
)
if(NOT DEFINED ESP_BMS_FEATURE_GPS)
    set(ESP_BMS_FEATURE_GPS 1)
endif()

idf_component_register(
    SRCS "esp_bms_idf_runtime.c" "esp_bms_speed_dashboard.c"
    INCLUDE_DIRS "include"
    REQUIRES ${ESP_BMS_RUNTIME_REQUIRES}
    EMBED_TXTFILES "../../main/web/index.html"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE ESP_BMS_FEATURE_GPS=${ESP_BMS_FEATURE_GPS})
