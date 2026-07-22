cmake_minimum_required(VERSION 3.16)

project(esp_bms_lvgl_simulator LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${REPO_ROOT}/managed_components/lvgl__lvgl" "${CMAKE_BINARY_DIR}/lvgl")
target_link_libraries(lvgl PUBLIC PkgConfig::SDL2)

if(NOT DEFINED ESP_BMS_FEATURE_DASHBOARD_S1000RR)
    set(ESP_BMS_FEATURE_DASHBOARD_S1000RR 1)
endif()
if(NOT DEFINED ESP_BMS_FEATURE_DASHBOARD_CONTROLLER)
    set(ESP_BMS_FEATURE_DASHBOARD_CONTROLLER 1)
endif()
if(NOT DEFINED ESP_BMS_FEATURE_DASHBOARD_FIREBLADE)
    set(ESP_BMS_FEATURE_DASHBOARD_FIREBLADE 1)
endif()

set(ESP_BMS_SIMULATOR_SRCS
    main.c
    "${REPO_ROOT}/components/esp_bms_idf_runtime/esp_bms_speed_dashboard.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/bluetoothon.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/settings_zh_10.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/settings_zh_13.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/settings_zh_16.c"
    "${REPO_ROOT}/wlanJZ.c"
)
if(ESP_BMS_FEATURE_DASHBOARD_CONTROLLER)
    list(APPEND ESP_BMS_SIMULATOR_SRCS
        "${REPO_ROOT}/components/esp_bms_lvgl_ui/controller_digits_72.c"
    )
endif()
if(ESP_BMS_FEATURE_DASHBOARD_FIREBLADE)
    list(APPEND ESP_BMS_SIMULATOR_SRCS
        "${REPO_ROOT}/components/esp_bms_lvgl_ui/fireblade_digits_64.c"
        "${REPO_ROOT}/components/esp_bms_lvgl_ui/fireblade_info_digits_12.c"
        "${REPO_ROOT}/components/esp_bms_lvgl_ui/fireblade_info_units_8.c"
        "${REPO_ROOT}/components/esp_bms_lvgl_ui/fireblade_scale_digits_14.c"
    )
endif()

add_executable(esp_bms_lvgl_simulator
    ${ESP_BMS_SIMULATOR_SRCS}
)

target_include_directories(esp_bms_lvgl_simulator PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${REPO_ROOT}/components/esp_bms_idf_runtime/include"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/include"
    "${REPO_ROOT}/components/esp_bms_lvgl_contract/include"
    "${REPO_ROOT}/managed_components/lvgl__lvgl/src"
)
target_compile_definitions(esp_bms_lvgl_simulator PRIVATE
    _POSIX_C_SOURCE=200809L
    ESP_BMS_LVGL_UI_SIMULATOR=1
    SDL_MAIN_HANDLED
    ESP_BMS_FEATURE_DASHBOARD_S1000RR=${ESP_BMS_FEATURE_DASHBOARD_S1000RR}
    ESP_BMS_FEATURE_DASHBOARD_CONTROLLER=${ESP_BMS_FEATURE_DASHBOARD_CONTROLLER}
    ESP_BMS_FEATURE_DASHBOARD_FIREBLADE=${ESP_BMS_FEATURE_DASHBOARD_FIREBLADE}
)
target_compile_options(esp_bms_lvgl_simulator PRIVATE -Wall -Wextra)
target_link_libraries(esp_bms_lvgl_simulator PRIVATE lvgl::lvgl PkgConfig::SDL2 m)
