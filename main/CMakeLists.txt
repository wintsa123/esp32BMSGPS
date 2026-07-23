set(ESP_BMS_MAIN_REQUIRES_DEFAULT
    esp_bms_audio_feedback esp_bms_bms_ble esp_bms_controller_ble esp_bms_gps esp_bms_idf_runtime esp_bms_lvgl_bridge esp_bms_network esp_bms_ota
    esp_bms_lvgl_ui lvgl esp_lvgl_adapter
)

# ESP-IDF scans component REQUIRES in a separate `cmake -P` process before
# configuring the project. That process does not receive top-level -D cache
# values, so load the generated profile passed by the configurator explicitly.
if(DEFINED ENV{ESP_BMS_PROFILE_FILE} AND NOT "$ENV{ESP_BMS_PROFILE_FILE}" STREQUAL "")
    if(DEFINED ENV{FIRMWARE_BUILD_ROOT} AND NOT "$ENV{FIRMWARE_BUILD_ROOT}" STREQUAL "")
        get_filename_component(_profile_root "$ENV{FIRMWARE_BUILD_ROOT}" REALPATH)
    else()
        get_filename_component(_profile_root "${CMAKE_CURRENT_LIST_DIR}/../firmware-builds" REALPATH)
    endif()
    get_filename_component(_profile_file "$ENV{ESP_BMS_PROFILE_FILE}" REALPATH)
    file(RELATIVE_PATH _profile_relative_path "${_profile_root}" "${_profile_file}")
    if(_profile_relative_path MATCHES "^\\.\\.(/|$)" OR _profile_relative_path STREQUAL "")
        message(FATAL_ERROR "ESP_BMS_PROFILE_FILE must be a generated profile under FIRMWARE_BUILD_ROOT")
    endif()
    include("${_profile_file}")
    get_filename_component(ESP_BMS_PROFILE_GENERATED_DIR "${_profile_file}" DIRECTORY)
elseif(NOT CMAKE_BUILD_EARLY_EXPANSION)
    message(FATAL_ERROR "Build through start.sh/start.ps1 so the selected hardware profile is generated")
endif()

if(DEFINED ESP_BMS_PROFILE_MAIN_REQUIRES)
    set(ESP_BMS_MAIN_REQUIRES ${ESP_BMS_PROFILE_MAIN_REQUIRES})
else()
    set(ESP_BMS_MAIN_REQUIRES ${ESP_BMS_MAIN_REQUIRES_DEFAULT})
endif()
if(NOT DEFINED ESP_BMS_FEATURE_AUDIO)
    set(ESP_BMS_FEATURE_AUDIO 1)
endif()
if(NOT DEFINED ESP_BMS_FEATURE_BMS)
    set(ESP_BMS_FEATURE_BMS 1)
endif()
if(NOT DEFINED ESP_BMS_FEATURE_CONTROLLER)
    set(ESP_BMS_FEATURE_CONTROLLER 1)
endif()
if(NOT DEFINED ESP_BMS_FEATURE_GPS)
    set(ESP_BMS_FEATURE_GPS 1)
endif()
if(NOT DEFINED ESP_BMS_FEATURE_NETWORK)
    set(ESP_BMS_FEATURE_NETWORK 1)
endif()
if(NOT DEFINED ESP_BMS_FEATURE_OTA)
    set(ESP_BMS_FEATURE_OTA 1)
endif()
if(NOT DEFINED ESP_BMS_FEATURE_CAST)
    set(ESP_BMS_FEATURE_CAST 1)
endif()

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/esp_bms_module_registry.c.in"
    "${CMAKE_CURRENT_BINARY_DIR}/esp_bms_module_registry.c"
    @ONLY
)

idf_component_register(
    SRCS "idf_main.c" "${CMAKE_CURRENT_BINARY_DIR}/esp_bms_module_registry.c"
    INCLUDE_DIRS "." ${ESP_BMS_PROFILE_GENERATED_DIR}
    REQUIRES ${ESP_BMS_MAIN_REQUIRES}
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE ESP_BMS_FEATURE_CAST=${ESP_BMS_FEATURE_CAST})
