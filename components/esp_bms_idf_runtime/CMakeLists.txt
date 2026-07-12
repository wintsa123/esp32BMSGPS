idf_component_register(
    SRCS "esp_bms_idf_runtime.c"
    INCLUDE_DIRS "include"
    REQUIRES bt driver esp_adc esp_bms_lvgl_bridge esp_bms_lvgl_ui esp_event esp_fardriver_protocol esp_http_server esp_hw_support esp_netif esp_wifi nvs_flash
    EMBED_TXTFILES "../../main/web/index.html"
)
