#include "esp_partition.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "fal.h"

#define FLASHDB_ERASE_SIZE 4096U

static const esp_partition_t *s_partition;
static SemaphoreHandle_t s_lock;

static int flashdb_init(void)
{
    s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                             ESP_PARTITION_SUBTYPE_ANY,
                                             "flashdb");
    if (!s_partition) return 0;
    nor_flash0.len = s_partition->size;
    s_lock = xSemaphoreCreateMutex();
    return s_lock != NULL;
}

static int flashdb_read(long offset, uint8_t *buf, size_t size)
{
    if (!s_partition || !buf || offset < 0 || (size_t)offset > s_partition->size ||
        size > s_partition->size - (size_t)offset ||
        xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return -1;
    esp_err_t ret = esp_partition_read(s_partition, (size_t)offset, buf, size);
    xSemaphoreGive(s_lock);
    return ret == ESP_OK ? 0 : -1;
}

static int flashdb_write(long offset, const uint8_t *buf, size_t size)
{
    if (!s_partition || !buf || offset < 0 || (size_t)offset > s_partition->size ||
        size > s_partition->size - (size_t)offset ||
        xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return -1;
    esp_err_t ret = esp_partition_write(s_partition, (size_t)offset, buf, size);
    xSemaphoreGive(s_lock);
    return ret == ESP_OK ? 0 : -1;
}

static int flashdb_erase(long offset, size_t size)
{
    if (!s_partition || offset < 0 || (size_t)offset > s_partition->size ||
        size > s_partition->size - (size_t)offset ||
        xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return -1;
    size_t rounded = ((size + FLASHDB_ERASE_SIZE - 1U) / FLASHDB_ERASE_SIZE) * FLASHDB_ERASE_SIZE;
    esp_err_t ret = esp_partition_erase_range(s_partition, (size_t)offset, rounded);
    xSemaphoreGive(s_lock);
    return ret == ESP_OK ? 0 : -1;
}

struct fal_flash_dev nor_flash0 = {
    .name = NOR_FLASH_DEV_NAME,
    .addr = 0,
    .len = 0,
    .blk_size = FLASHDB_ERASE_SIZE,
    .ops = {flashdb_init, flashdb_read, flashdb_write, flashdb_erase},
    .write_gran = 1,
};
