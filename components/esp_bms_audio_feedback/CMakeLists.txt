if(DEFINED ENV{ESP_BMS_PROFILE_FILE} AND NOT "$ENV{ESP_BMS_PROFILE_FILE}" STREQUAL "")
    get_filename_component(ESP_BMS_PROFILE_GENERATED_DIR "$ENV{ESP_BMS_PROFILE_FILE}" DIRECTORY)
endif()

idf_component_register(
    SRCS "esp_bms_audio_feedback.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "${ESP_BMS_PROFILE_GENERATED_DIR}"
    REQUIRES driver esp_driver_dac esp_driver_gpio esp_driver_i2s
    EMBED_FILES "bms_connected.pcm" "controller_connected.pcm"
)
