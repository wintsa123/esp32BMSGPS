#include "esp_bms_controller_ble.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"

#include "esp_bms_idf_runtime.h"

static const char *TAG = "esp_bms_controller_ble";

#define CONTROLLER_SERVICE_UUID_16 0xFFE0U
#define CONTROLLER_CHARACTERISTIC_UUID_16 0xFFECU
#define CONTROLLER_SCAN_DURATION_MS 10000U
#define CONTROLLER_CONNECT_TIMEOUT_MS 10000U
#define CONTROLLER_KEEPALIVE_PERIOD_MS 2000U
#define CONTROLLER_TIRE_RIM_MIN ESP_BMS_CONTROLLER_TIRE_RIM_MIN
#define CONTROLLER_TIRE_RIM_MAX ESP_BMS_CONTROLLER_TIRE_RIM_MAX
#define CONTROLLER_TIRE_ASPECT_MIN ESP_BMS_CONTROLLER_TIRE_ASPECT_MIN
#define CONTROLLER_TIRE_ASPECT_MAX ESP_BMS_CONTROLLER_TIRE_ASPECT_MAX
#define CONTROLLER_TIRE_ASPECT_STEP ESP_BMS_CONTROLLER_TIRE_ASPECT_STEP
#define CONTROLLER_TIRE_WIDTH_MIN ESP_BMS_CONTROLLER_TIRE_WIDTH_MIN
#define CONTROLLER_TIRE_WIDTH_MAX ESP_BMS_CONTROLLER_TIRE_WIDTH_MAX
#define CONTROLLER_TIRE_WIDTH_STEP ESP_BMS_CONTROLLER_TIRE_WIDTH_STEP
#define CONTROLLER_RATIO_CENTI_MIN ESP_BMS_CONTROLLER_RATIO_CENTI_MIN
#define CONTROLLER_RATIO_CENTI_MAX ESP_BMS_CONTROLLER_RATIO_CENTI_MAX

#define RUNTIME_FLAG(runtime, name) \
    esp_bms_idf_runtime_flag_get((runtime), ESP_BMS_IDF_RUNTIME_FLAG_##name)
#define RUNTIME_SET_FLAG(runtime, name, enabled) \
    esp_bms_idf_runtime_flag_set((runtime), ESP_BMS_IDF_RUNTIME_FLAG_##name, (enabled))

typedef enum {
    CONTROLLER_BLE_PHASE_IDLE = 0,
    CONTROLLER_BLE_PHASE_SCANNING = 1,
    CONTROLLER_BLE_PHASE_CONNECTING = 2,
    CONTROLLER_BLE_PHASE_DISCOVERING_SERVICE = 3,
    CONTROLLER_BLE_PHASE_DISCOVERING_CHARACTERISTIC = 4,
    CONTROLLER_BLE_PHASE_DISCOVERING_CCCD = 5,
    CONTROLLER_BLE_PHASE_ONLINE = 7,
    CONTROLLER_BLE_PHASE_BACKOFF = 8,
} controller_ble_phase_t;

typedef struct {
    char mac[18];
    char name[ESP_BMS_IDF_BMS_SCAN_NAME_LEN + 1U];
} controller_scan_name_cache_entry_t;

static controller_scan_name_cache_entry_t
    s_controller_scan_name_cache[ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES];
static uint8_t s_controller_scan_name_cache_count;
static uint8_t s_controller_scan_name_cache_next;

static void controller_copy_text(char *out, size_t out_len, const char *text)
{
    if (!out || out_len == 0U) {
        return;
    }
    if (!text) {
        out[0] = '\0';
        return;
    }
    strncpy(out, text, out_len - 1U);
    out[out_len - 1U] = '\0';
}

static bool controller_tire_matches_policy(uint8_t rim_inch,
                                            uint8_t aspect_percent,
                                            uint16_t width_mm)
{
    return rim_inch >= CONTROLLER_TIRE_RIM_MIN && rim_inch <= CONTROLLER_TIRE_RIM_MAX &&
           aspect_percent >= CONTROLLER_TIRE_ASPECT_MIN &&
           aspect_percent <= CONTROLLER_TIRE_ASPECT_MAX &&
           (aspect_percent - CONTROLLER_TIRE_ASPECT_MIN) % CONTROLLER_TIRE_ASPECT_STEP == 0U &&
           width_mm >= CONTROLLER_TIRE_WIDTH_MIN && width_mm <= CONTROLLER_TIRE_WIDTH_MAX &&
           (width_mm - CONTROLLER_TIRE_WIDTH_MIN) % CONTROLLER_TIRE_WIDTH_STEP == 0U;
}

static bool controller_ratio_matches_policy(uint16_t ratio_centi)
{
    return ratio_centi >= CONTROLLER_RATIO_CENTI_MIN &&
           ratio_centi <= CONTROLLER_RATIO_CENTI_MAX;
}

static void controller_sync_parameters(esp_bms_idf_runtime_t *runtime)
{
    const esp_fardriver_state_t *state = &runtime->controller_state;
    if (!state->controller_speed_params_valid) {
        return;
    }
    if (runtime->controller_observed_tire_rim_inch == state->tire_rim_inch &&
        runtime->controller_observed_tire_aspect_percent == state->tire_aspect_percent &&
        runtime->controller_observed_tire_width_mm == state->tire_width_mm &&
        runtime->controller_observed_gear_ratio_centi == state->gear_ratio_centi) {
        return;
    }
    runtime->controller_observed_tire_rim_inch = state->tire_rim_inch;
    runtime->controller_observed_tire_aspect_percent = state->tire_aspect_percent;
    runtime->controller_observed_tire_width_mm = state->tire_width_mm;
    runtime->controller_observed_gear_ratio_centi = state->gear_ratio_centi;

    if (!controller_tire_matches_policy(state->tire_rim_inch,
                                        state->tire_aspect_percent,
                                        state->tire_width_mm) ||
        !controller_ratio_matches_policy(state->gear_ratio_centi)) {
        ESP_LOGW(TAG,
                 "parameters not synchronized: tire=%u-%u-%u ratio=%u.%02u",
                 state->tire_rim_inch,
                 state->tire_aspect_percent,
                 state->tire_width_mm,
                 state->gear_ratio_centi / 100U,
                 state->gear_ratio_centi % 100U);
        return;
    }

    if (runtime->controller_fallback_tire_rim_inch == state->tire_rim_inch &&
        runtime->controller_fallback_tire_aspect_percent == state->tire_aspect_percent &&
        runtime->controller_fallback_tire_width_mm == state->tire_width_mm &&
        runtime->controller_state.fallback_gear_ratio_centi == state->gear_ratio_centi) {
        return;
    }
    runtime->controller_fallback_tire_rim_inch = state->tire_rim_inch;
    runtime->controller_fallback_tire_aspect_percent = state->tire_aspect_percent;
    runtime->controller_fallback_tire_width_mm = state->tire_width_mm;
    runtime->controller_state.fallback_wheel_circumference_mm =
        state->wheel_circumference_mm;
    runtime->controller_state.fallback_gear_ratio_centi = state->gear_ratio_centi;
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SETTINGS_SAVE_REQUESTED, true);
    ESP_LOGI(TAG,
             "parameters synchronized: tire=%u-%u-%u ratio=%u.%02u",
             state->tire_rim_inch,
             state->tire_aspect_percent,
             state->tire_width_mm,
             state->gear_ratio_centi / 100U,
             state->gear_ratio_centi % 100U);
}

static void controller_clear_telemetry(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return;
    }
    const uint16_t wheel = runtime->controller_state.fallback_wheel_circumference_mm;
    const uint16_t ratio = runtime->controller_state.fallback_gear_ratio_centi;
    memset(&runtime->controller_state, 0, sizeof(runtime->controller_state));
    runtime->controller_state.fallback_wheel_circumference_mm = wheel;
    runtime->controller_state.fallback_gear_ratio_centi = ratio;
    esp_bms_idf_runtime_project_controller_snapshot(runtime);
}

static char controller_hex_char(uint8_t value)
{
    return value < 10U ? (char)('0' + value) : (char)('A' + value - 10U);
}

static void controller_addr_to_mac_text(const uint8_t addr[6], char *out, size_t out_len)
{
    if (!addr || !out || out_len < 18U) {
        return;
    }
    (void)snprintf(out,
                   out_len,
                   "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
                   controller_hex_char(addr[5] >> 4U), controller_hex_char(addr[5] & 0x0FU),
                   controller_hex_char(addr[4] >> 4U), controller_hex_char(addr[4] & 0x0FU),
                   controller_hex_char(addr[3] >> 4U), controller_hex_char(addr[3] & 0x0FU),
                   controller_hex_char(addr[2] >> 4U), controller_hex_char(addr[2] & 0x0FU),
                   controller_hex_char(addr[1] >> 4U), controller_hex_char(addr[1] & 0x0FU),
                   controller_hex_char(addr[0] >> 4U), controller_hex_char(addr[0] & 0x0FU));
}

static bool controller_name_copy(char *out, size_t out_len, const uint8_t *name, size_t name_len)
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

static const char *controller_cached_name_locked(const char *mac)
{
    if (!mac || mac[0] == '\0') {
        return NULL;
    }
    for (uint8_t index = 0U; index < s_controller_scan_name_cache_count; ++index) {
        if (strcmp(s_controller_scan_name_cache[index].mac, mac) == 0) {
            return s_controller_scan_name_cache[index].name;
        }
    }
    return NULL;
}

static void controller_cache_name_locked(const char *mac, const char *name)
{
    if (!mac || mac[0] == '\0' || !name || name[0] == '\0') {
        return;
    }
    for (uint8_t index = 0U; index < s_controller_scan_name_cache_count; ++index) {
        if (strcmp(s_controller_scan_name_cache[index].mac, mac) == 0) {
            controller_copy_text(s_controller_scan_name_cache[index].name,
                                 sizeof(s_controller_scan_name_cache[index].name),
                                 name);
            return;
        }
    }
    uint8_t slot = s_controller_scan_name_cache_count;
    if (slot < ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES) {
        s_controller_scan_name_cache_count++;
    } else {
        slot = s_controller_scan_name_cache_next;
        s_controller_scan_name_cache_next =
            (uint8_t)((s_controller_scan_name_cache_next + 1U) % ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES);
    }
    controller_copy_text(s_controller_scan_name_cache[slot].mac,
                         sizeof(s_controller_scan_name_cache[slot].mac),
                         mac);
    controller_copy_text(s_controller_scan_name_cache[slot].name,
                         sizeof(s_controller_scan_name_cache[slot].name),
                         name);
}

static int controller_gap_event(struct ble_gap_event *event, void *arg);

static int controller_write_cb(uint16_t conn_handle,
                               const struct ble_gatt_error *error,
                               struct ble_gatt_attr *attr,
                               void *arg)
{
    (void)attr;
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || conn_handle != runtime->controller_conn_handle) {
        return 0;
    }
    if (error && error->status != 0) {
        ESP_LOGW(TAG, "GATT write failed: status=%u", (unsigned)error->status);
    }
    return 0;
}

static void controller_send_gather(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || runtime->controller_conn_handle == 0xFFFFU ||
        runtime->controller_char_val_handle == 0U) {
        return;
    }
    uint8_t frame[8] = { 0xAA, 0x46, 0xA0, 0xA0, 0x06, 0x88 };
    const uint16_t crc = esp_fardriver_crc(frame, sizeof(frame) - 2U);
    frame[6] = (uint8_t)(crc >> 8U);
    frame[7] = (uint8_t)crc;
    (void)ble_gattc_write_flat(runtime->controller_conn_handle,
                               runtime->controller_char_val_handle,
                               frame,
                               sizeof(frame),
                               controller_write_cb,
                               runtime);
    runtime->controller_keepalive_elapsed_ms = 0U;
}

static void controller_set_subscription(esp_bms_idf_runtime_t *runtime, bool enabled)
{
    if (!runtime || runtime->controller_conn_handle == 0xFFFFU ||
        runtime->controller_cccd_handle == 0U ||
        RUNTIME_FLAG(runtime, CONTROLLER_SUBSCRIBED) == enabled) {
        return;
    }
    const uint8_t value[2] = { enabled ? 1U : 0U, 0U };
    if (ble_gattc_write_flat(runtime->controller_conn_handle,
                             runtime->controller_cccd_handle,
                             value,
                             sizeof(value),
                             controller_write_cb,
                             runtime) == 0) {
        RUNTIME_SET_FLAG(runtime, CONTROLLER_SUBSCRIBED, enabled);
        if (enabled) {
            controller_send_gather(runtime);
        }
    }
}

static int controller_dsc_cb(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             uint16_t chr_val_handle,
                             const struct ble_gatt_dsc *dsc,
                             void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || conn_handle != runtime->controller_conn_handle ||
        chr_val_handle != runtime->controller_char_val_handle) {
        return 0;
    }
    if (error && error->status == 0 && dsc) {
        if (ble_uuid_cmp(&dsc->uuid.u, BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16)) == 0) {
            runtime->controller_cccd_handle = dsc->handle;
        }
        return 0;
    }
    if (error && error->status == BLE_HS_EDONE && runtime->controller_cccd_handle != 0U) {
        runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_ONLINE;
        esp_bms_idf_runtime_project_controller_snapshot(runtime);
        controller_set_subscription(runtime, true);
        return 0;
    }
    (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

static int controller_chr_cb(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             const struct ble_gatt_chr *chr,
                             void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || conn_handle != runtime->controller_conn_handle) {
        return 0;
    }
    if (error && error->status == 0 && chr) {
        runtime->controller_char_val_handle = chr->val_handle;
        return 0;
    }
    if (error && error->status == BLE_HS_EDONE && runtime->controller_char_val_handle != 0U) {
        runtime->controller_cccd_handle = 0U;
        runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_DISCOVERING_CCCD;
        if (ble_gattc_disc_all_dscs(conn_handle,
                                    runtime->controller_char_val_handle,
                                    runtime->controller_service_end_handle,
                                    controller_dsc_cb,
                                    runtime) == 0) {
            return 0;
        }
    }
    (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

static int controller_service_cb(uint16_t conn_handle,
                                 const struct ble_gatt_error *error,
                                 const struct ble_gatt_svc *service,
                                 void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || conn_handle != runtime->controller_conn_handle) {
        return 0;
    }
    if (error && error->status == 0 && service) {
        runtime->controller_service_start_handle = service->start_handle;
        runtime->controller_service_end_handle = service->end_handle;
        return 0;
    }
    if (error && error->status == BLE_HS_EDONE && runtime->controller_service_start_handle != 0U) {
        ble_uuid16_t uuid = BLE_UUID16_INIT(CONTROLLER_CHARACTERISTIC_UUID_16);
        runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_DISCOVERING_CHARACTERISTIC;
        if (ble_gattc_disc_chrs_by_uuid(conn_handle,
                                        runtime->controller_service_start_handle,
                                        runtime->controller_service_end_handle,
                                        &uuid.u,
                                        controller_chr_cb,
                                        runtime) == 0) {
            return 0;
        }
    }
    (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

static esp_err_t controller_connect(esp_bms_idf_runtime_t *runtime,
                                    const struct ble_gap_disc_desc *disc)
{
    if (!runtime || !disc || runtime->controller_conn_handle != 0xFFFFU) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ble_gap_disc_active()) {
        (void)ble_gap_disc_cancel();
    }
    uint8_t own_addr_type = 0U;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0 ||
        ble_gap_connect(own_addr_type,
                        &disc->addr,
                        CONTROLLER_CONNECT_TIMEOUT_MS,
                        NULL,
                        controller_gap_event,
                        runtime) != 0) {
        return ESP_FAIL;
    }
    runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_CONNECTING;
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
    return ESP_OK;
}

static esp_err_t controller_start_scan(esp_bms_idf_runtime_t *runtime);

static int controller_gap_event(struct ble_gap_event *event, void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || !event) {
        return 0;
    }
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            if (!runtime->controller_connection_enabled) {
                (void)ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                return 0;
            }
            runtime->controller_conn_handle = event->connect.conn_handle;
            __atomic_fetch_or(&runtime->pending_audio_events,
                              ESP_BMS_IDF_RUNTIME_AUDIO_EVENT_CONTROLLER_CONNECTED,
                              __ATOMIC_RELAXED);
            runtime->controller_service_start_handle = 0U;
            runtime->controller_service_end_handle = 0U;
            runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_DISCOVERING_SERVICE;
            ble_uuid16_t uuid = BLE_UUID16_INIT(CONTROLLER_SERVICE_UUID_16);
            if (ble_gattc_disc_svc_by_uuid(event->connect.conn_handle,
                                           &uuid.u,
                                           controller_service_cb,
                                           runtime) != 0) {
                (void)ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
        } else {
            runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_BACKOFF;
            controller_clear_telemetry(runtime);
        }
        esp_bms_idf_runtime_project_controller_snapshot(runtime);
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        if (event->disconnect.conn.conn_handle == runtime->controller_conn_handle) {
            runtime->controller_conn_handle = 0xFFFFU;
            runtime->controller_cccd_handle = 0U;
            runtime->controller_char_val_handle = 0U;
            runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_BACKOFF;
            RUNTIME_SET_FLAG(runtime, CONTROLLER_SUBSCRIBED, false);
            controller_clear_telemetry(runtime);
            if (runtime->controller_connection_enabled &&
                RUNTIME_FLAG(runtime, CONTROLLER_SCAN_REQUESTED)) {
                const esp_err_t ret = controller_start_scan(runtime);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "deferred rebind scan failed: %s", esp_err_to_name(ret));
                }
            }
        }
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX:
        if (event->notify_rx.conn_handle == runtime->controller_conn_handle &&
            event->notify_rx.attr_handle == runtime->controller_char_val_handle) {
            uint8_t frame[ESP_FARDRIVER_FRAME_LEN];
            const int len = OS_MBUF_PKTLEN(event->notify_rx.om);
            if (len == (int)sizeof(frame) &&
                os_mbuf_copydata(event->notify_rx.om, 0, len, frame) == 0) {
                const esp_fardriver_layout_t layout = (frame[1] & 0x3FU) <= 29U
                                                           ? ESP_FARDRIVER_LAYOUT_COMPACT
                                                           : ESP_FARDRIVER_LAYOUT_EXTENDED;
                if (esp_fardriver_parse_frame(&runtime->controller_state,
                                              frame,
                                              sizeof(frame),
                                              layout)) {
                    controller_sync_parameters(runtime);
                    esp_bms_idf_runtime_project_controller_snapshot(runtime);
                }
            }
        }
        return 0;
    default:
        return 0;
    }
}

static void controller_store_candidate(esp_bms_idf_runtime_t *runtime,
                                       const char *mac,
                                       const char *name,
                                       int8_t rssi)
{
    if (!runtime || !mac || mac[0] == '\0') {
        return;
    }
    if (runtime->bms_scan_lock &&
        xSemaphoreTake(runtime->bms_scan_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    if (name && name[0] != '\0') {
        controller_cache_name_locked(mac, name);
    } else {
        name = controller_cached_name_locked(mac);
    }
    bool changed = false;
    if (name && name[0] != '\0' && strcmp(mac, runtime->controller_bound_mac) == 0 &&
        strcmp(name, runtime->controller_bound_name) != 0) {
        controller_copy_text(runtime->controller_bound_name,
                             sizeof(runtime->controller_bound_name),
                             name);
        changed = true;
    }
    for (uint8_t index = 0U; index < runtime->controller_scan_candidate_count; ++index) {
        if (strcmp(runtime->controller_scan_candidates[index].mac, mac) == 0) {
            runtime->controller_scan_candidates[index].rssi = rssi;
            if (name && name[0] != '\0' &&
                (!runtime->controller_scan_candidates[index].has_name ||
                 strcmp(runtime->controller_scan_candidates[index].name, name) != 0)) {
                controller_copy_text(runtime->controller_scan_candidates[index].name,
                                     sizeof(runtime->controller_scan_candidates[index].name),
                                     name);
                runtime->controller_scan_candidates[index].has_name = true;
                changed = true;
            }
            if (runtime->bms_scan_lock) {
                xSemaphoreGive(runtime->bms_scan_lock);
            }
            if (changed) {
                esp_bms_idf_runtime_project_controller_snapshot(runtime);
            }
            return;
        }
    }
    if (runtime->controller_scan_candidate_count >= ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES) {
        if (runtime->bms_scan_lock) {
            xSemaphoreGive(runtime->bms_scan_lock);
        }
        return;
    }
    esp_bms_idf_bms_scan_candidate_t *candidate =
        &runtime->controller_scan_candidates[runtime->controller_scan_candidate_count++];
    controller_copy_text(candidate->mac, sizeof(candidate->mac), mac);
    controller_copy_text(candidate->name, sizeof(candidate->name), name);
    candidate->has_name = name && name[0] != '\0';
    candidate->rssi = rssi;
    if (runtime->bms_scan_lock) {
        xSemaphoreGive(runtime->bms_scan_lock);
    }
    esp_bms_idf_runtime_project_controller_snapshot(runtime);
}

static int controller_scan_gap_event(struct ble_gap_event *event, void *arg)
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
        char mac[sizeof(runtime->controller_bound_mac)] = { 0 };
        char name[ESP_BMS_IDF_BMS_SCAN_NAME_LEN + 1U] = { 0 };
        controller_addr_to_mac_text(event->disc.addr.val, mac, sizeof(mac));
        const bool has_name = controller_name_copy(name, sizeof(name), fields.name, fields.name_len);
        const int8_t rssi = event->disc.rssi == 127 ? INT8_MIN : event->disc.rssi;
        if (RUNTIME_FLAG(runtime, CONTROLLER_SCAN_ACTIVE)) {
            controller_store_candidate(runtime, mac, has_name ? name : NULL, rssi);
            if (runtime->controller_connection_enabled &&
                runtime->controller_bound_mac[0] != '\0' &&
                strcmp(mac, runtime->controller_bound_mac) == 0) {
                (void)controller_connect(runtime, &event->disc);
            }
        }
        return 0;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
        esp_bms_idf_runtime_project_controller_snapshot(runtime);
        return 0;
    default:
        return 0;
    }
}

static esp_err_t controller_start_scan(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(esp_bms_idf_runtime_ensure_ble_host(runtime), TAG, "NimBLE init failed");
    if (!RUNTIME_FLAG(runtime, BLE_HOST_SYNCED)) {
        RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_REQUESTED, true);
        return ESP_OK;
    }
    if (RUNTIME_FLAG(runtime, CONTROLLER_SCAN_ACTIVE)) {
        esp_bms_idf_runtime_project_controller_snapshot(runtime);
        return ESP_OK;
    }
    if (ble_gap_disc_active()) {
        /* NimBLE has one global discovery callback; hand ownership to controller. */
        RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, false);
        RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_REQUESTED, true);
        RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
        (void)ble_gap_disc_cancel();
        ESP_LOGI(TAG, "BLE scan handoff requested: BMS -> controller");
        esp_bms_idf_runtime_project_controller_snapshot(runtime);
        return ESP_OK;
    }
    runtime->controller_scan_candidate_count = 0U;
    memset(runtime->controller_scan_candidates, 0, sizeof(runtime->controller_scan_candidates));
    runtime->controller_scan_revision++;
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_REQUESTED, false);
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, true);
    uint8_t own_addr_type = 0U;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
        esp_bms_idf_runtime_project_controller_snapshot(runtime);
        return ESP_FAIL;
    }
    const struct ble_gap_disc_params params = {
        .filter_duplicates = 0,
        .passive = 0,
        .filter_policy = 0,
        .limited = 0,
    };
    if (ble_gap_disc(own_addr_type,
                     CONTROLLER_SCAN_DURATION_MS,
                     &params,
                     controller_scan_gap_event,
                     runtime) != 0) {
        RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
        esp_bms_idf_runtime_project_controller_snapshot(runtime);
        return ESP_FAIL;
    }
    esp_bms_idf_runtime_project_controller_snapshot(runtime);
    return ESP_OK;
}

static esp_err_t controller_start_if_enabled(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!runtime->controller_connection_enabled ||
        runtime->controller_bound_mac[0] == '\0' ||
        runtime->controller_conn_handle != 0xFFFFU) {
        esp_bms_idf_runtime_project_controller_snapshot(runtime);
        return ESP_OK;
    }
    return controller_start_scan(runtime);
}

static void controller_stop(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return;
    }
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_REQUESTED, false);
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
    if (runtime->controller_conn_handle != 0xFFFFU) {
        (void)ble_gap_terminate(runtime->controller_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    esp_bms_idf_runtime_project_controller_snapshot(runtime);
}

static void controller_on_ble_reset(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return;
    }
    runtime->controller_conn_handle = 0xFFFFU;
    runtime->controller_service_start_handle = 0U;
    runtime->controller_service_end_handle = 0U;
    runtime->controller_char_val_handle = 0U;
    runtime->controller_cccd_handle = 0U;
    runtime->controller_ble_phase = (uint8_t)CONTROLLER_BLE_PHASE_BACKOFF;
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SUBSCRIBED, false);
    RUNTIME_SET_FLAG(runtime,
                     CONTROLLER_SCAN_REQUESTED,
                     runtime->controller_connection_enabled && runtime->controller_bound_mac[0] != '\0');
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
    controller_clear_telemetry(runtime);
}

static bool controller_tick(esp_bms_idf_runtime_t *runtime, uint32_t elapsed_ms)
{
    if (!runtime) {
        return false;
    }
    bool changed = false;
    if (RUNTIME_FLAG(runtime, CONTROLLER_SCAN_REQUESTED) && !ble_gap_disc_active()) {
        (void)controller_start_scan(runtime);
        changed = true;
    }
    if ((runtime->active_data_source == ESP_BMS_LVGL_DATA_SOURCE_CONTROLLER ||
         runtime->active_data_source == ESP_BMS_LVGL_DATA_SOURCE_SPEED_DASHBOARD) &&
        RUNTIME_FLAG(runtime, CONTROLLER_SUBSCRIBED)) {
        runtime->controller_keepalive_elapsed_ms += elapsed_ms;
        if (runtime->controller_keepalive_elapsed_ms >= CONTROLLER_KEEPALIVE_PERIOD_MS) {
            controller_send_gather(runtime);
        }
    }
    return changed;
}

static const esp_bms_idf_runtime_controller_ble_driver_t s_controller_ble_driver = {
    .start_if_enabled = controller_start_if_enabled,
    .start_scan = controller_start_scan,
    .stop = controller_stop,
    .tick = controller_tick,
    .on_ble_reset = controller_on_ble_reset,
};

esp_err_t esp_bms_controller_ble_init(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_bms_idf_runtime_register_controller_ble_driver(runtime, &s_controller_ble_driver);
    return ESP_OK;
}

esp_err_t esp_bms_controller_ble_start(esp_bms_idf_runtime_t *runtime)
{
    return controller_start_if_enabled(runtime);
}
