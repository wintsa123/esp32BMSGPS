set(ESP_BMS_CTRL_PROTOCOL_DIRS
    apt_a8
    apt_legacy
    ausi
    bluestone
    datai
    dunen
    fardriver
    hobbywing
    jh
    jiazhou
    lingbo
    suniu
    xinda
    yanxin
    yuanqu_foc
    zhike
)

set(ESP_BMS_CTRL_PROTOCOL_SRCS
    "esp_bms_controller_protocol.c"
)

set(ESP_BMS_CTRL_PROTOCOL_INCLUDE_DIRS
    "include"
)

foreach(protocol_dir IN LISTS ESP_BMS_CTRL_PROTOCOL_DIRS)
    list(APPEND ESP_BMS_CTRL_PROTOCOL_SRCS
        "protocols/${protocol_dir}/esp_bms_ctrl_${protocol_dir}.c")
    list(APPEND ESP_BMS_CTRL_PROTOCOL_INCLUDE_DIRS
        "protocols/${protocol_dir}")
endforeach()

idf_component_register(
    SRCS ${ESP_BMS_CTRL_PROTOCOL_SRCS}
    INCLUDE_DIRS ${ESP_BMS_CTRL_PROTOCOL_INCLUDE_DIRS}
    REQUIRES esp_fardriver_protocol
)
