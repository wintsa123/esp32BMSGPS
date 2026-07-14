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

add_executable(esp_bms_lvgl_simulator
    main.c
    "${REPO_ROOT}/components/esp_bms_idf_runtime/esp_bms_speed_dashboard.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/controller_digits_72.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/bluetoothon.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/settings_zh_10.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/settings_zh_13.c"
    "${REPO_ROOT}/components/esp_bms_lvgl_ui/settings_zh_16.c"
    "${REPO_ROOT}/wlanJZ.c"
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
    SDL_MAIN_HANDLED
    ESP_BMS_LVGL_SIMULATOR=1
)
target_compile_options(esp_bms_lvgl_simulator PRIVATE -Wall -Wextra)
target_link_libraries(esp_bms_lvgl_simulator PRIVATE lvgl::lvgl PkgConfig::SDL2 m)
