idf_component_register(
    SRCS "esp_bms_audio_feedback.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_dac
    EMBED_FILES "bms_connected.pcm" "controller_connected.pcm"
)
