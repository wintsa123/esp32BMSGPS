idf_component_register(
    SRCS "esp_bms_classic_media_hid.c"
    INCLUDE_DIRS "include"
    REQUIRES bt esp_hid nvs_flash
)
