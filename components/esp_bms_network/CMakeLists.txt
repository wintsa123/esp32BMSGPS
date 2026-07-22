idf_component_register(
    SRCS "esp_bms_network.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_bms_idf_runtime esp_event esp_http_server esp_netif esp_wifi nvs_flash
    EMBED_TXTFILES "../../main/web/index.html"
)
