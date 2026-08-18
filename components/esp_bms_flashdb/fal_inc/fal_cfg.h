#ifndef ESP_BMS_FLASHDB_FAL_CFG_H
#define ESP_BMS_FLASHDB_FAL_CFG_H

#include "fal_def.h"
#define FAL_PART_HAS_TABLE_CFG

#define NOR_FLASH_DEV_NAME "esp_flashdb"

extern struct fal_flash_dev nor_flash0;

#define FAL_FLASH_DEV_TABLE { &nor_flash0, }
#if CONFIG_IDF_TARGET_ESP32S3
#define FAL_PART_TABLE \
    { { FAL_PART_MAGIC_WORD, "history0", NOR_FLASH_DEV_NAME, 0x000000, 0x0E0000, 0 }, \
      { FAL_PART_MAGIC_WORD, "history1", NOR_FLASH_DEV_NAME, 0x0E0000, 0x0E0000, 0 }, \
      { FAL_PART_MAGIC_WORD, "history2", NOR_FLASH_DEV_NAME, 0x1C0000, 0x0E0000, 0 }, \
      { FAL_PART_MAGIC_WORD, "faults", NOR_FLASH_DEV_NAME, 0x2A0000, 0x150000, 0 }, }
#else
#define FAL_PART_TABLE \
    { { FAL_PART_MAGIC_WORD, "history0", NOR_FLASH_DEV_NAME, 0x00000, 0x2C000, 0 }, \
      { FAL_PART_MAGIC_WORD, "faults", NOR_FLASH_DEV_NAME, 0x2C000, 0x04000, 0 }, }
#endif

#endif
