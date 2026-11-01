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
#include "esp_bms_bms_safety.h"
#include "protocols/ant/esp_bms_ant_protocol.h"
#include "protocols/bluebabe/esp_bms_bluebabe_protocol.h"
#include "protocols/daly/esp_bms_daly_protocol.h"
#include "protocols/em02/esp_bms_em02_protocol.h"
#include "protocols/esp_bms_bms_protocol.h"
#include "protocols/jbd/esp_bms_jbd_protocol.h"
#include "protocols/jk/esp_bms_jk_protocol.h"
#include "protocols/mingtang/esp_bms_mingtang_protocol.h"
#include "protocols/yanyang/esp_bms_yanyang_protocol.h"

static const char *TAG = "esp_bms_bms_ble";

#define BMS_SCAN_DEBUG_REPORT_LIMIT 32U
#define ANT_BMS_SERVICE_UUID_16 0xFFE0U
#define ANT_BMS_CHARACTERISTIC_UUID_16 0xFFE1U
#define BMS_SCAN_DURATION_MS 10000
#define BMS_CONNECT_TIMEOUT_MS 10000
#define BMS_ESTABLISH_TIMEOUT_MS 10000U
#define BMS_STATUS_POLL_PERIOD_MS 500U
#define BMS_HEARTBEAT_TIMEOUT_MS 30000U
#define BMS_RECONNECT_BACKOFF_MS 3000U

typedef enum {
    ANT_BLE_PROTOCOL_PROBE_NEW = 0U,
    ANT_BLE_PROTOCOL_PROBE_OLD = 1U,
    ANT_BLE_PROTOCOL_NEW = 2U,
    ANT_BLE_PROTOCOL_OLD = 3U,
} ant_ble_protocol_t;

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

/* The runtime and protocol modules have one device owner; tokens outlive callbacks. */
static esp_bms_idf_runtime_t *s_bms_runtime;

static void bms_advance_session(esp_bms_idf_runtime_t *runtime)
{
    if (++runtime->bms_ble_session == 0U) {
        ++runtime->bms_ble_session;
    }
}

static void *bms_callback_arg(const esp_bms_idf_runtime_t *runtime)
{
    return (void *)(uintptr_t)runtime->bms_ble_session;
}

static esp_bms_idf_runtime_t *bms_callback_runtime(void *arg)
{
    return s_bms_runtime && (uint32_t)(uintptr_t)arg == s_bms_runtime->bms_ble_session
               ? s_bms_runtime : NULL;
}

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

static bool bms_mask_has_any(uint64_t mask, uint64_t bits)
{
    return (mask & bits) != 0ULL;
}

static void bms_apply_safety_status(esp_bms_idf_runtime_t *runtime,
                                    const esp_bms_bms_telemetry_t *telemetry)
{
    if (!runtime || !telemetry || telemetry->partial) {
        return;
    }

    uint16_t supported = 0U;
    uint16_t active = 0U;
    const uint64_t mask = telemetry->protection_mask;
    if (runtime->bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_ANT && telemetry->ant_diagnostics.valid) {
        supported = ESP_BMS_BMS_SAFETY_CELL_OV | ESP_BMS_BMS_SAFETY_CELL_UV |
                    ESP_BMS_BMS_SAFETY_OVER_TEMP | ESP_BMS_BMS_SAFETY_UNDER_TEMP |
                    ESP_BMS_BMS_SAFETY_OVER_CURRENT | ESP_BMS_BMS_SAFETY_SHORT_CIRCUIT |
                    ESP_BMS_BMS_SAFETY_CELL_DELTA;
        if (bms_mask_has_any(mask, (UINT64_C(1) << 1U) | (UINT64_C(1) << 2U) | (UINT64_C(1) << 3U))) {
            active |= ESP_BMS_BMS_SAFETY_CELL_OV;
        }
        if (bms_mask_has_any(mask, (UINT64_C(1) << 4U) | (UINT64_C(1) << 5U) | (UINT64_C(1) << 6U))) {
            active |= ESP_BMS_BMS_SAFETY_CELL_UV;
        }
        if (bms_mask_has_any(mask, (UINT64_C(1) << 8U) | (UINT64_C(1) << 9U) | (UINT64_C(1) << 10U))) {
            active |= ESP_BMS_BMS_SAFETY_OVER_TEMP;
        }
        if (bms_mask_has_any(mask, (UINT64_C(1) << 11U) | (UINT64_C(1) << 12U))) {
            active |= ESP_BMS_BMS_SAFETY_UNDER_TEMP;
        }
        if (bms_mask_has_any(mask, (UINT64_C(1) << 13U) | (UINT64_C(1) << 14U) | (UINT64_C(1) << 15U))) {
            active |= ESP_BMS_BMS_SAFETY_OVER_CURRENT;
        }
        if (bms_mask_has_any(mask, UINT64_C(1) << 16U)) {
            active |= ESP_BMS_BMS_SAFETY_SHORT_CIRCUIT;
        }
        if (bms_mask_has_any(mask, UINT64_C(1) << 7U)) {
            active |= ESP_BMS_BMS_SAFETY_CELL_DELTA;
        }
    } else if (runtime->bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_JBD) {
        supported = ESP_BMS_BMS_SAFETY_CELL_OV | ESP_BMS_BMS_SAFETY_CELL_UV |
                    ESP_BMS_BMS_SAFETY_OVER_TEMP | ESP_BMS_BMS_SAFETY_UNDER_TEMP |
                    ESP_BMS_BMS_SAFETY_OVER_CURRENT | ESP_BMS_BMS_SAFETY_SHORT_CIRCUIT;
        if (bms_mask_has_any(mask, UINT64_C(0x0005))) {
            active |= ESP_BMS_BMS_SAFETY_CELL_OV;
        }
        if (bms_mask_has_any(mask, UINT64_C(0x000A))) {
            active |= ESP_BMS_BMS_SAFETY_CELL_UV;
        }
        if (bms_mask_has_any(mask, UINT64_C(0x0050))) {
            active |= ESP_BMS_BMS_SAFETY_OVER_TEMP;
        }
        if (bms_mask_has_any(mask, UINT64_C(0x00A0))) {
            active |= ESP_BMS_BMS_SAFETY_UNDER_TEMP;
        }
        if (bms_mask_has_any(mask, UINT64_C(0x0300))) {
            active |= ESP_BMS_BMS_SAFETY_OVER_CURRENT;
        }
        if (bms_mask_has_any(mask, UINT64_C(0x0400))) {
            active |= ESP_BMS_BMS_SAFETY_SHORT_CIRCUIT;
        }
    } else if (runtime->bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_JK) {
        supported = ESP_BMS_BMS_SAFETY_CELL_OV | ESP_BMS_BMS_SAFETY_CELL_UV |
                    ESP_BMS_BMS_SAFETY_OVER_TEMP | ESP_BMS_BMS_SAFETY_UNDER_TEMP |
                    ESP_BMS_BMS_SAFETY_OVER_CURRENT | ESP_BMS_BMS_SAFETY_SHORT_CIRCUIT;
        if (bms_mask_has_any(mask, UINT64_C(1) << 5U)) {
            active |= ESP_BMS_BMS_SAFETY_CELL_OV;
        }
        if (bms_mask_has_any(mask, (UINT64_C(1) << 11U) | (UINT64_C(1) << 12U))) {
            active |= ESP_BMS_BMS_SAFETY_CELL_UV;
        }
        if (bms_mask_has_any(mask,
                             (UINT64_C(1) << 1U) | (UINT64_C(1) << 8U) |
                                 (UINT64_C(1) << 15U) | (UINT64_C(1) << 21U))) {
            active |= ESP_BMS_BMS_SAFETY_OVER_TEMP;
        }
        if (bms_mask_has_any(mask, (UINT64_C(1) << 9U) | (UINT64_C(1) << 27U))) {
            active |= ESP_BMS_BMS_SAFETY_UNDER_TEMP;
        }
        if (bms_mask_has_any(mask,
                             (UINT64_C(1) << 6U) | (UINT64_C(1) << 13U) |
                                 (UINT64_C(1) << 25U) | (UINT64_C(1) << 26U))) {
            active |= ESP_BMS_BMS_SAFETY_OVER_CURRENT;
        }
        if (bms_mask_has_any(mask,
                             (UINT64_C(1) << 7U) | (UINT64_C(1) << 14U) |
                                 (UINT64_C(1) << 24U))) {
            active |= ESP_BMS_BMS_SAFETY_SHORT_CIRCUIT;
        }
    } else if (runtime->bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_MINGTANG) {
        /* 明唐：只有充电/放电/短路三类硬件保护位置位，位图由协议模块给到 bit0..2。
         * 充电/放电保护分别视为过流；短路单独映射。 */
        supported = ESP_BMS_BMS_SAFETY_OVER_CURRENT | ESP_BMS_BMS_SAFETY_SHORT_CIRCUIT;
        if (bms_mask_has_any(mask, UINT64_C(1) << 0U) ||
            bms_mask_has_any(mask, UINT64_C(1) << 1U)) {
            active |= ESP_BMS_BMS_SAFETY_OVER_CURRENT;
        }
        if (bms_mask_has_any(mask, UINT64_C(1) << 2U)) {
            active |= ESP_BMS_BMS_SAFETY_SHORT_CIRCUIT;
        }
    } else if (runtime->bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_DALY) {
#define DALY_PROTECTION_BIT(byte_index, bit_index) \
    (UINT64_C(1) << (((7U - (byte_index)) * 8U) + (bit_index)))
        supported = ESP_BMS_BMS_SAFETY_CELL_OV | ESP_BMS_BMS_SAFETY_CELL_UV |
                    ESP_BMS_BMS_SAFETY_OVER_TEMP | ESP_BMS_BMS_SAFETY_UNDER_TEMP |
                    ESP_BMS_BMS_SAFETY_OVER_CURRENT | ESP_BMS_BMS_SAFETY_SHORT_CIRCUIT |
                    ESP_BMS_BMS_SAFETY_CELL_DELTA;
        if (bms_mask_has_any(mask,
                             DALY_PROTECTION_BIT(0U, 0U) | DALY_PROTECTION_BIT(0U, 2U))) {
            active |= ESP_BMS_BMS_SAFETY_CELL_OV;
        }
        if (bms_mask_has_any(mask,
                             DALY_PROTECTION_BIT(0U, 1U) | DALY_PROTECTION_BIT(0U, 3U))) {
            active |= ESP_BMS_BMS_SAFETY_CELL_UV;
        }
        if (bms_mask_has_any(mask,
                             DALY_PROTECTION_BIT(1U, 0U) | DALY_PROTECTION_BIT(1U, 2U) |
                                 DALY_PROTECTION_BIT(4U, 0U) | DALY_PROTECTION_BIT(4U, 1U))) {
            active |= ESP_BMS_BMS_SAFETY_OVER_TEMP;
        }
        if (bms_mask_has_any(mask,
                             DALY_PROTECTION_BIT(1U, 1U) | DALY_PROTECTION_BIT(1U, 3U))) {
            active |= ESP_BMS_BMS_SAFETY_UNDER_TEMP;
        }
        if (bms_mask_has_any(mask,
                             DALY_PROTECTION_BIT(2U, 0U) | DALY_PROTECTION_BIT(2U, 1U))) {
            active |= ESP_BMS_BMS_SAFETY_OVER_CURRENT;
        }
        if (bms_mask_has_any(mask, DALY_PROTECTION_BIT(6U, 2U))) {
            active |= ESP_BMS_BMS_SAFETY_SHORT_CIRCUIT;
        }
        if (bms_mask_has_any(mask,
                             DALY_PROTECTION_BIT(3U, 0U) | DALY_PROTECTION_BIT(3U, 1U))) {
            active |= ESP_BMS_BMS_SAFETY_CELL_DELTA;
        }
#undef DALY_PROTECTION_BIT
    }

    if (telemetry->balancing_supported) {
        supported |= ESP_BMS_BMS_SAFETY_BALANCING;
        if (telemetry->balancing_active) {
            active |= ESP_BMS_BMS_SAFETY_BALANCING;
        }
    }
    runtime->snapshot.bms_safety_supported_mask = supported;
    runtime->snapshot.bms_safety_active_mask = active & supported;
}

static bool bms_apply_telemetry(esp_bms_idf_runtime_t *runtime,
                                const esp_bms_bms_telemetry_t *telemetry)
{
    if (!runtime || !telemetry) {
        return false;
    }
    const bool new_connection = !esp_bms_dashboard_snapshot_flag_get(
        &runtime->snapshot,
        ESP_BMS_DASHBOARD_FLAG_BMS_ONLINE);
    const uint32_t fields = telemetry->partial ? telemetry->present_fields : UINT32_MAX;
    const bool live = (fields & ESP_BMS_BMS_FIELD_PACK_VOLTAGE) != 0U;
#define BMS_APPLY_FIELD(field, flag, member) \
    if ((fields & ESP_BMS_BMS_FIELD_##field) != 0U) { \
        runtime->snapshot.member = telemetry->member; \
        RUNTIME_SET_SNAPSHOT_FLAG(runtime, flag, true); \
    }
    BMS_APPLY_FIELD(PACK_VOLTAGE, PACK_VOLTAGE_VALID, pack_voltage_mv);
    BMS_APPLY_FIELD(CURRENT, CURRENT_VALID, current_deci_amps);
    BMS_APPLY_FIELD(SOC, SOC_VALID, soc_percent);
    BMS_APPLY_FIELD(MAX_CELL, MAX_CELL_VALID, max_cell_voltage_mv);
    BMS_APPLY_FIELD(MIN_CELL, MIN_CELL_VALID, min_cell_voltage_mv);
    BMS_APPLY_FIELD(DELTA_CELL, DELTA_CELL_VALID, delta_cell_voltage_mv);
    BMS_APPLY_FIELD(AVERAGE_CELL, AVERAGE_CELL_VALID, average_cell_voltage_mv);
    BMS_APPLY_FIELD(TOTAL_CAPACITY, TOTAL_CAPACITY_VALID, total_capacity_mah);
    BMS_APPLY_FIELD(CAPACITY_REMAINING, CAPACITY_REMAINING_VALID, capacity_remaining_mah);
#undef BMS_APPLY_FIELD
    if ((fields & ESP_BMS_BMS_FIELD_TEMPERATURES) != 0U) {
        for (uint8_t index = 0U; index < ESP_BMS_BMS_TEMP_MAX_COUNT; ++index) {
            esp_bms_dashboard_snapshot_temperature_valid_set(&runtime->snapshot,
                                                             index,
                                                             telemetry->temperature_valid[index]);
        }
        memcpy(runtime->snapshot.bms_temperature_celsius,
               telemetry->temperatures_celsius,
               sizeof(runtime->snapshot.bms_temperature_celsius));
    }
    if (!telemetry->partial) {
        runtime->snapshot.bms_running_time_seconds = telemetry->running_time_seconds;
        runtime->snapshot.bms_running_time_valid = telemetry->running_time_valid;
        runtime->snapshot.bms_cycle_capacity_mah = telemetry->total_cycle_mah;
        runtime->snapshot.bms_cycle_capacity_valid = telemetry->total_cycle_valid;
        const esp_bms_bms_ant_diagnostics_t *diag = &telemetry->ant_diagnostics;
        runtime->snapshot.bms_ant_diagnostics = (esp_bms_ant_snapshot_diagnostics_t) {
            .valid = diag->valid,
            .cell_count = diag->cell_count,
            .soh_percent = diag->soh_percent,
            .charge_mos_state = diag->charge_mos_state,
            .discharge_mos_state = diag->discharge_mos_state,
            .balance_state = diag->balance_state,
            .balance_mask = diag->balance_mask,
            .power_w = diag->power_w,
            .charge_time_seconds = diag->charge_time_seconds,
            .last_charge_gap_seconds = diag->last_charge_gap_seconds,
            .remaining_charge_minutes = diag->remaining_charge_minutes,
            .remaining_discharge_minutes = diag->remaining_discharge_minutes,
            .charge_time_valid = diag->charge_time_valid,
            .last_charge_gap_valid = diag->last_charge_gap_valid,
            .remaining_charge_valid = diag->remaining_charge_valid,
            .remaining_discharge_valid = diag->remaining_discharge_valid,
        };
        memcpy(runtime->snapshot.bms_ant_diagnostics.cell_voltage_mv,
               diag->cell_voltage_mv,
               sizeof(runtime->snapshot.bms_ant_diagnostics.cell_voltage_mv));
        bms_apply_fault_masks(&runtime->snapshot, telemetry->protection_mask, telemetry->warning_mask);
        bms_apply_safety_status(runtime, telemetry);
    }
    if (live) {
        RUNTIME_SET_SNAPSHOT_FLAG(runtime, BMS_ONLINE, true);
        runtime->bms_telemetry_last_us = esp_timer_get_time();
    }
    if (!telemetry->partial && telemetry->total_cycle_valid &&
        (runtime->bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_ANT ||
         runtime->bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_JK)) {
        (void)esp_bms_idf_runtime_observe_bms_capacity(runtime,
                                                        new_connection,
                                                        telemetry->total_cycle_mah,
                                                        telemetry->soc_percent);
    } else if (!telemetry->partial && telemetry->pack_voltage_mv != 0U &&
               telemetry->soc_percent <= 100U &&
               (runtime->bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_DALY ||
                runtime->bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_YANYANG)) {
        (void)esp_bms_idf_runtime_observe_bms_capacity_from_current(runtime,
                                                                     new_connection,
                                                                     telemetry->current_deci_amps,
                                                                     telemetry->soc_percent);
    }
    if (live) {
        bms_set_info(runtime, "BMS OK");
    } else {
        RUNTIME_SET_FLAG(runtime, BMS_SNAPSHOT_DIRTY, true);
    }
    if (!telemetry->partial && telemetry->pack_voltage_mv != 0U && telemetry->soc_percent <= 100U) {
        esp_bms_ride_record_sample_t sample = {
            .valid = true,
            .snapshot = {
                .pack_voltage_mv = telemetry->pack_voltage_mv,
                .current_deci_amps = telemetry->current_deci_amps,
                .delta_cell_voltage_mv = telemetry->delta_cell_voltage_mv,
                .soc_percent = telemetry->soc_percent,
            },
        };
        for (uint8_t index = 0U; index < ESP_BMS_RIDE_RECORD_TEMP_MAX_COUNT; ++index) {
            sample.snapshot.temperatures_celsius[index] = telemetry->temperatures_celsius[index];
            if (telemetry->temperature_valid[index]) {
                sample.snapshot.temperature_valid_mask |= UINT8_C(1) << index;
            }
        }
        (void)esp_bms_idf_runtime_record_bms_sample(runtime, &sample);
    }
    return true;
}

static bool bms_is_yanyang(const esp_bms_idf_runtime_t *runtime)
{
    return runtime && runtime->bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_YANYANG;
}

static bool bms_is_jk(const esp_bms_idf_runtime_t *runtime)
{
    return runtime && runtime->bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_JK;
}

static bool bms_is_jbd(const esp_bms_idf_runtime_t *runtime)
{
    return runtime && runtime->bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_JBD;
}

static bool bms_is_daly(const esp_bms_idf_runtime_t *runtime)
{
    return runtime && runtime->bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_DALY;
}

static bool bms_is_bluebabe(const esp_bms_idf_runtime_t *runtime)
{
    return runtime && runtime->bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_BLUEBABE;
}

static bool bms_is_mingtang(const esp_bms_idf_runtime_t *runtime)
{
    return runtime && runtime->bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_MINGTANG;
}

static bool bms_is_em02(const esp_bms_idf_runtime_t *runtime)
{
    return runtime && runtime->bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_EM02;
}

static bool bms_ant_uses_old_protocol(const esp_bms_idf_runtime_t *runtime)
{
    return runtime && (runtime->bms_poll_index == (uint8_t)ANT_BLE_PROTOCOL_PROBE_OLD ||
                       runtime->bms_poll_index == (uint8_t)ANT_BLE_PROTOCOL_OLD);
}

static bool bms_is_jk_characteristic(const struct ble_gatt_chr *chr)
{
    return chr && ble_uuid_cmp(&chr->uuid.u, BLE_UUID16_DECLARE(ANT_BMS_CHARACTERISTIC_UUID_16)) == 0;
}

static uint16_t bms_service_uuid_16(const esp_bms_idf_runtime_t *runtime)
{
    if (bms_is_jbd(runtime)) {
        return 0xFF00U;
    }
    if (bms_is_daly(runtime)) {
        return 0xFFF0U;
    }
    if (bms_is_bluebabe(runtime)) {
        return 0xFFE0U;
    }
    if (bms_is_em02(runtime)) {
        return 0xFFE0U;
    }
    if (bms_is_mingtang(runtime)) {
        return 0xFA00U;
    }
    return ANT_BMS_SERVICE_UUID_16;
}

static ble_uuid128_t bms_uuid128(const uint8_t bytes[ESP_BMS_YANYANG_UUID_LEN])
{
    ble_uuid128_t uuid = { .u = { .type = BLE_UUID_TYPE_128 } };
    memcpy(uuid.value, bytes, sizeof(uuid.value));
    return uuid;
}

static void bms_clear_telemetry(esp_bms_idf_runtime_t *runtime);

static bool bms_frame_push(esp_bms_idf_runtime_t *runtime,
                           const uint8_t *chunk,
                           size_t chunk_len)
{
    if (!runtime || !chunk || chunk_len == 0U) {
        return false;
    }
    esp_bms_bms_telemetry_t telemetry = { 0 };
    if (bms_is_yanyang(runtime)) {
        size_t stream_len = runtime->bms_frame_len;
        const bool decoded = esp_bms_yanyang_feed(runtime->bms_frame,
                                                  &stream_len,
                                                  sizeof(runtime->bms_frame),
                                                  chunk,
                                                  chunk_len,
                                                  &telemetry);
        runtime->bms_frame_len = (uint16_t)stream_len;
        return !decoded || bms_apply_telemetry(runtime, &telemetry);
    }
    const bool ant_old_start = chunk_len >= 3U && chunk[0] == 0xAAU && chunk[1] == 0x55U &&
                               chunk[2] == 0xAAU;
    const bool ant_old_stream = runtime->bms_frame_len >= 3U && runtime->bms_frame[0] == 0xAAU &&
                                runtime->bms_frame[1] == 0x55U && runtime->bms_frame[2] == 0xAAU;
    if (runtime->bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_ANT && ant_old_start) {
        runtime->bms_poll_index = (uint8_t)ANT_BLE_PROTOCOL_PROBE_OLD;
    }
    if (runtime->bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_ANT &&
        (ant_old_start || ant_old_stream)) {
        size_t stream_len = runtime->bms_frame_len;
        const bool decoded = esp_bms_ant_protocol_old_feed(runtime->bms_frame,
                                                            &stream_len,
                                                            sizeof(runtime->bms_frame),
                                                            chunk,
                                                            chunk_len,
                                                            &telemetry);
        runtime->bms_frame_len = (uint16_t)stream_len;
        if (!decoded) {
            return true;
        }
        runtime->bms_poll_index = (uint8_t)ANT_BLE_PROTOCOL_OLD;
        return bms_apply_telemetry(runtime, &telemetry);
    }
    if (bms_is_jk(runtime)) {
        size_t stream_len = runtime->bms_frame_len;
        const bool decoded = esp_bms_jk_feed(runtime->bms_frame,
                                             &stream_len,
                                             sizeof(runtime->bms_frame),
                                             chunk,
                                             chunk_len,
                                             &telemetry);
        runtime->bms_frame_len = (uint16_t)stream_len;
        return !decoded || bms_apply_telemetry(runtime, &telemetry);
    }
    if (bms_is_jbd(runtime)) {
        size_t stream_len = runtime->bms_frame_len;
        const bool decoded = esp_bms_jbd_feed(runtime->bms_frame,
                                              &stream_len,
                                              sizeof(runtime->bms_frame),
                                              chunk,
                                              chunk_len,
                                              &telemetry);
        runtime->bms_frame_len = (uint16_t)stream_len;
        return !decoded || bms_apply_telemetry(runtime, &telemetry);
    }
    if (bms_is_bluebabe(runtime)) {
        size_t stream_len = runtime->bms_frame_len;
        const bool decoded = esp_bms_bluebabe_feed(runtime->bms_frame,
                                                   &stream_len,
                                                   sizeof(runtime->bms_frame),
                                                   chunk,
                                                   chunk_len,
                                                   &telemetry);
        runtime->bms_frame_len = (uint16_t)stream_len;
        return !decoded || bms_apply_telemetry(runtime, &telemetry);
    }
    if (bms_is_em02(runtime)) {
        size_t stream_len = runtime->bms_frame_len;
        bool identity_known = RUNTIME_FLAG(runtime, BMS_DEVICE_INFO_KNOWN);
        uint8_t response_index = 0xFFU;
        bool consumed = false;
        bool applied = false;
        const uint8_t *input = chunk;
        size_t input_len = chunk_len;
        while (esp_bms_em02_feed(runtime->bms_frame, &stream_len,
                                  sizeof(runtime->bms_frame), input, input_len,
                                  &response_index, &identity_known, &telemetry)) {
            consumed = true;
            input = NULL;
            input_len = 0U;
            if (response_index == 0U) {
                RUNTIME_SET_FLAG(runtime, BMS_DEVICE_INFO_KNOWN, identity_known);
                if (!identity_known) {
                    bms_clear_telemetry(runtime);
                    bms_set_info(runtime, "BMS MODEL");
                }
            } else if (response_index == 1U && identity_known) {
                applied = bms_apply_telemetry(runtime, &telemetry) || applied;
            }
        }
        runtime->bms_frame_len = (uint16_t)stream_len;
        return applied || consumed || stream_len > 0U;
    }
    if (bms_is_mingtang(runtime)) {
        size_t stream_len = runtime->bms_frame_len;
        const bool decoded = esp_bms_mingtang_feed(runtime->bms_frame,
                                                   &stream_len,
                                                   sizeof(runtime->bms_frame),
                                                   chunk,
                                                   chunk_len,
                                                   &telemetry);
        runtime->bms_frame_len = (uint16_t)stream_len;
        return !decoded || bms_apply_telemetry(runtime, &telemetry);
    }
    if (bms_is_daly(runtime)) {
        size_t stream_len = runtime->bms_frame_len;
        const bool decoded = esp_bms_daly_feed(runtime->bms_frame,
                                               &stream_len,
                                               sizeof(runtime->bms_frame),
                                               chunk,
                                               chunk_len,
                                               &telemetry);
        runtime->bms_frame_len = (uint16_t)stream_len;
        return !decoded || bms_apply_telemetry(runtime, &telemetry);
    }
    if (runtime->bms_type != (uint8_t)ESP_BMS_IDF_BMS_TYPE_ANT) {
        return false;
    }
    size_t stream_len = runtime->bms_frame_len;
    bool device_info = false;
    const bool decoded = esp_bms_ant_protocol_feed(runtime->bms_frame, &stream_len,
                                                    sizeof(runtime->bms_frame), chunk, chunk_len,
                                                    &telemetry, &device_info);
    runtime->bms_frame_len = (uint16_t)stream_len;
    if (device_info) {
        RUNTIME_SET_FLAG(runtime, BMS_DEVICE_INFO_KNOWN, true);
    }
    if (decoded) {
        runtime->bms_poll_index = (uint8_t)ANT_BLE_PROTOCOL_NEW;
        return bms_apply_telemetry(runtime, &telemetry);
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
    memset(&runtime->snapshot.bms_ant_diagnostics, 0, sizeof(runtime->snapshot.bms_ant_diagnostics));
    runtime->snapshot.bms_running_time_seconds = 0U;
    runtime->snapshot.bms_running_time_valid = false;
    runtime->snapshot.bms_cycle_capacity_mah = 0U;
    runtime->snapshot.bms_cycle_capacity_valid = false;
    runtime->snapshot.bms_protection_count = 0U;
    runtime->snapshot.bms_warning_count = 0U;
    runtime->snapshot.bms_safety_supported_mask = 0U;
    runtime->snapshot.bms_safety_active_mask = 0U;
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
    bms_advance_session(runtime);
    runtime->bms_establish_deadline_us = 0;
    runtime->bms_ble_phase = (uint8_t)phase;
    runtime->bms_conn_handle = 0xFFFFU;
    runtime->bms_service_start_handle = 0U;
    runtime->bms_service_end_handle = 0U;
    runtime->bms_char_val_handle = 0U;
    runtime->bms_write_char_val_handle = 0U;
    runtime->bms_cccd_handle = 0U;
    runtime->bms_cccd_value = 1U;
    runtime->bms_notify_end_handle = 0U;
    runtime->bms_write_properties = 0U;
    runtime->bms_ant_alt_available = false;
    runtime->bms_frame_len = 0U;
    runtime->bms_poll_index = 0U;
    runtime->bms_status_poll_elapsed_ms = 0U;
    if (bms_is_jk(runtime)) {
        esp_bms_jk_reset();
    }
    RUNTIME_SET_FLAG(runtime, BMS_SCAN_ACTIVE, false);
    RUNTIME_SET_FLAG(runtime, BMS_WRITE_IN_FLIGHT, false);
    RUNTIME_SET_FLAG(runtime, BMS_DEVICE_INFO_REQUESTED, false);
    RUNTIME_SET_FLAG(runtime, BMS_DEVICE_INFO_KNOWN, false);
}

static esp_err_t bms_send_poll_request(esp_bms_idf_runtime_t *runtime,
                                       bool include_device_info);
static esp_err_t bms_start_scan(esp_bms_idf_runtime_t *runtime);

static void bms_fail_connection(esp_bms_idf_runtime_t *runtime, const char *text)
{
    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_BACKOFF;
    runtime->bms_establish_deadline_us = 0;
    runtime->bms_status_poll_elapsed_ms = 0U;
    RUNTIME_SET_FLAG(runtime, BMS_WRITE_IN_FLIGHT, false);
    bms_clear_telemetry(runtime);
    bms_set_info(runtime, text);
    if (runtime->bms_conn_handle == 0xFFFFU ||
        ble_gap_terminate(runtime->bms_conn_handle, BLE_ERR_REM_USER_CONN_TERM) == BLE_HS_ENOTCONN) {
        bms_reset_connection_state(runtime, BMS_BLE_PHASE_BACKOFF);
    }
}

static int bms_write_cb(uint16_t conn_handle,
                        const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr,
                        void *arg)
{
    esp_bms_idf_runtime_t *runtime = bms_callback_runtime(arg);
    if (!runtime || conn_handle != runtime->bms_conn_handle ||
        (runtime->bms_ble_phase != (uint8_t)BMS_BLE_PHASE_SUBSCRIBING &&
         runtime->bms_ble_phase != (uint8_t)BMS_BLE_PHASE_ONLINE)) {
        return 0;
    }
    if (!attr || attr->handle != (runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_SUBSCRIBING
                                      ? runtime->bms_cccd_handle
                                      : runtime->bms_write_char_val_handle)) {
        if (!error || error->status == 0) {
            return 0;
        }
    }
    RUNTIME_SET_FLAG(runtime, BMS_WRITE_IN_FLIGHT, false);
    if (error && error->status != 0) {
        ESP_LOGW(TAG, "GATT write failed: conn=%u status=%u", conn_handle, (unsigned)error->status);
        bms_fail_connection(runtime, "BMS WR");
        return 0;
    }
    if (runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_SUBSCRIBING) {
        runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_ONLINE;
        runtime->bms_establish_deadline_us = 0;
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
    if (!runtime || !frame || frame_len == 0U || frame_len > UINT16_MAX ||
        runtime->bms_ble_phase != (uint8_t)BMS_BLE_PHASE_ONLINE ||
        runtime->bms_conn_handle == 0xFFFFU || runtime->bms_write_char_val_handle == 0U ||
        RUNTIME_FLAG(runtime, BMS_WRITE_IN_FLIGHT)) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((runtime->bms_write_properties & BLE_GATT_CHR_F_WRITE) != 0U) {
        RUNTIME_SET_FLAG(runtime, BMS_WRITE_IN_FLIGHT, true);
        const int rc = ble_gattc_write_flat(runtime->bms_conn_handle,
                                           runtime->bms_write_char_val_handle,
                                           frame, (uint16_t)frame_len,
                                           bms_write_cb, bms_callback_arg(runtime));
        if (rc != 0) {
            RUNTIME_SET_FLAG(runtime, BMS_WRITE_IN_FLIGHT, false);
            return ESP_FAIL;
        }
        return ESP_OK;
    }
    if (!bms_is_em02(runtime) &&
        (runtime->bms_write_properties & BLE_GATT_CHR_F_WRITE_NO_RSP) != 0U) {
        return ble_gattc_write_no_rsp_flat(runtime->bms_conn_handle,
                                           runtime->bms_write_char_val_handle,
                                           frame, (uint16_t)frame_len) == 0 ? ESP_OK : ESP_FAIL;
    }
    return ESP_ERR_INVALID_STATE;
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
    if (bms_is_yanyang(runtime)) {
        uint8_t request[8] = { 0 };
        if (!esp_bms_yanyang_poll_request(runtime->bms_poll_index, request)) {
            return ESP_FAIL;
        }
        const esp_err_t ret = bms_write_frame(runtime, request, sizeof(request));
        if (ret == ESP_OK) {
            runtime->bms_poll_index = (uint8_t)((runtime->bms_poll_index + 1U) % 4U);
            runtime->bms_status_poll_elapsed_ms = 0U;
        }
        return ret;
    }
    if (bms_is_jk(runtime)) {
        uint8_t request[20] = { 0 };
        if (!esp_bms_jk_poll_request(runtime->bms_poll_index, request)) {
            return ESP_FAIL;
        }
        const esp_err_t ret = bms_write_frame(runtime, request, sizeof(request));
        if (ret == ESP_OK) {
            runtime->bms_poll_index = (uint8_t)((runtime->bms_poll_index + 1U) % 2U);
            runtime->bms_status_poll_elapsed_ms = 0U;
        }
        return ret;
    }
    if (bms_is_jbd(runtime)) {
        uint8_t request[7] = { 0 };
        if (!esp_bms_jbd_poll_request(runtime->bms_poll_index, request)) {
            return ESP_FAIL;
        }
        const esp_err_t ret = bms_write_frame(runtime, request, sizeof(request));
        if (ret == ESP_OK) {
            runtime->bms_poll_index = (uint8_t)((runtime->bms_poll_index + 1U) % 2U);
            runtime->bms_status_poll_elapsed_ms = 0U;
        }
        return ret;
    }
    if (bms_is_daly(runtime)) {
        uint8_t request[8] = { 0 };
        if (!esp_bms_daly_poll_request(runtime->bms_poll_index, request)) {
            return ESP_FAIL;
        }
        const esp_err_t ret = bms_write_frame(runtime, request, sizeof(request));
        if (ret == ESP_OK) {
            runtime->bms_poll_index = (uint8_t)((runtime->bms_poll_index + 1U) % 4U);
            runtime->bms_status_poll_elapsed_ms = 0U;
        }
        return ret;
    }
    if (bms_is_bluebabe(runtime)) {
        if (!esp_bms_bluebabe_matching_code_configured()) {
            uint8_t code[ESP_BMS_BLUEBABE_MATCHING_CODE_LEN] = { 0 };
            bool code_ok = strlen(runtime->bluebabe_code) == ESP_BMS_BLUEBABE_MATCHING_CODE_LEN;
            for (size_t index = 0U; code_ok && index < ESP_BMS_BLUEBABE_MATCHING_CODE_LEN; ++index) {
                const char digit = runtime->bluebabe_code[index];
                code_ok = digit >= '0' && digit <= '9';
                code[index] = code_ok ? (uint8_t)(digit - '0') : 0U;
            }
            if (!code_ok || !esp_bms_bluebabe_set_matching_code(code)) {
                return ESP_FAIL;
            }
        }
        uint8_t request[ESP_BMS_BLUEBABE_REQUEST_LEN] = { 0 };
        if (!esp_bms_bluebabe_poll_request(runtime->bms_poll_index % 2U, request)) {
            return ESP_FAIL;
        }
        const esp_err_t ret = bms_write_frame(runtime, request, sizeof(request));
        if (ret == ESP_OK) {
            runtime->bms_poll_index = (uint8_t)((runtime->bms_poll_index + 1U) % 2U);
            runtime->bms_status_poll_elapsed_ms = 0U;
        }
        return ret;
    }
    if (bms_is_em02(runtime)) {
        uint8_t request[ESP_BMS_EM02_REQUEST_LEN] = { 0 };
        if (!esp_bms_em02_poll_request(runtime->bms_poll_index % 4U, request)) {
            return ESP_FAIL;
        }
        const esp_err_t ret = bms_write_frame(runtime, request, sizeof(request));
        if (ret == ESP_OK) {
            runtime->bms_poll_index = (uint8_t)((runtime->bms_poll_index + 1U) % 4U);
            runtime->bms_status_poll_elapsed_ms = 0U;
        }
        return ret;
    }
    if (bms_is_mingtang(runtime)) {
        uint8_t request[24] = { 0 };
        size_t request_len = 0U;
        if (!esp_bms_mingtang_poll_request(runtime->bms_poll_index % 4U, request, &request_len) ||
            request_len == 0U || request_len > sizeof(request)) {
            return ESP_FAIL;
        }
        const esp_err_t ret = bms_write_frame(runtime, request, request_len);
        if (ret == ESP_OK) {
            runtime->bms_poll_index = (uint8_t)((runtime->bms_poll_index + 1U) % 4U);
            runtime->bms_status_poll_elapsed_ms = 0U;
        }
        return ret;
    }
    const bool send_old = bms_ant_uses_old_protocol(runtime);
    uint8_t old_status_request[6] = { 0 };
    if (send_old && !esp_bms_ant_protocol_old_poll_request(old_status_request)) {
        return ESP_FAIL;
    }
    const bool send_device_info = include_device_info &&
                                  runtime->bms_poll_index == (uint8_t)ANT_BLE_PROTOCOL_NEW &&
                                  !RUNTIME_FLAG(runtime, BMS_DEVICE_INFO_REQUESTED);
    const uint8_t *frame = send_old ? old_status_request :
                           (send_device_info ? device_info_request : status_request);
    const size_t frame_len = send_old ? sizeof(old_status_request) :
                             (send_device_info ? sizeof(device_info_request) : sizeof(status_request));
    const esp_err_t ret = bms_write_frame(runtime, frame, frame_len);
    if (ret == ESP_OK) {
        runtime->bms_status_poll_elapsed_ms = 0U;
        if (send_device_info) {
            RUNTIME_SET_FLAG(runtime, BMS_DEVICE_INFO_REQUESTED, true);
        }
        if (runtime->bms_poll_index == (uint8_t)ANT_BLE_PROTOCOL_PROBE_NEW) {
            runtime->bms_poll_index = (uint8_t)ANT_BLE_PROTOCOL_PROBE_OLD;
        } else if (runtime->bms_poll_index == (uint8_t)ANT_BLE_PROTOCOL_PROBE_OLD) {
            runtime->bms_poll_index = (uint8_t)ANT_BLE_PROTOCOL_PROBE_NEW;
        }
    }
    return ret;
}

static esp_err_t bms_subscribe(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || runtime->bms_conn_handle == 0xFFFFU || runtime->bms_cccd_handle == 0U) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint16_t cccd_value = runtime->bms_cccd_value == 2U ? 2U : 1U;
    const uint8_t value[2] = { (uint8_t)cccd_value, (uint8_t)(cccd_value >> 8U) };
    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_SUBSCRIBING;
    RUNTIME_SET_FLAG(runtime, BMS_WRITE_IN_FLIGHT, true);
    const int rc = ble_gattc_write_flat(runtime->bms_conn_handle,
                                        runtime->bms_cccd_handle,
                                        value,
                                        sizeof(value),
                                        bms_write_cb,
                                        bms_callback_arg(runtime));
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
    esp_bms_idf_runtime_t *runtime = bms_callback_runtime(arg);
    if (!runtime || conn_handle != runtime->bms_conn_handle ||
        runtime->bms_ble_phase != (uint8_t)BMS_BLE_PHASE_DISCOVERING_CCCD ||
        chr_val_handle != runtime->bms_char_val_handle) {
        return 0;
    }
    if (error && error->status == 0 && dsc) {
        if (dsc->handle > chr_val_handle && dsc->handle <= runtime->bms_notify_end_handle &&
            ble_uuid_cmp(&dsc->uuid.u, BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16)) == 0) {
            runtime->bms_cccd_handle = dsc->handle;
        }
        return 0;
    }
    if (error && error->status == BLE_HS_EDONE) {
        if (runtime->bms_cccd_handle == 0U || bms_subscribe(runtime) != ESP_OK) {
            bms_fail_connection(runtime, runtime->bms_cccd_handle == 0U ? "BMS NO CCCD" : "BMS SUB");
        }
        return 0;
    }
    bms_fail_connection(runtime, "BMS DSC");
    return 0;
}

static esp_err_t bms_start_descriptor_discovery(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || runtime->bms_conn_handle == 0xFFFFU || runtime->bms_char_val_handle == 0U ||
        runtime->bms_char_val_handle >= runtime->bms_notify_end_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_DISCOVERING_CCCD;
    runtime->bms_cccd_handle = 0U;
    return ble_gattc_disc_all_dscs(runtime->bms_conn_handle,
                                   runtime->bms_char_val_handle,
                                   runtime->bms_notify_end_handle,
                                   bms_dsc_cb,
                                   bms_callback_arg(runtime)) == 0
               ? ESP_OK : ESP_FAIL;
}

static esp_err_t bms_start_characteristic_discovery(esp_bms_idf_runtime_t *runtime);

static void bms_select_characteristic(esp_bms_idf_runtime_t *runtime,
                                       const struct ble_gatt_chr *chr,
                                       bool notify, bool write)
{
    if (notify && runtime->bms_char_val_handle == 0U &&
        (chr->properties & (BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE)) != 0U) {
        runtime->bms_char_val_handle = chr->val_handle;
        runtime->bms_cccd_value = (chr->properties & BLE_GATT_CHR_F_NOTIFY) != 0U ? 1U : 2U;
        runtime->bms_notify_end_handle = runtime->bms_service_end_handle;
    }
    const uint8_t allowed = bms_is_em02(runtime) ? BLE_GATT_CHR_F_WRITE
                               : (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP);
    if (write && runtime->bms_write_char_val_handle == 0U && (chr->properties & allowed) != 0U) {
        runtime->bms_write_char_val_handle = chr->val_handle;
        runtime->bms_write_properties = chr->properties;
    }
}

static int bms_chr_cb(uint16_t conn_handle,
                      const struct ble_gatt_error *error,
                      const struct ble_gatt_chr *chr,
                      void *arg)
{
    esp_bms_idf_runtime_t *runtime = bms_callback_runtime(arg);
    if (!runtime || conn_handle != runtime->bms_conn_handle ||
        runtime->bms_ble_phase != (uint8_t)BMS_BLE_PHASE_DISCOVERING_CHARACTERISTIC) {
        return 0;
    }
    if (error && error->status == 0 && chr) {
        if (runtime->bms_char_val_handle != 0U && chr->def_handle > runtime->bms_char_val_handle &&
            chr->def_handle <= runtime->bms_notify_end_handle) {
            runtime->bms_notify_end_handle = chr->def_handle - 1U;
        }
        if (bms_is_jk(runtime)) {
            if (bms_is_jk_characteristic(chr)) {
                bms_select_characteristic(runtime, chr, true, true);
            }
        } else if (bms_is_yanyang(runtime)) {
            const ble_uuid128_t write_uuid = bms_uuid128(esp_bms_yanyang_write_uuid);
            const ble_uuid128_t notify_uuid = bms_uuid128(esp_bms_yanyang_notify_uuid);
            bms_select_characteristic(runtime, chr,
                ble_uuid_cmp(&chr->uuid.u, &notify_uuid.u) == 0,
                ble_uuid_cmp(&chr->uuid.u, &write_uuid.u) == 0);
        } else if (bms_is_mingtang(runtime)) {
            bms_select_characteristic(runtime, chr, true, true);
        } else {
            uint16_t write_uuid_16 = ANT_BMS_CHARACTERISTIC_UUID_16;
            uint16_t notify_uuid_16 = ANT_BMS_CHARACTERISTIC_UUID_16;
            if (bms_is_bluebabe(runtime) || bms_is_em02(runtime)) {
                write_uuid_16 = 0xFFE2U;
            } else if (bms_is_jbd(runtime)) {
                write_uuid_16 = 0xFF02U;
                notify_uuid_16 = 0xFF01U;
            } else if (bms_is_daly(runtime)) {
                write_uuid_16 = 0xFFF2U;
                notify_uuid_16 = 0xFFF1U;
            } else if (runtime->bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_ANT) {
                if (ble_uuid_cmp(&chr->uuid.u, BLE_UUID16_DECLARE(0xFFF5U)) == 0 &&
                    (chr->properties & (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP)) != 0U) {
                    runtime->bms_ant_alt_available = true;
                }
                if (runtime->bms_ant_channel == 1U) {
                    write_uuid_16 = 0xFFF3U;
                    notify_uuid_16 = 0xFFF4U;
                } else if (runtime->bms_ant_channel == 2U) {
                    write_uuid_16 = 0xFFF5U;
                    notify_uuid_16 = 0xFFF6U;
                }
            }
            bms_select_characteristic(runtime, chr,
                ble_uuid_cmp(&chr->uuid.u, BLE_UUID16_DECLARE(notify_uuid_16)) == 0,
                ble_uuid_cmp(&chr->uuid.u, BLE_UUID16_DECLARE(write_uuid_16)) == 0);
        }
        return 0;
    }
    if (error && error->status == BLE_HS_EDONE) {
        if (runtime->bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_ANT &&
            runtime->bms_ant_channel != 0U && !runtime->bms_ant_alt_available) {
            bms_fail_connection(runtime, "BMS NO ALT");
        } else if (runtime->bms_char_val_handle == 0U || runtime->bms_write_char_val_handle == 0U ||
                   bms_start_descriptor_discovery(runtime) != ESP_OK) {
            bms_fail_connection(runtime, "BMS NO CHR");
        }
        return 0;
    }
    bms_fail_connection(runtime, "BMS CHR");
    return 0;
}

static esp_err_t bms_start_characteristic_discovery(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || runtime->bms_conn_handle == 0xFFFFU || runtime->bms_service_start_handle == 0U ||
        runtime->bms_service_end_handle == 0U) {
        return ESP_ERR_INVALID_STATE;
    }
    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_DISCOVERING_CHARACTERISTIC;
    runtime->bms_char_val_handle = 0U;
    runtime->bms_write_char_val_handle = 0U;
    runtime->bms_notify_end_handle = 0U;
    runtime->bms_write_properties = 0U;
    runtime->bms_ant_alt_available = false;
    return ble_gattc_disc_all_chrs(runtime->bms_conn_handle,
                                   runtime->bms_service_start_handle,
                                   runtime->bms_service_end_handle,
                                   bms_chr_cb,
                                   bms_callback_arg(runtime)) == 0
               ? ESP_OK : ESP_FAIL;
}

static esp_err_t bms_start_service_discovery(esp_bms_idf_runtime_t *runtime);

static int bms_service_cb(uint16_t conn_handle,
                          const struct ble_gatt_error *error,
                          const struct ble_gatt_svc *service,
                          void *arg)
{
    esp_bms_idf_runtime_t *runtime = bms_callback_runtime(arg);
    if (!runtime || conn_handle != runtime->bms_conn_handle ||
        runtime->bms_ble_phase != (uint8_t)BMS_BLE_PHASE_DISCOVERING_SERVICE) {
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
            bms_fail_connection(runtime, "BMS NO SVC");
        }
        return 0;
    }
    bms_fail_connection(runtime, "BMS SVC");
    return 0;
}

static esp_err_t bms_start_service_discovery(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || runtime->bms_conn_handle == 0xFFFFU) {
        return ESP_ERR_INVALID_STATE;
    }
    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_DISCOVERING_SERVICE;
    runtime->bms_service_start_handle = 0U;
    runtime->bms_service_end_handle = 0U;
    if (bms_is_yanyang(runtime)) {
        const ble_uuid128_t service_uuid = bms_uuid128(esp_bms_yanyang_service_uuid);
        return ble_gattc_disc_svc_by_uuid(runtime->bms_conn_handle,
                                          &service_uuid.u,
                                          bms_service_cb,
                                          bms_callback_arg(runtime)) == 0
                   ? ESP_OK
                   : ESP_FAIL;
    }
    ble_uuid16_t service_uuid = BLE_UUID16_INIT(bms_service_uuid_16(runtime));
    return ble_gattc_disc_svc_by_uuid(runtime->bms_conn_handle,
                                      &service_uuid.u,
                                      bms_service_cb,
                                      bms_callback_arg(runtime)) == 0
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
    const size_t limit = out_len - 1U < ESP_BMS_IDF_BMS_SCAN_NAME_LEN
                             ? out_len - 1U
                             : ESP_BMS_IDF_BMS_SCAN_NAME_LEN;
    size_t copied = 0U;
    for (size_t index = 0U; index < name_len;) {
        size_t sequence_len = 1U;
        const uint8_t first = name[index];
        if ((first & 0xE0U) == 0xC0U) {
            sequence_len = 2U;
        } else if ((first & 0xF0U) == 0xE0U) {
            sequence_len = 3U;
        } else if ((first & 0xF8U) == 0xF0U) {
            sequence_len = 4U;
        }
        if (index + sequence_len > name_len || copied + sequence_len > limit) {
            break;
        }
        memcpy(out + copied, name + index, sequence_len);
        copied += sequence_len;
        index += sequence_len;
    }
    out[copied] = '\0';
    return copied > 0U;
}

static int bms_gap_event(struct ble_gap_event *event, void *arg);

static int bms_connection_gap_event(struct ble_gap_event *event, void *arg)
{
    esp_bms_idf_runtime_t *runtime = bms_callback_runtime(arg);
    if (!runtime) {
        if (event && event->type == BLE_GAP_EVENT_CONNECT && event->connect.status == 0 &&
            (!s_bms_runtime || event->connect.conn_handle != s_bms_runtime->bms_conn_handle)) {
            (void)ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;
    }
    return bms_gap_event(event, runtime);
}

static int bms_scan_gap_event(struct ble_gap_event *event, void *arg)
{
    esp_bms_idf_runtime_t *runtime = bms_callback_runtime(arg);
    return runtime ? bms_gap_event(event, runtime) : 0;
}

static esp_err_t bms_connect_to_disc(esp_bms_idf_runtime_t *runtime,
                                     const struct ble_gap_disc_desc *disc,
                                     const char *mac)
{
    if (!runtime || !disc || !mac || !RUNTIME_FLAG(runtime, BMS_BIND_ACTIVE) ||
        runtime->bms_ble_phase != (uint8_t)BMS_BLE_PHASE_SCANNING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ble_gap_disc_active() && ble_gap_disc_cancel() != 0) {
        return ESP_FAIL;
    }
    RUNTIME_SET_FLAG(runtime, BMS_SCAN_ACTIVE, false);
    uint8_t own_addr_type = 0U;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        bms_reset_connection_state(runtime, BMS_BLE_PHASE_BACKOFF);
        return ESP_FAIL;
    }
    bms_reset_connection_state(runtime, BMS_BLE_PHASE_CONNECTING);
    runtime->bms_establish_deadline_us = esp_timer_get_time() +
        (int64_t)BMS_CONNECT_TIMEOUT_MS * 1000;
    if (ble_gap_connect(own_addr_type,
                        &disc->addr,
                        BMS_CONNECT_TIMEOUT_MS,
                        NULL,
                        bms_connection_gap_event,
                        bms_callback_arg(runtime)) != 0) {
        bms_reset_connection_state(runtime, BMS_BLE_PHASE_BACKOFF);
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
    if (!runtime || !event || runtime->bms_ble_phase != (uint8_t)BMS_BLE_PHASE_ONLINE ||
        event->notify_rx.conn_handle != runtime->bms_conn_handle ||
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
    static uint8_t debug_report_count;
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || !event) {
        return 0;
    }
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        if (runtime->bms_ble_phase != (uint8_t)BMS_BLE_PHASE_SCANNING) {
            return 0;
        }
        const struct ble_hs_adv_field *name_field = NULL;
        const struct ble_hs_adv_field *short_name_field = NULL;
        const int complete_name_rc = ble_hs_adv_find_field(BLE_HS_ADV_TYPE_COMP_NAME,
                                                            event->disc.data,
                                                            event->disc.length_data,
                                                            &name_field);
        const int short_name_rc = ble_hs_adv_find_field(BLE_HS_ADV_TYPE_INCOMP_NAME,
                                                         event->disc.data,
                                                         event->disc.length_data,
                                                         &short_name_field);
        if (complete_name_rc != 0) {
            name_field = short_name_field;
        }
        char mac[sizeof(runtime->bms_bound_mac)] = { 0 };
        char name[ESP_BMS_IDF_BMS_SCAN_NAME_LEN + 1U] = { 0 };
        bms_addr_to_mac_text(event->disc.addr.val, mac, sizeof(mac));
        const bool has_name = name_field && name_field->length > 1U &&
                              bms_name_copy(name,
                                            sizeof(name),
                                            name_field->value,
                                            (size_t)name_field->length - 1U);
        const int8_t rssi = event->disc.rssi == 127 ? -128 : event->disc.rssi;
        if (debug_report_count < BMS_SCAN_DEBUG_REPORT_LIMIT) {
            ESP_LOGI(TAG,
                     "[bms-scan-debug] report=%u mac=%s event_type=%u addr_type=%u rssi=%d len=%u name_rc=%d/%d name=%s raw:",
                     (unsigned)debug_report_count + 1U,
                     mac,
                     (unsigned)event->disc.event_type,
                     (unsigned)event->disc.addr.type,
                     (int)rssi,
                     (unsigned)event->disc.length_data,
                     complete_name_rc,
                     short_name_rc,
                     has_name ? name : "-");
            ESP_LOG_BUFFER_HEX_LEVEL(TAG,
                                     event->disc.data,
                                     event->disc.length_data,
                                     ESP_LOG_INFO);
            debug_report_count++;
        }
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
        if (!RUNTIME_FLAG(runtime, BMS_BIND_ACTIVE) ||
            runtime->bms_ble_phase != (uint8_t)BMS_BLE_PHASE_CONNECTING) {
            if (event->connect.status == 0 && event->connect.conn_handle != runtime->bms_conn_handle) {
                (void)ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
            return 0;
        }
        if (event->connect.status == 0) {
            runtime->bms_conn_handle = event->connect.conn_handle;
            runtime->bms_establish_deadline_us = esp_timer_get_time() +
                (int64_t)BMS_ESTABLISH_TIMEOUT_MS * 1000;
            esp_bms_idf_runtime_request_coded_phy(event->connect.conn_handle, "BMS");
            __atomic_fetch_or(&runtime->pending_audio_events,
                              ESP_BMS_IDF_RUNTIME_AUDIO_EVENT_BMS_CONNECTED,
                              __ATOMIC_RELAXED);
            RUNTIME_SET_FLAG(runtime, BMS_SCAN_ACTIVE, false);
            runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_DISCOVERING_SERVICE;
            bms_set_info(runtime, "BMS DISC");
            if (bms_start_service_discovery(runtime) != ESP_OK) {
                bms_fail_connection(runtime, "BMS SVC");
            }
        } else {
            bms_reset_connection_state(runtime, BMS_BLE_PHASE_BACKOFF);
            bms_clear_telemetry(runtime);
            bms_set_info(runtime, "BMS CONN FAIL");
        }
        return 0;
#if CONFIG_BT_NIMBLE_LL_CFG_FEAT_LE_CODED_PHY
    case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
        if (event->phy_updated.conn_handle == runtime->bms_conn_handle &&
            event->phy_updated.status != 0) {
            ESP_LOGW(TAG, "Coded PHY unavailable: conn=%u status=%d",
                     event->phy_updated.conn_handle, event->phy_updated.status);
        }
        return 0;
#endif
    case BLE_GAP_EVENT_DISCONNECT:
        if (event->disconnect.conn.conn_handle == runtime->bms_conn_handle) {
            ESP_LOGW(TAG,
                     "BMS disconnected: conn=%u reason=%d phase=%u",
                     (unsigned)event->disconnect.conn.conn_handle,
                     event->disconnect.reason,
                     (unsigned)runtime->bms_ble_phase);
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
        ESP_LOGI(TAG,
                 "[bms-scan-debug] complete logged=%u limit=%u",
                 (unsigned)debug_report_count,
                 (unsigned)BMS_SCAN_DEBUG_REPORT_LIMIT);
        debug_report_count = 0U;
        if (runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_SCANNING) {
            runtime->bms_ble_phase = RUNTIME_FLAG(runtime, BMS_BIND_ACTIVE)
                                         ? (uint8_t)BMS_BLE_PHASE_BACKOFF
                                         : (uint8_t)BMS_BLE_PHASE_IDLE;
            runtime->bms_status_poll_elapsed_ms = 0U;
            RUNTIME_SET_FLAG(runtime, BMS_SCAN_ACTIVE, false);
            RUNTIME_SET_FLAG(runtime, BMS_SCAN_SNAPSHOT_DIRTY, true);
            bms_set_info(runtime, "BMS DONE");
        }
        if (RUNTIME_FLAG(runtime, CONTROLLER_SCAN_REQUESTED)) {
            const esp_err_t ret = esp_bms_idf_runtime_start_controller_scan(runtime);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "controller scan handoff failed: %s", esp_err_to_name(ret));
            }
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
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    if (runtime->bms_conn_handle != 0xFFFFU ||
        (runtime->bms_ble_phase >= (uint8_t)BMS_BLE_PHASE_CONNECTING &&
         runtime->bms_ble_phase <= (uint8_t)BMS_BLE_PHASE_ONLINE)) {
        RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, true);
        return ESP_ERR_INVALID_STATE;
    }
    /* NimBLE owns one discovery callback: the most recent request wins. */
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_REQUESTED, false);
    if (!RUNTIME_FLAG(runtime, BMS_BLE_READY) || !RUNTIME_FLAG(runtime, BMS_BLE_SYNCED)) {
        RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, true);
        return ESP_ERR_INVALID_STATE;
    }
    if (RUNTIME_FLAG(runtime, BMS_SCAN_ACTIVE) && ble_gap_disc_active()) {
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
    bms_advance_session(runtime);
    if (ble_gap_disc(own_addr_type,
                     BMS_SCAN_DURATION_MS,
                     &params,
                     bms_scan_gap_event,
                     bms_callback_arg(runtime)) != 0) {
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

static bool bms_stop(esp_bms_idf_runtime_t *runtime);

static esp_err_t bms_start_for_bind(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    bms_clear_telemetry(runtime);
    esp_bms_idf_runtime_bms_scan_clear_candidates(runtime);
    if (runtime->bms_conn_handle != 0xFFFFU) {
        RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, true);
        bms_fail_connection(runtime, "BMS WAIT");
        return ESP_OK;
    }
    if (runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_CONNECTING) {
        const bool bound = RUNTIME_FLAG(runtime, BMS_BIND_ACTIVE);
        (void)bms_stop(runtime);
        RUNTIME_SET_FLAG(runtime, BMS_BIND_ACTIVE, bound);
        RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, true);
        return ESP_OK;
    }
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
    const uint16_t old_conn_handle = runtime->bms_conn_handle;
    bool retry_terminate = false;
    RUNTIME_SET_FLAG(runtime, BMS_BIND_ACTIVE, false);
    RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, false);
    RUNTIME_SET_FLAG(runtime, BMS_SCAN_ACTIVE, false);
    if (RUNTIME_FLAG(runtime, BMS_BLE_READY) && RUNTIME_FLAG(runtime, BMS_BLE_SYNCED)) {
        if (ble_gap_disc_active()) {
            (void)ble_gap_disc_cancel();
        }
        if (runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_CONNECTING) {
            (void)ble_gap_conn_cancel();
        } else if (runtime->bms_conn_handle != 0xFFFFU) {
            const int rc = ble_gap_terminate(runtime->bms_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            retry_terminate = rc != 0 && rc != BLE_HS_ENOTCONN;
        }
    }
    bms_reset_connection_state(runtime, retry_terminate ? BMS_BLE_PHASE_BACKOFF : BMS_BLE_PHASE_IDLE);
    if (retry_terminate) {
        runtime->bms_conn_handle = old_conn_handle;
    }
    bms_clear_telemetry(runtime);
    bms_set_info(runtime, "BMS OFF");
    ESP_LOGI(TAG, "BMS connection cancelled");
    return active;
}

static void bms_on_ble_reset(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return;
    }
    const bool bound = RUNTIME_FLAG(runtime, BMS_BIND_ACTIVE);
    bms_reset_connection_state(runtime, bound ? BMS_BLE_PHASE_BACKOFF : BMS_BLE_PHASE_IDLE);
    bms_clear_telemetry(runtime);
    RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, bound);
}

static bool bms_tick(esp_bms_idf_runtime_t *runtime, uint32_t elapsed_ms)
{
    if (!runtime) {
        return false;
    }
    bool changed = false;
    if (RUNTIME_FLAG(runtime, BMS_SCAN_REQUESTED) &&
        RUNTIME_FLAG(runtime, BMS_BLE_READY) && RUNTIME_FLAG(runtime, BMS_BLE_SYNCED) &&
        runtime->bms_conn_handle == 0xFFFFU &&
        (runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_IDLE ||
         runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_BACKOFF) && !ble_gap_disc_active()) {
        (void)bms_start_scan(runtime);
        changed = true;
    }
    if (RUNTIME_FLAG(runtime, BMS_SCAN_SNAPSHOT_DIRTY)) {
        RUNTIME_SET_FLAG(runtime, BMS_SCAN_SNAPSHOT_DIRTY, false);
        changed = esp_bms_idf_runtime_bms_scan_project_snapshot(runtime) || changed;
    }
    if (runtime->bms_ble_phase >= (uint8_t)BMS_BLE_PHASE_CONNECTING &&
        runtime->bms_ble_phase <= (uint8_t)BMS_BLE_PHASE_SUBSCRIBING &&
        runtime->bms_establish_deadline_us > 0 &&
        esp_timer_get_time() >= runtime->bms_establish_deadline_us) {
        ESP_LOGW(TAG, "BMS establishment timeout: phase=%u", (unsigned)runtime->bms_ble_phase);
        if (runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_CONNECTING) {
            (void)ble_gap_conn_cancel();
        }
        bms_fail_connection(runtime, "BMS GATT TIMEOUT");
        return true;
    }
    if (runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_ONLINE) {
        const int64_t heartbeat_age_us = esp_timer_get_time() - runtime->bms_telemetry_last_us;
        if (runtime->bms_telemetry_last_us > 0 && heartbeat_age_us >= 0 &&
            heartbeat_age_us >= (int64_t)BMS_HEARTBEAT_TIMEOUT_MS * 1000) {
            ESP_LOGW(TAG, "BMS heartbeat timeout: age_ms=%lld", (long long)(heartbeat_age_us / 1000));
            bms_fail_connection(runtime, "BMS TIMEOUT");
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
    } else if (RUNTIME_FLAG(runtime, BMS_BLE_READY) && RUNTIME_FLAG(runtime, BMS_BLE_SYNCED) &&
               (runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_IDLE ||
                runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_BACKOFF)) {
        runtime->bms_status_poll_elapsed_ms += elapsed_ms;
        if (runtime->bms_status_poll_elapsed_ms >= BMS_RECONNECT_BACKOFF_MS) {
            runtime->bms_status_poll_elapsed_ms = 0U;
            if (runtime->bms_conn_handle != 0xFFFFU) {
                /* Wait for disconnect before reusing the link; retry a failed terminate. */
                if (ble_gap_terminate(runtime->bms_conn_handle, BLE_ERR_REM_USER_CONN_TERM) == BLE_HS_ENOTCONN) {
                    bms_reset_connection_state(runtime, BMS_BLE_PHASE_BACKOFF);
                }
            } else if (RUNTIME_FLAG(runtime, BMS_BIND_ACTIVE)) {
                (void)bms_start_scan(runtime);
            }
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
    if (s_bms_runtime && s_bms_runtime != runtime) {
        return ESP_ERR_INVALID_STATE;
    }
    s_bms_runtime = runtime;
    bms_advance_session(runtime);
    esp_bms_idf_runtime_register_bms_frame_handler(runtime, bms_frame_push);
    esp_bms_idf_runtime_register_bms_ble_driver(runtime, &s_bms_ble_driver);
    return ESP_OK;
}

esp_err_t esp_bms_bms_ble_start(esp_bms_idf_runtime_t *runtime)
{
    return bms_start_if_bound(runtime);
}
