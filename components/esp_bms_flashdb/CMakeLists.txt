set(FLASHDB_CORE_SRCS
    src/fdb.c src/fdb_tsdb.c src/fdb_utils.c
    src/fal.c src/fal_flash.c src/fal_partition.c
)
idf_component_register(
    SRCS ${FLASHDB_CORE_SRCS} "esp_bms_flashdb_port.c" "esp_bms_flashdb.c"
    INCLUDE_DIRS "include" "inc" "fal_inc"
    REQUIRES esp_partition freertos nvs_flash
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE FDB_USING_TSDB FDB_USING_FAL_MODE FDB_USING_TIMESTAMP_64BIT FDB_WRITE_GRAN=1)
