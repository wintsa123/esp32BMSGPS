idf_component_register(
    SRCS "esp_bms_bms_ble.c"
         "protocols/ant/esp_bms_ant_protocol.c"
         "protocols/daly/esp_bms_daly_protocol.c"
         "protocols/jbd/esp_bms_jbd_protocol.c"
         "protocols/jk/esp_bms_jk_protocol.c"
         "protocols/yanyang/esp_bms_yanyang_protocol.c"
    INCLUDE_DIRS "include"
    REQUIRES bt esp_bms_idf_runtime esp_bms_lvgl_ui esp_timer
)
