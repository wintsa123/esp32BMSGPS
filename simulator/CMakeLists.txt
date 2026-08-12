cmake_minimum_required(VERSION 3.16)

project(esp_bms_lvgl_simulator LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

add_library(esp_bms_simulator_sdl2 INTERFACE)

set(_esp_bms_sdl2_hints)
if(DEFINED SDL2_ROOT)
    list(APPEND _esp_bms_sdl2_hints
        "${SDL2_ROOT}"
        "${SDL2_ROOT}/cmake"
        "${SDL2_ROOT}/lib/cmake/SDL2"
        "${SDL2_ROOT}/x86_64-w64-mingw32/lib/cmake/SDL2"
    )
endif()

find_package(SDL2 CONFIG QUIET HINTS ${_esp_bms_sdl2_hints})
if(TARGET SDL2::SDL2)
    target_link_libraries(esp_bms_simulator_sdl2 INTERFACE SDL2::SDL2)
elseif(TARGET SDL2::SDL2-static)
    target_link_libraries(esp_bms_simulator_sdl2 INTERFACE SDL2::SDL2-static)
elseif(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    target_link_libraries(esp_bms_simulator_sdl2 INTERFACE PkgConfig::SDL2)
else()
    message(FATAL_ERROR
        "SDL2 development files were not found. On Windows, set SDL2_ROOT to an "
        "extracted SDL2 development package. On Debian, install libsdl2-dev and pkg-config.")
endif()

set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${REPO_ROOT}/managed_components/lvgl__lvgl" "${CMAKE_BINARY_DIR}/lvgl")
target_link_libraries(lvgl PUBLIC esp_bms_simulator_sdl2)

if(NOT DEFINED ESP_BMS_FEATURE_DASHBOARD_S1000RR)
    set(ESP_BMS_FEATURE_DASHBOARD_S1000RR 1)
endif()
if(NOT DEFINED ESP_BMS_FEATURE_DASHBOARD_CONTROLLER)
    set(ESP_BMS_FEATURE_DASHBOARD_CONTROLLER 1)
endif()
if(NOT DEFINED ESP_BMS_FEATURE_DASHBOARD_FIREBLADE)
    set(ESP_BMS_FEATURE_DASHBOARD_FIREBLADE 1)
endif()
if(NOT DEFINED ESP_BMS_FEATURE_AUDIO)
    set(ESP_BMS_FEATURE_AUDIO 1)
endif()
if(NOT DEFINED ESP_BMS_FEATURE_GPS)
    set(ESP_BMS_FEATURE_GPS 1)
endif()
if(NOT DEFINED ESP_BMS_FEATURE_CONTROLLER)
    set(ESP_BMS_FEATURE_CONTROLLER 1)
endif()
if(NOT DEFINED ESP_BMS_FEATURE_PHONE_MEDIA)
    set(ESP_BMS_FEATURE_PHONE_MEDIA 0)
endif()
if(NOT DEFINED ESP_BMS_FEATURE_BLE_MEDIA_HID)
    set(ESP_BMS_FEATURE_BLE_MEDIA_HID 0)
endif()

set(ESP_BMS_SIMULATOR_SRCS
    main.c
    host_ble_client.c
    "${REPO_ROOT}/components/esp_bms_idf_runtime/esp_bms_speed_dashboard.c"
    "${REPO_ROOT}/components/esp_fardriver_protocol/esp_fardriver_protocol.c"
    "${REPO_ROOT}/components/esp_bms_bms_ble/protocols/ant/esp_bms_ant_protocol.c"
    "${REPO_ROOT}/components/esp_bms_bms_ble/protocols/daly/esp_bms_daly_protocol.c"
    "${REPO_ROOT}/components/esp_bms_bms_ble/protocols/jbd/esp_bms_jbd_protocol.c"
    "${REPO_ROOT}/components/esp_bms_bms_ble/protocols/jk/esp_bms_jk_protocol.c"
    "${REPO_ROOT}/components/esp_bms_bms_ble/protocols/yanyang/esp_bms_yanyang_protocol.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/bluetoothon.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/settings_zh_10.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/settings_zh_13.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/settings_zh_16.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/settings_zh_18.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/wlanJZ.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/ui_bms_dashboard.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/ui_quick_panel.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/ui_settings.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/ui_settings_pickers.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/ui_settings_system.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/ui_dashboard_pages.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/ui_dashboard_controller.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/ui_dashboard_fireblade.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/ui_dashboard_speed.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/ui_screen.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/ui_boot_ota.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/ui_simulator.c"
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
    "${REPO_ROOT}/components/esp_fardriver_protocol/include"
    "${REPO_ROOT}/components/esp_bms_bms_ble/protocols/ant"
    "${REPO_ROOT}/components/esp_bms_bms_ble/protocols/daly"
    "${REPO_ROOT}/components/esp_bms_bms_ble/protocols/jbd"
    "${REPO_ROOT}/components/esp_bms_bms_ble/protocols/jk"
    "${REPO_ROOT}/components/esp_bms_bms_ble/protocols/yanyang"
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
    ESP_BMS_FEATURE_AUDIO=${ESP_BMS_FEATURE_AUDIO}
    ESP_BMS_FEATURE_GPS=${ESP_BMS_FEATURE_GPS}
    ESP_BMS_FEATURE_CONTROLLER=${ESP_BMS_FEATURE_CONTROLLER}
    ESP_BMS_FEATURE_PHONE_MEDIA=${ESP_BMS_FEATURE_PHONE_MEDIA}
    ESP_BMS_FEATURE_BLE_MEDIA_HID=${ESP_BMS_FEATURE_BLE_MEDIA_HID}
    ESP_BMS_SIMULATOR_RESTART_RUNNER="${REPO_ROOT}/scripts/run-lvgl-simulator.sh"
)
if(MSVC)
    target_compile_options(esp_bms_lvgl_simulator PRIVATE /W4)
else()
    target_compile_options(esp_bms_lvgl_simulator PRIVATE -Wall -Wextra)
endif()

target_link_libraries(esp_bms_lvgl_simulator PRIVATE lvgl::lvgl esp_bms_simulator_sdl2)
if(WIN32)
    target_link_libraries(esp_bms_lvgl_simulator PRIVATE ws2_32)
else()
    target_link_libraries(esp_bms_lvgl_simulator PRIVATE m)
endif()
