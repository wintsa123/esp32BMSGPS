idf_component_register(
    SRCS "esp_bms_controller_ble.c"
    INCLUDE_DIRS "include"
    REQUIRES bt esp_bms_idf_runtime esp_fardriver_protocol esp_timer freertos
)
