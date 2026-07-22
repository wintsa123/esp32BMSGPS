idf_component_register(
    SRCS "esp_bms_ota.c"
    INCLUDE_DIRS "include"
    REQUIRES app_update esp_http_server
)
