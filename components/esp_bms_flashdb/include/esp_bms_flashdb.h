#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define ESP_BMS_FLASHDB_SAMPLE_VERSION 1U
#define ESP_BMS_FLASHDB_SAMPLE_PAYLOAD_SIZE 32U
#define ESP_BMS_FLASHDB_FLAG_GPS_VALID (1U << 0)
#define ESP_BMS_FLASHDB_FLAG_BMS_VALID (1U << 1)
#define ESP_BMS_FLASHDB_FLAG_GPS_UTC (1U << 2)
#define ESP_BMS_FLASHDB_FLAG_TRUNCATED (1U << 3)
#define ESP_BMS_FLASHDB_MAX_SESSIONS 3U
#define ESP_BMS_FLASHDB_MAX_SAMPLES 18000U

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t flags;
    uint8_t bms_type;
    uint8_t temperature_count;
    uint16_t elapsed_s;
    int32_t latitude_e7;
    int32_t longitude_e7;
    uint16_t pack_voltage_mv;
    int16_t current_deci_amps;
    uint8_t soc_percent;
    uint8_t cell_delta_mv;
    uint16_t cell_min_mv;
    uint16_t cell_max_mv;
    uint16_t cell_avg_mv;
    int8_t temperatures_c[6];
} esp_bms_flashdb_sample_t;

_Static_assert(sizeof(esp_bms_flashdb_sample_t) == ESP_BMS_FLASHDB_SAMPLE_PAYLOAD_SIZE,
               "FlashDB sample payload must remain 32 bytes");

typedef bool (*esp_bms_flashdb_sample_cb_t)(uint64_t timestamp, const esp_bms_flashdb_sample_t *sample,
                                            void *ctx);

typedef struct {
    uint64_t session_id;
    uint64_t start_time_s;
    uint64_t end_time_s;
    uint32_t sample_count;
    uint32_t capacity_samples;
    uint32_t elapsed_seconds;
    bool calibrated;
    bool truncated;
    bool capacity_reached;
} esp_bms_flashdb_session_t;

typedef struct {
    uint64_t timestamp;
    uint64_t session_id;
    uint32_t elapsed_s;
    uint16_t active_mask;
    uint16_t supported_mask;
    uint8_t bms_type;
    uint8_t flags;
} esp_bms_flashdb_fault_t;

typedef bool (*esp_bms_flashdb_fault_cb_t)(const esp_bms_flashdb_fault_t *fault, void *ctx);

esp_err_t esp_bms_flashdb_init(void);
bool esp_bms_flashdb_ready(void);
esp_err_t esp_bms_flashdb_append_sample(uint64_t timestamp, const esp_bms_flashdb_sample_t *sample);
esp_err_t esp_bms_flashdb_start_session(uint64_t *session_id);
esp_err_t esp_bms_flashdb_resume_session(uint64_t session_id, size_t *sample_count);
esp_err_t esp_bms_flashdb_set_session_anchor(uint64_t session_id, uint32_t elapsed_s,
                                             uint64_t utc_s);
esp_err_t esp_bms_flashdb_append_fault(const esp_bms_flashdb_fault_t *fault);
esp_err_t esp_bms_flashdb_query_faults(uint64_t from, uint64_t to, size_t limit,
                                       esp_bms_flashdb_fault_cb_t callback, void *ctx,
                                       size_t *returned);
size_t esp_bms_flashdb_session_count(void);
esp_err_t esp_bms_flashdb_get_session(size_t index, esp_bms_flashdb_session_t *session);
bool esp_bms_flashdb_session_full(void);
esp_err_t esp_bms_flashdb_query_samples(uint64_t from, uint64_t to, size_t limit,
                                        esp_bms_flashdb_sample_cb_t callback, void *ctx,
                                        size_t *returned);
esp_err_t esp_bms_flashdb_query_session_samples(uint64_t session_id, uint64_t from, uint64_t to,
                                                size_t limit, esp_bms_flashdb_sample_cb_t callback,
                                                void *ctx, size_t *returned);
size_t esp_bms_flashdb_sample_count(void);
size_t esp_bms_flashdb_capacity_bytes(void);
