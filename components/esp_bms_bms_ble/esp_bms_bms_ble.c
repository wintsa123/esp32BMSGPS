#include "esp_bms_bms_ble.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "host/ble_uuid.h"
#include "nvs.h"
#include "os/os_mbuf.h"

#include "esp_bms_idf_runtime.h"

static const char *TAG = "esp_bms_bms_ble";

#define BMS_FRAME_MIN_LEN 10U
#define BMS_FRAME_START_1 0x7EU
#define BMS_FRAME_START_2 0xA1U
#define BMS_FRAME_END_1 0xAAU
#define BMS_FRAME_END_2 0x55U
#define BMS_FRAME_TYPE_STATUS 0x11U
#define BMS_FRAME_TYPE_DEVICE_INFO 0x12U
#define BMS_MAX_CELLS 32U
#define BMS_MAX_TEMPERATURE_SENSORS 4U
#define BMS_STATUS_PROTECTION_MASK_OFFSET 10U
#define BMS_STATUS_WARNING_MASK_OFFSET 18U
#define BMS_STATUS_DYNAMIC_BASE_OFFSET 34U
#define ANT_BMS_SERVICE_UUID_16 0xFFE0U
#define ANT_BMS_CHARACTERISTIC_UUID_16 0xFFE1U
#define BMS_SCAN_DURATION_MS 10000
#define BMS_CONNECT_TIMEOUT_MS 10000
#define BMS_STATUS_POLL_PERIOD_MS 500U
#define BMS_HEARTBEAT_TIMEOUT_MS 5000U
#define BMS_RECONNECT_BACKOFF_MS 3000U

#define RUNTIME_FLAG(runtime, name) \
    esp_bms_idf_runtime_flag_get((runtime), ESP_BMS_IDF_RUNTIME_FLAG_##name)
#define RUNTIME_SET_FLAG(runtime, name, enabled) \
    esp_bms_idf_runtime_flag_set((runtime), ESP_BMS_IDF_RUNTIME_FLAG_##name, (enabled))
#define RUNTIME_SET_SNAPSHOT_FLAG(runtime, name, enabled) \
    esp_bms_dashboard_snapshot_flag_set(&(runtime)->snapshot, \
                                         ESP_BMS_DASHBOARD_FLAG_##name, \
                                         (enabled))

typedef enum {
    BMS_BLE_PHASE_IDLE = 0,
    BMS_BLE_PHASE_SCANNING = 1,
    BMS_BLE_PHASE_CONNECTING = 2,
    BMS_BLE_PHASE_DISCOVERING_SERVICE = 3,
    BMS_BLE_PHASE_DISCOVERING_CHARACTERISTIC = 4,
    BMS_BLE_PHASE_DISCOVERING_CCCD = 5,
    BMS_BLE_PHASE_SUBSCRIBING = 6,
    BMS_BLE_PHASE_ONLINE = 7,
    BMS_BLE_PHASE_BACKOFF = 8,
} bms_ble_phase_t;

static void bms_set_info(esp_bms_idf_runtime_t *runtime, const char *text)
{
    if (!runtime) {
        return;
    }
    const char *const value = text ? text : "";
    strncpy(runtime->snapshot.bms_info_text,
            value,
            sizeof(runtime->snapshot.bms_info_text) - 1U);
    runtime->snapshot.bms_info_text[sizeof(runtime->snapshot.bms_info_text) - 1U] = '\0';
    strncpy(runtime->snapshot.bms_error_text,
            value,
            sizeof(runtime->snapshot.bms_error_text) - 1U);
    runtime->snapshot.bms_error_text[sizeof(runtime->snapshot.bms_error_text) - 1U] = '\0';
    RUNTIME_SET_FLAG(runtime, BMS_SNAPSHOT_DIRTY, true);
}

static void bms_append_code(char codes[][ESP_BMS_BMS_CODE_TEXT_LEN],
                            uint8_t *count,
                            char prefix,
                            uint8_t bit)
{
    if (!codes || !count || *count >= ESP_BMS_BMS_CODE_MAX_COUNT) {
        return;
    }
    (void)snprintf(codes[*count], ESP_BMS_BMS_CODE_TEXT_LEN, "%c%02u", prefix, (unsigned)bit);
    (*count)++;
}

static void bms_apply_fault_masks(esp_bms_dashboard_snapshot_t *snapshot,
                                  uint64_t protection_mask,
                                  uint64_t warning_mask)
{
    snapshot->bms_protection_count = 0U;
    memset(snapshot->bms_protection_codes, 0, sizeof(snapshot->bms_protection_codes));
    snapshot->bms_warning_count = 0U;
    memset(snapshot->bms_warning_codes, 0, sizeof(snapshot->bms_warning_codes));

    uint64_t remaining = protection_mask;
    while (remaining != 0ULL && snapshot->bms_protection_count < ESP_BMS_BMS_CODE_MAX_COUNT) {
        const uint8_t bit = (uint8_t)__builtin_ctzll(remaining);
        bms_append_code(snapshot->bms_protection_codes, &snapshot->bms_protection_count, 'P', bit);
        remaining &= remaining - 1ULL;
    }

    remaining = warning_mask;
    while (remaining != 0ULL && snapshot->bms_warning_count < ESP_BMS_BMS_CODE_MAX_COUNT) {
        const uint8_t bit = (uint8_t)__builtin_ctzll(remaining);
        bms_append_code(snapshot->bms_warning_codes, &snapshot->bms_warning_count, 'W', bit);
        remaining &= remaining - 1ULL;
    }
}

static uint16_t bms_crc16_modbus(const uint8_t *bytes, size_t len)
{
    uint16_t crc = 0xFFFFU;
    const uint8_t *cursor = bytes;
    const uint8_t *const end = bytes + len;
    while (cursor < end) {
        crc ^= *cursor++;
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 0x0001U) ? (uint16_t)((crc >> 1U) ^ 0xA001U)
                                  : (uint16_t)(crc >> 1U);
        }
    }
    return crc;
}

static bool bms_read_u16_le(const uint8_t *data, size_t len, size_t index, uint16_t *out)
{
    if (!data || !out || len < 2U || index > len - 2U) {
        return false;
    }
    const uint8_t *const cursor = data + index;
    *out = (uint16_t)cursor[0] | ((uint16_t)cursor[1] << 8U);
    return true;
}

static bool bms_read_i16_le(const uint8_t *data, size_t len, size_t index, int16_t *out)
{
    uint16_t value = 0U;
    if (!bms_read_u16_le(data, len, index, &value)) {
        return false;
    }
    *out = (int16_t)value;
    return true;
}

static bool bms_read_u32_le(const uint8_t *data, size_t len, size_t index, uint32_t *out)
{
    if (!data || !out || len < 4U || index > len - 4U) {
        return false;
    }
    const uint8_t *const cursor = data + index;
    *out = (uint32_t)cursor[0] | ((uint32_t)cursor[1] << 8U) |
           ((uint32_t)cursor[2] << 16U) | ((uint32_t)cursor[3] << 24U);
    return true;
}

static bool bms_read_u64_le(const uint8_t *data, size_t len, size_t index, uint64_t *out)
{
    if (!data || !out || len < 8U || index > len - 8U) {
        return false;
    }
    uint64_t value = 0ULL;
    const uint8_t *cursor = data + index;
    for (uint8_t shift = 0U; shift < 64U; shift += 8U) {
        value |= ((uint64_t)*cursor++) << shift;
    }
    *out = value;
    return true;
}

static bool bms_validate_frame(const uint8_t *data,
                               size_t len,
                               uint8_t *function,
                               size_t *protocol_len)
{
    if (!data || !function || !protocol_len || len < BMS_FRAME_MIN_LEN ||
        len > ESP_BMS_IDF_BMS_FRAME_MAX_LEN || data[0] != BMS_FRAME_START_1 ||
        data[1] != BMS_FRAME_START_2 || data[len - 2U] != BMS_FRAME_END_1 ||
        data[len - 1U] != BMS_FRAME_END_2) {
        return false;
    }

    *function = data[2];
    *protocol_len = 6U + data[5] + 4U;
    if (*protocol_len > len || *protocol_len < BMS_FRAME_MIN_LEN) {
        return false;
    }
    if (*function != BMS_FRAME_TYPE_DEVICE_INFO && *protocol_len != len) {
        return false;
    }

    const size_t crc_offset = *protocol_len - 4U;
    const uint16_t expected_crc = bms_crc16_modbus(&data[1], crc_offset - 1U);
    const uint16_t remote_crc = (uint16_t)data[crc_offset] |
                                ((uint16_t)data[crc_offset + 1U] << 8U);
    return expected_crc == remote_crc;
}

static bool bms_apply_status_frame(esp_bms_idf_runtime_t *runtime,
                                   const uint8_t *data,
                                   size_t len)
{
    uint8_t function = 0U;
    size_t protocol_len = 0U;
    if (!bms_validate_frame(data, len, &function, &protocol_len) ||
        function != BMS_FRAME_TYPE_STATUS) {
        return false;
    }

    const uint8_t temperature_sensor_count = data[8];
    const uint8_t cell_count = data[9];
    if (cell_count > BMS_MAX_CELLS || temperature_sensor_count > BMS_MAX_TEMPERATURE_SENSORS) {
        return false;
    }

    const size_t dynamic_offset = ((size_t)cell_count * 2U) +
                                  ((size_t)temperature_sensor_count * 2U);
    uint16_t pack_voltage_dv = 0U;
    int16_t current_deci_amps = 0;
    uint16_t soc_percent = 0U;
    uint32_t total_capacity_uah = 0U;
    uint32_t capacity_remaining_uah = 0U;
    uint64_t protection_mask = 0ULL;
    uint64_t warning_mask = 0ULL;
    uint16_t max_cell_mv = 0U;
    uint16_t min_cell_mv = 0U;
    uint16_t delta_cell_mv = 0U;
    uint16_t average_cell_mv = 0U;
    int16_t temperatures[ESP_BMS_BMS_TEMP_MAX_COUNT] = { 0 };
    bool temperature_valid[ESP_BMS_BMS_TEMP_MAX_COUNT] = { false };

    if (!bms_read_u64_le(data, protocol_len, BMS_STATUS_PROTECTION_MASK_OFFSET, &protection_mask) ||
        !bms_read_u64_le(data, protocol_len, BMS_STATUS_WARNING_MASK_OFFSET, &warning_mask) ||
        !bms_read_u16_le(data, protocol_len, 38U + dynamic_offset, &pack_voltage_dv) ||
        !bms_read_i16_le(data, protocol_len, 40U + dynamic_offset, &current_deci_amps) ||
        !bms_read_u16_le(data, protocol_len, 42U + dynamic_offset, &soc_percent) ||
        !bms_read_u32_le(data, protocol_len, 50U + dynamic_offset, &total_capacity_uah) ||
        !bms_read_u32_le(data, protocol_len, 54U + dynamic_offset, &capacity_remaining_uah) ||
        !bms_read_u16_le(data, protocol_len, 74U + dynamic_offset, &max_cell_mv) ||
        !bms_read_u16_le(data, protocol_len, 78U + dynamic_offset, &min_cell_mv) ||
        !bms_read_u16_le(data, protocol_len, 82U + dynamic_offset, &delta_cell_mv) ||
        !bms_read_u16_le(data, protocol_len, 84U + dynamic_offset, &average_cell_mv)) {
        return false;
    }

    const size_t temperature_offset = BMS_STATUS_DYNAMIC_BASE_OFFSET + ((size_t)cell_count * 2U);
    const uint8_t temperature_count = temperature_sensor_count > ESP_BMS_BMS_TEMP_MAX_COUNT - 2U
                                          ? ESP_BMS_BMS_TEMP_MAX_COUNT - 2U
                                          : temperature_sensor_count;
    for (uint8_t index = 0U; index < temperature_count; ++index) {
        if (!bms_read_i16_le(data, protocol_len, temperature_offset + ((size_t)index * 2U), &temperatures[index])) {
            return false;
        }
        temperature_valid[index] = true;
    }
    if (!bms_read_i16_le(data, protocol_len, BMS_STATUS_DYNAMIC_BASE_OFFSET + dynamic_offset, &temperatures[4]) ||
        !bms_read_i16_le(data, protocol_len, BMS_STATUS_DYNAMIC_BASE_OFFSET + dynamic_offset + 2U, &temperatures[5])) {
        return false;
    }
    temperature_valid[4] = true;
    temperature_valid[5] = true;

    RUNTIME_SET_SNAPSHOT_FLAG(runtime, BMS_ONLINE, true);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, PACK_VOLTAGE_VALID, true);
    runtime->snapshot.pack_voltage_mv = (uint32_t)pack_voltage_dv * 10U;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CURRENT_VALID, true);
    runtime->snapshot.current_deci_amps = current_deci_amps;
    runtime->bms_telemetry_last_us = esp_timer_get_time();
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, SOC_VALID, true);
    runtime->snapshot.soc_percent = soc_percent;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, MAX_CELL_VALID, true);
    runtime->snapshot.max_cell_voltage_mv = max_cell_mv;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, MIN_CELL_VALID, true);
    runtime->snapshot.min_cell_voltage_mv = min_cell_mv;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, DELTA_CELL_VALID, true);
    runtime->snapshot.delta_cell_voltage_mv = delta_cell_mv;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, AVERAGE_CELL_VALID, true);
    runtime->snapshot.average_cell_voltage_mv = average_cell_mv;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, TOTAL_CAPACITY_VALID, true);
    runtime->snapshot.total_capacity_mah = total_capacity_uah / 1000U;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CAPACITY_REMAINING_VALID, true);
    runtime->snapshot.capacity_remaining_mah = capacity_remaining_uah / 1000U;
    for (uint8_t index = 0U; index < ESP_BMS_BMS_TEMP_MAX_COUNT; ++index) {
        esp_bms_dashboard_snapshot_temperature_valid_set(&runtime->snapshot,
                                                         index,
                                                         temperature_valid[index]);
    }
    memcpy(runtime->snapshot.bms_temperature_celsius,
           temperatures,
           sizeof(runtime->snapshot.bms_temperature_celsius));
    bms_apply_fault_masks(&runtime->snapshot, protection_mask, warning_mask);
    bms_set_info(runtime, "BMS OK");
    ESP_LOGI(TAG,
             "telemetry parsed: voltage=%lumV current_deci_amps=%d soc=%u%% temps=%u prot=%u warn=%u",
             (unsigned long)runtime->snapshot.pack_voltage_mv,
             (int)current_deci_amps,
             (unsigned)runtime->snapshot.soc_percent,
             (unsigned)temperature_count,
             (unsigned)runtime->snapshot.bms_protection_count,
             (unsigned)runtime->snapshot.bms_warning_count);
    return true;
}

static bool bms_apply_frame(esp_bms_idf_runtime_t *runtime, const uint8_t *data, size_t len)
{
    uint8_t function = 0U;
    size_t protocol_len = 0U;
    if (!bms_validate_frame(data, len, &function, &protocol_len)) {
        return false;
    }
    if (function == BMS_FRAME_TYPE_STATUS) {
        return bms_apply_status_frame(runtime, data, len);
    }
    if (function == BMS_FRAME_TYPE_DEVICE_INFO) {
        RUNTIME_SET_FLAG(runtime, BMS_DEVICE_INFO_KNOWN, true);
        ESP_LOGI(TAG, "device info parsed: len=%u", (unsigned)protocol_len);
        return true;
    }
    return false;
}

static bool bms_frame_push(esp_bms_idf_runtime_t *runtime,
                           const uint8_t *chunk,
                           size_t chunk_len)
{
    if (!runtime || !chunk || chunk_len == 0U) {
        return false;
    }
    if (chunk_len >= 2U && chunk[0] == BMS_FRAME_START_1 && chunk[1] == BMS_FRAME_START_2) {
        runtime->bms_frame_len = 0U;
    } else if (runtime->bms_frame_len == 0U && chunk[0] != BMS_FRAME_START_1) {
        return false;
    }
    if ((size_t)runtime->bms_frame_len + chunk_len > sizeof(runtime->bms_frame)) {
        runtime->bms_frame_len = 0U;
        return false;
    }

    memcpy(&runtime->bms_frame[runtime->bms_frame_len], chunk, chunk_len);
    runtime->bms_frame_len = (uint16_t)(runtime->bms_frame_len + chunk_len);
    if (runtime->bms_frame_len >= BMS_FRAME_MIN_LEN &&
        runtime->bms_frame[runtime->bms_frame_len - 2U] == BMS_FRAME_END_1 &&
        runtime->bms_frame[runtime->bms_frame_len - 1U] == BMS_FRAME_END_2) {
        const bool applied = bms_apply_frame(runtime, runtime->bms_frame, runtime->bms_frame_len);
        runtime->bms_frame_len = 0U;
        return applied;
    }
    return true;
}

static void bms_clear_telemetry(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return;
    }
    runtime->bms_telemetry_last_us = 0;
    runtime->trip_efficiency.anchor_valid = false;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, BMS_ONLINE, false);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, PACK_VOLTAGE_VALID, false);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CURRENT_VALID, false);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, SOC_VALID, false);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, MIN_CELL_VALID, false);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, AVERAGE_CELL_VALID, false);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, MAX_CELL_VALID, false);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, DELTA_CELL_VALID, false);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, TOTAL_CAPACITY_VALID, false);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CAPACITY_REMAINING_VALID, false);
    runtime->snapshot.pack_voltage_mv = 0U;
    runtime->snapshot.current_deci_amps = 0;
    runtime->snapshot.soc_percent = 0U;
    runtime->snapshot.min_cell_voltage_mv = 0U;
    runtime->snapshot.average_cell_voltage_mv = 0U;
    runtime->snapshot.max_cell_voltage_mv = 0U;
    runtime->snapshot.delta_cell_voltage_mv = 0U;
    runtime->snapshot.total_capacity_mah = 0U;
    runtime->snapshot.capacity_remaining_mah = 0U;
    runtime->snapshot.bms_protection_count = 0U;
    runtime->snapshot.bms_warning_count = 0U;
    memset(runtime->snapshot.bms_protection_codes, 0, sizeof(runtime->snapshot.bms_protection_codes));
    memset(runtime->snapshot.bms_warning_codes, 0, sizeof(runtime->snapshot.bms_warning_codes));
    memset(runtime->snapshot.bms_temperature_celsius,
           0,
           sizeof(runtime->snapshot.bms_temperature_celsius));
    for (uint8_t index = 0U; index < ESP_BMS_BMS_TEMP_MAX_COUNT; ++index) {
        esp_bms_dashboard_snapshot_temperature_valid_set(&runtime->snapshot, index, false);
    }
    bms_set_info(runtime, "BMS OFF");
}

static void bms_reset_connection_state(esp_bms_idf_runtime_t *runtime, bms_ble_phase_t phase)
{
    if (!runtime) {
        return;
    }
    runtime->bms_ble_phase = (uint8_t)phase;
    runtime->bms_conn_handle = 0xFFFFU;
    runtime->bms_service_start_handle = 0U;
    runtime->bms_service_end_handle = 0U;
    runtime->bms_char_val_handle = 0U;
    runtime->bms_cccd_handle = 0U;
    runtime->bms_frame_len = 0U;
    runtime->bms_status_poll_elapsed_ms = 0U;
    RUNTIME_SET_FLAG(runtime, BMS_WRITE_IN_FLIGHT, false);
    RUNTIME_SET_FLAG(runtime, BMS_DEVICE_INFO_REQUESTED, false);
    RUNTIME_SET_FLAG(runtime, BMS_DEVICE_INFO_KNOWN, false);
}

static esp_err_t bms_send_poll_request(esp_bms_idf_runtime_t *runtime,
                                       bool include_device_info);
static esp_err_t bms_start_scan(esp_bms_idf_runtime_t *runtime);

static int bms_write_cb(uint16_t conn_handle,
                        const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr,
                        void *arg)
{
    (void)attr;
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || conn_handle != runtime->bms_conn_handle) {
        return 0;
    }
    RUNTIME_SET_FLAG(runtime, BMS_WRITE_IN_FLIGHT, false);
    if (error && error->status != 0) {
        bms_set_info(runtime, "BMS WR");
        ESP_LOGW(TAG, "GATT write failed: conn=%u status=%u", conn_handle, (unsigned)error->status);
        return 0;
    }
    if (runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_SUBSCRIBING) {
        runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_ONLINE;
        runtime->bms_telemetry_last_us = esp_timer_get_time();
        bms_set_info(runtime, "BMS ON");
        const esp_err_t poll_ret = bms_send_poll_request(runtime, false);
        if (poll_ret != ESP_OK) {
            runtime->bms_status_poll_elapsed_ms = BMS_STATUS_POLL_PERIOD_MS;
            ESP_LOGW(TAG, "initial status poll failed: %s", esp_err_to_name(poll_ret));
        }
    }
    return 0;
}

static esp_err_t bms_write_frame(esp_bms_idf_runtime_t *runtime,
                                 const uint8_t *frame,
                                 size_t frame_len)
{
    if (!runtime || !frame || frame_len == 0U || runtime->bms_conn_handle == 0xFFFFU ||
        runtime->bms_char_val_handle == 0U || RUNTIME_FLAG(runtime, BMS_WRITE_IN_FLIGHT)) {
        return ESP_ERR_INVALID_STATE;
    }
    const int rc = ble_gattc_write_flat(runtime->bms_conn_handle,
                                        runtime->bms_char_val_handle,
                                        frame,
                                        (uint16_t)frame_len,
                                        bms_write_cb,
                                        runtime);
    if (rc != 0) {
        return ESP_FAIL;
    }
    RUNTIME_SET_FLAG(runtime, BMS_WRITE_IN_FLIGHT, true);
    return ESP_OK;
}

static esp_err_t bms_send_poll_request(esp_bms_idf_runtime_t *runtime,
                                       bool include_device_info)
{
    static const uint8_t status_request[] = { 0x7E, 0xA1, 0x01, 0x00, 0x00,
                                              0xBE, 0x18, 0x55, 0xAA, 0x55 };
    static const uint8_t device_info_request[] = { 0x7E, 0xA1, 0x02, 0x6C, 0x02,
                                                   0x20, 0x58, 0xC4, 0xAA, 0x55 };
    if (!runtime || runtime->bms_ble_phase != (uint8_t)BMS_BLE_PHASE_ONLINE) {
        return ESP_ERR_INVALID_STATE;
    }
    const bool send_device_info =
        include_device_info && !RUNTIME_FLAG(runtime, BMS_DEVICE_INFO_REQUESTED);
    const uint8_t *frame = send_device_info ? device_info_request : status_request;
    const size_t frame_len = send_device_info ? sizeof(device_info_request) : sizeof(status_request);
    const esp_err_t ret = bms_write_frame(runtime, frame, frame_len);
    if (ret == ESP_OK) {
        runtime->bms_status_poll_elapsed_ms = 0U;
        if (send_device_info) {
            RUNTIME_SET_FLAG(runtime, BMS_DEVICE_INFO_REQUESTED, true);
        }
    }
    return ret;
}

static esp_err_t bms_subscribe(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || runtime->bms_conn_handle == 0xFFFFU || runtime->bms_cccd_handle == 0U) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t value[2] = { 1U, 0U };
    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_SUBSCRIBING;
    RUNTIME_SET_FLAG(runtime, BMS_WRITE_IN_FLIGHT, true);
    const int rc = ble_gattc_write_flat(runtime->bms_conn_handle,
                                        runtime->bms_cccd_handle,
                                        value,
                                        sizeof(value),
                                        bms_write_cb,
                                        runtime);
    if (rc != 0) {
        RUNTIME_SET_FLAG(runtime, BMS_WRITE_IN_FLIGHT, false);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t bms_start_descriptor_discovery(esp_bms_idf_runtime_t *runtime);

static int bms_dsc_cb(uint16_t conn_handle,
                      const struct ble_gatt_error *error,
                      uint16_t chr_val_handle,
                      const struct ble_gatt_dsc *dsc,
                      void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || conn_handle != runtime->bms_conn_handle ||
        chr_val_handle != runtime->bms_char_val_handle) {
        return 0;
    }
    if (error && error->status == 0 && dsc) {
        if (ble_uuid_cmp(&dsc->uuid.u, BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16)) == 0) {
            runtime->bms_cccd_handle = dsc->handle;
        }
        return 0;
    }
    if (error && error->status == BLE_HS_EDONE) {
        if (runtime->bms_cccd_handle == 0U || bms_subscribe(runtime) != ESP_OK) {
            bms_set_info(runtime, runtime->bms_cccd_handle == 0U ? "BMS NO CCCD" : "BMS SUB");
            (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;
    }
    bms_set_info(runtime, "BMS DSC");
    (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

static esp_err_t bms_start_descriptor_discovery(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || runtime->bms_conn_handle == 0xFFFFU || runtime->bms_char_val_handle == 0U ||
        runtime->bms_char_val_handle >= runtime->bms_service_end_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_DISCOVERING_CCCD;
    runtime->bms_cccd_handle = 0U;
    return ble_gattc_disc_all_dscs(runtime->bms_conn_handle,
                                   runtime->bms_char_val_handle,
                                   runtime->bms_service_end_handle,
                                   bms_dsc_cb,
                                   runtime) == 0
               ? ESP_OK
               : ESP_FAIL;
}

static esp_err_t bms_start_characteristic_discovery(esp_bms_idf_runtime_t *runtime);

static int bms_chr_cb(uint16_t conn_handle,
                      const struct ble_gatt_error *error,
                      const struct ble_gatt_chr *chr,
                      void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || conn_handle != runtime->bms_conn_handle) {
        return 0;
    }
    if (error && error->status == 0 && chr) {
        runtime->bms_char_val_handle = chr->val_handle;
        return 0;
    }
    if (error && error->status == BLE_HS_EDONE) {
        if (runtime->bms_char_val_handle == 0U || bms_start_descriptor_discovery(runtime) != ESP_OK) {
            bms_set_info(runtime, "BMS NO CHR");
            (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;
    }
    bms_set_info(runtime, "BMS CHR");
    (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

static esp_err_t bms_start_characteristic_discovery(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || runtime->bms_conn_handle == 0xFFFFU || runtime->bms_service_start_handle == 0U ||
        runtime->bms_service_end_handle == 0U) {
        return ESP_ERR_INVALID_STATE;
    }
    ble_uuid16_t characteristic_uuid = BLE_UUID16_INIT(ANT_BMS_CHARACTERISTIC_UUID_16);
    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_DISCOVERING_CHARACTERISTIC;
    runtime->bms_char_val_handle = 0U;
    return ble_gattc_disc_chrs_by_uuid(runtime->bms_conn_handle,
                                       runtime->bms_service_start_handle,
                                       runtime->bms_service_end_handle,
                                       &characteristic_uuid.u,
                                       bms_chr_cb,
                                       runtime) == 0
               ? ESP_OK
               : ESP_FAIL;
}

static esp_err_t bms_start_service_discovery(esp_bms_idf_runtime_t *runtime);

static int bms_service_cb(uint16_t conn_handle,
                          const struct ble_gatt_error *error,
                          const struct ble_gatt_svc *service,
                          void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || conn_handle != runtime->bms_conn_handle) {
        return 0;
    }
    if (error && error->status == 0 && service) {
        runtime->bms_service_start_handle = service->start_handle;
        runtime->bms_service_end_handle = service->end_handle;
        return 0;
    }
    if (error && error->status == BLE_HS_EDONE) {
        if (runtime->bms_service_start_handle == 0U ||
            bms_start_characteristic_discovery(runtime) != ESP_OK) {
            bms_set_info(runtime, "BMS NO SVC");
            (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;
    }
    bms_set_info(runtime, "BMS SVC");
    (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

static esp_err_t bms_start_service_discovery(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || runtime->bms_conn_handle == 0xFFFFU) {
        return ESP_ERR_INVALID_STATE;
    }
    ble_uuid16_t service_uuid = BLE_UUID16_INIT(ANT_BMS_SERVICE_UUID_16);
    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_DISCOVERING_SERVICE;
    runtime->bms_service_start_handle = 0U;
    runtime->bms_service_end_handle = 0U;
    return ble_gattc_disc_svc_by_uuid(runtime->bms_conn_handle,
                                      &service_uuid.u,
                                      bms_service_cb,
                                      runtime) == 0
               ? ESP_OK
               : ESP_FAIL;
}

static char bms_hex_char(uint8_t value)
{
    return value < 10U ? (char)('0' + value) : (char)('A' + value - 10U);
}

static void bms_addr_to_mac_text(const uint8_t addr[6], char *out, size_t out_len)
{
    if (!addr || !out || out_len < 18U) {
        return;
    }
    (void)snprintf(out,
                   out_len,
                   "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
                   bms_hex_char(addr[5] >> 4U), bms_hex_char(addr[5] & 0x0FU),
                   bms_hex_char(addr[4] >> 4U), bms_hex_char(addr[4] & 0x0FU),
                   bms_hex_char(addr[3] >> 4U), bms_hex_char(addr[3] & 0x0FU),
                   bms_hex_char(addr[2] >> 4U), bms_hex_char(addr[2] & 0x0FU),
                   bms_hex_char(addr[1] >> 4U), bms_hex_char(addr[1] & 0x0FU),
                   bms_hex_char(addr[0] >> 4U), bms_hex_char(addr[0] & 0x0FU));
}

static bool bms_name_copy(char *out, size_t out_len, const uint8_t *name, size_t name_len)
{
    if (!out || out_len == 0U) {
        return false;
    }
    out[0] = '\0';
    if (!name || name_len == 0U) {
        return false;
    }
    const size_t limit = name_len < ESP_BMS_IDF_BMS_SCAN_NAME_LEN
                             ? name_len
                             : ESP_BMS_IDF_BMS_SCAN_NAME_LEN;
    size_t copied = 0U;
    for (size_t index = 0U; index < limit && copied + 1U < out_len; ++index) {
        const uint8_t value = name[index];
        if (value < 0x20U || value > 0x7EU || value == '"' || value == '\\') {
            break;
        }
        out[copied++] = (char)value;
    }
    out[copied] = '\0';
    return copied > 0U;
}

static int bms_gap_event(struct ble_gap_event *event, void *arg);

static esp_err_t bms_connect_to_disc(esp_bms_idf_runtime_t *runtime,
                                     const struct ble_gap_disc_desc *disc,
                                     const char *mac)
{
    if (!runtime || !disc || !mac || runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_CONNECTING ||
        runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_ONLINE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ble_gap_disc_active() && ble_gap_disc_cancel() != 0) {
        return ESP_FAIL;
    }
    uint8_t own_addr_type = 0U;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        return ESP_FAIL;
    }
    if (ble_gap_connect(own_addr_type,
                        &disc->addr,
                        BMS_CONNECT_TIMEOUT_MS,
                        NULL,
                        bms_gap_event,
                        runtime) != 0) {
        return ESP_FAIL;
    }
    runtime->bms_own_addr_type = own_addr_type;
    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_CONNECTING;
    RUNTIME_SET_FLAG(runtime, BMS_SCAN_ACTIVE, false);
    bms_set_info(runtime, "BMS CONN");
    ESP_LOGI(TAG, "connecting to bound BMS: mac=%s addr_type=%u", mac, disc->addr.type);
    return ESP_OK;
}

static void bms_handle_notification(esp_bms_idf_runtime_t *runtime,
                                    const struct ble_gap_event *event)
{
    if (!runtime || !event || event->notify_rx.conn_handle != runtime->bms_conn_handle ||
        event->notify_rx.attr_handle != runtime->bms_char_val_handle) {
        return;
    }
    const int len = OS_MBUF_PKTLEN(event->notify_rx.om);
    if (len <= 0 || len > (int)sizeof(runtime->bms_frame)) {
        bms_set_info(runtime, "BMS RX LEN");
        return;
    }
    uint8_t chunk[ESP_BMS_IDF_BMS_FRAME_MAX_LEN] = { 0 };
    const int rc = os_mbuf_copydata(event->notify_rx.om, 0, len, chunk);
    if (rc != 0 || !runtime->bms_frame_handler ||
        !runtime->bms_frame_handler(runtime, chunk, (size_t)len)) {
        bms_set_info(runtime, "BMS RX");
        ESP_LOGW(TAG, "notification parse failed: len=%d rc=%d", len, rc);
    }
}

static int bms_gap_event(struct ble_gap_event *event, void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || !event) {
        return 0;
    }
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        struct ble_hs_adv_fields fields = { 0 };
        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0) {
            return 0;
        }
        char mac[sizeof(runtime->bms_bound_mac)] = { 0 };
        char name[ESP_BMS_IDF_BMS_SCAN_NAME_LEN + 1U] = { 0 };
        bms_addr_to_mac_text(event->disc.addr.val, mac, sizeof(mac));
        const bool has_name = bms_name_copy(name, sizeof(name), fields.name, fields.name_len);
        const int8_t rssi = event->disc.rssi == 127 ? -128 : event->disc.rssi;
        if (RUNTIME_FLAG(runtime, BMS_SCAN_ACTIVE)) {
            esp_bms_idf_runtime_bms_scan_store_candidate(runtime,
                                                         mac,
                                                         has_name ? name : NULL,
                                                         rssi);
        }
        if (RUNTIME_FLAG(runtime, BMS_BIND_ACTIVE) && runtime->bms_bound_mac[0] != '\0' &&
            strcmp(mac, runtime->bms_bound_mac) == 0) {
            const esp_err_t ret = bms_connect_to_disc(runtime, &event->disc, mac);
            if (ret != ESP_OK) {
                bms_set_info(runtime, "BMS CONN ERR");
                ESP_LOGW(TAG, "connect request failed: mac=%s ret=%s", mac, esp_err_to_name(ret));
            }
        }
        return 0;
    }
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            runtime->bms_conn_handle = event->connect.conn_handle;
            __atomic_fetch_or(&runtime->pending_audio_events,
                              ESP_BMS_IDF_RUNTIME_AUDIO_EVENT_BMS_CONNECTED,
                              __ATOMIC_RELAXED);
            RUNTIME_SET_FLAG(runtime, BMS_SCAN_ACTIVE, false);
            runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_DISCOVERING_SERVICE;
            bms_set_info(runtime, "BMS DISC");
            if (bms_start_service_discovery(runtime) != ESP_OK) {
                bms_set_info(runtime, "BMS SVC");
                (void)ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
        } else {
            bms_reset_connection_state(runtime, BMS_BLE_PHASE_BACKOFF);
            bms_clear_telemetry(runtime);
            bms_set_info(runtime, "BMS CONN FAIL");
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        if (event->disconnect.conn.conn_handle == runtime->bms_conn_handle ||
            runtime->bms_ble_phase != (uint8_t)BMS_BLE_PHASE_SCANNING) {
            const bool scan_requested = RUNTIME_FLAG(runtime, BMS_SCAN_REQUESTED);
            bms_reset_connection_state(runtime, BMS_BLE_PHASE_BACKOFF);
            bms_clear_telemetry(runtime);
            bms_set_info(runtime, "BMS OFF");
            if (scan_requested && bms_start_scan(runtime) != ESP_OK) {
                ESP_LOGW(TAG, "deferred scan after disconnect failed");
            }
        }
        return 0;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_SCANNING) {
            runtime->bms_ble_phase = RUNTIME_FLAG(runtime, BMS_BIND_ACTIVE)
                                         ? (uint8_t)BMS_BLE_PHASE_BACKOFF
                                         : (uint8_t)BMS_BLE_PHASE_IDLE;
            runtime->bms_status_poll_elapsed_ms = 0U;
            RUNTIME_SET_FLAG(runtime, BMS_SCAN_ACTIVE, false);
            RUNTIME_SET_FLAG(runtime, BMS_SCAN_SNAPSHOT_DIRTY, true);
            bms_set_info(runtime, "BMS DONE");
        }
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX:
        bms_handle_notification(runtime, event);
        return 0;
    default:
        return 0;
    }
}

static esp_err_t bms_start_scan(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || !RUNTIME_FLAG(runtime, BMS_BLE_READY) ||
        !RUNTIME_FLAG(runtime, BMS_BLE_SYNCED)) {
        if (runtime) {
            RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, true);
        }
        return ESP_ERR_INVALID_STATE;
    }
    if (RUNTIME_FLAG(runtime, BMS_SCAN_ACTIVE)) {
        RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, false);
        return ESP_OK;
    }
    if (ble_gap_disc_active()) {
        /* NimBLE has one global discovery callback; hand ownership to BMS. */
        RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_REQUESTED, false);
        RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, true);
        RUNTIME_SET_FLAG(runtime, BMS_SCAN_ACTIVE, false);
        runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_IDLE;
        bms_set_info(runtime, "BMS WAIT");
        (void)ble_gap_disc_cancel();
        ESP_LOGI(TAG, "BLE scan handoff requested: controller -> BMS");
        return ESP_OK;
    }
    uint8_t own_addr_type = 0U;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, true);
        return ESP_FAIL;
    }
    const struct ble_gap_disc_params params = {
        .filter_duplicates = 0,
        .passive = 0,
        .filter_policy = 0,
        .limited = 0,
    };
    if (ble_gap_disc(own_addr_type,
                     BMS_SCAN_DURATION_MS,
                     &params,
                     bms_gap_event,
                     runtime) != 0) {
        RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, true);
        bms_reset_connection_state(runtime, BMS_BLE_PHASE_BACKOFF);
        return ESP_FAIL;
    }
    runtime->bms_own_addr_type = own_addr_type;
    RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, false);
    RUNTIME_SET_FLAG(runtime, BMS_SCAN_ACTIVE, true);
    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_SCANNING;
    bms_set_info(runtime, "BMS SCAN");
    ESP_LOGI(TAG, "BLE scan started: duration_ms=%u", (unsigned)BMS_SCAN_DURATION_MS);
    return ESP_OK;
}

static esp_err_t bms_start_scan_or_defer(esp_bms_idf_runtime_t *runtime)
{
    const esp_err_t ret = bms_start_scan(runtime);
    return ret == ESP_ERR_INVALID_STATE && RUNTIME_FLAG(runtime, BMS_SCAN_REQUESTED) ? ESP_OK : ret;
}

static esp_err_t bms_start_if_bound(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t load_ret = esp_bms_idf_runtime_load_bms_binding(runtime);
    if (load_ret == ESP_ERR_NVS_NOT_FOUND || load_ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "no valid bound MAC; BLE stays off");
        return ESP_OK;
    }
    if (load_ret != ESP_OK) {
        return load_ret;
    }
    RUNTIME_SET_FLAG(runtime, BMS_BIND_ACTIVE, true);
    const esp_err_t host_ret = esp_bms_idf_runtime_ensure_ble_host(runtime);
    return host_ret == ESP_OK ? bms_start_scan_or_defer(runtime) : host_ret;
}

static esp_err_t bms_start_for_bind(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    bms_clear_telemetry(runtime);
    esp_bms_idf_runtime_bms_scan_clear_candidates(runtime);
    const esp_err_t host_ret = esp_bms_idf_runtime_ensure_ble_host(runtime);
    return host_ret == ESP_OK ? bms_start_scan_or_defer(runtime) : host_ret;
}

static esp_err_t bms_resume_scan(esp_bms_idf_runtime_t *runtime)
{
    return bms_start_scan(runtime);
}

static bool bms_stop(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return false;
    }

    const bool active = runtime->bms_ble_phase != (uint8_t)BMS_BLE_PHASE_IDLE ||
                        RUNTIME_FLAG(runtime, BMS_SCAN_ACTIVE) ||
                        RUNTIME_FLAG(runtime, BMS_SCAN_REQUESTED) ||
                        RUNTIME_FLAG(runtime, BMS_BIND_ACTIVE);
    RUNTIME_SET_FLAG(runtime, BMS_BIND_ACTIVE, false);
    RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, false);
    if (ble_gap_disc_active()) {
        (void)ble_gap_disc_cancel();
    }
    if (runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_CONNECTING) {
        (void)ble_gap_conn_cancel();
    } else if (runtime->bms_conn_handle != 0xFFFFU) {
        (void)ble_gap_terminate(runtime->bms_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    bms_reset_connection_state(runtime, BMS_BLE_PHASE_IDLE);
    bms_clear_telemetry(runtime);
    bms_set_info(runtime, "BMS OFF");
    ESP_LOGI(TAG, "BMS connection cancelled");
    return active;
}

static void bms_on_ble_reset(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || !RUNTIME_FLAG(runtime, BMS_BIND_ACTIVE)) {
        return;
    }
    bms_reset_connection_state(runtime, BMS_BLE_PHASE_BACKOFF);
    RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, true);
}

static bool bms_tick(esp_bms_idf_runtime_t *runtime, uint32_t elapsed_ms)
{
    if (!runtime) {
        return false;
    }
    bool changed = false;
    if (RUNTIME_FLAG(runtime, BMS_SCAN_REQUESTED) &&
        RUNTIME_FLAG(runtime, BMS_BLE_READY) &&
        RUNTIME_FLAG(runtime, BMS_BLE_SYNCED) && !ble_gap_disc_active()) {
        (void)bms_start_scan(runtime);
        changed = true;
    }
    if (RUNTIME_FLAG(runtime, BMS_SCAN_SNAPSHOT_DIRTY)) {
        RUNTIME_SET_FLAG(runtime, BMS_SCAN_SNAPSHOT_DIRTY, false);
        changed = esp_bms_idf_runtime_bms_scan_project_snapshot(runtime) || changed;
    }
    if (runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_ONLINE) {
        const int64_t heartbeat_age_us = esp_timer_get_time() - runtime->bms_telemetry_last_us;
        if (runtime->bms_telemetry_last_us > 0 && heartbeat_age_us >= 0 &&
            heartbeat_age_us >= (int64_t)BMS_HEARTBEAT_TIMEOUT_MS * 1000) {
            const uint16_t conn_handle = runtime->bms_conn_handle;
            runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_BACKOFF;
            runtime->bms_status_poll_elapsed_ms = 0U;
            bms_clear_telemetry(runtime);
            bms_set_info(runtime, "BMS TIMEOUT");
            if (ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM) != 0) {
                bms_reset_connection_state(runtime, BMS_BLE_PHASE_BACKOFF);
            }
            changed = true;
        } else {
            runtime->bms_status_poll_elapsed_ms += elapsed_ms;
            if (runtime->bms_status_poll_elapsed_ms >= BMS_STATUS_POLL_PERIOD_MS &&
                !RUNTIME_FLAG(runtime, BMS_WRITE_IN_FLIGHT) &&
                bms_send_poll_request(runtime, true) != ESP_OK) {
                bms_set_info(runtime, "BMS POLL");
                changed = true;
            }
        }
    } else if (RUNTIME_FLAG(runtime, BMS_BIND_ACTIVE) && RUNTIME_FLAG(runtime, BMS_BLE_READY) &&
               RUNTIME_FLAG(runtime, BMS_BLE_SYNCED) &&
               (runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_IDLE ||
                runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_BACKOFF)) {
        runtime->bms_status_poll_elapsed_ms += elapsed_ms;
        if (runtime->bms_status_poll_elapsed_ms >= BMS_RECONNECT_BACKOFF_MS) {
            runtime->bms_status_poll_elapsed_ms = 0U;
            (void)bms_start_scan(runtime);
            changed = true;
        }
    }
    return changed;
}

static const esp_bms_idf_runtime_bms_ble_driver_t s_bms_ble_driver = {
    .start_if_bound = bms_start_if_bound,
    .start_for_bind = bms_start_for_bind,
    .resume_scan = bms_resume_scan,
    .stop = bms_stop,
    .tick = bms_tick,
    .on_ble_reset = bms_on_ble_reset,
};

esp_err_t esp_bms_bms_ble_init(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_bms_idf_runtime_register_bms_frame_handler(runtime, bms_frame_push);
    esp_bms_idf_runtime_register_bms_ble_driver(runtime, &s_bms_ble_driver);
    return ESP_OK;
}

esp_err_t esp_bms_bms_ble_start(esp_bms_idf_runtime_t *runtime)
{
    return bms_start_if_bound(runtime);
}
