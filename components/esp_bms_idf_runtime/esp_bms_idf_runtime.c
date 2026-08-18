#include "esp_bms_idf_runtime.h"
#include "esp_bms_flashdb.h"
#include "esp_bms_ble_media_hid.h"
#include "esp_bms_cast_protocol.h"
#if ESP_BMS_FEATURE_CLASSIC_MEDIA_HID
#include "esp_bms_classic_media_hid.h"
#endif

#include "esp_bms_display_service.h"
#include "esp_bms_profile_hardware.h"
#if ESP_BMS_FEATURE_OTA
#include "esp_bms_ota.h"
#endif

#include "esp_err.h"
#include "esp_check.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#if ESP_BMS_FEATURE_BLE
#include "esp_bt.h"
#include "host/ble_gap.h"
#include "host/ble_att.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#endif
#include "nvs.h"
#include "nvs_flash.h"
#if ESP_BMS_FEATURE_BLE
#include "os/os_mbuf.h"
#endif
#include "sdkconfig.h"
#if ESP_BMS_FEATURE_BLE
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "bms_idf_runtime";

#define BATTERY_GPIO ESP_BMS_PROFILE_BATTERY_ADC
#define BATTERY_SAMPLE_PERIOD_MS 2000U
#define BATTERY_ADC_MAX 4095U
#define BATTERY_REFERENCE_MV 3300U
#define BATTERY_DIVIDER_TOP_OHMS 100000U
#define BATTERY_DIVIDER_BOTTOM_OHMS 100000U
#define BMS_TELEMETRY_FRESHNESS_US INT64_C(2000000)
#define SETUP_AP_SSID_PREFIX "fuckingBms_"
#define SETUP_AP_SSID_SUFFIX_LEN 6U
#define SETUP_AP_PASSWORD_LEN 8U
#define SETUP_AP_NVS_NAMESPACE "esp_bms"
#define SETUP_AP_NVS_SSID_KEY "setup_ssid"
#define SETUP_AP_NVS_PASSWORD_KEY "setup_pw"
#define ANT_BMS_SERVICE_UUID_16 0xFFE0U
#define ANT_BMS_CHARACTERISTIC_UUID_16 0xFFE1U
#define BMS_SCAN_DURATION_MS 10000
#define LOCAL_BLUETOOTH_NAME "ESP32 BMS GPS"
#define LOCAL_BLUETOOTH_ADV_INTERVAL_MS 500U
#define LOCAL_BLUETOOTH_PAIR_TIMEOUT_MS 10000U
#define LOCAL_BLUETOOTH_PAIR_INITIATE_DELAY_MS 1000U
#define BMS_CONNECT_TIMEOUT_MS 10000
#define BMS_STATUS_POLL_PERIOD_MS 500U
#define BMS_HEARTBEAT_TIMEOUT_MS 5000U
#define BMS_RECONNECT_BACKOFF_MS 3000U
#define BMS_NVS_BOUND_MAC_KEY "bms_mac"
#define BMS_NVS_BOUND_NAME_KEY "bms_name"
#define RIDE_RECORDS_NVS_KEY "ride_records"
#define RIDE_RECORDS_PERSIST_RETRY_US INT64_C(5000000)
#define GPS_TRACK_NVS_KEY "gps_track"
#define GPS_TRACK_MIGRATION_NVS_KEY "gps_mig"
#define GPS_TRACK_MIGRATION_SESSION_NVS_KEY "gps_mig_sid"
#define GPS_TRACK_SAMPLE_INTERVAL_US INT64_C(5000000)
#define CAPACITY_ESTIMATE_NVS_KEY "bms_cap_est"
#define CAPACITY_ESTIMATE_MAGIC UINT32_C(0x43415031)
#define CAPACITY_ESTIMATE_VERSION 2U
#define CAPACITY_ESTIMATE_BLOB_MAX_BYTES 80U
#define CAPACITY_ESTIMATE_PERSIST_RETRY_US INT64_C(5000000)
#define DISPLAY_NVS_BRIGHTNESS_KEY "disp_bright"
#define DISPLAY_NVS_VOLUME_KEY "disp_vol"
#define DISPLAY_NVS_ROTATION_KEY "disp_rot"
#define DISPLAY_NVS_ROTATION_DEFAULT_VERSION_KEY "disp_rot_ver"
#define DISPLAY_NVS_SPEED_UNIT_KEY "speed_unit"
#define DISPLAY_NVS_SPEED_SOURCE_KEY "speed_src"
#define DISPLAY_NVS_SPEED_STYLE_KEY "speed_style"
#define DISPLAY_NVS_BOOT_ANIMATION_KEY "boot_anim"
#define DISPLAY_NVS_LANGUAGE_KEY "lang"
#define DISPLAY_NVS_BMS_TYPE_KEY "bms_type"
#define DISPLAY_NVS_PRESET_RANGE_KEY "preset_rng"
#define CONTROLLER_NVS_CONNECTION_KEY "ctl_conn"
#define CONTROLLER_NVS_PAGE_KEY "ctl_page"
#define CONTROLLER_NVS_WHEEL_KEY "ctl_wheel"
#define CONTROLLER_NVS_RATIO_KEY "ctl_ratio"
#define CONTROLLER_NVS_RIM_KEY "ctl_rim"
#define CONTROLLER_NVS_ASPECT_KEY "ctl_aspect"
#define CONTROLLER_NVS_WIDTH_KEY "ctl_width"
#define CONTROLLER_NVS_BOUND_MAC_KEY "ctl_mac"
#define CONTROLLER_NVS_BOUND_NAME_KEY "ctl_name"
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
#define CONTROLLER_RATIO_CENTI_DEFAULT ESP_BMS_CONTROLLER_RATIO_CENTI_DEFAULT
#define HTTP_BODY_MAX_LEN 384U
#define HTTP_JSON_MAX_LEN 2048U
#define HTTP_BMS_CANDIDATES_JSON_MAX_LEN 2560U
#define HTTP_MANIFEST_JSON_MAX_LEN 3072U
#define CAST_HEARTBEAT_TIMEOUT_MS 5000U
#define CAST_METRICS_LOG_WINDOW_US INT64_C(5000000)
#define BLE_MEDIA_HID_USAGE_QUEUE_LEN 8U
#define BLE_MEDIA_HID_WORKER_STACK 2048U
#define BLE_MEDIA_HID_WORKER_PRIORITY 4U
#define BLE_MEDIA_HID_REPORT_RELEASE_DELAY_MS 30U
#define BLE_HOST_MIN_INTERNAL_FREE_BYTES (24U * 1024U)
#define BLE_API_REQUEST_MAX_LEN 512U
#define BLE_API_RESPONSE_MAX_LEN 4096U
#define BLE_API_QUEUE_LEN 1U
#define BLE_API_WORKER_STACK 4096U
#define BLE_API_WORKER_PRIORITY 4U
#define BLE_API_NOTIFY_RETRY_COUNT 3U
#define BLE_API_NOTIFY_RETRY_MS 5U

static bool runtime_cast_rotation_valid(uint8_t rotation)
{
    return esp_bms_cast_protocol_rotation_valid(rotation);
}

static esp_err_t runtime_cast_resolution(uint8_t rotation, uint16_t *width, uint16_t *height)
{
    if (!runtime_cast_rotation_valid(rotation) || !width || !height) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_bms_display_service_get_logical_resolution(
        (esp_bms_display_rotation_t)rotation, width, height);
}

void esp_bms_idf_runtime_stop_cast(esp_bms_idf_runtime_t *runtime, const char *reason)
{
    if (!runtime) {
        return;
    }
    if (__atomic_exchange_n(&runtime->cast_active, false, __ATOMIC_ACQ_REL)) {
        ESP_LOGI(TAG, "[cast] stopped: %s", reason);
    }
    __atomic_store_n(&runtime->cast_active, false, __ATOMIC_RELAXED);
    runtime->cast_frame_active = false;
    runtime->cast_socket_fd = -1;
    runtime->cast_rotation = (uint8_t)ESP_BMS_DISPLAY_ROTATION_PORTRAIT;
    runtime->cast_width = 0U;
    runtime->cast_height = 0U;
    runtime->cast_sequence = 0U;
    __atomic_store_n(&runtime->cast_heartbeat_elapsed_ms, 0U, __ATOMIC_RELAXED);
}

static void runtime_log_heap_state(const char *stage)
{
    const uint32_t internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t psram_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    ESP_LOGI(TAG,
             "[heap] %s default_free=%u default_min=%u internal8_free=%u internal8_min=%u internal8_largest=%u psram_free=%u psram_largest=%u",
             stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT),
             (unsigned)heap_caps_get_free_size(internal_caps),
             (unsigned)heap_caps_get_minimum_free_size(internal_caps),
             (unsigned)heap_caps_get_largest_free_block(internal_caps),
             (unsigned)heap_caps_get_free_size(psram_caps),
             (unsigned)heap_caps_get_largest_free_block(psram_caps));
}

#if ESP_BMS_FEATURE_BLE
void ble_store_config_init(void);
#endif

/* Controller compatibility still uses this numeric phase representation. */
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

typedef struct {
    char mac[18];
    char name[ESP_BMS_IDF_BMS_SCAN_NAME_LEN + 1U];
} bms_scan_name_cache_entry_t;

static bms_scan_name_cache_entry_t s_bms_scan_name_cache[ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES];
static uint8_t s_bms_scan_name_cache_count;
static uint8_t s_bms_scan_name_cache_next;

#if ESP_BMS_FEATURE_BLE
typedef struct {
    uint16_t conn_handle;
    char json[BLE_API_REQUEST_MAX_LEN + 1U];
} runtime_ble_api_request_t;

static const ble_uuid128_t BLE_API_SERVICE_UUID = BLE_UUID128_INIT(
    0x01, 0x0a, 0x3f, 0x09, 0xd1, 0x86, 0x9a, 0x9f,
    0xe7, 0x4e, 0xb2, 0x23, 0x00, 0xe1, 0x91, 0x4f);
static const ble_uuid128_t BLE_API_REQUEST_UUID = BLE_UUID128_INIT(
    0x02, 0x0a, 0x3f, 0x09, 0xd1, 0x86, 0x9a, 0x9f,
    0xe7, 0x4e, 0xb2, 0x23, 0x00, 0xe1, 0x91, 0x4f);
static const ble_uuid128_t BLE_API_RESPONSE_UUID = BLE_UUID128_INIT(
    0x03, 0x0a, 0x3f, 0x09, 0xd1, 0x86, 0x9a, 0x9f,
    0xe7, 0x4e, 0xb2, 0x23, 0x00, 0xe1, 0x91, 0x4f);
static QueueHandle_t s_ble_api_request_queue;
static uint16_t s_ble_api_response_handle;
static uint16_t s_ble_api_subscribed_conn = 0xFFFFU;
static uint16_t s_ble_api_fragment_conn = 0xFFFFU;
static size_t s_ble_api_fragment_len;
static char s_ble_api_fragment[BLE_API_REQUEST_MAX_LEN + 1U];

static int runtime_bluetooth_gap_event(struct ble_gap_event *event, void *arg);
static esp_err_t runtime_bluetooth_start_advertising_now(esp_bms_idf_runtime_t *runtime);
static esp_err_t runtime_init_ble_host(esp_bms_idf_runtime_t *runtime);
static bool runtime_status_json(esp_bms_idf_runtime_t *runtime, char *json, size_t json_len);
static bool runtime_config_json(esp_bms_idf_runtime_t *runtime, char *json, size_t json_len);
static bool runtime_settings_manifest_json(esp_bms_idf_runtime_t *runtime, char *json, size_t json_len);
static int runtime_apply_config_json(esp_bms_idf_runtime_t *runtime, const char *body, const char **message);
static esp_err_t runtime_ble_api_register_gatt(void);
static void runtime_ble_api_worker(void *param);
#endif
static void runtime_copy_snapshot_text(char *out, size_t out_len, const char *text);
static esp_err_t runtime_save_bms_binding(esp_bms_idf_runtime_t *runtime);
static esp_err_t runtime_save_setup_ap_credentials(const esp_bms_idf_runtime_t *runtime);
static void runtime_ensure_setup_ap_credentials(esp_bms_idf_runtime_t *runtime);
static char runtime_hex_char(uint8_t value);
static void runtime_update_snapshot_speed(esp_bms_idf_runtime_t *runtime);
static bool runtime_project_bluetooth_snapshot(esp_bms_idf_runtime_t *runtime);
static void runtime_reset_gps_track(esp_bms_gps_track_t *track);
static bool runtime_gps_track_valid(const esp_bms_gps_track_t *track);

#define RUNTIME_FLAG(runtime, name) \
    esp_bms_idf_runtime_flag_get((runtime), ESP_BMS_IDF_RUNTIME_FLAG_##name)
#define RUNTIME_SET_FLAG(runtime, name, enabled) \
    esp_bms_idf_runtime_flag_set((runtime), ESP_BMS_IDF_RUNTIME_FLAG_##name, (enabled))
#define RUNTIME_SNAPSHOT(runtime) ((runtime) ? &(runtime)->snapshot : NULL)
#define RUNTIME_SNAPSHOT_FLAG(runtime, name) \
    esp_bms_dashboard_snapshot_flag_get(RUNTIME_SNAPSHOT(runtime), ESP_BMS_DASHBOARD_FLAG_##name)
#define RUNTIME_SET_SNAPSHOT_FLAG(runtime, name, enabled) \
    esp_bms_dashboard_snapshot_flag_set(RUNTIME_SNAPSHOT(runtime), ESP_BMS_DASHBOARD_FLAG_##name, (enabled))
#define ACTION_EVENT_FLAG(event, name) \
    esp_bms_lvgl_action_event_flag_get((event), ESP_BMS_LVGL_ACTION_EVENT_FLAG_##name)

#if ESP_BMS_FEATURE_BLE
static esp_bms_idf_runtime_t *s_ble_host_runtime;

#if ESP_BMS_FEATURE_BLE_MEDIA_HID
#define BLE_MEDIA_HID_SERVICE_UUID16 0x1812U
#define BLE_MEDIA_HID_INFORMATION_UUID16 0x2A4AU
#define BLE_MEDIA_HID_REPORT_MAP_UUID16 0x2A4BU
#define BLE_MEDIA_HID_CONTROL_POINT_UUID16 0x2A4CU
#define BLE_MEDIA_HID_REPORT_UUID16 0x2A4DU
#define BLE_MEDIA_HID_PROTOCOL_MODE_UUID16 0x2A4EU
#define BLE_MEDIA_HID_DIS_SERVICE_UUID16 0x180AU
#define BLE_MEDIA_HID_DIS_MANUFACTURER_UUID16 0x2A29U
#define BLE_MEDIA_HID_DIS_MODEL_UUID16 0x2A24U
#define BLE_MEDIA_HID_DIS_PNP_ID_UUID16 0x2A50U
#define BLE_MEDIA_HID_BAS_SERVICE_UUID16 0x180FU
#define BLE_MEDIA_HID_BAS_BATTERY_LEVEL_UUID16 0x2A19U
#define BLE_MEDIA_HID_EXTERNAL_REPORT_REFERENCE_UUID16 0x2907U
#define BLE_MEDIA_HID_REPORT_REFERENCE_UUID16 0x2908U
#define BLE_MEDIA_HID_APPEARANCE 0x03C0U
#if CONFIG_BT_NIMBLE_SM_LVL >= 2
#define BLE_MEDIA_HID_READ_SECURITY_FLAGS BLE_GATT_CHR_F_READ_ENC
#define BLE_MEDIA_HID_WRITE_SECURITY_FLAGS BLE_GATT_CHR_F_WRITE_ENC
#define BLE_MEDIA_HID_NOTIFY_SECURITY_FLAGS BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC
#else
#define BLE_MEDIA_HID_READ_SECURITY_FLAGS 0
#define BLE_MEDIA_HID_WRITE_SECURITY_FLAGS 0
#define BLE_MEDIA_HID_NOTIFY_SECURITY_FLAGS 0
#endif

static const ble_uuid16_t BLE_MEDIA_HID_SERVICE_UUID =
    BLE_UUID16_INIT(BLE_MEDIA_HID_SERVICE_UUID16);
static const ble_uuid16_t BLE_MEDIA_HID_INFORMATION_UUID =
    BLE_UUID16_INIT(BLE_MEDIA_HID_INFORMATION_UUID16);
static const ble_uuid16_t BLE_MEDIA_HID_REPORT_MAP_UUID =
    BLE_UUID16_INIT(BLE_MEDIA_HID_REPORT_MAP_UUID16);
static const ble_uuid16_t BLE_MEDIA_HID_CONTROL_POINT_UUID =
    BLE_UUID16_INIT(BLE_MEDIA_HID_CONTROL_POINT_UUID16);
static const ble_uuid16_t BLE_MEDIA_HID_REPORT_UUID =
    BLE_UUID16_INIT(BLE_MEDIA_HID_REPORT_UUID16);
static const ble_uuid16_t BLE_MEDIA_HID_PROTOCOL_MODE_UUID =
    BLE_UUID16_INIT(BLE_MEDIA_HID_PROTOCOL_MODE_UUID16);
static const ble_uuid16_t BLE_MEDIA_HID_DIS_SERVICE_UUID =
    BLE_UUID16_INIT(BLE_MEDIA_HID_DIS_SERVICE_UUID16);
static const ble_uuid16_t BLE_MEDIA_HID_DIS_MANUFACTURER_UUID =
    BLE_UUID16_INIT(BLE_MEDIA_HID_DIS_MANUFACTURER_UUID16);
static const ble_uuid16_t BLE_MEDIA_HID_DIS_MODEL_UUID =
    BLE_UUID16_INIT(BLE_MEDIA_HID_DIS_MODEL_UUID16);
static const ble_uuid16_t BLE_MEDIA_HID_DIS_PNP_ID_UUID =
    BLE_UUID16_INIT(BLE_MEDIA_HID_DIS_PNP_ID_UUID16);
static const ble_uuid16_t BLE_MEDIA_HID_BAS_SERVICE_UUID =
    BLE_UUID16_INIT(BLE_MEDIA_HID_BAS_SERVICE_UUID16);
static const ble_uuid16_t BLE_MEDIA_HID_BAS_BATTERY_LEVEL_UUID =
    BLE_UUID16_INIT(BLE_MEDIA_HID_BAS_BATTERY_LEVEL_UUID16);
static const ble_uuid16_t BLE_MEDIA_HID_EXTERNAL_REPORT_REFERENCE_UUID =
    BLE_UUID16_INIT(BLE_MEDIA_HID_EXTERNAL_REPORT_REFERENCE_UUID16);
static const ble_uuid16_t BLE_MEDIA_HID_REPORT_REFERENCE_UUID =
    BLE_UUID16_INIT(BLE_MEDIA_HID_REPORT_REFERENCE_UUID16);
static uint16_t s_ble_media_hid_input_report_handle;

static const uint8_t BLE_MEDIA_HID_INFORMATION[] = { 0x11U, 0x01U, 0x00U, 0x03U };
static const uint8_t BLE_MEDIA_HID_REPORT_MAP[] = {
    0x05U, 0x0CU,
    0x09U, 0x01U,
    0xA1U, 0x01U,
    0x85U, ESP_BMS_BLE_MEDIA_HID_REPORT_ID,
    0x15U, 0x00U,
    0x26U, 0xFFU, 0x03U,
    0x19U, 0x00U,
    0x2AU, 0xFFU, 0x03U,
    0x75U, 0x10U,
    0x95U, 0x01U,
    0x81U, 0x00U,
    0xC0U,
};
static const uint8_t BLE_MEDIA_HID_REPORT_REFERENCE[] = {
    ESP_BMS_BLE_MEDIA_HID_REPORT_ID,
    0x01U,
};
static const uint8_t BLE_MEDIA_HID_EXTERNAL_REPORT_REFERENCE[] = {
    BLE_MEDIA_HID_BAS_SERVICE_UUID16 & 0xFFU,
    (BLE_MEDIA_HID_BAS_SERVICE_UUID16 >> 8U) & 0xFFU,
};
static const uint8_t BLE_MEDIA_HID_PNP_ID[] = {
    0x02U,
    0xC0U, 0x16U,
    0xDFU, 0x05U,
    0x00U, 0x01U,
};
static const uint8_t BLE_MEDIA_HID_BATTERY_LEVEL = 100U;
static const char BLE_MEDIA_HID_MANUFACTURER[] = "Espressif";
static const char BLE_MEDIA_HID_MODEL[] = "ESP32 BMS GPS";

static void runtime_ble_media_hid_snapshot_clear(esp_bms_idf_runtime_t *runtime)
{
    runtime->ble_media_hid_input_report_subscribed = false;
    runtime->snapshot.ble_media_hid_connected = false;
    runtime->snapshot.ble_media_hid_suspended = false;
}

static int runtime_ble_media_hid_append(struct ble_gatt_access_ctxt *ctxt,
                                        const uint8_t *value,
                                        uint16_t value_len)
{
    return os_mbuf_append(ctxt->om, value, value_len) == 0
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static bool runtime_ble_media_hid_connection_is_encrypted(
    const esp_bms_idf_runtime_t *runtime,
    uint16_t conn_handle)
{
    struct ble_gap_conn_desc desc = { 0 };
    return runtime && conn_handle == runtime->bluetooth_conn_handle &&
           ble_gap_conn_find(conn_handle, &desc) == 0 && desc.sec_state.encrypted;
}

static int runtime_ble_media_hid_information_access_cb(uint16_t conn_handle,
                                                        uint16_t attr_handle,
                                                        struct ble_gatt_access_ctxt *ctxt,
                                                        void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    ESP_LOGI(TAG, "[hid] read HID information: conn=%u attr=%u op=%d",
             conn_handle, attr_handle, ctxt->op);
    return ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR
               ? runtime_ble_media_hid_append(ctxt,
                                              BLE_MEDIA_HID_INFORMATION,
                                              sizeof(BLE_MEDIA_HID_INFORMATION))
               : BLE_ATT_ERR_UNLIKELY;
}

static int runtime_ble_media_hid_report_map_access_cb(uint16_t conn_handle,
                                                       uint16_t attr_handle,
                                                       struct ble_gatt_access_ctxt *ctxt,
                                                       void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    ESP_LOGI(TAG, "[hid] read report map: conn=%u attr=%u op=%d len=%u",
             conn_handle, attr_handle, ctxt->op, (unsigned)sizeof(BLE_MEDIA_HID_REPORT_MAP));
    return ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR
               ? runtime_ble_media_hid_append(ctxt,
                                              BLE_MEDIA_HID_REPORT_MAP,
                                              sizeof(BLE_MEDIA_HID_REPORT_MAP))
               : BLE_ATT_ERR_UNLIKELY;
}

static int runtime_ble_media_hid_input_report_access_cb(uint16_t conn_handle,
                                                         uint16_t attr_handle,
                                                         struct ble_gatt_access_ctxt *ctxt,
                                                         void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    const uint8_t release[ESP_BMS_BLE_MEDIA_HID_REPORT_LEN] = { 0 };
    ESP_LOGI(TAG, "[hid] read input report: conn=%u attr=%u op=%d",
             conn_handle, attr_handle, ctxt->op);
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return runtime_ble_media_hid_append(ctxt, release, sizeof(release));
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR &&
        OS_MBUF_PKTLEN(ctxt->om) <= ESP_BMS_BLE_MEDIA_HID_REPORT_LEN) {
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int runtime_ble_media_hid_report_reference_access_cb(uint16_t conn_handle,
                                                             uint16_t attr_handle,
                                                             struct ble_gatt_access_ctxt *ctxt,
                                                             void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    ESP_LOGI(TAG, "[hid] read report reference: conn=%u attr=%u op=%d",
             conn_handle, attr_handle, ctxt->op);
    return ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC
               ? runtime_ble_media_hid_append(ctxt,
                                              BLE_MEDIA_HID_REPORT_REFERENCE,
                                              sizeof(BLE_MEDIA_HID_REPORT_REFERENCE))
               : BLE_ATT_ERR_UNLIKELY;
}

static int runtime_ble_media_hid_external_report_reference_access_cb(
    uint16_t conn_handle,
    uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt,
    void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    ESP_LOGI(TAG, "[hid] read external report reference: conn=%u attr=%u op=%d",
             conn_handle, attr_handle, ctxt->op);
    return ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC
               ? runtime_ble_media_hid_append(ctxt,
                                              BLE_MEDIA_HID_EXTERNAL_REPORT_REFERENCE,
                                              sizeof(BLE_MEDIA_HID_EXTERNAL_REPORT_REFERENCE))
               : BLE_ATT_ERR_UNLIKELY;
}

static int runtime_ble_media_hid_string_access_cb(uint16_t conn_handle,
                                                   uint16_t attr_handle,
                                                   struct ble_gatt_access_ctxt *ctxt,
                                                   void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    const char *value = (const char *)arg;
    ESP_LOGI(TAG, "[hid] read DIS string: conn=%u attr=%u op=%d",
             conn_handle, attr_handle, ctxt->op);
    return ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR && value
               ? runtime_ble_media_hid_append(ctxt,
                                              (const uint8_t *)value,
                                              (uint16_t)strlen(value))
               : BLE_ATT_ERR_UNLIKELY;
}

static int runtime_ble_media_hid_pnp_id_access_cb(uint16_t conn_handle,
                                                   uint16_t attr_handle,
                                                   struct ble_gatt_access_ctxt *ctxt,
                                                   void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    ESP_LOGI(TAG, "[hid] read DIS PnP ID: conn=%u attr=%u op=%d",
             conn_handle, attr_handle, ctxt->op);
    return ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR
               ? runtime_ble_media_hid_append(ctxt,
                                              BLE_MEDIA_HID_PNP_ID,
                                              sizeof(BLE_MEDIA_HID_PNP_ID))
               : BLE_ATT_ERR_UNLIKELY;
}

static int runtime_ble_media_hid_battery_level_access_cb(uint16_t conn_handle,
                                                          uint16_t attr_handle,
                                                          struct ble_gatt_access_ctxt *ctxt,
                                                          void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    ESP_LOGI(TAG, "[hid] read battery level: conn=%u attr=%u op=%d",
             conn_handle, attr_handle, ctxt->op);
    return ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR
               ? runtime_ble_media_hid_append(ctxt,
                                              &BLE_MEDIA_HID_BATTERY_LEVEL,
                                              sizeof(BLE_MEDIA_HID_BATTERY_LEVEL))
               : BLE_ATT_ERR_UNLIKELY;
}

static int runtime_ble_media_hid_protocol_mode_access_cb(uint16_t conn_handle,
                                                          uint16_t attr_handle,
                                                          struct ble_gatt_access_ctxt *ctxt,
                                                          void *arg)
{
    (void)attr_handle;
    (void)arg;
    const uint8_t report_protocol = 0x01U;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return runtime_ble_media_hid_append(ctxt, &report_protocol, sizeof(report_protocol));
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR ||
        OS_MBUF_PKTLEN(ctxt->om) != sizeof(report_protocol)) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    uint8_t mode = 0U;
    return os_mbuf_copydata(ctxt->om, 0U, sizeof(mode), &mode) == 0 && mode == report_protocol
               ? 0
               : BLE_ATT_ERR_UNLIKELY;
}

static int runtime_ble_media_hid_control_point_access_cb(uint16_t conn_handle,
                                                          uint16_t attr_handle,
                                                          struct ble_gatt_access_ctxt *ctxt,
                                                          void *arg)
{
    (void)attr_handle;
    (void)arg;
    esp_bms_idf_runtime_t *runtime = s_ble_host_runtime;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR ||
        OS_MBUF_PKTLEN(ctxt->om) != 1U) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    uint8_t command = 0U;
    if (os_mbuf_copydata(ctxt->om, 0U, sizeof(command), &command) != 0 || command > 1U) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    runtime->snapshot.ble_media_hid_suspended = command == 0U;
    (void)runtime_project_bluetooth_snapshot(runtime);
    ESP_LOGI(TAG, "[hid] consumer control %s", command == 0U ? "suspended" : "resumed");
    return 0;
}

static struct ble_gatt_dsc_def BLE_MEDIA_HID_INPUT_REPORT_DESCRIPTORS[] = {
    {
        .uuid = &BLE_MEDIA_HID_REPORT_REFERENCE_UUID.u,
        .access_cb = runtime_ble_media_hid_report_reference_access_cb,
        .att_flags = BLE_ATT_F_READ,
    },
    { 0 },
};

static struct ble_gatt_dsc_def BLE_MEDIA_HID_REPORT_MAP_DESCRIPTORS[] = {
    {
        .uuid = &BLE_MEDIA_HID_EXTERNAL_REPORT_REFERENCE_UUID.u,
        .access_cb = runtime_ble_media_hid_external_report_reference_access_cb,
        .att_flags = BLE_ATT_F_READ,
    },
    { 0 },
};

static const struct ble_gatt_svc_def BLE_MEDIA_HID_GATT_SERVICES[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &BLE_MEDIA_HID_DIS_SERVICE_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &BLE_MEDIA_HID_DIS_MANUFACTURER_UUID.u,
                .access_cb = runtime_ble_media_hid_string_access_cb,
                .arg = (void *)BLE_MEDIA_HID_MANUFACTURER,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = &BLE_MEDIA_HID_DIS_MODEL_UUID.u,
                .access_cb = runtime_ble_media_hid_string_access_cb,
                .arg = (void *)BLE_MEDIA_HID_MODEL,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = &BLE_MEDIA_HID_DIS_PNP_ID_UUID.u,
                .access_cb = runtime_ble_media_hid_pnp_id_access_cb,
                .flags = BLE_GATT_CHR_F_READ,
            },
            { 0 },
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &BLE_MEDIA_HID_BAS_SERVICE_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &BLE_MEDIA_HID_BAS_BATTERY_LEVEL_UUID.u,
                .access_cb = runtime_ble_media_hid_battery_level_access_cb,
                .flags = BLE_GATT_CHR_F_READ,
            },
            { 0 },
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &BLE_MEDIA_HID_SERVICE_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &BLE_MEDIA_HID_INFORMATION_UUID.u,
                .access_cb = runtime_ble_media_hid_information_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_MEDIA_HID_READ_SECURITY_FLAGS,
            },
            {
                .uuid = &BLE_MEDIA_HID_REPORT_MAP_UUID.u,
                .access_cb = runtime_ble_media_hid_report_map_access_cb,
                .descriptors = BLE_MEDIA_HID_REPORT_MAP_DESCRIPTORS,
                .flags = BLE_GATT_CHR_F_READ | BLE_MEDIA_HID_READ_SECURITY_FLAGS,
            },
            {
                .uuid = &BLE_MEDIA_HID_CONTROL_POINT_UUID.u,
                .access_cb = runtime_ble_media_hid_control_point_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_MEDIA_HID_WRITE_SECURITY_FLAGS,
            },
            {
                .uuid = &BLE_MEDIA_HID_PROTOCOL_MODE_UUID.u,
                .access_cb = runtime_ble_media_hid_protocol_mode_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP |
                         BLE_MEDIA_HID_READ_SECURITY_FLAGS | BLE_MEDIA_HID_WRITE_SECURITY_FLAGS,
            },
            {
                .uuid = &BLE_MEDIA_HID_REPORT_UUID.u,
                .access_cb = runtime_ble_media_hid_input_report_access_cb,
                .val_handle = &s_ble_media_hid_input_report_handle,
                .descriptors = BLE_MEDIA_HID_INPUT_REPORT_DESCRIPTORS,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_WRITE |
                         BLE_MEDIA_HID_READ_SECURITY_FLAGS | BLE_MEDIA_HID_WRITE_SECURITY_FLAGS |
                         BLE_MEDIA_HID_NOTIFY_SECURITY_FLAGS,
            },
            { 0 },
        },
    },
    { 0 },
};

static esp_err_t runtime_ble_media_hid_register_gatt(void)
{
    int rc = ble_gatts_count_cfg(BLE_MEDIA_HID_GATT_SERVICES);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(BLE_MEDIA_HID_GATT_SERVICES);
    }
    if (rc != 0) {
        ESP_LOGW(TAG, "[hid] GATT service registration failed: rc=%d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static bool runtime_ble_media_hid_send_report(
    esp_bms_idf_runtime_t *runtime,
    const uint8_t report[ESP_BMS_BLE_MEDIA_HID_REPORT_LEN])
{
    if (!runtime_ble_media_hid_connection_is_encrypted(runtime, runtime->bluetooth_conn_handle) ||
        !RUNTIME_FLAG(runtime, BLUETOOTH_CONNECTED) ||
        !report ||
        !runtime->ble_media_hid_input_report_subscribed ||
        runtime->snapshot.ble_media_hid_suspended || s_ble_media_hid_input_report_handle == 0U) {
        return false;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(report, ESP_BMS_BLE_MEDIA_HID_REPORT_LEN);
    if (!om) {
        return false;
    }
    const int rc = ble_gatts_notify_custom(runtime->bluetooth_conn_handle,
                                           s_ble_media_hid_input_report_handle,
                                           om);
    if (rc != 0) {
        ESP_LOGW(TAG, "[hid] report notify failed: rc=%d", rc);
        return false;
    }
    return true;
}

static void runtime_ble_media_hid_worker(void *arg)
{
    esp_bms_idf_runtime_t *runtime = arg;
    esp_bms_ble_media_hid_usage_t usage;
    while (xQueueReceive(runtime->ble_media_hid_usage_queue, &usage, portMAX_DELAY) == pdTRUE) {
        uint8_t report[ESP_BMS_BLE_MEDIA_HID_REPORT_LEN] = { 0 };
        if (!esp_bms_ble_media_hid_report_from_usage(usage, report) ||
            !runtime_ble_media_hid_send_report(runtime, report)) {
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(BLE_MEDIA_HID_REPORT_RELEASE_DELAY_MS));
        const uint8_t release[ESP_BMS_BLE_MEDIA_HID_REPORT_LEN] = { 0 };
        (void)runtime_ble_media_hid_send_report(runtime, release);
    }
    vTaskDelete(NULL);
}

static bool runtime_ble_media_hid_enqueue(esp_bms_idf_runtime_t *runtime,
                                          esp_bms_ble_media_hid_usage_t usage)
{
    uint8_t report[ESP_BMS_BLE_MEDIA_HID_REPORT_LEN] = { 0 };
    return runtime && runtime->ble_media_hid_usage_queue &&
           esp_bms_ble_media_hid_report_from_usage(usage, report) &&
           runtime_ble_media_hid_connection_is_encrypted(runtime, runtime->bluetooth_conn_handle) &&
           RUNTIME_FLAG(runtime, BLUETOOTH_CONNECTED) &&
           runtime->ble_media_hid_input_report_subscribed &&
           !runtime->snapshot.ble_media_hid_suspended &&
           xQueueSend(runtime->ble_media_hid_usage_queue, &usage, 0U) == pdPASS;
}
#endif

static void runtime_set_ble_tx_power(void)
{
#if CONFIG_IDF_TARGET_ESP32C3
    const esp_power_level_t level = ESP_PWR_LVL_P20;
#else
    const esp_power_level_t level = ESP_PWR_LVL_P9;
#endif
    const esp_err_t adv_ret = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, level);
    const esp_err_t scan_ret = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, level);
    const esp_err_t default_ret = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, level);
    if (adv_ret != ESP_OK || scan_ret != ESP_OK || default_ret != ESP_OK) {
        ESP_LOGW(TAG, "[ble] TX power setup failed: adv=%s scan=%s default=%s",
                 esp_err_to_name(adv_ret), esp_err_to_name(scan_ret), esp_err_to_name(default_ret));
    }
}
#endif

static bool runtime_controller_tire_matches_policy(uint8_t rim_inch,
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

static bool runtime_controller_ratio_matches_policy(uint16_t ratio_centi)
{
    return ratio_centi >= CONTROLLER_RATIO_CENTI_MIN &&
           ratio_centi <= CONTROLLER_RATIO_CENTI_MAX;
}

static void runtime_project_controller_snapshot(esp_bms_idf_runtime_t *runtime)
{
    esp_bms_dashboard_snapshot_t *snapshot = &runtime->snapshot;
    const esp_fardriver_state_t *state = &runtime->controller_state;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CONTROLLER_CONNECTION_ENABLED, runtime->controller_connection_enabled);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CONTROLLER_PAGE_ENABLED, runtime->controller_page_enabled);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime,
                              CONTROLLER_ONLINE,
                              runtime->controller_conn_handle != 0xFFFFU &&
                                  runtime->controller_ble_phase == (uint8_t)BMS_BLE_PHASE_ONLINE &&
                                  RUNTIME_FLAG(runtime, CONTROLLER_SUBSCRIBED));
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CONTROLLER_SPEED_VALID, state->speed_valid);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CONTROLLER_RPM_VALID, state->rpm_valid);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CONTROLLER_GEAR_VALID, state->gear_valid);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CONTROLLER_POWER_VALID, state->power_valid);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CONTROLLER_TEMP_VALID, state->controller_temp_valid);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, MOTOR_TEMP_VALID, state->motor_temp_valid);
    snapshot->controller_speed_deci_units = state->speed_deci_kmh;
    if (snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH && state->speed_valid) {
        snapshot->controller_speed_deci_units = (uint16_t)(((uint32_t)state->speed_deci_kmh * 621371U) / 1000000U);
    }
    snapshot->controller_rpm = state->rpm;
    snapshot->controller_gear = state->gear;
    snapshot->controller_power_w = state->power_w;
    snapshot->controller_temp_c = state->controller_temp_c;
    snapshot->motor_temp_c = state->motor_temp_c;
    snapshot->controller_fallback_tire_rim_inch = runtime->controller_fallback_tire_rim_inch;
    snapshot->controller_fallback_tire_aspect_percent =
        runtime->controller_fallback_tire_aspect_percent;
    snapshot->controller_fallback_tire_width_mm = runtime->controller_fallback_tire_width_mm;
    snapshot->controller_fallback_wheel_circumference_mm =
        state->fallback_wheel_circumference_mm;
    snapshot->controller_fallback_gear_ratio_centi = state->fallback_gear_ratio_centi;
    snapshot->controller_tire_rim_inch = 0U;
    snapshot->controller_tire_aspect_percent = 0U;
    snapshot->controller_tire_width_mm = 0U;
    snapshot->controller_wheel_circumference_mm = state->fallback_wheel_circumference_mm;
    snapshot->controller_gear_ratio_centi = state->fallback_gear_ratio_centi;
    snapshot->controller_scan_active = RUNTIME_FLAG(runtime, CONTROLLER_SCAN_ACTIVE) ? 1U : 0U;
    snapshot->controller_scan_revision = runtime->controller_scan_revision;
    snapshot->controller_param_source = (uint8_t)ESP_BMS_CONTROLLER_PARAM_SOURCE_UNSET;
    if (state->controller_speed_params_valid) {
        snapshot->controller_tire_rim_inch = state->tire_rim_inch;
        snapshot->controller_tire_aspect_percent = state->tire_aspect_percent;
        snapshot->controller_tire_width_mm = state->tire_width_mm;
        snapshot->controller_wheel_circumference_mm = state->wheel_circumference_mm;
        snapshot->controller_gear_ratio_centi = state->gear_ratio_centi;
        snapshot->controller_param_source = (uint8_t)ESP_BMS_CONTROLLER_PARAM_SOURCE_CONTROLLER;
    } else if (runtime_controller_tire_matches_policy(runtime->controller_fallback_tire_rim_inch,
                                                       runtime->controller_fallback_tire_aspect_percent,
                                                       runtime->controller_fallback_tire_width_mm)) {
        snapshot->controller_tire_rim_inch = runtime->controller_fallback_tire_rim_inch;
        snapshot->controller_tire_aspect_percent =
            runtime->controller_fallback_tire_aspect_percent;
        snapshot->controller_tire_width_mm = runtime->controller_fallback_tire_width_mm;
        snapshot->controller_param_source = (uint8_t)ESP_BMS_CONTROLLER_PARAM_SOURCE_LOCAL;
    } else if (state->fallback_wheel_circumference_mm > 0U) {
        snapshot->controller_param_source = (uint8_t)ESP_BMS_CONTROLLER_PARAM_SOURCE_LEGACY_WHEEL;
    }
    snapshot->controller_scan_candidate_count = runtime->controller_scan_candidate_count;
    memcpy(snapshot->controller_scan_candidates,
           runtime->controller_scan_candidates,
           sizeof(snapshot->controller_scan_candidates));
    runtime_copy_snapshot_text(snapshot->controller_bound_name,
                               sizeof(snapshot->controller_bound_name),
                               runtime->controller_bound_name);
    runtime_update_snapshot_speed(runtime);
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SNAPSHOT_DIRTY, true);
}

void esp_bms_idf_runtime_project_controller_snapshot(esp_bms_idf_runtime_t *runtime)
{
    if (runtime) {
        runtime_project_controller_snapshot(runtime);
    }
}

static void runtime_set_error(esp_bms_idf_runtime_t *runtime, const char *text)
{
    strncpy(runtime->snapshot.bms_error_text, text, sizeof(runtime->snapshot.bms_error_text) - 1);
    runtime->snapshot.bms_error_text[sizeof(runtime->snapshot.bms_error_text) - 1] = '\0';
}

static void runtime_copy_snapshot_text(char *out, size_t out_len, const char *text)
{
    if (!out || out_len == 0) {
        return;
    }
    if (!text) {
        out[0] = '\0';
        return;
    }
    strncpy(out, text, out_len - 1);
    out[out_len - 1] = '\0';
}

static void runtime_set_bms_info(esp_bms_idf_runtime_t *runtime, const char *text)
{
    runtime_copy_snapshot_text(runtime->snapshot.bms_info_text,
                               sizeof(runtime->snapshot.bms_info_text),
                               text);
    runtime_set_error(runtime, text);
    RUNTIME_SET_FLAG(runtime, BMS_SNAPSHOT_DIRTY, true);
}

static bool runtime_project_bluetooth_snapshot(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return false;
    }

    const bool enabled = true;
    const bool discoverable = RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED) ||
                              RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISING);
    const bool changed = RUNTIME_SNAPSHOT_FLAG(runtime, BLUETOOTH_ENABLED) != enabled ||
                         RUNTIME_SNAPSHOT_FLAG(runtime, BLUETOOTH_ADVERTISING) != discoverable ||
                         RUNTIME_SNAPSHOT_FLAG(runtime, BLUETOOTH_CONNECTED) !=
                             RUNTIME_FLAG(runtime, BLUETOOTH_CONNECTED) ||
                         strcmp(runtime->snapshot.bluetooth_name, runtime->bluetooth_name) != 0;

    RUNTIME_SET_SNAPSHOT_FLAG(runtime, BLUETOOTH_ENABLED, enabled);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, BLUETOOTH_ADVERTISING, discoverable);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, BLUETOOTH_CONNECTED, RUNTIME_FLAG(runtime, BLUETOOTH_CONNECTED));
    runtime_copy_snapshot_text(runtime->snapshot.bluetooth_name,
                               sizeof(runtime->snapshot.bluetooth_name),
                               runtime->bluetooth_name);
    if (changed) {
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_SNAPSHOT_DIRTY, true);
    }
    return changed;
}

static void runtime_update_setup_ap_snapshot(esp_bms_idf_runtime_t *runtime)
{
    runtime_copy_snapshot_text(runtime->snapshot.setup_ap_ssid,
                               sizeof(runtime->snapshot.setup_ap_ssid),
                               runtime->setup_ap_ssid);
    runtime_copy_snapshot_text(runtime->snapshot.setup_ap_password,
                               sizeof(runtime->snapshot.setup_ap_password),
                               runtime->setup_ap_password);

    if (runtime->setup_ap_ssid[0] == '\0' || runtime->setup_ap_password[0] == '\0') {
        runtime->snapshot.setup_ap_qr_payload[0] = '\0';
        return;
    }

    const int written = snprintf(runtime->snapshot.setup_ap_qr_payload,
                                 sizeof(runtime->snapshot.setup_ap_qr_payload),
                                 "WIFI:S:%s;T:WPA;P:%s;;",
                                 runtime->setup_ap_ssid,
                                 runtime->setup_ap_password);
    if (written < 0 || (size_t)written >= sizeof(runtime->snapshot.setup_ap_qr_payload)) {
        runtime->snapshot.setup_ap_qr_payload[0] = '\0';
    }
}

static void runtime_generate_setup_ap_credentials(esp_bms_idf_runtime_t *runtime)
{
    const unsigned long suffix = (unsigned long)(esp_random() & 0xFFFFFFU);
    (void)snprintf(runtime->setup_ap_ssid, sizeof(runtime->setup_ap_ssid),
                   SETUP_AP_SSID_PREFIX "%06lx", suffix);

    for (size_t index = 0; index < SETUP_AP_PASSWORD_LEN; index++) {
        runtime->setup_ap_password[index] = (char)('0' + (esp_random() % 10U));
    }
    runtime->setup_ap_password[SETUP_AP_PASSWORD_LEN] = '\0';
    runtime_update_setup_ap_snapshot(runtime);
}

static bool runtime_setup_ap_ssid_matches_policy(const char *ssid)
{
    const size_t prefix_len = strlen(SETUP_AP_SSID_PREFIX);
    const size_t ssid_len = strlen(ssid);
    if (ssid_len != prefix_len + SETUP_AP_SSID_SUFFIX_LEN ||
        memcmp(ssid, SETUP_AP_SSID_PREFIX, prefix_len) != 0) {
        return false;
    }

    for (size_t index = prefix_len; index < ssid_len; index++) {
        const char value = ssid[index];
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) {
            return false;
        }
    }
    return true;
}

static bool runtime_setup_ap_password_matches_policy(const char *password)
{
    if (strlen(password) != SETUP_AP_PASSWORD_LEN) {
        return false;
    }
    for (size_t index = 0; index < SETUP_AP_PASSWORD_LEN; index++) {
        if (password[index] < '0' || password[index] > '9') {
            return false;
        }
    }
    return true;
}

static bool runtime_bms_name_copy(char *out, size_t out_len, const uint8_t *name, size_t name_len)
{
    if (!out || out_len == 0U) {
        return false;
    }
    out[0] = '\0';
    if (!name || name_len == 0U) {
        return false;
    }

    size_t copied = 0;
    const size_t limit = out_len - 1U < ESP_BMS_IDF_BMS_SCAN_NAME_LEN
                             ? out_len - 1U
                             : ESP_BMS_IDF_BMS_SCAN_NAME_LEN;
    for (size_t index = 0; index < name_len; index++) {
        const unsigned char value = name[index];
        if (value < 0x20U || value > 0x7EU) {
            /* Skip characters without a TFT glyph (e.g. UTF-8 Chinese)
             * instead of truncating, so the printable ASCII part of the
             * name is still shown. */
            continue;
        }
        if (copied < limit) {
            out[copied++] = (char)value;
        }
    }
    out[copied] = '\0';
    return copied > 0U;
}

bool esp_bms_idf_runtime_bms_scan_project_snapshot(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return false;
    }
    if (runtime->bms_scan_lock &&
        xSemaphoreTake(runtime->bms_scan_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    uint8_t count = runtime->bms_scan_candidate_count;
    if (count > ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES) {
        count = ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES;
    }

    const bool changed =
        runtime->snapshot.bms_scan_candidate_count != count ||
        memcmp(runtime->snapshot.bms_scan_candidates,
               runtime->bms_scan_candidates,
               sizeof(runtime->snapshot.bms_scan_candidates)) != 0;
    runtime->snapshot.bms_scan_candidate_count = count;
    memset(runtime->snapshot.bms_scan_candidates, 0, sizeof(runtime->snapshot.bms_scan_candidates));
    memcpy(runtime->snapshot.bms_scan_candidates,
           runtime->bms_scan_candidates,
           sizeof(runtime->snapshot.bms_scan_candidates[0]) * count);

    if (runtime->bms_scan_lock) {
        xSemaphoreGive(runtime->bms_scan_lock);
    }
    return changed;
}

void esp_bms_idf_runtime_bms_scan_clear_candidates(esp_bms_idf_runtime_t *runtime)
{
    if (runtime->bms_scan_lock &&
        xSemaphoreTake(runtime->bms_scan_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    memset(runtime->bms_scan_candidates, 0, sizeof(runtime->bms_scan_candidates));
    runtime->bms_scan_candidate_count = 0;
    if (runtime->bms_scan_lock) {
        xSemaphoreGive(runtime->bms_scan_lock);
    }
    (void)esp_bms_idf_runtime_bms_scan_project_snapshot(runtime);
    RUNTIME_SET_FLAG(runtime, BMS_SCAN_SNAPSHOT_DIRTY, true);
}

static size_t runtime_bms_scan_find_candidate(const esp_bms_idf_runtime_t *runtime, const char *mac)
{
    for (size_t index = 0; index < runtime->bms_scan_candidate_count; index++) {
        if (strcmp(runtime->bms_scan_candidates[index].mac, mac) == 0) {
            return index;
        }
    }
    return ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES;
}

static const char *runtime_bms_scan_cached_name_locked(const char *mac)
{
    for (uint8_t index = 0; index < s_bms_scan_name_cache_count; index++) {
        if (strcmp(s_bms_scan_name_cache[index].mac, mac) == 0) {
            return s_bms_scan_name_cache[index].name;
        }
    }
    return NULL;
}

static void runtime_bms_scan_cache_name_locked(const char *mac, const char *name)
{
    uint8_t index = 0;
    for (; index < s_bms_scan_name_cache_count; index++) {
        if (strcmp(s_bms_scan_name_cache[index].mac, mac) == 0) {
            break;
        }
    }
    if (index == s_bms_scan_name_cache_count) {
        if (s_bms_scan_name_cache_count < ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES) {
            s_bms_scan_name_cache_count++;
        } else {
            index = s_bms_scan_name_cache_next++ % ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES;
        }
    }
    runtime_copy_snapshot_text(s_bms_scan_name_cache[index].mac,
                               sizeof(s_bms_scan_name_cache[index].mac),
                               mac);
    runtime_copy_snapshot_text(s_bms_scan_name_cache[index].name,
                               sizeof(s_bms_scan_name_cache[index].name),
                               name);
}

void esp_bms_idf_runtime_bms_scan_store_candidate(esp_bms_idf_runtime_t *runtime,
                                                   const char *mac,
                                                   const char *name,
                                                   int8_t rssi)
{
    if (!mac || mac[0] == '\0') {
        return;
    }
    if (runtime->bms_scan_lock &&
        xSemaphoreTake(runtime->bms_scan_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    if (name && name[0] != '\0') {
        runtime_bms_scan_cache_name_locked(mac, name);
    } else {
        name = runtime_bms_scan_cached_name_locked(mac);
    }

    const bool bound_name_changed = name && name[0] != '\0' &&
                                    strcmp(mac, runtime->bms_bound_mac) == 0 &&
                                    strlen(name) > strlen(runtime->bms_bound_name);
    if (bound_name_changed) {
        runtime_copy_snapshot_text(runtime->bms_bound_name,
                                   sizeof(runtime->bms_bound_name),
                                   name);
        runtime_copy_snapshot_text(runtime->snapshot.bms_bound_name,
                                   sizeof(runtime->snapshot.bms_bound_name),
                                   runtime->bms_bound_name);
    }

    size_t index = runtime_bms_scan_find_candidate(runtime, mac);
    if (index < ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES) {
        const bool candidate_changed =
            bound_name_changed ||
            (name && name[0] != '\0' &&
             (!runtime->bms_scan_candidates[index].has_name ||
              strcmp(runtime->bms_scan_candidates[index].name, name) != 0));
        runtime->bms_scan_candidates[index].rssi = rssi;
        if (name && name[0] != '\0') {
            runtime_copy_snapshot_text(runtime->bms_scan_candidates[index].name,
                                       sizeof(runtime->bms_scan_candidates[index].name),
                                       name);
            runtime->bms_scan_candidates[index].has_name = true;
        }
        if (runtime->bms_scan_lock) {
            xSemaphoreGive(runtime->bms_scan_lock);
        }
        if (candidate_changed) {
            RUNTIME_SET_FLAG(runtime, BMS_SCAN_SNAPSHOT_DIRTY, true);
        }
        return;
    }

    if (runtime->bms_scan_candidate_count < ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES) {
        index = runtime->bms_scan_candidate_count++;
    } else {
        if (runtime->bms_scan_lock) {
            xSemaphoreGive(runtime->bms_scan_lock);
        }
        return;
    }

    runtime_copy_snapshot_text(runtime->bms_scan_candidates[index].mac,
                               sizeof(runtime->bms_scan_candidates[index].mac),
                               mac);
    runtime_copy_snapshot_text(runtime->bms_scan_candidates[index].name,
                               sizeof(runtime->bms_scan_candidates[index].name),
                               name ? name : "");
    runtime->bms_scan_candidates[index].rssi = rssi;
    runtime->bms_scan_candidates[index].has_name = name && name[0] != '\0';
    if (runtime->bms_scan_lock) {
        xSemaphoreGive(runtime->bms_scan_lock);
    }
    RUNTIME_SET_FLAG(runtime, BMS_SCAN_SNAPSHOT_DIRTY, true);
    ESP_LOGI(TAG,
             "[bms] scan candidate stored: count=%u mac=%s name=%s rssi=%d",
             (unsigned)runtime->bms_scan_candidate_count,
             mac,
             name && name[0] != '\0' ? name : "-",
             (int)rssi);
}

static void runtime_clear_bms_telemetry(esp_bms_idf_runtime_t *runtime)
{
    runtime->bms_telemetry_last_us = 0;
    runtime->trip_efficiency.anchor_valid = false;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, BMS_ONLINE, false);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, PACK_VOLTAGE_VALID, false);
    runtime->snapshot.pack_voltage_mv = 0;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CURRENT_VALID, false);
    runtime->snapshot.current_deci_amps = 0;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, SOC_VALID, false);
    runtime->snapshot.soc_percent = 0;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, MIN_CELL_VALID, false);
    runtime->snapshot.min_cell_voltage_mv = 0;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, AVERAGE_CELL_VALID, false);
    runtime->snapshot.average_cell_voltage_mv = 0;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, MAX_CELL_VALID, false);
    runtime->snapshot.max_cell_voltage_mv = 0;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, DELTA_CELL_VALID, false);
    runtime->snapshot.delta_cell_voltage_mv = 0;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, TOTAL_CAPACITY_VALID, false);
    runtime->snapshot.total_capacity_mah = 0;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, CAPACITY_REMAINING_VALID, false);
    runtime->snapshot.capacity_remaining_mah = 0;
    runtime->snapshot.bms_running_time_seconds = 0U;
    runtime->snapshot.bms_running_time_valid = false;
    runtime->snapshot.bms_cycle_capacity_mah = 0U;
    runtime->snapshot.bms_cycle_capacity_valid = false;
    runtime->snapshot.bms_protection_count = 0;
    memset(runtime->snapshot.bms_protection_codes, 0, sizeof(runtime->snapshot.bms_protection_codes));
    runtime->snapshot.bms_warning_count = 0;
    memset(runtime->snapshot.bms_warning_codes, 0, sizeof(runtime->snapshot.bms_warning_codes));
    runtime->snapshot.bms_safety_supported_mask = 0;
    runtime->snapshot.bms_safety_active_mask = 0;
    for (uint8_t index = 0; index < ESP_BMS_BMS_TEMP_MAX_COUNT; ++index) {
        esp_bms_dashboard_snapshot_temperature_valid_set(&runtime->snapshot, index, false);
    }
    memset(runtime->snapshot.bms_temperature_celsius, 0, sizeof(runtime->snapshot.bms_temperature_celsius));
    runtime_copy_snapshot_text(runtime->snapshot.bms_info_text,
                               sizeof(runtime->snapshot.bms_info_text),
                               "BMS OFF");
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'A' && value <= 'F') {
        return 10 + value - 'A';
    }
    if (value >= 'a' && value <= 'f') {
        return 10 + value - 'a';
    }
    return -1;
}

static char runtime_hex_char(uint8_t value)
{
    return value < 10U ? (char)('0' + value) : (char)('A' + (value - 10U));
}

static bool runtime_normalize_mac_text(const char *input, char *output, size_t output_len)
{
    if (!input || !output || output_len < 18U || strlen(input) != 17U) {
        return false;
    }

    size_t cursor = 0;
    for (size_t index = 0; index < 6U; index++) {
        const size_t base = index * 3U;
        const int high = hex_value(input[base]);
        const int low = hex_value(input[base + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        if (index < 5U && input[base + 2U] != ':') {
            return false;
        }
        if (index > 0) {
            output[cursor++] = ':';
        }
        output[cursor++] = runtime_hex_char((uint8_t)high);
        output[cursor++] = runtime_hex_char((uint8_t)low);
    }
    output[cursor] = '\0';
    return true;
}

static uint32_t runtime_battery_mv_from_raw(uint16_t raw)
{
    const uint64_t pin_mv = (uint64_t)raw * BATTERY_REFERENCE_MV / BATTERY_ADC_MAX;
    const uint64_t divider_total = BATTERY_DIVIDER_TOP_OHMS + BATTERY_DIVIDER_BOTTOM_OHMS;
    const uint64_t battery_mv = pin_mv * divider_total / BATTERY_DIVIDER_BOTTOM_OHMS;
    return battery_mv > UINT32_MAX ? UINT32_MAX : (uint32_t)battery_mv;
}

static uint16_t runtime_speed_deci_units(esp_bms_speed_unit_t unit, uint32_t speed_knots_milli)
{
    uint64_t speed = 0;
    if (unit == ESP_BMS_SPEED_UNIT_MPH) {
        speed = (uint64_t)speed_knots_milli * 11507795U / 1000000000U;
    } else {
        speed = (uint64_t)speed_knots_milli * 1852U / 100000U;
    }
    return speed > UINT16_MAX ? UINT16_MAX : (uint16_t)speed;
}

static void runtime_update_snapshot_speed(esp_bms_idf_runtime_t *runtime)
{
    esp_bms_dashboard_snapshot_t *snapshot = &runtime->snapshot;
    const bool controller_online = runtime->controller_connection_enabled &&
                                   RUNTIME_SNAPSHOT_FLAG(runtime, CONTROLLER_ONLINE);
    const bool gps_available =
        snapshot->gps_module_state == (uint8_t)ESP_BMS_GPS_MODULE_AVAILABLE;
    snapshot->active_speed_source = esp_bms_speed_source_resolve(snapshot->speed_source,
                                                                 gps_available,
                                                                 controller_online);

    const bool speed_valid = snapshot->active_speed_source == ESP_BMS_SPEED_SOURCE_CONTROLLER
                                 ? controller_online && runtime->controller_state.speed_valid
                                 : gps_available &&
                                       RUNTIME_SNAPSHOT_FLAG(runtime, GPS_FIX_VALID);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, SPEED_VALID, speed_valid);
    if (!speed_valid) {
        snapshot->speed_deci_units = 0U;
    } else if (snapshot->active_speed_source == ESP_BMS_SPEED_SOURCE_CONTROLLER) {
        snapshot->speed_deci_units = runtime->controller_state.speed_deci_kmh;
        if (snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH) {
            snapshot->speed_deci_units =
                (uint16_t)(((uint32_t)runtime->controller_state.speed_deci_kmh * 621371U) /
                           1000000U);
        }
    } else {
        snapshot->speed_deci_units = runtime_speed_deci_units(snapshot->speed_unit,
                                                               runtime->gps_speed_knots_milli);
    }

    int32_t metric_consumption_deci_wh_per_km = 0;
    snapshot->average_consumption_valid =
        esp_bms_trip_efficiency_consumption(&runtime->trip_efficiency,
                                            false,
                                            &metric_consumption_deci_wh_per_km);
    if (!snapshot->average_consumption_valid) {
        snapshot->average_consumption_deci_wh_per_distance = 0;
    } else if (snapshot->speed_unit == ESP_BMS_SPEED_UNIT_MPH) {
        (void)esp_bms_trip_efficiency_consumption(
            &runtime->trip_efficiency,
            true,
            &snapshot->average_consumption_deci_wh_per_distance);
    } else {
        snapshot->average_consumption_deci_wh_per_distance =
            metric_consumption_deci_wh_per_km;
    }

    snapshot->remaining_range_valid = esp_bms_remaining_range_km(
        snapshot->preset_range_km,
        RUNTIME_SNAPSHOT_FLAG(runtime, SOC_VALID),
        snapshot->soc_percent,
        snapshot->average_consumption_valid &&
            RUNTIME_SNAPSHOT_FLAG(runtime, PACK_VOLTAGE_VALID) &&
            RUNTIME_SNAPSHOT_FLAG(runtime, CAPACITY_REMAINING_VALID),
        snapshot->pack_voltage_mv,
        snapshot->capacity_remaining_mah,
        metric_consumption_deci_wh_per_km,
        &snapshot->remaining_range_km);
    if (!snapshot->remaining_range_valid) {
        snapshot->remaining_range_km = 0U;
    }
}

#if ESP_BMS_FEATURE_GPS
bool esp_bms_idf_runtime_set_gps_module_state(esp_bms_idf_runtime_t *runtime,
                                              esp_bms_gps_module_state_t state,
                                              const char *reason)
{
    if (!runtime ||
        (uint32_t)state > (uint32_t)ESP_BMS_GPS_MODULE_UNAVAILABLE ||
        runtime->snapshot.gps_module_state == (uint8_t)state) {
        return false;
    }

    runtime->snapshot.gps_module_state = (uint8_t)state;
    if (state == ESP_BMS_GPS_MODULE_UNAVAILABLE) {
        RUNTIME_SET_SNAPSHOT_FLAG(runtime, GPS_FIX_VALID, false);
        runtime->gps_speed_knots_milli = 0U;
        runtime->snapshot.gps_local_time_valid = false;
        runtime->snapshot.gps_local_date_valid = false;
        runtime->snapshot.gps_satellite_info_valid = false;
    }
    runtime_update_snapshot_speed(runtime);

    if (state == ESP_BMS_GPS_MODULE_UNAVAILABLE) {
        ESP_LOGW(TAG, "[gps] module unavailable: reason=%s", reason ? reason : "unknown");
    } else if (state == ESP_BMS_GPS_MODULE_AVAILABLE) {
        ESP_LOGI(TAG, "[gps] module available: evidence=%s", reason ? reason : "protocol");
    } else {
        ESP_LOGI(TAG, "[gps] module probe started");
    }
    return true;
}

bool esp_bms_idf_runtime_publish_gps_sample(esp_bms_idf_runtime_t *runtime,
                                            bool fix_valid,
                                            uint32_t speed_knots_milli)
{
    if (!runtime) {
        return false;
    }

    RUNTIME_SET_SNAPSHOT_FLAG(runtime, GPS_FIX_VALID, fix_valid);
    runtime->gps_speed_knots_milli = speed_knots_milli;
    runtime->snapshot.gps_sentences_seen++;

    const int64_t now_us = esp_timer_get_time();
    const int64_t bms_age_us = now_us - runtime->bms_telemetry_last_us;
    const bool bms_sample_valid = runtime->bms_telemetry_last_us > 0 && bms_age_us >= 0 &&
                                  bms_age_us <= BMS_TELEMETRY_FRESHNESS_US &&
                                  RUNTIME_SNAPSHOT_FLAG(runtime, BMS_ONLINE) &&
                                  RUNTIME_SNAPSHOT_FLAG(runtime, PACK_VOLTAGE_VALID) &&
                                  RUNTIME_SNAPSHOT_FLAG(runtime, CURRENT_VALID);
    esp_bms_trip_efficiency_sample(&runtime->trip_efficiency,
                                   now_us,
                                   fix_valid,
                                   speed_knots_milli,
                                   bms_sample_valid,
                                   runtime->snapshot.pack_voltage_mv,
                                   runtime->snapshot.current_deci_amps);
    runtime_update_snapshot_speed(runtime);
    return true;
}

bool esp_bms_idf_runtime_publish_gps_position(esp_bms_idf_runtime_t *runtime,
                                               bool fix_valid,
                                               int32_t latitude_e7,
                                               int32_t longitude_e7)
{
    if (!runtime || !fix_valid || latitude_e7 < -900000000 || latitude_e7 > 900000000 ||
        longitude_e7 < -1800000000 || longitude_e7 > 1800000000) {
        return false;
    }
    runtime->gps_last_latitude_e7 = latitude_e7;
    runtime->gps_last_longitude_e7 = longitude_e7;
    runtime->gps_last_fix_valid = true;
    const int64_t now_us = esp_timer_get_time();
    const bool locked = runtime->gps_track_lock != NULL;
    if (locked && xSemaphoreTake(runtime->gps_track_lock, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    if (runtime->gps_track_last_sample_us > 0 &&
        now_us - runtime->gps_track_last_sample_us < GPS_TRACK_SAMPLE_INTERVAL_US) {
        if (locked) {
            xSemaphoreGive(runtime->gps_track_lock);
        }
        return false;
    }
    if (!runtime_gps_track_valid(&runtime->gps_track)) {
        runtime_reset_gps_track(&runtime->gps_track);
    }
    if (runtime->gps_track.count == ESP_BMS_GPS_TRACK_MAX_POINTS) {
        memmove(runtime->gps_track.points,
                runtime->gps_track.points + 1U,
                sizeof(runtime->gps_track.points) - sizeof(runtime->gps_track.points[0]));
        runtime->gps_track.count--;
    }
    esp_bms_gps_track_point_t *point =
        &runtime->gps_track.points[runtime->gps_track.count++];
    point->latitude_e7 = latitude_e7;
    point->longitude_e7 = longitude_e7;
    point->timestamp_s = (uint32_t)(now_us / INT64_C(1000000));
    runtime->gps_track_last_sample_us = now_us;
    runtime->gps_track_generation++;
    runtime->gps_track_dirty = true;
    runtime->gps_track_retry_after_us = 0;
    if (locked) {
        xSemaphoreGive(runtime->gps_track_lock);
    }
    return true;
}

void esp_bms_idf_runtime_publish_gps_datetime(esp_bms_idf_runtime_t *runtime,
                                              uint16_t year,
                                              uint8_t month,
                                              uint8_t day,
                                              uint8_t hour,
                                              uint8_t minute,
                                              uint8_t second,
                                              uint64_t utc_epoch_s,
                                              bool valid)
{
    if (!runtime) {
        return;
    }
    runtime->snapshot.gps_local_year = year;
    runtime->snapshot.gps_local_month = month;
    runtime->snapshot.gps_local_day = day;
    runtime->snapshot.gps_local_hour = hour;
    runtime->snapshot.gps_local_minute = minute;
    runtime->snapshot.gps_local_date_valid = valid;
    runtime->snapshot.gps_local_time_valid = valid;
    (void)second;
    runtime->gps_utc_epoch_s = valid ? utc_epoch_s : 0U;
    runtime->gps_utc_valid = valid && utc_epoch_s != 0U;
    if (runtime->gps_utc_valid && runtime->history_session_started &&
        !runtime->history_time_anchored &&
        esp_bms_flashdb_set_session_anchor(runtime->history_session_id,
                                           runtime->history_elapsed_s,
                                           utc_epoch_s) == ESP_OK) {
        runtime->history_time_anchored = true;
    }
}

bool esp_bms_idf_runtime_publish_gps_satellites(esp_bms_idf_runtime_t *runtime,
                                                uint8_t satellites_visible,
                                                uint8_t satellites_used,
                                                uint8_t max_cn0,
                                                uint8_t average_cn0,
                                                uint8_t constellation_mask,
                                                uint8_t fix_dimension,
                                                uint16_t hdop_centi,
                                                bool hdop_valid,
                                                bool valid)
{
    if (!runtime) {
        return false;
    }
    esp_bms_dashboard_snapshot_t *snapshot = &runtime->snapshot;
    const bool changed = snapshot->gps_satellites_visible != satellites_visible ||
                         snapshot->gps_satellites_used != satellites_used ||
                         snapshot->gps_max_cn0 != max_cn0 ||
                         snapshot->gps_average_cn0 != average_cn0 ||
                         snapshot->gps_constellation_mask != constellation_mask ||
                         snapshot->gps_fix_dimension != fix_dimension ||
                         snapshot->gps_hdop_centi != hdop_centi ||
                         snapshot->gps_hdop_valid != hdop_valid ||
                         snapshot->gps_satellite_info_valid != valid;
    snapshot->gps_satellites_visible = satellites_visible;
    snapshot->gps_satellites_used = satellites_used;
    snapshot->gps_max_cn0 = max_cn0;
    snapshot->gps_average_cn0 = average_cn0;
    snapshot->gps_constellation_mask = constellation_mask;
    snapshot->gps_fix_dimension = fix_dimension;
    snapshot->gps_hdop_centi = hdop_centi;
    snapshot->gps_hdop_valid = hdop_valid;
    snapshot->gps_satellite_info_valid = valid;
    return changed;
}

bool esp_bms_idf_runtime_timeout_gps(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || !RUNTIME_SNAPSHOT_FLAG(runtime, GPS_FIX_VALID)) {
        return false;
    }

    RUNTIME_SET_SNAPSHOT_FLAG(runtime, GPS_FIX_VALID, false);
    runtime->gps_speed_knots_milli = 0U;
    runtime->snapshot.gps_local_time_valid = false;
    runtime->snapshot.gps_local_date_valid = false;
    esp_bms_trip_efficiency_sample(&runtime->trip_efficiency,
                                   esp_timer_get_time(),
                                   false,
                                   0U,
                                   false,
                                   0U,
                                   0);
    runtime_update_snapshot_speed(runtime);
    return true;
}
#endif

static esp_bms_idf_display_rotation_t runtime_next_rotation(esp_bms_idf_display_rotation_t rotation)
{
    switch (rotation) {
    case ESP_BMS_IDF_DISPLAY_ROTATION_PORTRAIT:
        return ESP_BMS_IDF_DISPLAY_ROTATION_LANDSCAPE;
    case ESP_BMS_IDF_DISPLAY_ROTATION_LANDSCAPE:
        return ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_PORTRAIT;
    case ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_PORTRAIT:
        return ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_LANDSCAPE;
    case ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_LANDSCAPE:
    default:
        return ESP_BMS_IDF_DISPLAY_ROTATION_PORTRAIT;
    }
}

static const char *runtime_rotation_text(esp_bms_idf_display_rotation_t rotation)
{
    switch (rotation) {
    case ESP_BMS_IDF_DISPLAY_ROTATION_PORTRAIT:
        return "ROT POR";
    case ESP_BMS_IDF_DISPLAY_ROTATION_LANDSCAPE:
        return "ROT LAN";
    case ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_PORTRAIT:
        return "ROT IPOR";
    case ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_LANDSCAPE:
    default:
        return "ROT ILAN";
    }
}

static const char *runtime_rotation_config_text(esp_bms_idf_display_rotation_t rotation)
{
    switch (rotation) {
    case ESP_BMS_IDF_DISPLAY_ROTATION_PORTRAIT:
        return "portrait";
    case ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_PORTRAIT:
        return "inverted_portrait";
    case ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_LANDSCAPE:
        return "inverted_landscape";
    case ESP_BMS_IDF_DISPLAY_ROTATION_LANDSCAPE:
    default:
        return "landscape";
    }
}

static bool runtime_parse_rotation_config_text(const char *text,
                                               esp_bms_idf_display_rotation_t *out_rotation)
{
    if (strcmp(text, "portrait") == 0) {
        *out_rotation = ESP_BMS_IDF_DISPLAY_ROTATION_PORTRAIT;
        return true;
    }
    if (strcmp(text, "landscape") == 0) {
        *out_rotation = ESP_BMS_IDF_DISPLAY_ROTATION_LANDSCAPE;
        return true;
    }
    if (strcmp(text, "inverted_portrait") == 0) {
        *out_rotation = ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_PORTRAIT;
        return true;
    }
    if (strcmp(text, "inverted_landscape") == 0) {
        *out_rotation = ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_LANDSCAPE;
        return true;
    }
    return false;
}

static const char *runtime_speed_unit_config_text(esp_bms_speed_unit_t unit)
{
    return unit == ESP_BMS_SPEED_UNIT_MPH ? "mph" : "km/h";
}

static bool runtime_parse_speed_unit_config_text(const char *text, esp_bms_speed_unit_t *out_unit)
{
    if (strcmp(text, "km/h") == 0) {
        *out_unit = ESP_BMS_SPEED_UNIT_KMH;
        return true;
    }
    if (strcmp(text, "mph") == 0) {
        *out_unit = ESP_BMS_SPEED_UNIT_MPH;
        return true;
    }
    return false;
}

static const char *runtime_speed_source_config_text(esp_bms_speed_source_t source)
{
    return source == ESP_BMS_SPEED_SOURCE_CONTROLLER ? "controller" : "gps";
}

static bool runtime_parse_speed_source_config_text(const char *text,
                                                   esp_bms_speed_source_t *out_source)
{
    if (strcmp(text, "gps") == 0) {
        *out_source = ESP_BMS_SPEED_SOURCE_GPS;
        return true;
    }
    if (strcmp(text, "controller") == 0) {
        *out_source = ESP_BMS_SPEED_SOURCE_CONTROLLER;
        return true;
    }
    return false;
}

static const char *runtime_bms_type_config_text(esp_bms_idf_bms_type_t type)
{
    switch (type) {
    case ESP_BMS_IDF_BMS_TYPE_JK:
        return "jk";
    case ESP_BMS_IDF_BMS_TYPE_JBD:
        return "jbd";
    case ESP_BMS_IDF_BMS_TYPE_DALY:
        return "daly";
    case ESP_BMS_IDF_BMS_TYPE_YANYANG:
        return "yanyang";
    case ESP_BMS_IDF_BMS_TYPE_ANT:
    default:
        return "ant";
    }
}

static bool runtime_parse_bms_type_config_text(const char *text, esp_bms_idf_bms_type_t *out_type)
{
    if (strcmp(text, "ant") == 0) {
        *out_type = ESP_BMS_IDF_BMS_TYPE_ANT;
        return true;
    }
    if (strcmp(text, "jk") == 0) {
        *out_type = ESP_BMS_IDF_BMS_TYPE_JK;
        return true;
    }
    if (strcmp(text, "jbd") == 0) {
        *out_type = ESP_BMS_IDF_BMS_TYPE_JBD;
        return true;
    }
    if (strcmp(text, "daly") == 0) {
        *out_type = ESP_BMS_IDF_BMS_TYPE_DALY;
        return true;
    }
    if (strcmp(text, "yanyang") == 0) {
        *out_type = ESP_BMS_IDF_BMS_TYPE_YANYANG;
        return true;
    }
    return false;
}

static const char *runtime_bms_type_status_text(esp_bms_idf_bms_type_t type)
{
    switch (type) {
    case ESP_BMS_IDF_BMS_TYPE_JK:
        return "BMS JK";
    case ESP_BMS_IDF_BMS_TYPE_JBD:
        return "BMS JBD";
    case ESP_BMS_IDF_BMS_TYPE_DALY:
        return "BMS DALY";
    case ESP_BMS_IDF_BMS_TYPE_YANYANG:
        return "BMS YY";
    case ESP_BMS_IDF_BMS_TYPE_ANT:
    default:
        return "BMS ANT";
    }
}

static const char *runtime_wifi_config_text(esp_bms_wifi_state_t wifi)
{
    switch (wifi) {
    case ESP_BMS_WIFI_OFFLINE:
        return "offline";
    case ESP_BMS_WIFI_SETUP_AP:
    default:
        return "setup";
    }
}

static bool runtime_brightness_matches_policy(uint8_t brightness_percent)
{
    return brightness_percent >= 10U && brightness_percent <= 100U;
}

static bool runtime_set_brightness_percent(esp_bms_idf_runtime_t *runtime, uint8_t brightness_percent)
{
    if (!runtime || !runtime_brightness_matches_policy(brightness_percent)) {
        return false;
    }
    runtime->brightness_percent = brightness_percent;
    runtime->snapshot.brightness_percent = brightness_percent;
    return true;
}

static bool runtime_volume_matches_policy(uint8_t volume_percent)
{
    return volume_percent <= 100U;
}

static bool runtime_set_volume_percent(esp_bms_idf_runtime_t *runtime, uint8_t volume_percent)
{
    if (!runtime || !runtime_volume_matches_policy(volume_percent)) {
        return false;
    }
    runtime->volume_percent = volume_percent;
    runtime->snapshot.volume_percent = volume_percent;
    return true;
}

static bool runtime_rotation_matches_policy(uint8_t rotation)
{
    return rotation <= (uint8_t)ESP_BMS_IDF_DISPLAY_ROTATION_INVERTED_LANDSCAPE;
}

void esp_bms_idf_runtime_set_display_rotation_default(
    esp_bms_idf_runtime_t *runtime,
    esp_bms_idf_display_rotation_t rotation,
    uint8_t version)
{
    if (!runtime || !runtime_rotation_matches_policy((uint8_t)rotation)) {
        return;
    }
    runtime->display_rotation = rotation;
    runtime->display_rotation_default_version = version;
}

static bool runtime_speed_unit_matches_policy(uint8_t speed_unit)
{
    return speed_unit == (uint8_t)ESP_BMS_SPEED_UNIT_KMH ||
           speed_unit == (uint8_t)ESP_BMS_SPEED_UNIT_MPH;
}

static bool runtime_speed_source_matches_policy(uint8_t speed_source)
{
    return speed_source == (uint8_t)ESP_BMS_SPEED_SOURCE_GPS ||
           speed_source == (uint8_t)ESP_BMS_SPEED_SOURCE_CONTROLLER;
}

static bool runtime_speed_dashboard_style_matches_policy(int32_t style)
{
    return esp_bms_display_service_speed_dashboard_style_available(
        (esp_bms_speed_dashboard_style_t)style);
}

static bool runtime_boot_animation_style_matches_policy(int32_t style)
{
    if (style == (int32_t)ESP_BMS_BOOT_ANIMATION_CHARGE ||
        style == (int32_t)ESP_BMS_BOOT_ANIMATION_GAUGE_S1000RR) {
        return true;
    }
#if ESP_BMS_FEATURE_DASHBOARD_FIREBLADE
    return style == (int32_t)ESP_BMS_BOOT_ANIMATION_GAUGE_HONDA_FIREBLADE;
#else
    return false;
#endif
}

static bool runtime_language_matches_policy(uint8_t language)
{
    return language <= 1U;
}

static bool runtime_bms_type_matches_policy(uint8_t bms_type)
{
    return bms_type <= (uint8_t)ESP_BMS_IDF_BMS_TYPE_YANYANG;
}

static bool runtime_bms_supports_capacity_estimate(uint8_t bms_type)
{
    return bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_ANT ||
           bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_JK ||
           bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_DALY ||
           bms_type == (uint8_t)ESP_BMS_IDF_BMS_TYPE_YANYANG;
}

static bool runtime_select_bms_type(esp_bms_idf_runtime_t *runtime, esp_bms_idf_bms_type_t bms_type)
{
    if (!runtime || !runtime_bms_type_matches_policy((uint8_t)bms_type)) {
        return false;
    }
    if (runtime->bms_type == (uint8_t)bms_type) {
        ESP_LOGI(TAG, "[bms] type unchanged: %s", runtime_bms_type_config_text(bms_type));
        return false;
    }

    runtime->bms_type = (uint8_t)bms_type;
    runtime->snapshot.bms_type = runtime->bms_type;
    runtime->snapshot.bms_capacity_estimate_mah = 0U;
    runtime_set_error(runtime, runtime_bms_type_status_text(bms_type));
    if (runtime->bms_ble_driver && runtime->bms_ble_driver->stop) {
        (void)runtime->bms_ble_driver->stop(runtime);
    }
    if (runtime->bms_bound_mac[0] != '\0' && runtime->bms_ble_driver &&
        runtime->bms_ble_driver->start_if_bound) {
        const esp_err_t ret = runtime->bms_ble_driver->start_if_bound(runtime);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "[bms] restart after type change failed: %s", esp_err_to_name(ret));
        }
    }
    ESP_LOGI(TAG, "[bms] type selected: %s", runtime_bms_type_config_text(bms_type));
    return true;
}

static bool runtime_json_write_u32_or_null(char *out, size_t out_len, bool valid, uint32_t value)
{
    const int written = valid ? snprintf(out, out_len, "%lu", (unsigned long)value)
                              : snprintf(out, out_len, "null");
    return written >= 0 && (size_t)written < out_len;
}

static bool runtime_json_write_i32_or_null(char *out, size_t out_len, bool valid, int32_t value)
{
    const int written = valid ? snprintf(out, out_len, "%ld", (long)value)
                              : snprintf(out, out_len, "null");
    return written >= 0 && (size_t)written < out_len;
}

static bool runtime_json_find_field(const char *body, const char *field, const char **out_value)
{
    if (!body || !field || !out_value) return false;
    const char *cursor = body;
    while (*cursor && isspace((unsigned char)*cursor)) cursor++;
    if (*cursor++ != '{') return false;
    for (;;) {
        while (*cursor && isspace((unsigned char)*cursor)) cursor++;
        if (*cursor == '}') return false;
        if (*cursor++ != '"') return false;
        const char *key = cursor;
        while (*cursor && *cursor != '"') {
            if (*cursor == '\\' || (unsigned char)*cursor < 0x20U) return false;
            cursor++;
        }
        const char *key_end = cursor;
        if (*cursor++ != '"') return false;
        while (*cursor && isspace((unsigned char)*cursor)) cursor++;
        if (*cursor++ != ':') return false;
        while (*cursor && isspace((unsigned char)*cursor)) cursor++;
        const char *value = cursor;
        const size_t key_len = (size_t)(key_end - key);
        if (strlen(field) == key_len && strncmp(key, field, key_len) == 0) {
            *out_value = value;
            return true;
        }
        if (*cursor == '"') {
            cursor++;
            while (*cursor && *cursor != '"') {
                if (*cursor == '\\') cursor++;
                if (*cursor) cursor++;
            }
            if (*cursor++ != '"') return false;
        } else if (*cursor == '{' || *cursor == '[') {
            const char open = *cursor++;
            const char close = open == '{' ? '}' : ']';
            unsigned depth = 1U;
            bool in_string = false;
            while (*cursor && depth != 0U) {
                if (*cursor == '\\' && in_string && cursor[1]) { cursor += 2; continue; }
                if (*cursor == '"') in_string = !in_string;
                else if (!in_string && *cursor == open) depth++;
                else if (!in_string && *cursor == close) depth--;
                cursor++;
            }
            if (depth != 0U) return false;
        } else {
            while (*cursor && *cursor != ',' && *cursor != '}') cursor++;
        }
        while (*cursor && isspace((unsigned char)*cursor)) cursor++;
        if (*cursor == ',') { cursor++; continue; }
        return false;
    }
}

static bool runtime_json_get_u8(const char *body, const char *field, uint8_t *out_value, bool *found)
{
    const char *cursor = NULL;
    *found = runtime_json_find_field(body, field, &cursor);
    if (!*found) {
        return true;
    }

    uint32_t value = 0;
    bool seen_digit = false;
    while (*cursor >= '0' && *cursor <= '9') {
        seen_digit = true;
        value = (value * 10U) + (uint32_t)(*cursor - '0');
        if (value > UINT8_MAX) {
            return false;
        }
        cursor++;
    }
    return seen_digit ? ((*out_value = (uint8_t)value), true) : false;
}

static bool runtime_json_get_u32(const char *body, const char *field, uint32_t *out_value, bool *found)
{
    const char *cursor = NULL;
    *found = runtime_json_find_field(body, field, &cursor);
    if (!*found) return true;
    uint64_t value = 0U;
    bool seen_digit = false;
    while (*cursor >= '0' && *cursor <= '9') {
        seen_digit = true;
        value = value * 10U + (uint64_t)(*cursor++ - '0');
        if (value > UINT32_MAX) return false;
    }
    while (*cursor && isspace((unsigned char)*cursor)) cursor++;
    if (!seen_digit || (*cursor != ',' && *cursor != '}')) return false;
    *out_value = (uint32_t)value;
    return true;
}

static bool runtime_json_get_string(const char *body,
                                    const char *field,
                                    char *out_value,
                                    size_t out_len,
                                    bool *found)
{
    const char *cursor = NULL;
    *found = runtime_json_find_field(body, field, &cursor);
    if (!*found) {
        return true;
    }
    if (*cursor != '"' || out_len == 0) {
        return false;
    }
    cursor++;

    size_t len = 0;
    while (*cursor && *cursor != '"') {
        if ((unsigned char)*cursor < 0x20U || len + 1U >= out_len) {
            return false;
        }
        if (*cursor == '\\') {
            cursor++;
            if (*cursor != '/') {
                return false;
            }
        }
        out_value[len++] = *cursor++;
    }
    if (*cursor != '"') {
        return false;
    }
    out_value[len] = '\0';
    return true;
}

static bool runtime_json_get_object(const char *body,
                                    const char *field,
                                    char *out_value,
                                    size_t out_len,
                                    bool *found)
{
    const char *cursor = NULL;
    *found = runtime_json_find_field(body, field, &cursor);
    if (!*found) return true;
    if (*cursor != '{' || out_len == 0U) return false;
    const char *start = cursor;
    unsigned depth = 0U;
    bool in_string = false;
    do {
        if (*cursor == '\0') return false;
        if (*cursor == '\\' && in_string && cursor[1]) { cursor += 2; continue; }
        if (*cursor == '"') in_string = !in_string;
        else if (!in_string && *cursor == '{') depth++;
        else if (!in_string && *cursor == '}') depth--;
        cursor++;
    } while (depth != 0U);
    const size_t len = (size_t)(cursor - start);
    if (len >= out_len) return false;
    memcpy(out_value, start, len);
    out_value[len] = '\0';
    return true;
}

static void runtime_reset_state(esp_bms_idf_runtime_t *runtime)
{
    memset(&runtime->snapshot, 0, sizeof(runtime->snapshot));
    esp_bms_ride_records_reset(&runtime->ride_records);
    runtime_reset_gps_track(&runtime->gps_track);
    (void)snprintf(runtime->snapshot.firmware_version,
                   sizeof(runtime->snapshot.firmware_version),
                   "%s",
                   ESP_BMS_PROFILE_FIRMWARE_VERSION);
    runtime->tick_count = 0;
    runtime->elapsed_ms = 0;
    runtime->battery_sample_elapsed_ms = 0;
    runtime->battery_samples_seen = 0;
    runtime->battery_read_failures = 0;
    runtime->gps_speed_knots_milli = 0;
    runtime->gps_last_latitude_e7 = 0;
    runtime->gps_last_longitude_e7 = 0;
    runtime->gps_last_fix_valid = false;
    runtime->history_sample_elapsed_ms = 0;
    runtime->history_elapsed_s = 0;
    runtime->history_session_id = 0;
    runtime->history_fault_mask = 0;
    runtime->history_session_started = false;
    runtime->bms_telemetry_last_us = 0;
    runtime->bms_status_poll_elapsed_ms = 0;
    runtime->bms_frame_len = 0;
    runtime->bms_conn_handle = 0xFFFFU;
    runtime->bms_service_start_handle = 0;
    runtime->bms_service_end_handle = 0;
    runtime->bms_char_val_handle = 0;
    runtime->bms_cccd_handle = 0;
    runtime->bms_own_addr_type = 0;
    runtime->bluetooth_own_addr_type = 0;
    runtime->bluetooth_conn_handle = 0xFFFFU;
    runtime->controller_conn_handle = 0xFFFFU;
    runtime->controller_service_start_handle = 0;
    runtime->controller_service_end_handle = 0;
    runtime->controller_char_val_handle = 0;
    runtime->controller_write_char_val_handle = 0;
    runtime->controller_cccd_handle = 0;
    runtime->controller_ble_phase = BMS_BLE_PHASE_IDLE;
    runtime->controller_keepalive_elapsed_ms = 0;
    runtime->controller_scan_revision = 0U;
    runtime->ride_records_generation = 0U;
    runtime->ride_records_retry_after_us = 0;
    runtime->ride_records_session_started = false;
    runtime->ride_records_dirty = false;
    runtime->gps_track_generation = 0U;
    runtime->gps_track_retry_after_us = 0;
    runtime->gps_track_dirty = false;
    runtime->gps_track_last_sample_us = 0;
    runtime->capacity_estimate_generation = 0U;
    runtime->capacity_estimate_retry_after_us = 0;
    runtime->capacity_estimate_dirty = false;
    runtime->controller_connection_enabled = false;
    runtime->controller_page_enabled = false;
    runtime->controller_fallback_tire_rim_inch = 0U;
    runtime->controller_fallback_tire_aspect_percent = 0U;
    runtime->controller_fallback_tire_width_mm = 0U;
    runtime->controller_observed_tire_rim_inch = 0U;
    runtime->controller_observed_tire_aspect_percent = 0U;
    runtime->controller_observed_tire_width_mm = 0U;
    runtime->controller_observed_gear_ratio_centi = 0U;
    runtime->active_data_source = ESP_BMS_LVGL_DATA_SOURCE_BMS;
    esp_bms_trip_efficiency_reset(&runtime->trip_efficiency);
    memset(&runtime->controller_state, 0, sizeof(runtime->controller_state));
    runtime->controller_state.fallback_gear_ratio_centi = CONTROLLER_RATIO_CENTI_DEFAULT;
    runtime->bms_ble_phase = BMS_BLE_PHASE_IDLE;
    RUNTIME_SET_FLAG(runtime, BMS_WRITE_IN_FLIGHT, false);
    RUNTIME_SET_FLAG(runtime, BMS_DEVICE_INFO_REQUESTED, false);
    RUNTIME_SET_FLAG(runtime, BMS_DEVICE_INFO_KNOWN, false);
    runtime_copy_snapshot_text(runtime->bluetooth_name,
                               sizeof(runtime->bluetooth_name),
                               LOCAL_BLUETOOTH_NAME);

    runtime->snapshot.speed_unit = ESP_BMS_SPEED_UNIT_KMH;
    runtime->snapshot.speed_source = ESP_BMS_SPEED_SOURCE_GPS;
    runtime->snapshot.active_speed_source = ESP_BMS_SPEED_SOURCE_GPS;
    runtime->snapshot.speed_dashboard_style = esp_bms_display_service_default_speed_dashboard_style();
#if ESP_BMS_FEATURE_GPS
    runtime->snapshot.gps_module_state = (uint8_t)ESP_BMS_GPS_MODULE_PROBING;
#else
    runtime->snapshot.gps_module_state = (uint8_t)ESP_BMS_GPS_MODULE_UNAVAILABLE;
#endif
    runtime->snapshot.boot_animation_style = (uint8_t)ESP_BMS_BOOT_ANIMATION_CHARGE;
    runtime->snapshot.preset_range_km = ESP_BMS_PRESET_RANGE_DEFAULT_KM;
    runtime->snapshot.uptime_seconds = 0U;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, SETUP_AP_ENABLED, false);
    runtime->snapshot.wifi = ESP_BMS_WIFI_OFFLINE;
    runtime->bms_type = (uint8_t)ESP_BMS_IDF_BMS_TYPE_ANT;
    runtime->snapshot.bms_type = runtime->bms_type;
    (void)runtime_set_brightness_percent(runtime, 85U);
    (void)runtime_set_volume_percent(runtime, 65U);
    runtime->display_rotation = ESP_BMS_IDF_DISPLAY_ROTATION_LANDSCAPE;
    RUNTIME_SET_FLAG(runtime, LANGUAGE_ZH, true);
    RUNTIME_SET_FLAG(runtime, BMS_BIND_ACTIVE, false);
    RUNTIME_SET_FLAG(runtime, HTTP_BMS_SCAN_PENDING, false);
    RUNTIME_SET_FLAG(runtime, BMS_SCAN_REQUESTED, false);
    RUNTIME_SET_FLAG(runtime, BMS_SCAN_ACTIVE, false);
    RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, false);
    RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, false);
    RUNTIME_SET_FLAG(runtime, BLUETOOTH_CONNECTED, false);
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_REQUESTED, false);
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SUBSCRIBED, false);
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SETTINGS_SAVE_REQUESTED, false);
    (void)runtime_project_bluetooth_snapshot(runtime);
    esp_bms_idf_runtime_bms_scan_clear_candidates(runtime);
    runtime_clear_bms_telemetry(runtime);
    runtime_update_setup_ap_snapshot(runtime);
    runtime_set_bms_info(runtime, "BMS OFF");
    runtime_update_snapshot_speed(runtime);
    runtime_project_controller_snapshot(runtime);
}

static void runtime_init_battery_adc(esp_bms_idf_runtime_t *runtime)
{
    if (BATTERY_GPIO == GPIO_NUM_NC) {
        ESP_LOGI(TAG, "battery ADC is not configured for this profile");
        return;
    }
    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_6;
    esp_err_t ret = adc_oneshot_io_to_channel(BATTERY_GPIO, &unit, &channel);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "battery ADC GPIO%d is not usable: %s", BATTERY_GPIO, esp_err_to_name(ret));
        return;
    }
    if (unit != ADC_UNIT_1) {
        ESP_LOGW(TAG, "battery ADC GPIO%d resolved to ADC unit %d, expected ADC1", BATTERY_GPIO, unit + 1);
        return;
    }

    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ret = adc_oneshot_new_unit(&unit_config, &runtime->battery_adc);
    if (ret != ESP_OK) {
        runtime->battery_adc = NULL;
        ESP_LOGW(TAG, "battery ADC unit init failed: %s", esp_err_to_name(ret));
        return;
    }

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ret = adc_oneshot_config_channel(runtime->battery_adc, channel, &channel_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "battery ADC channel config failed: %s", esp_err_to_name(ret));
        ESP_ERROR_CHECK_WITHOUT_ABORT(adc_oneshot_del_unit(runtime->battery_adc));
        runtime->battery_adc = NULL;
        return;
    }

    runtime->battery_adc_channel = channel;
    RUNTIME_SET_FLAG(runtime, BATTERY_ADC_READY, true);
    ESP_LOGI(TAG, "battery ADC ready: gpio=%d unit=ADC1 channel=%d", BATTERY_GPIO, channel);
}

static esp_err_t runtime_init_nvs(esp_bms_idf_runtime_t *runtime)
{
    if (runtime && RUNTIME_FLAG(runtime, NVS_READY)) {
        return ESP_OK;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase failed");
        ret = nvs_flash_init();
    }
    if (ret == ESP_ERR_INVALID_STATE) {
        ret = ESP_OK;
    }
    if (ret == ESP_OK && runtime) {
        RUNTIME_SET_FLAG(runtime, NVS_READY, true);
    }
    return ret;
}

static bool runtime_copy_ride_records(esp_bms_idf_runtime_t *runtime,
                                      esp_bms_ride_records_t *records,
                                      bool *session_started,
                                      uint32_t *generation,
                                      bool *dirty,
                                      int64_t *retry_after_us)
{
    if (!runtime || !records) {
        return false;
    }

    const bool locked = runtime->ride_records_lock != NULL;
    if (locked && xSemaphoreTake(runtime->ride_records_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    memcpy(records, &runtime->ride_records, sizeof(*records));
    if (session_started) {
        *session_started = runtime->ride_records_session_started;
    }
    if (generation) {
        *generation = runtime->ride_records_generation;
    }
    if (dirty) {
        *dirty = runtime->ride_records_dirty;
    }
    if (retry_after_us) {
        *retry_after_us = runtime->ride_records_retry_after_us;
    }
    if (locked) {
        xSemaphoreGive(runtime->ride_records_lock);
    }
    return true;
}

static esp_err_t runtime_load_ride_records(esp_bms_idf_runtime_t *runtime)
{
    ESP_RETURN_ON_FALSE(runtime, ESP_ERR_INVALID_ARG, TAG, "runtime is required");
    ESP_RETURN_ON_ERROR(runtime_init_nvs(runtime), TAG, "NVS init failed");

    esp_bms_ride_records_t loaded;
    esp_bms_ride_records_reset(&loaded);
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(SETUP_AP_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    size_t blob_len = 0U;
    ret = nvs_get_blob(handle, RIDE_RECORDS_NVS_KEY, NULL, &blob_len);
    if (ret == ESP_OK && blob_len == sizeof(loaded)) {
        ret = nvs_get_blob(handle, RIDE_RECORDS_NVS_KEY, &loaded, &blob_len);
    }
    nvs_close(handle);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }
    if (blob_len != sizeof(loaded) || !esp_bms_ride_records_valid(&loaded)) {
        ESP_LOGW(TAG, "[bms] ignored invalid ride record history");
        return ESP_OK;
    }

    runtime->ride_records = loaded;
    return ESP_OK;
}

static esp_err_t runtime_save_ride_records(esp_bms_idf_runtime_t *runtime,
                                           const esp_bms_ride_records_t *records)
{
    ESP_RETURN_ON_FALSE(runtime && records && esp_bms_ride_records_valid(records),
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "invalid ride record history");
    ESP_RETURN_ON_ERROR(runtime_init_nvs(runtime), TAG, "NVS init failed");

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(SETUP_AP_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_set_blob(handle, RIDE_RECORDS_NVS_KEY, records, sizeof(*records));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

bool esp_bms_idf_runtime_record_bms_sample(esp_bms_idf_runtime_t *runtime,
                                            const esp_bms_ride_record_sample_t *sample)
{
    if (!runtime || !sample || !sample->valid) {
        return false;
    }

    const bool locked = runtime->ride_records_lock != NULL;
    if (locked && xSemaphoreTake(runtime->ride_records_lock, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    const bool changed = esp_bms_ride_records_apply(&runtime->ride_records,
                                                     &runtime->ride_records_session_started,
                                                     sample);
    if (changed) {
        runtime->ride_records_dirty = true;
        runtime->ride_records_generation++;
        runtime->ride_records_retry_after_us = 0;
    }
    if (locked) {
        xSemaphoreGive(runtime->ride_records_lock);
    }
    return changed;
}

static void runtime_persist_ride_records(esp_bms_idf_runtime_t *runtime)
{
    const int64_t now_us = esp_timer_get_time();
    esp_bms_ride_records_t records;
    uint32_t generation = 0U;
    bool dirty = false;
    int64_t retry_after_us = 0;
    if (!runtime_copy_ride_records(runtime,
                                   &records,
                                   NULL,
                                   &generation,
                                   &dirty,
                                   &retry_after_us) ||
        !dirty || now_us < retry_after_us) {
        return;
    }

    const esp_err_t ret = runtime_save_ride_records(runtime, &records);
    const bool locked = runtime->ride_records_lock != NULL;
    if (locked && xSemaphoreTake(runtime->ride_records_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    if (ret == ESP_OK) {
        if (runtime->ride_records_generation == generation) {
            runtime->ride_records_dirty = false;
        }
    } else if (runtime->ride_records_generation == generation) {
        runtime->ride_records_retry_after_us = now_us + RIDE_RECORDS_PERSIST_RETRY_US;
    }
    if (locked) {
        xSemaphoreGive(runtime->ride_records_lock);
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "[bms] ride record save failed: %s", esp_err_to_name(ret));
    }
}

static void runtime_reset_gps_track(esp_bms_gps_track_t *track)
{
    if (!track) {
        return;
    }
    memset(track, 0, sizeof(*track));
    track->format_version = ESP_BMS_GPS_TRACK_FORMAT_VERSION;
}

static bool runtime_gps_track_valid(const esp_bms_gps_track_t *track)
{
    return track && track->format_version == ESP_BMS_GPS_TRACK_FORMAT_VERSION &&
           track->count <= ESP_BMS_GPS_TRACK_MAX_POINTS;
}

static bool runtime_copy_gps_track(esp_bms_idf_runtime_t *runtime,
                                   esp_bms_gps_track_t *track,
                                   uint32_t *generation,
                                   bool *dirty,
                                   int64_t *retry_after_us)
{
    if (!runtime || !track) {
        return false;
    }
    const bool locked = runtime->gps_track_lock != NULL;
    if (locked && xSemaphoreTake(runtime->gps_track_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    memcpy(track, &runtime->gps_track, sizeof(*track));
    if (generation) {
        *generation = runtime->gps_track_generation;
    }
    if (dirty) {
        *dirty = runtime->gps_track_dirty;
    }
    if (retry_after_us) {
        *retry_after_us = runtime->gps_track_retry_after_us;
    }
    if (locked) {
        xSemaphoreGive(runtime->gps_track_lock);
    }
    return true;
}

static esp_err_t runtime_load_gps_track(esp_bms_idf_runtime_t *runtime)
{
    ESP_RETURN_ON_FALSE(runtime, ESP_ERR_INVALID_ARG, TAG, "runtime is required");
    ESP_RETURN_ON_ERROR(runtime_init_nvs(runtime), TAG, "NVS init failed");

    esp_bms_gps_track_t loaded;
    runtime_reset_gps_track(&loaded);
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(SETUP_AP_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    size_t blob_len = 0U;
    ret = nvs_get_blob(handle, GPS_TRACK_NVS_KEY, NULL, &blob_len);
    if (ret == ESP_OK && blob_len == sizeof(loaded)) {
        ret = nvs_get_blob(handle, GPS_TRACK_NVS_KEY, &loaded, &blob_len);
    }
    nvs_close(handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }
    if (blob_len != sizeof(loaded) || !runtime_gps_track_valid(&loaded)) {
        ESP_LOGW(TAG, "[gps] ignored invalid track history");
        return ESP_OK;
    }
    runtime->gps_track = loaded;
    return ESP_OK;
}

static esp_err_t runtime_migrate_gps_track(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || !esp_bms_flashdb_ready() || runtime->gps_track.count == 0U) return ESP_OK;
    nvs_handle_t handle = 0;
    ESP_RETURN_ON_ERROR(nvs_open(SETUP_AP_NVS_NAMESPACE, NVS_READWRITE, &handle),
                        TAG, "GPS migration NVS open failed");
    uint8_t state = 0U;
    uint64_t session_id = 0U;
    (void)nvs_get_u8(handle, GPS_TRACK_MIGRATION_NVS_KEY, &state);
    (void)nvs_get_u64(handle, GPS_TRACK_MIGRATION_SESSION_NVS_KEY, &session_id);
    if (state == 2U) {
        esp_err_t ret = nvs_erase_key(handle, GPS_TRACK_NVS_KEY);
        if (ret == ESP_ERR_NVS_NOT_FOUND) ret = ESP_OK;
        if (ret == ESP_OK) ret = nvs_commit(handle);
        nvs_close(handle);
        return ret;
    }

    size_t imported = 0U;
    esp_err_t ret = state == 1U
                        ? esp_bms_flashdb_resume_session(session_id, &imported)
                        : ESP_ERR_NOT_FOUND;
    if (ret == ESP_ERR_NOT_FOUND) {
        ret = esp_bms_flashdb_start_session(&session_id);
        if (ret == ESP_OK) {
            state = 1U;
            ret = nvs_set_u8(handle, GPS_TRACK_MIGRATION_NVS_KEY, state);
        }
        if (ret == ESP_OK) ret = nvs_set_u64(handle, GPS_TRACK_MIGRATION_SESSION_NVS_KEY, session_id);
        if (ret == ESP_OK) ret = nvs_commit(handle);
        imported = 0U;
    }
    if (ret != ESP_OK || imported > runtime->gps_track.count) {
        nvs_close(handle);
        return ret != ESP_OK ? ret : ESP_ERR_INVALID_SIZE;
    }

    const uint32_t first_timestamp = runtime->gps_track.points[0].timestamp_s;
    uint32_t previous_elapsed = 0U;
    for (size_t i = 0; i < runtime->gps_track.count; ++i) {
        const esp_bms_gps_track_point_t *point = &runtime->gps_track.points[i];
        uint32_t elapsed = point->timestamp_s >= first_timestamp
                               ? point->timestamp_s - first_timestamp
                               : (uint32_t)i;
        if (i > 0U && elapsed <= previous_elapsed) elapsed = previous_elapsed + 1U;
        previous_elapsed = elapsed;
        if (i < imported) continue;
        esp_bms_flashdb_sample_t sample = {
            .version = ESP_BMS_FLASHDB_SAMPLE_VERSION,
            .flags = ESP_BMS_FLASHDB_FLAG_GPS_VALID,
            .elapsed_s = elapsed > UINT16_MAX ? UINT16_MAX : (uint16_t)elapsed,
            .latitude_e7 = point->latitude_e7,
            .longitude_e7 = point->longitude_e7,
        };
        ret = esp_bms_flashdb_append_sample((session_id << 32) | elapsed, &sample);
        if (ret != ESP_OK) {
            nvs_close(handle);
            return ret;
        }
    }

    ret = nvs_set_u8(handle, GPS_TRACK_MIGRATION_NVS_KEY, 2U);
    if (ret == ESP_OK) ret = nvs_commit(handle);
    if (ret == ESP_OK) ret = nvs_erase_key(handle, GPS_TRACK_NVS_KEY);
    if (ret == ESP_ERR_NVS_NOT_FOUND) ret = ESP_OK;
    if (ret == ESP_OK) ret = nvs_commit(handle);
    nvs_close(handle);
    if (ret == ESP_OK) ESP_LOGI(TAG, "[history] migrated %u legacy GPS points", (unsigned)imported);
    return ret;
}

typedef struct {
    uint32_t magic;
    uint32_t estimate_mah;
    uint32_t last_accepted_cycle_mah;
    uint32_t sample_history_mah[ESP_BMS_CAPACITY_ESTIMATE_HISTORY_MAX_COUNT];
    char bms_mac[18];
    uint8_t version;
    uint8_t bms_type;
    uint8_t sample_count;
    uint8_t next_sample_index;
    uint8_t ready;
} runtime_capacity_estimate_blob_t;

_Static_assert(sizeof(runtime_capacity_estimate_blob_t) <= CAPACITY_ESTIMATE_BLOB_MAX_BYTES,
               "capacity estimate history must remain compact");

static bool runtime_capacity_estimate_blob_valid(const runtime_capacity_estimate_blob_t *blob)
{
    if (!blob || blob->magic != CAPACITY_ESTIMATE_MAGIC ||
        blob->version != CAPACITY_ESTIMATE_VERSION ||
        !runtime_bms_supports_capacity_estimate(blob->bms_type) ||
        blob->bms_mac[sizeof(blob->bms_mac) - 1U] != '\0' ||
        blob->sample_count > ESP_BMS_CAPACITY_ESTIMATE_HISTORY_MAX_COUNT ||
        blob->next_sample_index >= ESP_BMS_CAPACITY_ESTIMATE_HISTORY_MAX_COUNT ||
        blob->ready > 1U ||
        (blob->sample_count < ESP_BMS_CAPACITY_ESTIMATE_HISTORY_MAX_COUNT &&
         blob->next_sample_index != blob->sample_count)) {
        return false;
    }

    uint64_t total_mah = 0U;
    for (uint8_t index = 0U; index < blob->sample_count; ++index) {
        if (blob->sample_history_mah[index] == 0U) {
            return false;
        }
        total_mah += blob->sample_history_mah[index];
    }
    const uint32_t expected_estimate_mah = blob->sample_count == 0U ? 0U :
        (uint32_t)(total_mah / blob->sample_count);
    return blob->estimate_mah == expected_estimate_mah &&
           blob->ready == (blob->sample_count >= ESP_BMS_CAPACITY_ESTIMATE_READY_SAMPLE_COUNT ?
                               1U :
                               0U);
}

static void runtime_capacity_estimate_blob_from_runtime(
    const esp_bms_idf_runtime_t *runtime,
    runtime_capacity_estimate_blob_t *blob)
{
    memset(blob, 0, sizeof(*blob));
    blob->magic = CAPACITY_ESTIMATE_MAGIC;
    blob->version = CAPACITY_ESTIMATE_VERSION;
    blob->bms_type = runtime->capacity_estimate_bms_type;
    blob->estimate_mah = runtime->capacity_estimate.estimate_mah;
    blob->last_accepted_cycle_mah = runtime->capacity_estimate.last_accepted_cycle_mah;
    memcpy(blob->sample_history_mah,
           runtime->capacity_estimate.sample_history_mah,
           sizeof(blob->sample_history_mah));
    blob->sample_count = runtime->capacity_estimate.sample_count;
    blob->next_sample_index = runtime->capacity_estimate.next_sample_index;
    blob->ready = runtime->capacity_estimate.ready ? 1U : 0U;
    memcpy(blob->bms_mac, runtime->capacity_estimate_bms_mac, sizeof(blob->bms_mac));
}

static esp_err_t runtime_load_capacity_estimate(esp_bms_idf_runtime_t *runtime)
{
    ESP_RETURN_ON_FALSE(runtime, ESP_ERR_INVALID_ARG, TAG, "runtime is required");
    ESP_RETURN_ON_ERROR(runtime_init_nvs(runtime), TAG, "NVS init failed");

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(SETUP_AP_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    runtime_capacity_estimate_blob_t blob = { 0 };
    size_t blob_len = sizeof(blob);
    ret = nvs_get_blob(handle, CAPACITY_ESTIMATE_NVS_KEY, &blob, &blob_len);
    nvs_close(handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }
    if (blob_len != sizeof(blob) || !runtime_capacity_estimate_blob_valid(&blob)) {
        ESP_LOGW(TAG, "[bms] ignored invalid capacity estimate history");
        return ESP_OK;
    }

    esp_bms_capacity_estimate_reset(&runtime->capacity_estimate);
    runtime->capacity_estimate.estimate_mah = blob.estimate_mah;
    runtime->capacity_estimate.last_accepted_cycle_mah = blob.last_accepted_cycle_mah;
    memcpy(runtime->capacity_estimate.sample_history_mah,
           blob.sample_history_mah,
           sizeof(runtime->capacity_estimate.sample_history_mah));
    runtime->capacity_estimate.sample_count = blob.sample_count;
    runtime->capacity_estimate.next_sample_index = blob.next_sample_index;
    runtime->capacity_estimate.ready = blob.ready == 1U;
    runtime->capacity_estimate_bms_type = blob.bms_type;
    memcpy(runtime->capacity_estimate_bms_mac,
           blob.bms_mac,
           sizeof(runtime->capacity_estimate_bms_mac));
    return ESP_OK;
}

static esp_err_t runtime_save_capacity_estimate(esp_bms_idf_runtime_t *runtime,
                                                const runtime_capacity_estimate_blob_t *blob)
{
    ESP_RETURN_ON_FALSE(runtime && runtime_capacity_estimate_blob_valid(blob),
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "invalid capacity estimate history");
    ESP_RETURN_ON_ERROR(runtime_init_nvs(runtime), TAG, "NVS init failed");

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(SETUP_AP_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_set_blob(handle, CAPACITY_ESTIMATE_NVS_KEY, blob, sizeof(*blob));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static bool runtime_capacity_estimate_identity_matches(const esp_bms_idf_runtime_t *runtime)
{
    return runtime_bms_supports_capacity_estimate(runtime->bms_type) &&
           runtime->bms_bound_mac[0] != '\0' &&
           runtime->capacity_estimate_bms_type == runtime->bms_type &&
           strcmp(runtime->capacity_estimate_bms_mac, runtime->bms_bound_mac) == 0;
}

static bool runtime_capacity_estimate_reset_identity(esp_bms_idf_runtime_t *runtime)
{
    if (runtime_capacity_estimate_identity_matches(runtime)) {
        return false;
    }
    esp_bms_capacity_estimate_reset(&runtime->capacity_estimate);
    esp_bms_capacity_integrator_reset(&runtime->capacity_integrator, 0U);
    runtime->capacity_estimate_bms_type = runtime->bms_type;
    runtime_copy_snapshot_text(runtime->capacity_estimate_bms_mac,
                               sizeof(runtime->capacity_estimate_bms_mac),
                               runtime->bms_bound_mac);
    return true;
}

static bool runtime_observe_bms_capacity_locked(esp_bms_idf_runtime_t *runtime,
                                                bool reset_anchor,
                                                uint32_t total_cycle_mah,
                                                uint16_t soc_percent,
                                                bool changed)
{
    if (reset_anchor) {
        esp_bms_capacity_estimate_reset_anchor(&runtime->capacity_estimate);
    }
    const esp_bms_capacity_estimate_result_t result =
        esp_bms_capacity_estimate_observe(&runtime->capacity_estimate,
                                          total_cycle_mah,
                                          soc_percent);
    changed = changed || result == ESP_BMS_CAPACITY_ESTIMATE_UPDATED ||
              result == ESP_BMS_CAPACITY_ESTIMATE_CLEARED;
    if (changed) {
        runtime->capacity_estimate_dirty = true;
        runtime->capacity_estimate_generation++;
        runtime->capacity_estimate_retry_after_us = 0;
    }
    runtime->snapshot.bms_capacity_estimate_mah = runtime->capacity_estimate.ready
                                                       ? runtime->capacity_estimate.estimate_mah
                                                       : 0U;
    xSemaphoreGive(runtime->capacity_estimate_lock);
    return changed;
}

bool esp_bms_idf_runtime_observe_bms_capacity(esp_bms_idf_runtime_t *runtime,
                                               bool new_connection,
                                               uint32_t total_cycle_mah,
                                               uint16_t soc_percent)
{
    if (!runtime || !runtime_bms_supports_capacity_estimate(runtime->bms_type) ||
        runtime->bms_bound_mac[0] == '\0' || !runtime->capacity_estimate_lock ||
        xSemaphoreTake(runtime->capacity_estimate_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    return runtime_observe_bms_capacity_locked(runtime,
                                               new_connection,
                                               total_cycle_mah,
                                               soc_percent,
                                               runtime_capacity_estimate_reset_identity(runtime));
}

bool esp_bms_idf_runtime_observe_bms_capacity_from_current(esp_bms_idf_runtime_t *runtime,
                                                            bool new_connection,
                                                            int16_t current_deci_amps,
                                                            uint16_t soc_percent)
{
    if (!runtime || !runtime_bms_supports_capacity_estimate(runtime->bms_type) ||
        runtime->bms_bound_mac[0] == '\0' || soc_percent > 100U ||
        !runtime->capacity_estimate_lock ||
        xSemaphoreTake(runtime->capacity_estimate_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    const bool changed = runtime_capacity_estimate_reset_identity(runtime);
    if (!runtime->capacity_integrator.initialized) {
        esp_bms_capacity_integrator_reset(&runtime->capacity_integrator,
                                          runtime->capacity_estimate.last_accepted_cycle_mah);
    }
    if (new_connection) {
        esp_bms_capacity_integrator_reset_anchor(&runtime->capacity_integrator);
    }
    const esp_bms_capacity_integrator_result_t integration =
        esp_bms_capacity_integrator_observe(&runtime->capacity_integrator,
                                            esp_timer_get_time(),
                                            current_deci_amps);
    return runtime_observe_bms_capacity_locked(
        runtime,
        new_connection || integration == ESP_BMS_CAPACITY_INTEGRATOR_REANCHORED ||
            integration == ESP_BMS_CAPACITY_INTEGRATOR_DISCONTINUITY,
        runtime->capacity_integrator.total_cycle_mah,
        soc_percent,
        changed);
}

static void runtime_persist_capacity_estimate(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || !runtime->capacity_estimate_lock) {
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    runtime_capacity_estimate_blob_t blob = { 0 };
    uint32_t generation = 0U;
    if (xSemaphoreTake(runtime->capacity_estimate_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    const bool should_save = runtime->capacity_estimate_dirty &&
                             now_us >= runtime->capacity_estimate_retry_after_us;
    if (should_save) {
        runtime_capacity_estimate_blob_from_runtime(runtime, &blob);
        generation = runtime->capacity_estimate_generation;
    }
    xSemaphoreGive(runtime->capacity_estimate_lock);
    if (!should_save) {
        return;
    }

    const esp_err_t ret = runtime_save_capacity_estimate(runtime, &blob);
    if (xSemaphoreTake(runtime->capacity_estimate_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    if (runtime->capacity_estimate_generation == generation) {
        if (ret == ESP_OK) {
            runtime->capacity_estimate_dirty = false;
        } else {
            runtime->capacity_estimate_retry_after_us = now_us + CAPACITY_ESTIMATE_PERSIST_RETRY_US;
        }
    }
    xSemaphoreGive(runtime->capacity_estimate_lock);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "[bms] capacity estimate save failed: %s", esp_err_to_name(ret));
    }
}

static esp_err_t runtime_nvs_get_optional_u8(nvs_handle_t handle, const char *key, uint8_t *value)
{
    const esp_err_t ret = nvs_get_u8(handle, key, value);
    return ret == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : ret;
}

static esp_err_t runtime_nvs_get_optional_u16(nvs_handle_t handle, const char *key, uint16_t *value)
{
    const esp_err_t ret = nvs_get_u16(handle, key, value);
    return ret == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : ret;
}

static esp_err_t runtime_nvs_get_optional_string(nvs_handle_t handle,
                                                 const char *key,
                                                 char *value,
                                                 size_t value_len)
{
    size_t len = value_len;
    const esp_err_t ret = nvs_get_str(handle, key, value, &len);
    return ret == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : ret;
}

esp_err_t esp_bms_idf_runtime_load_display_settings(esp_bms_idf_runtime_t *runtime, bool *loaded)
{
    ESP_RETURN_ON_FALSE(runtime, ESP_ERR_INVALID_ARG, TAG, "runtime is required");
    ESP_RETURN_ON_FALSE(loaded, ESP_ERR_INVALID_ARG, TAG, "loaded output is required");
    *loaded = false;

    ESP_RETURN_ON_ERROR(runtime_init_nvs(runtime), TAG, "NVS init failed");

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(SETUP_AP_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t brightness_percent = 0;
    uint8_t volume_percent = 0;
    uint8_t rotation = 0;
    uint8_t rotation_default_version = 0;
    uint8_t speed_unit = 0;
    uint8_t speed_source = (uint8_t)ESP_BMS_SPEED_SOURCE_GPS;
    uint8_t speed_dashboard_style =
        (uint8_t)esp_bms_display_service_default_speed_dashboard_style();
    uint8_t boot_animation_style = (uint8_t)ESP_BMS_BOOT_ANIMATION_CHARGE;
    uint8_t language = 0;
    uint8_t bms_type = (uint8_t)ESP_BMS_IDF_BMS_TYPE_ANT;
    uint8_t controller_connection_enabled = 0;
    uint8_t legacy_controller_page_enabled = 0;
    uint8_t controller_tire_rim_inch = 0;
    uint8_t controller_tire_aspect_percent = 0;
    uint16_t controller_tire_width_mm = 0;
    uint16_t controller_wheel_mm = 0;
    uint16_t controller_ratio_centi = 0;
    uint16_t preset_range_km = ESP_BMS_PRESET_RANGE_DEFAULT_KM;
    bool speed_source_migration_needed = false;
    bool rotation_migration_needed = false;
    bool preset_range_migration_needed = false;
    bool dashboard_style_migration_needed = false;

    ret = nvs_get_u8(handle, DISPLAY_NVS_BRIGHTNESS_KEY, &brightness_percent);
    if (ret == ESP_OK) {
        const esp_err_t volume_ret = nvs_get_u8(handle, DISPLAY_NVS_VOLUME_KEY, &volume_percent);
        if (volume_ret == ESP_ERR_NVS_NOT_FOUND) {
            volume_percent = runtime->volume_percent;
        } else {
            ret = volume_ret;
        }
    }
    if (ret == ESP_OK) {
        ret = nvs_get_u8(handle, DISPLAY_NVS_ROTATION_KEY, &rotation);
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_u8(handle,
                                          DISPLAY_NVS_ROTATION_DEFAULT_VERSION_KEY,
                                          &rotation_default_version);
    }
    if (ret == ESP_OK) {
        ret = nvs_get_u8(handle, DISPLAY_NVS_SPEED_UNIT_KEY, &speed_unit);
    }
    if (ret == ESP_OK) {
        ret = nvs_get_u8(handle, DISPLAY_NVS_LANGUAGE_KEY, &language);
    }
    if (ret == ESP_OK) {
        const esp_err_t bms_type_ret = nvs_get_u8(handle, DISPLAY_NVS_BMS_TYPE_KEY, &bms_type);
        if (bms_type_ret != ESP_ERR_NVS_NOT_FOUND) {
            ret = bms_type_ret;
        }
    }
    if (ret == ESP_OK) {
        const esp_err_t preset_range_ret = nvs_get_u16(handle,
                                                       DISPLAY_NVS_PRESET_RANGE_KEY,
                                                       &preset_range_km);
        if (preset_range_ret == ESP_ERR_NVS_NOT_FOUND) {
            preset_range_migration_needed = true;
        } else {
            ret = preset_range_ret;
        }
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_u8(handle, CONTROLLER_NVS_CONNECTION_KEY,
                                          &controller_connection_enabled);
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_u8(handle, CONTROLLER_NVS_PAGE_KEY,
                                          &legacy_controller_page_enabled);
    }
    if (ret == ESP_OK) {
        const esp_err_t source_ret = nvs_get_u8(handle,
                                                DISPLAY_NVS_SPEED_SOURCE_KEY,
                                                &speed_source);
        if (source_ret == ESP_ERR_NVS_NOT_FOUND) {
            speed_source = legacy_controller_page_enabled != 0U
                               ? (uint8_t)ESP_BMS_SPEED_SOURCE_CONTROLLER
                               : (uint8_t)ESP_BMS_SPEED_SOURCE_GPS;
            speed_source_migration_needed = true;
        } else {
            ret = source_ret;
        }
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_u8(handle,
                                          DISPLAY_NVS_SPEED_STYLE_KEY,
                                          &speed_dashboard_style);
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_u8(handle,
                                          DISPLAY_NVS_BOOT_ANIMATION_KEY,
                                          &boot_animation_style);
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_u16(handle, CONTROLLER_NVS_WHEEL_KEY,
                                           &controller_wheel_mm);
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_u16(handle, CONTROLLER_NVS_RATIO_KEY,
                                           &controller_ratio_centi);
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_u8(handle, CONTROLLER_NVS_RIM_KEY,
                                          &controller_tire_rim_inch);
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_u8(handle, CONTROLLER_NVS_ASPECT_KEY,
                                          &controller_tire_aspect_percent);
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_u16(handle, CONTROLLER_NVS_WIDTH_KEY,
                                           &controller_tire_width_mm);
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_string(handle, CONTROLLER_NVS_BOUND_MAC_KEY,
                                              runtime->controller_bound_mac,
                                              sizeof(runtime->controller_bound_mac));
    }
    if (ret == ESP_OK) {
        ret = runtime_nvs_get_optional_string(handle, CONTROLLER_NVS_BOUND_NAME_KEY,
                                              runtime->controller_bound_name,
                                              sizeof(runtime->controller_bound_name));
    }
    nvs_close(handle);
    if (ret != ESP_OK) {
        return ret;
    }

    if (!runtime_speed_dashboard_style_matches_policy(speed_dashboard_style)) {
        ESP_LOGW(TAG,
                 "[display] saved dashboard style %u is unavailable; using the configured default",
                 speed_dashboard_style);
        speed_dashboard_style = (uint8_t)esp_bms_display_service_default_speed_dashboard_style();
        dashboard_style_migration_needed = true;
    }

    if (rotation_default_version < runtime->display_rotation_default_version) {
        rotation = (uint8_t)runtime->display_rotation;
        rotation_migration_needed = true;
    }

    if (!runtime_brightness_matches_policy(brightness_percent) ||
        !runtime_volume_matches_policy(volume_percent) ||
        !runtime_rotation_matches_policy(rotation) ||
        !runtime_speed_unit_matches_policy(speed_unit) ||
        !runtime_speed_source_matches_policy(speed_source) ||
        !runtime_language_matches_policy(language) ||
        !runtime_bms_type_matches_policy(bms_type) ||
        preset_range_km > ESP_BMS_REMAINING_RANGE_MAX_KM ||
        controller_connection_enabled > 1U || legacy_controller_page_enabled > 1U ||
        !runtime_boot_animation_style_matches_policy(boot_animation_style)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (controller_wheel_mm != 0U &&
        (controller_wheel_mm < 500U || controller_wheel_mm > 4000U)) {
        ESP_LOGW(TAG, "[controller] ignored invalid legacy circumference: %u",
                 controller_wheel_mm);
        controller_wheel_mm = 0U;
    }
    if (!runtime_controller_ratio_matches_policy(controller_ratio_centi)) {
        if (controller_ratio_centi != 0U) {
            ESP_LOGW(TAG, "[controller] ignored invalid saved ratio: %u", controller_ratio_centi);
        }
        controller_ratio_centi = CONTROLLER_RATIO_CENTI_DEFAULT;
    }
    const bool tire_fields_present = controller_tire_rim_inch != 0U ||
                                     controller_tire_aspect_percent != 0U ||
                                     controller_tire_width_mm != 0U;
    if (runtime_controller_tire_matches_policy(controller_tire_rim_inch,
                                               controller_tire_aspect_percent,
                                               controller_tire_width_mm)) {
        uint16_t calculated_wheel_mm = 0U;
        if (esp_fardriver_tire_circumference_mm(controller_tire_rim_inch,
                                                controller_tire_aspect_percent,
                                                controller_tire_width_mm,
                                                &calculated_wheel_mm)) {
            controller_wheel_mm = calculated_wheel_mm;
        }
    } else {
        if (tire_fields_present) {
            ESP_LOGW(TAG,
                     "[controller] ignored incomplete/invalid saved tire: %u-%u-%u",
                     controller_tire_rim_inch,
                     controller_tire_aspect_percent,
                     controller_tire_width_mm);
        }
        controller_tire_rim_inch = 0U;
        controller_tire_aspect_percent = 0U;
        controller_tire_width_mm = 0U;
    }

    (void)runtime_set_brightness_percent(runtime, brightness_percent);
    (void)runtime_set_volume_percent(runtime, volume_percent);
    runtime->display_rotation = (esp_bms_idf_display_rotation_t)rotation;
    runtime->snapshot.speed_unit = (esp_bms_speed_unit_t)speed_unit;
    runtime->snapshot.speed_source = (esp_bms_speed_source_t)speed_source;
    runtime->snapshot.speed_dashboard_style =
        (esp_bms_speed_dashboard_style_t)speed_dashboard_style;
    runtime->snapshot.boot_animation_style = boot_animation_style;
    runtime->snapshot.preset_range_km = preset_range_km;
    RUNTIME_SET_FLAG(runtime, LANGUAGE_ZH, language != 0U);
    runtime->bms_type = bms_type;
    runtime->snapshot.bms_type = runtime->bms_type;
    runtime->controller_page_enabled =
        runtime->snapshot.speed_dashboard_style == ESP_BMS_SPEED_DASHBOARD_STYLE_CONTROLLER;
    runtime->controller_connection_enabled = controller_connection_enabled != 0U;
    runtime->controller_fallback_tire_rim_inch = controller_tire_rim_inch;
    runtime->controller_fallback_tire_aspect_percent = controller_tire_aspect_percent;
    runtime->controller_fallback_tire_width_mm = controller_tire_width_mm;
    runtime->controller_state.fallback_wheel_circumference_mm = controller_wheel_mm;
    runtime->controller_state.fallback_gear_ratio_centi = controller_ratio_centi;
    runtime_project_controller_snapshot(runtime);
    runtime_update_snapshot_speed(runtime);
    *loaded = true;
    if (rotation_migration_needed || speed_source_migration_needed || preset_range_migration_needed ||
        dashboard_style_migration_needed) {
        const esp_err_t migration_ret = esp_bms_idf_runtime_save_display_settings(runtime);
        if (migration_ret != ESP_OK) {
            ESP_LOGW(TAG, "[settings] migration save failed: %s",
                     esp_err_to_name(migration_ret));
        }
    }
    return ESP_OK;
}

esp_err_t esp_bms_idf_runtime_save_display_settings(esp_bms_idf_runtime_t *runtime)
{
    ESP_RETURN_ON_FALSE(runtime, ESP_ERR_INVALID_ARG, TAG, "runtime is required");
    ESP_RETURN_ON_FALSE(runtime_brightness_matches_policy(runtime->brightness_percent),
                        ESP_ERR_INVALID_STATE, TAG, "invalid brightness");
    ESP_RETURN_ON_FALSE(runtime_volume_matches_policy(runtime->volume_percent),
                        ESP_ERR_INVALID_STATE, TAG, "invalid volume");
    ESP_RETURN_ON_FALSE(runtime_rotation_matches_policy((uint8_t)runtime->display_rotation),
                        ESP_ERR_INVALID_STATE, TAG, "invalid display rotation");
    ESP_RETURN_ON_FALSE(runtime_speed_unit_matches_policy((uint8_t)runtime->snapshot.speed_unit),
                        ESP_ERR_INVALID_STATE, TAG, "invalid speed unit");
    ESP_RETURN_ON_FALSE(runtime_speed_source_matches_policy(
                            (uint8_t)runtime->snapshot.speed_source),
                        ESP_ERR_INVALID_STATE, TAG, "invalid speed source");
    ESP_RETURN_ON_FALSE(runtime_speed_dashboard_style_matches_policy(
                            runtime->snapshot.speed_dashboard_style),
                        ESP_ERR_INVALID_STATE, TAG, "invalid speed dashboard style");
    ESP_RETURN_ON_FALSE(runtime_boot_animation_style_matches_policy(
                            runtime->snapshot.boot_animation_style),
                        ESP_ERR_INVALID_STATE, TAG, "invalid boot animation style");
    ESP_RETURN_ON_FALSE(runtime_bms_type_matches_policy(runtime->bms_type),
                        ESP_ERR_INVALID_STATE, TAG, "invalid BMS type");
    ESP_RETURN_ON_FALSE(runtime->snapshot.preset_range_km <= ESP_BMS_REMAINING_RANGE_MAX_KM,
                        ESP_ERR_INVALID_STATE, TAG, "invalid preset range");
    ESP_RETURN_ON_FALSE(runtime_controller_ratio_matches_policy(
                            runtime->controller_state.fallback_gear_ratio_centi),
                        ESP_ERR_INVALID_STATE, TAG, "invalid controller ratio");
    const bool tire_unset = runtime->controller_fallback_tire_rim_inch == 0U &&
                            runtime->controller_fallback_tire_aspect_percent == 0U &&
                            runtime->controller_fallback_tire_width_mm == 0U;
    ESP_RETURN_ON_FALSE(tire_unset ||
                            runtime_controller_tire_matches_policy(
                                runtime->controller_fallback_tire_rim_inch,
                                runtime->controller_fallback_tire_aspect_percent,
                                runtime->controller_fallback_tire_width_mm),
                        ESP_ERR_INVALID_STATE, TAG, "invalid controller tire");
    ESP_RETURN_ON_FALSE(runtime->controller_state.fallback_wheel_circumference_mm == 0U ||
                            (runtime->controller_state.fallback_wheel_circumference_mm >= 500U &&
                             runtime->controller_state.fallback_wheel_circumference_mm <= 4000U),
                        ESP_ERR_INVALID_STATE, TAG, "invalid controller circumference");

    ESP_RETURN_ON_ERROR(runtime_init_nvs(runtime), TAG, "NVS init failed");

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(SETUP_AP_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_u8(handle, DISPLAY_NVS_BRIGHTNESS_KEY, runtime->brightness_percent);
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, DISPLAY_NVS_VOLUME_KEY, runtime->volume_percent);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, DISPLAY_NVS_ROTATION_KEY, (uint8_t)runtime->display_rotation);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle,
                         DISPLAY_NVS_ROTATION_DEFAULT_VERSION_KEY,
                         runtime->display_rotation_default_version);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, DISPLAY_NVS_SPEED_UNIT_KEY, (uint8_t)runtime->snapshot.speed_unit);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle,
                         DISPLAY_NVS_SPEED_SOURCE_KEY,
                         (uint8_t)runtime->snapshot.speed_source);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle,
                         DISPLAY_NVS_SPEED_STYLE_KEY,
                         (uint8_t)runtime->snapshot.speed_dashboard_style);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle,
                         DISPLAY_NVS_BOOT_ANIMATION_KEY,
                         runtime->snapshot.boot_animation_style);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, DISPLAY_NVS_LANGUAGE_KEY, RUNTIME_FLAG(runtime, LANGUAGE_ZH) ? 1U : 0U);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, DISPLAY_NVS_BMS_TYPE_KEY, runtime->bms_type);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u16(handle,
                          DISPLAY_NVS_PRESET_RANGE_KEY,
                          runtime->snapshot.preset_range_km);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, CONTROLLER_NVS_CONNECTION_KEY,
                         runtime->controller_connection_enabled ? 1U : 0U);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, CONTROLLER_NVS_PAGE_KEY,
                         runtime->snapshot.speed_source == ESP_BMS_SPEED_SOURCE_CONTROLLER ? 1U : 0U);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u16(handle, CONTROLLER_NVS_WHEEL_KEY,
                          runtime->controller_state.fallback_wheel_circumference_mm);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u16(handle, CONTROLLER_NVS_RATIO_KEY,
                          runtime->controller_state.fallback_gear_ratio_centi);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, CONTROLLER_NVS_RIM_KEY,
                         runtime->controller_fallback_tire_rim_inch);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, CONTROLLER_NVS_ASPECT_KEY,
                         runtime->controller_fallback_tire_aspect_percent);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u16(handle, CONTROLLER_NVS_WIDTH_KEY,
                          runtime->controller_fallback_tire_width_mm);
    }
    if (ret == ESP_OK && runtime->controller_bound_mac[0] != '\0') {
        ret = nvs_set_str(handle, CONTROLLER_NVS_BOUND_MAC_KEY, runtime->controller_bound_mac);
    }
    if (ret == ESP_OK && runtime->controller_bound_name[0] != '\0') {
        ret = nvs_set_str(handle, CONTROLLER_NVS_BOUND_NAME_KEY, runtime->controller_bound_name);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static bool runtime_set_pending_http_config(esp_bms_idf_runtime_t *runtime,
                                            uint8_t brightness_percent,
                                            uint8_t volume_percent,
                                            esp_bms_idf_display_rotation_t rotation,
                                            esp_bms_speed_unit_t speed_unit,
                                            esp_bms_speed_source_t speed_source,
                                            bool language_zh,
                                            esp_bms_idf_bms_type_t bms_type)
{
    if (!runtime->http_pending_lock) {
        return false;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    runtime->http_pending_brightness_percent = brightness_percent;
    runtime->http_pending_volume_percent = volume_percent;
    runtime->http_pending_display_rotation = rotation;
    runtime->http_pending_speed_unit = speed_unit;
    runtime->http_pending_speed_source = speed_source;
    RUNTIME_SET_FLAG(runtime, HTTP_PENDING_LANGUAGE_ZH, language_zh);
    runtime->http_pending_bms_type = (uint8_t)bms_type;
    RUNTIME_SET_FLAG(runtime, HTTP_CONFIG_PENDING, true);

    xSemaphoreGive(runtime->http_pending_lock);
    return true;
}

bool esp_bms_idf_runtime_apply_pending_http_config(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime->http_pending_lock) {
        return false;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, 0) != pdTRUE) {
        return false;
    }

    const bool pending = RUNTIME_FLAG(runtime, HTTP_CONFIG_PENDING);
    const uint8_t brightness_percent = runtime->http_pending_brightness_percent;
    const uint8_t volume_percent = runtime->http_pending_volume_percent;
    const esp_bms_idf_display_rotation_t rotation = runtime->http_pending_display_rotation;
    const esp_bms_speed_unit_t speed_unit = runtime->http_pending_speed_unit;
    const esp_bms_speed_source_t speed_source = runtime->http_pending_speed_source;
    const bool language_zh = RUNTIME_FLAG(runtime, HTTP_PENDING_LANGUAGE_ZH);
    const uint8_t bms_type = runtime->http_pending_bms_type;
    RUNTIME_SET_FLAG(runtime, HTTP_CONFIG_PENDING, false);
    xSemaphoreGive(runtime->http_pending_lock);

    if (!pending) {
        return false;
    }

    RUNTIME_SET_FLAG(runtime, HTTP_CONFIG_APPLIED, true);

    const bool bms_type_changed = runtime->bms_type != bms_type;
    const bool changed = runtime->brightness_percent != brightness_percent ||
                         runtime->volume_percent != volume_percent ||
                         runtime->display_rotation != rotation ||
                         runtime->snapshot.speed_unit != speed_unit ||
                         runtime->snapshot.speed_source != speed_source ||
                         RUNTIME_FLAG(runtime, LANGUAGE_ZH) != language_zh ||
                         bms_type_changed;
    if (!changed) {
        ESP_LOGI(TAG, "[http] config consumed without value changes");
        return true;
    }

    (void)runtime_set_brightness_percent(runtime, brightness_percent);
    (void)runtime_set_volume_percent(runtime, volume_percent);
    runtime->display_rotation = rotation;
    runtime->snapshot.speed_unit = speed_unit;
    runtime->snapshot.speed_source = speed_source;
    RUNTIME_SET_FLAG(runtime, LANGUAGE_ZH, language_zh);
    if (bms_type_changed) {
        (void)runtime_select_bms_type(runtime, (esp_bms_idf_bms_type_t)bms_type);
    }
    runtime_project_controller_snapshot(runtime);
    runtime_set_error(runtime, "HTTP CFG");

    const esp_err_t save_ret = esp_bms_idf_runtime_save_display_settings(runtime);
    if (save_ret != ESP_OK) {
        ESP_LOGW(TAG, "[http] display setting save failed: %s", esp_err_to_name(save_ret));
    }
    return true;
}

static esp_err_t runtime_http_set_common_headers(httpd_req_t *req)
{
    esp_err_t ret = httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    if (ret == ESP_OK) {
        ret = httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    }
    if (ret == ESP_OK) {
        ret = httpd_resp_set_hdr(req,
                                 "Access-Control-Allow-Headers",
                                 "Content-Type, X-Firmware-Code");
    }
    if (ret == ESP_OK) {
        ret = httpd_resp_set_hdr(req, "Access-Control-Max-Age", "600");
    }
    if (ret == ESP_OK) {
        ret = httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    }
    return ret;
}

static esp_err_t runtime_http_send_text(httpd_req_t *req, const char *status, const char *text)
{
    ESP_RETURN_ON_ERROR(runtime_http_set_common_headers(req), TAG, "set HTTP headers failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_status(req, status), TAG, "set HTTP status failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "text/plain; charset=utf-8"), TAG, "set HTTP type failed");
    return httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t runtime_http_send_no_content(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(runtime_http_set_common_headers(req), TAG, "set HTTP headers failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_status(req, "204 No Content"), TAG, "set HTTP status failed");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t runtime_http_send_json(httpd_req_t *req, const char *json)
{
    ESP_RETURN_ON_ERROR(runtime_http_set_common_headers(req), TAG, "set HTTP headers failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "application/json"), TAG, "set HTTP type failed");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static bool runtime_json_write_ride_snapshot(char *out,
                                             size_t out_len,
                                             const esp_bms_ride_record_snapshot_t *snapshot)
{
    if (!out || !snapshot || out_len == 0U) {
        return false;
    }

    int written = snprintf(out,
                           out_len,
                           "{\"pack_voltage_mv\":%u,\"current_deci_amps\":%d,"
                           "\"delta_cell_voltage_mv\":%u,\"soc_percent\":%u,"
                           "\"temperatures_c\":[",
                           (unsigned)snapshot->pack_voltage_mv,
                           (int)snapshot->current_deci_amps,
                           (unsigned)snapshot->delta_cell_voltage_mv,
                           (unsigned)snapshot->soc_percent);
    if (written < 0 || (size_t)written >= out_len) {
        return false;
    }
    size_t offset = (size_t)written;
    for (uint8_t index = 0U; index < ESP_BMS_RIDE_RECORD_TEMP_MAX_COUNT; ++index) {
        const bool valid = (snapshot->temperature_valid_mask & (UINT8_C(1) << index)) != 0U;
        written = valid
                      ? snprintf(out + offset,
                                 out_len - offset,
                                 "%s%d",
                                 index == 0U ? "" : ",",
                                 (int)snapshot->temperatures_celsius[index])
                      : snprintf(out + offset, out_len - offset, "%snull", index == 0U ? "" : ",");
        if (written < 0 || (size_t)written >= out_len - offset) {
            return false;
        }
        offset += (size_t)written;
    }
    written = snprintf(out + offset, out_len - offset, "]}");
    return written >= 0 && (size_t)written < out_len - offset;
}

static esp_err_t runtime_http_ride_records_handler(httpd_req_t *req,
                                                    esp_bms_idf_runtime_t *runtime)
{
    esp_bms_ride_records_t records;
    bool session_started = false;
    if (!runtime_copy_ride_records(runtime,
                                   &records,
                                   &session_started,
                                   NULL,
                                   NULL,
                                   NULL)) {
        return runtime_http_send_text(req, "503 Service Unavailable", "ride records busy");
    }
    if (!esp_bms_ride_records_valid(&records)) {
        esp_bms_ride_records_reset(&records);
        session_started = false;
    }

    ESP_RETURN_ON_ERROR(runtime_http_set_common_headers(req), TAG, "set HTTP headers failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "application/json"), TAG, "set HTTP type failed");
    ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(req, "{\"records\":[", HTTPD_RESP_USE_STRLEN),
                        TAG,
                        "send ride record header failed");

    bool first = true;
    for (uint8_t offset = records.count; offset > 0U; --offset) {
        const uint8_t index = offset - 1U;
        const bool current = session_started && index == records.count - 1U;
        const esp_bms_ride_record_t *record = &records.records[index];
        char prefix[48] = { 0 };
        const int prefix_len = snprintf(prefix,
                                        sizeof(prefix),
                                        "%s{\"current\":%s,\"max_current\":",
                                        first ? "" : ",",
                                        current ? "true" : "false");
        if (prefix_len < 0 || (size_t)prefix_len >= sizeof(prefix)) {
            return ESP_FAIL;
        }
        char snapshot[256] = { 0 };
        if (!runtime_json_write_ride_snapshot(snapshot, sizeof(snapshot), &record->max_current)) {
            return ESP_FAIL;
        }
        ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(req, prefix, HTTPD_RESP_USE_STRLEN),
                            TAG,
                            "send ride record prefix failed");
        ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(req, snapshot, HTTPD_RESP_USE_STRLEN),
                            TAG,
                            "send maximum current failed");
        ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(req, ",\"max_delta\":", HTTPD_RESP_USE_STRLEN),
                            TAG,
                            "send ride record divider failed");
        if (!runtime_json_write_ride_snapshot(snapshot, sizeof(snapshot), &record->max_delta)) {
            return ESP_FAIL;
        }
        ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(req, snapshot, HTTPD_RESP_USE_STRLEN),
                            TAG,
                            "send maximum delta failed");
        ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(req, "}", HTTPD_RESP_USE_STRLEN),
                            TAG,
                            "send ride record end failed");
        first = false;
    }

    ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN),
                        TAG,
                        "send ride records end failed");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t runtime_http_gps_track_handler(httpd_req_t *req,
                                                 esp_bms_idf_runtime_t *runtime)
{
    /* HTTPD serializes handlers; static avoids overflowing its small task stack. */
    static esp_bms_gps_track_t track;
    if (!runtime_copy_gps_track(runtime, &track, NULL, NULL, NULL)) {
        return runtime_http_send_text(req, "503 Service Unavailable", "gps track busy");
    }
    if (!runtime_gps_track_valid(&track)) {
        runtime_reset_gps_track(&track);
    }

    ESP_RETURN_ON_ERROR(runtime_http_set_common_headers(req), TAG, "set HTTP headers failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "application/json"), TAG, "set HTTP type failed");
    ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(req, "{\"points\":[", HTTPD_RESP_USE_STRLEN),
                        TAG,
                        "send GPS track header failed");
    for (uint16_t index = 0U; index < track.count; index++) {
        char point[96];
        const esp_bms_gps_track_point_t *value = &track.points[index];
        const int written = snprintf(point,
                                     sizeof(point),
                                     "%s{\"lat_e7\":%ld,\"lon_e7\":%ld,\"timestamp_s\":%lu}",
                                     index == 0U ? "" : ",",
                                     (long)value->latitude_e7,
                                     (long)value->longitude_e7,
                                     (unsigned long)value->timestamp_s);
        if (written < 0 || (size_t)written >= sizeof(point)) {
            return runtime_http_send_text(req, "500 Internal Server Error", "gps track format error");
        }
        ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(req, point, HTTPD_RESP_USE_STRLEN),
                            TAG,
                            "send GPS track point failed");
    }
    ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN),
                        TAG,
                        "send GPS track end failed");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static bool runtime_json_write_bms_codes(char *out,
                                         size_t out_len,
                                         const char codes[][ESP_BMS_BMS_CODE_TEXT_LEN],
                                         uint8_t count)
{
    if (!out || out_len == 0U) {
        return false;
    }

    size_t offset = 0;
    int written = snprintf(out, out_len, "[");
    if (written < 0 || (size_t)written >= out_len) {
        return false;
    }
    offset = (size_t)written;

    const uint8_t safe_count = count > ESP_BMS_BMS_CODE_MAX_COUNT ? ESP_BMS_BMS_CODE_MAX_COUNT : count;
    for (uint8_t index = 0; index < safe_count; index++) {
        written = snprintf(out + offset,
                           out_len - offset,
                           "%s\"%s\"",
                           index == 0U ? "" : ",",
                           codes[index]);
        if (written < 0 || (size_t)written >= out_len - offset) {
            return false;
        }
        offset += (size_t)written;
    }

    written = snprintf(out + offset, out_len - offset, "]");
    return written >= 0 && (size_t)written < out_len - offset;
}

static bool runtime_json_write_bms_temperatures(char *out,
                                                size_t out_len,
                                                const esp_bms_dashboard_snapshot_t *snapshot)
{
    if (!out || out_len == 0U || !snapshot) {
        return false;
    }

    size_t offset = 0;
    int written = snprintf(out, out_len, "[");
    if (written < 0 || (size_t)written >= out_len) {
        return false;
    }
    offset = (size_t)written;

    for (uint8_t index = 0; index < ESP_BMS_BMS_TEMP_MAX_COUNT; index++) {
        if (esp_bms_dashboard_snapshot_temperature_valid(snapshot, index)) {
            written = snprintf(out + offset,
                               out_len - offset,
                               "%s%d",
                               index == 0U ? "" : ",",
                               (int)snapshot->bms_temperature_celsius[index]);
        } else {
            written = snprintf(out + offset, out_len - offset, "%snull", index == 0U ? "" : ",");
        }
        if (written < 0 || (size_t)written >= out_len - offset) {
            return false;
        }
        offset += (size_t)written;
    }

    written = snprintf(out + offset, out_len - offset, "]");
    return written >= 0 && (size_t)written < out_len - offset;
}

static bool runtime_status_json(esp_bms_idf_runtime_t *runtime, char *json, size_t json_len)
{
    char pack_voltage[16] = { 0 };
    char current[16] = { 0 };
    char soc[16] = { 0 };
    char local_battery[16] = { 0 };
    char capacity_estimate[16] = { 0 };
    char protections[96] = { 0 };
    char warnings[96] = { 0 };
    char temperatures[80] = { 0 };
    char min_cell[16] = { 0 };
    char average_cell[16] = { 0 };
    char max_cell[16] = { 0 };
    char delta_cell[16] = { 0 };
    char total_capacity[16] = { 0 };
    char remaining_capacity[16] = { 0 };
    char running_time[16] = { 0 };
    char cycle_capacity[16] = { 0 };
    bool capacity_ready = false;
    uint32_t capacity_estimate_mah = 0U;
    if (runtime_bms_supports_capacity_estimate(runtime->bms_type) &&
        runtime->capacity_estimate_lock &&
        xSemaphoreTake(runtime->capacity_estimate_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        capacity_ready = runtime_capacity_estimate_identity_matches(runtime) &&
                         runtime->capacity_estimate.ready;
        capacity_estimate_mah = runtime->capacity_estimate.estimate_mah;
        xSemaphoreGive(runtime->capacity_estimate_lock);
    }
    if (!runtime_json_write_u32_or_null(pack_voltage,
                                        sizeof(pack_voltage),
                                        RUNTIME_SNAPSHOT_FLAG(runtime, PACK_VOLTAGE_VALID),
                                        runtime->snapshot.pack_voltage_mv) ||
        !runtime_json_write_i32_or_null(current,
                                        sizeof(current),
                                        RUNTIME_SNAPSHOT_FLAG(runtime, CURRENT_VALID),
                                        runtime->snapshot.current_deci_amps) ||
        !runtime_json_write_u32_or_null(soc,
                                        sizeof(soc),
                                        RUNTIME_SNAPSHOT_FLAG(runtime, SOC_VALID),
                                        runtime->snapshot.soc_percent) ||
        !runtime_json_write_u32_or_null(local_battery,
                                        sizeof(local_battery),
                                        RUNTIME_SNAPSHOT_FLAG(runtime, LOCAL_BATTERY_VALID),
                                        runtime->snapshot.local_battery_mv) ||
        !runtime_json_write_u32_or_null(capacity_estimate,
                                        sizeof(capacity_estimate),
                                        capacity_ready,
                                        capacity_estimate_mah) ||
        !runtime_json_write_bms_codes(protections,
                                      sizeof(protections),
                                      runtime->snapshot.bms_protection_codes,
                                      runtime->snapshot.bms_protection_count) ||
        !runtime_json_write_bms_codes(warnings,
                                      sizeof(warnings),
                                      runtime->snapshot.bms_warning_codes,
                                      runtime->snapshot.bms_warning_count) ||
        !runtime_json_write_bms_temperatures(temperatures, sizeof(temperatures), &runtime->snapshot) ||
        !runtime_json_write_u32_or_null(min_cell,
                                        sizeof(min_cell),
                                        RUNTIME_SNAPSHOT_FLAG(runtime, MIN_CELL_VALID),
                                        runtime->snapshot.min_cell_voltage_mv) ||
        !runtime_json_write_u32_or_null(average_cell,
                                        sizeof(average_cell),
                                        RUNTIME_SNAPSHOT_FLAG(runtime, AVERAGE_CELL_VALID),
                                        runtime->snapshot.average_cell_voltage_mv) ||
        !runtime_json_write_u32_or_null(max_cell,
                                        sizeof(max_cell),
                                        RUNTIME_SNAPSHOT_FLAG(runtime, MAX_CELL_VALID),
                                        runtime->snapshot.max_cell_voltage_mv) ||
        !runtime_json_write_u32_or_null(delta_cell,
                                        sizeof(delta_cell),
                                        RUNTIME_SNAPSHOT_FLAG(runtime, DELTA_CELL_VALID),
                                        runtime->snapshot.delta_cell_voltage_mv) ||
        !runtime_json_write_u32_or_null(total_capacity,
                                        sizeof(total_capacity),
                                        RUNTIME_SNAPSHOT_FLAG(runtime, TOTAL_CAPACITY_VALID),
                                        runtime->snapshot.total_capacity_mah) ||
        !runtime_json_write_u32_or_null(remaining_capacity,
                                        sizeof(remaining_capacity),
                                        RUNTIME_SNAPSHOT_FLAG(runtime, CAPACITY_REMAINING_VALID),
                                        runtime->snapshot.capacity_remaining_mah) ||
        !runtime_json_write_u32_or_null(running_time,
                                        sizeof(running_time),
                                        runtime->snapshot.bms_running_time_valid,
                                        runtime->snapshot.bms_running_time_seconds) ||
        !runtime_json_write_u32_or_null(cycle_capacity,
                                        sizeof(cycle_capacity),
                                        runtime->snapshot.bms_cycle_capacity_valid,
                                        runtime->snapshot.bms_cycle_capacity_mah)) {
        return false;
    }

    char speed[16] = "--";
    if (RUNTIME_SNAPSHOT_FLAG(runtime, SPEED_VALID)) {
        const unsigned value = runtime->snapshot.speed_deci_units;
        (void)snprintf(speed, sizeof(speed), "%u.%u", value / 10U, value % 10U);
    }

    /* 运行中固件的 SHA-256 前缀，供网页端 OTA 重启后对比判断刷入是否生效 */
    char firmware_sha256[13] = "unknown";
    (void)esp_app_get_elf_sha256(firmware_sha256, sizeof(firmware_sha256));

    const char *capacity_state = runtime_bms_supports_capacity_estimate(runtime->bms_type)
                                     ? capacity_ready ? "ready" : "estimating"
                                     : "unsupported";
    const int written = snprintf(json,
                                 json_len,
                                 "{\"version\":\"%s\",\"firmware_sha256\":\"%s\","
                                 "\"speed\":\"%s\",\"speed_unit\":\"%s\","
                                 "\"gps_fix\":%s,\"bms\":\"%s\",\"pack_voltage_mv\":%s,"
                                 "\"current_deci_amps\":%s,\"soc_percent\":%s,"
                                 "\"local_battery_mv\":%s,\"bms_info\":\"%s\","
                                 "\"bms_protections\":%s,\"bms_warnings\":%s,"
                                 "\"bms_temperatures_c\":%s,\"min_cell_voltage_mv\":%s,"
                                 "\"average_cell_voltage_mv\":%s,\"max_cell_voltage_mv\":%s,"
                                 "\"delta_cell_voltage_mv\":%s,\"total_capacity_mah\":%s,"
                                 "\"capacity_remaining_mah\":%s,\"bms_running_time_seconds\":%s,"
                                 "\"bms_cycle_capacity_mah\":%s,\"wifi\":\"%s\","
                                 "\"setup_ap_enabled\":%s,\"bms_capacity_estimate_mah\":%s,"
                                 "\"bms_capacity_estimate_state\":\"%s\"}",
                                 runtime->snapshot.firmware_version,
                                 firmware_sha256,
                                 speed,
                                 runtime_speed_unit_config_text(runtime->snapshot.speed_unit),
                                 RUNTIME_SNAPSHOT_FLAG(runtime, GPS_FIX_VALID) ? "true" : "false",
                                 RUNTIME_SNAPSHOT_FLAG(runtime, BMS_ONLINE) ? "online" : "offline",
                                 pack_voltage,
                                 current,
                                 soc,
                                 local_battery,
                                 runtime->snapshot.bms_info_text[0] != '\0' ? runtime->snapshot.bms_info_text : "BMS OFF",
                                 protections,
                                 warnings,
                                 temperatures,
                                 min_cell,
                                 average_cell,
                                 max_cell,
                                 delta_cell,
                                 total_capacity,
                                 remaining_capacity,
                                 running_time,
                                 cycle_capacity,
                                 runtime_wifi_config_text(runtime->snapshot.wifi),
                                 RUNTIME_SNAPSHOT_FLAG(runtime, SETUP_AP_ENABLED) ? "true" : "false",
                                 capacity_estimate,
                                 capacity_state);
    return written >= 0 && (size_t)written < json_len;
}

static esp_err_t runtime_http_status_handler(httpd_req_t *req, esp_bms_idf_runtime_t *runtime)
{
    /* HTTPD serializes handlers; static avoids consuming the small task stack. */
    static char json[HTTP_JSON_MAX_LEN];
    return runtime_status_json(runtime, json, sizeof(json))
               ? runtime_http_send_json(req, json)
               : runtime_http_send_text(req, "500 Internal Server Error", "json format error");
}

static bool runtime_json_escape(char *out,
                                size_t out_len,
                                const char *value,
                                size_t value_len)
{
    if (!out || out_len == 0U || (!value && value_len != 0U)) {
        return false;
    }

    size_t used = 0U;
    for (size_t index = 0U; index < value_len; index++) {
        const unsigned char byte = (unsigned char)value[index];
        const char *escape = NULL;
        size_t escape_len = 0U;
        switch (byte) {
        case '"':
            escape = "\\\"";
            escape_len = 2U;
            break;
        case '\\':
            escape = "\\\\";
            escape_len = 2U;
            break;
        case '\b':
            escape = "\\b";
            escape_len = 2U;
            break;
        case '\f':
            escape = "\\f";
            escape_len = 2U;
            break;
        case '\n':
            escape = "\\n";
            escape_len = 2U;
            break;
        case '\r':
            escape = "\\r";
            escape_len = 2U;
            break;
        case '\t':
            escape = "\\t";
            escape_len = 2U;
            break;
        default:
            break;
        }
        if (byte < 0x20U && !escape) {
            if (used + 6U >= out_len) {
                return false;
            }
            const int written = snprintf(out + used,
                                         out_len - used,
                                         "\\u%04x",
                                         (unsigned)byte);
            if (written != 6) {
                return false;
            }
            used += 6U;
            continue;
        }
        if (escape) {
            if (used + escape_len >= out_len) {
                return false;
            }
            memcpy(out + used, escape, escape_len);
            used += escape_len;
            continue;
        }
        if (used + 1U >= out_len) {
            return false;
        }
        out[used++] = (char)byte;
    }
    out[used] = '\0';
    return true;
}

static esp_err_t runtime_http_bms_candidates_handler(httpd_req_t *req,
                                                     esp_bms_idf_runtime_t *runtime)
{
    esp_bms_idf_bms_scan_candidate_t candidates[ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES] = { 0 };
    uint8_t count = 0;

    if (runtime->bms_scan_lock &&
        xSemaphoreTake(runtime->bms_scan_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        count = runtime->bms_scan_candidate_count;
        if (count > ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES) {
            count = ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES;
        }
        memcpy(candidates, runtime->bms_scan_candidates, sizeof(candidates));
        xSemaphoreGive(runtime->bms_scan_lock);
    } else if (!runtime->bms_scan_lock) {
        count = runtime->bms_scan_candidate_count;
        if (count > ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES) {
            count = ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES;
        }
        memcpy(candidates, runtime->bms_scan_candidates, sizeof(candidates));
    }

    static char json[HTTP_BMS_CANDIDATES_JSON_MAX_LEN];
    static char escaped_name[(ESP_BMS_IDF_BMS_SCAN_NAME_LEN * 6U) + 1U];
    size_t offset = 0;
    int written = snprintf(json,
                           sizeof(json),
                           "{\"scan_active\":%s,\"candidates\":[",
                           RUNTIME_FLAG(runtime, BMS_SCAN_ACTIVE) ? "true" : "false");
    if (written < 0 || (size_t)written >= sizeof(json)) {
        return runtime_http_send_text(req, "500 Internal Server Error", "json too large");
    }
    offset = (size_t)written;

    for (uint8_t index = 0; index < count; index++) {
        const esp_bms_idf_bms_scan_candidate_t *candidate = &candidates[index];
        written = snprintf(json + offset,
                           sizeof(json) - offset,
                           "%s{\"mac\":\"%s\",\"rssi\":%d",
                           index == 0U ? "" : ",",
                           candidate->mac,
                           (int)candidate->rssi);
        if (written < 0 || (size_t)written >= sizeof(json) - offset) {
            return runtime_http_send_text(req, "500 Internal Server Error", "json too large");
        }
        offset += (size_t)written;

        if (candidate->has_name && candidate->name[0] != '\0') {
            const size_t name_len = strnlen(candidate->name, sizeof(candidate->name));
            if (!runtime_json_escape(escaped_name,
                                     sizeof(escaped_name),
                                     candidate->name,
                                     name_len)) {
                return runtime_http_send_text(req, "500 Internal Server Error", "json too large");
            }
            written = snprintf(json + offset,
                               sizeof(json) - offset,
                               ",\"name\":\"%s\"",
                               escaped_name);
            if (written < 0 || (size_t)written >= sizeof(json) - offset) {
                return runtime_http_send_text(req, "500 Internal Server Error", "json too large");
            }
            offset += (size_t)written;
        }

        written = snprintf(json + offset, sizeof(json) - offset, "}");
        if (written < 0 || (size_t)written >= sizeof(json) - offset) {
            return runtime_http_send_text(req, "500 Internal Server Error", "json too large");
        }
        offset += (size_t)written;
    }

    written = snprintf(json + offset, sizeof(json) - offset, "]}");
    if (written < 0 || (size_t)written >= sizeof(json) - offset) {
        return runtime_http_send_text(req, "500 Internal Server Error", "json too large");
    }
    return runtime_http_send_json(req, json);
}

static void runtime_config_bms_mac_json(const esp_bms_idf_runtime_t *runtime, char *out, size_t out_len)
{
    char mac[sizeof(runtime->bms_bound_mac)] = { 0 };

    if (runtime->http_pending_lock &&
        xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        runtime_copy_snapshot_text(mac, sizeof(mac), runtime->bms_bound_mac);
        if (RUNTIME_FLAG(runtime, HTTP_BMS_BIND_PENDING)) {
            runtime_copy_snapshot_text(mac, sizeof(mac), runtime->http_pending_bms_bound_mac);
        }
        xSemaphoreGive(runtime->http_pending_lock);
    } else {
        runtime_copy_snapshot_text(mac, sizeof(mac), runtime->bms_bound_mac);
    }

    if (mac[0] == '\0') {
        runtime_copy_snapshot_text(out, out_len, "null");
        return;
    }
    (void)snprintf(out, out_len, "\"%s\"", mac);
}

static bool runtime_config_json(esp_bms_idf_runtime_t *runtime, char *json, size_t json_len)
{
    char bms_mac[24] = { 0 };
    runtime_config_bms_mac_json(runtime, bms_mac, sizeof(bms_mac));

    const int written = snprintf(json,
                                 json_len,
                                 "{\"brightness\":%u,\"volume\":%u,\"display_rotation\":\"%s\","
                                 "\"speed_unit\":\"%s\",\"speed_source\":\"%s\","
                                 "\"active_speed_source\":\"%s\",\"controller_online\":%s,"
                                 "\"language\":\"%s\","
                                 "\"setup_ap_ssid\":\"%s\",\"setup_ap_password_saved\":%s,"
                                 "\"setup_ap_state\":\"%s\",\"bms_mac\":%s,\"bms_type\":\"%s\"}",
                                 runtime->brightness_percent,
                                 runtime->volume_percent,
                                 runtime_rotation_config_text(runtime->display_rotation),
                                 runtime_speed_unit_config_text(runtime->snapshot.speed_unit),
                                 runtime_speed_source_config_text(runtime->snapshot.speed_source),
                                 runtime_speed_source_config_text(runtime->snapshot.active_speed_source),
                                 RUNTIME_SNAPSHOT_FLAG(runtime, CONTROLLER_ONLINE) ? "true" : "false",
                                 RUNTIME_FLAG(runtime, LANGUAGE_ZH) ? "zh" : "en",
                                 runtime->setup_ap_ssid,
                                 runtime->setup_ap_password[0] == '\0' ? "false" : "true",
                                 RUNTIME_SNAPSHOT_FLAG(runtime, SETUP_AP_ENABLED) ? "enabled" : "disabled",
                                 bms_mac,
                                 runtime_bms_type_config_text((esp_bms_idf_bms_type_t)runtime->bms_type));
    return written >= 0 && (size_t)written < json_len;
}

static esp_err_t runtime_http_config_handler(httpd_req_t *req, esp_bms_idf_runtime_t *runtime)
{
    static char json[HTTP_JSON_MAX_LEN];
    return runtime_config_json(runtime, json, sizeof(json))
               ? runtime_http_send_json(req, json)
               : runtime_http_send_text(req, "500 Internal Server Error", "json too large");
}

static bool runtime_json_append(char *json, size_t json_len, size_t *offset,
                                const char *format, ...)
{
    if (!json || !offset || *offset >= json_len) {
        return false;
    }
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(json + *offset, json_len - *offset, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= json_len - *offset) {
        return false;
    }
    *offset += (size_t)written;
    return true;
}

static bool runtime_settings_manifest_json(esp_bms_idf_runtime_t *runtime,
                                           char *json,
                                           size_t json_len)
{
    char bms_mac[24] = { 0 };
    runtime_config_bms_mac_json(runtime, bms_mac, sizeof(bms_mac));
    size_t offset = 0;
    bool first_item = true;
    bool ok = runtime_json_append(json, json_len, &offset,
        "{\"protocol_version\":1,\"sections\":[{\"id\":\"device\","
        "\"label\":{\"zh\":\"设备\",\"en\":\"Device\"},"
        "\"submit\":{\"endpoint\":\"/api/config\",\"method\":\"POST\"},\"items\":[");
#define MANIFEST_ITEM(...) \
    do { \
        ok = ok && runtime_json_append(json, json_len, &offset, "%s", first_item ? "" : ","); \
        ok = ok && runtime_json_append(json, json_len, &offset, __VA_ARGS__); \
        first_item = false; \
    } while (0)
#if CONFIG_ESP_BMS_LVGL_BRIDGE_BACKLIGHT_DIMMING
    MANIFEST_ITEM("{\"id\":\"brightness\",\"kind\":\"range\",\"label\":{\"zh\":\"亮度\",\"en\":\"Brightness\"},\"value\":%u,\"min\":10,\"max\":100,\"step\":1}", runtime->brightness_percent);
#endif
    MANIFEST_ITEM("{\"id\":\"volume\",\"kind\":\"range\",\"label\":{\"zh\":\"音量\",\"en\":\"Volume\"},\"value\":%u,\"min\":0,\"max\":100,\"step\":1}", runtime->volume_percent);
    MANIFEST_ITEM("{\"id\":\"display_rotation\",\"kind\":\"select\",\"label\":{\"zh\":\"屏幕方向\",\"en\":\"Screen rotation\"},\"value\":\"%s\",\"options\":[{\"value\":\"portrait\",\"label\":{\"zh\":\"竖屏\",\"en\":\"Portrait\"}},{\"value\":\"landscape\",\"label\":{\"zh\":\"横屏\",\"en\":\"Landscape\"}},{\"value\":\"inverted_portrait\",\"label\":{\"zh\":\"反向竖屏\",\"en\":\"Inverted portrait\"}},{\"value\":\"inverted_landscape\",\"label\":{\"zh\":\"反向横屏\",\"en\":\"Inverted landscape\"}}]}", runtime_rotation_config_text(runtime->display_rotation));
#if ESP_BMS_FEATURE_GPS || ESP_BMS_FEATURE_CONTROLLER
    MANIFEST_ITEM("{\"id\":\"speed_unit\",\"kind\":\"select\",\"label\":{\"zh\":\"速度单位\",\"en\":\"Speed unit\"},\"value\":\"%s\",\"options\":[{\"value\":\"km/h\",\"label\":{\"zh\":\"公里/小时\",\"en\":\"km/h\"}},{\"value\":\"mph\",\"label\":{\"zh\":\"英里/小时\",\"en\":\"mph\"}}]}", runtime_speed_unit_config_text(runtime->snapshot.speed_unit));
#endif
#if ESP_BMS_FEATURE_GPS && ESP_BMS_FEATURE_CONTROLLER
    MANIFEST_ITEM("{\"id\":\"speed_source\",\"kind\":\"select\",\"label\":{\"zh\":\"速度来源\",\"en\":\"Speed source\"},\"value\":\"%s\",\"options\":[{\"value\":\"gps\",\"label\":{\"zh\":\"GPS\",\"en\":\"GPS\"}},{\"value\":\"controller\",\"label\":{\"zh\":\"控制器\",\"en\":\"Controller\"}}]}", runtime_speed_source_config_text(runtime->snapshot.speed_source));
#endif
    MANIFEST_ITEM("{\"id\":\"language\",\"kind\":\"select\",\"label\":{\"zh\":\"语言\",\"en\":\"Language\"},\"value\":\"%s\",\"options\":[{\"value\":\"zh\",\"label\":{\"zh\":\"中文\",\"en\":\"Chinese\"}},{\"value\":\"en\",\"label\":{\"zh\":\"英文\",\"en\":\"English\"}}]}", RUNTIME_FLAG(runtime, LANGUAGE_ZH) ? "zh" : "en");
#if ESP_BMS_FEATURE_BMS
    MANIFEST_ITEM("{\"id\":\"bms_type\",\"kind\":\"select\",\"label\":{\"zh\":\"保护板类型\",\"en\":\"BMS type\"},\"value\":\"%s\",\"options\":[{\"value\":\"ant\",\"label\":{\"zh\":\"蚂蚁 ANT\",\"en\":\"ANT\"}},{\"value\":\"jk\",\"label\":{\"zh\":\"极空 JK\",\"en\":\"JK\"}},{\"value\":\"jbd\",\"label\":{\"zh\":\"嘉佰达 JBD\",\"en\":\"JBD\"}},{\"value\":\"daly\",\"label\":{\"zh\":\"达锂 Daly\",\"en\":\"Daly\"}},{\"value\":\"yanyang\",\"label\":{\"zh\":\"彦阳 BMS\",\"en\":\"Yanyang BMS\"}}]}", runtime_bms_type_config_text((esp_bms_idf_bms_type_t)runtime->bms_type));
#endif
#undef MANIFEST_ITEM
    ok = ok && runtime_json_append(json, json_len, &offset, "]}");
#if ESP_BMS_FEATURE_GPS
    ok = ok && runtime_json_append(json, json_len, &offset,
        ",{\"id\":\"records\",\"label\":{\"zh\":\"记录\",\"en\":\"Records\"},\"items\":["
        "{\"id\":\"gps_track\",\"kind\":\"map\",\"label\":{\"zh\":\"GPS 轨迹\",\"en\":\"GPS track\"},\"endpoint\":\"/api/gps/track\"}]}");
#endif
#if ESP_BMS_FEATURE_BMS
    ok = ok && runtime_json_append(json, json_len, &offset,
        ",{\"id\":\"bms\",\"label\":{\"zh\":\"保护板\",\"en\":\"BMS\"},\"items\":["
        "{\"id\":\"bms_scan\",\"kind\":\"action\",\"label\":{\"zh\":\"扫描 BMS\",\"en\":\"Scan BMS\"},\"endpoint\":\"/api/bms/scan\"},"
        "{\"id\":\"bms_mac\",\"kind\":\"choice\",\"label\":{\"zh\":\"蓝牙连接\",\"en\":\"Bluetooth connection\"},\"value\":%s,\"options_endpoint\":\"/api/bms/candidates\",\"submit_endpoint\":\"/api/bms/bind\",\"submit_key\":\"mac\"}]}", bms_mac);
#endif
#if ESP_BMS_FEATURE_OTA
    ok = ok && runtime_json_append(json, json_len, &offset,
        ",{\"id\":\"update\",\"label\":{\"zh\":\"固件更新\",\"en\":\"Firmware update\"},\"items\":["
        "{\"id\":\"ota\",\"kind\":\"upload\",\"label\":{\"zh\":\"上传并更新\",\"en\":\"Upload and update\"},\"endpoint\":\"/api/ota\",\"code_header\":\"X-Firmware-Code\",\"accept\":\".bin,application/octet-stream\"}]}");
#endif
    ok = ok && runtime_json_append(json, json_len, &offset, "]}");
    return ok;
}

static esp_err_t runtime_http_settings_manifest_handler(httpd_req_t *req,
                                                         esp_bms_idf_runtime_t *runtime)
{
    /* HTTPD serializes this handler; static storage avoids consuming its small task stack. */
    static char json[HTTP_MANIFEST_JSON_MAX_LEN];
    return runtime_settings_manifest_json(runtime, json, sizeof(json))
               ? runtime_http_send_json(req, json)
               : runtime_http_send_text(req, "500 Internal Server Error", "manifest too large");
}

static esp_err_t runtime_http_read_body(httpd_req_t *req, char *body, size_t body_len)
{
    if (req->content_len >= body_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t offset = 0;
    size_t remaining = req->content_len;
    while (remaining > 0U) {
        const int read = httpd_req_recv(req, body + offset, remaining);
        if (read <= 0) {
            return ESP_FAIL;
        }
        offset += (size_t)read;
        remaining -= (size_t)read;
    }
    body[offset] = '\0';
    return ESP_OK;
}

static bool runtime_set_pending_http_ap_password(esp_bms_idf_runtime_t *runtime, const char *password)
{
    if (!runtime->http_pending_lock) {
        return false;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    runtime_copy_snapshot_text(runtime->http_pending_setup_ap_password,
                               sizeof(runtime->http_pending_setup_ap_password),
                               password);
    RUNTIME_SET_FLAG(runtime, HTTP_SETUP_AP_PASSWORD_PENDING, true);
    xSemaphoreGive(runtime->http_pending_lock);
    return true;
}

static bool runtime_set_pending_http_bms_bind(esp_bms_idf_runtime_t *runtime, const char *mac)
{
    if (!runtime->http_pending_lock) {
        return false;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    runtime_copy_snapshot_text(runtime->http_pending_bms_bound_mac,
                               sizeof(runtime->http_pending_bms_bound_mac),
                               mac);
    RUNTIME_SET_FLAG(runtime, HTTP_BMS_BIND_PENDING, true);
    xSemaphoreGive(runtime->http_pending_lock);
    return true;
}

static bool runtime_set_pending_http_bms_scan(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime->http_pending_lock) {
        return false;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    RUNTIME_SET_FLAG(runtime, HTTP_BMS_SCAN_PENDING, true);
    xSemaphoreGive(runtime->http_pending_lock);
    return true;
}

static int runtime_apply_config_json(esp_bms_idf_runtime_t *runtime,
                                     const char *body,
                                     const char **message)
{
#define CONFIG_REJECT(text) do { *message = (text); return 400; } while (0)

    uint8_t brightness_percent = runtime->brightness_percent;
    uint8_t volume_percent = runtime->volume_percent;
    esp_bms_idf_display_rotation_t rotation = runtime->display_rotation;
    esp_bms_speed_unit_t speed_unit = runtime->snapshot.speed_unit;
    esp_bms_speed_source_t speed_source = runtime->snapshot.speed_source;
    bool language_zh = RUNTIME_FLAG(runtime, LANGUAGE_ZH);
    esp_bms_idf_bms_type_t bms_type = (esp_bms_idf_bms_type_t)runtime->bms_type;

    bool found = false;
    uint8_t parsed_u8 = 0;
    if (!runtime_json_get_u8(body, "brightness", &parsed_u8, &found)) {
        CONFIG_REJECT("invalid brightness");
    }
    if (found) {
        if (!runtime_brightness_matches_policy(parsed_u8)) {
            CONFIG_REJECT("invalid brightness");
        }
        brightness_percent = parsed_u8;
    }

    if (!runtime_json_get_u8(body, "volume", &parsed_u8, &found)) {
        CONFIG_REJECT("invalid volume");
    }
    if (found) {
        if (!runtime_volume_matches_policy(parsed_u8)) {
            CONFIG_REJECT("invalid volume");
        }
        volume_percent = parsed_u8;
    }

    char parsed_text[32] = { 0 };
    if (!runtime_json_get_string(body, "display_rotation", parsed_text, sizeof(parsed_text), &found)) {
        CONFIG_REJECT("invalid rotation");
    }
    if (found && !runtime_parse_rotation_config_text(parsed_text, &rotation)) {
        CONFIG_REJECT("invalid rotation");
    }

    memset(parsed_text, 0, sizeof(parsed_text));
    if (!runtime_json_get_string(body, "speed_unit", parsed_text, sizeof(parsed_text), &found)) {
        CONFIG_REJECT("invalid speed unit");
    }
    if (found && !runtime_parse_speed_unit_config_text(parsed_text, &speed_unit)) {
        CONFIG_REJECT("invalid speed unit");
    }

    memset(parsed_text, 0, sizeof(parsed_text));
    if (!runtime_json_get_string(body, "speed_source", parsed_text, sizeof(parsed_text), &found)) {
        CONFIG_REJECT("invalid speed source");
    }
    if (found && !runtime_parse_speed_source_config_text(parsed_text, &speed_source)) {
        CONFIG_REJECT("invalid speed source");
    }

    memset(parsed_text, 0, sizeof(parsed_text));
    if (!runtime_json_get_string(body, "language", parsed_text, sizeof(parsed_text), &found)) {
        CONFIG_REJECT("invalid language");
    }
    if (found) {
        if (strcmp(parsed_text, "zh") == 0) {
            language_zh = true;
        } else if (strcmp(parsed_text, "en") == 0) {
            language_zh = false;
        } else {
            CONFIG_REJECT("invalid language");
        }
    }

    memset(parsed_text, 0, sizeof(parsed_text));
    if (!runtime_json_get_string(body, "bms_type", parsed_text, sizeof(parsed_text), &found)) {
        CONFIG_REJECT("invalid BMS type");
    }
    if (found && !runtime_parse_bms_type_config_text(parsed_text, &bms_type)) {
        CONFIG_REJECT("invalid BMS type");
    }

    if (!runtime_set_pending_http_config(runtime, brightness_percent, volume_percent,
                                         rotation, speed_unit, speed_source,
                                         language_zh, bms_type)) {
        *message = "config queue failed";
        return 500;
    }
    *message = NULL;
    return 204;
#undef CONFIG_REJECT
}

static esp_err_t runtime_http_post_config_handler(httpd_req_t *req, esp_bms_idf_runtime_t *runtime)
{
    static char body[HTTP_BODY_MAX_LEN];
    memset(body, 0, sizeof(body));
    if (runtime_http_read_body(req, body, sizeof(body)) != ESP_OK) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid body");
    }
    const char *message = NULL;
    const int status = runtime_apply_config_json(runtime, body, &message);
    if (status == 204) {
        return runtime_http_send_no_content(req);
    }
    return runtime_http_send_text(req,
                                  status == 400 ? "400 Bad Request" : "500 Internal Server Error",
                                  message ? message : "config failed");
}

#if ESP_BMS_FEATURE_BLE
static int runtime_ble_api_access(uint16_t conn_handle,
                                  uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt,
                                  void *arg)
{
    (void)attr_handle;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR || !s_ble_api_request_queue) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    uint8_t chunk[CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU] = { 0 };
    uint16_t chunk_len = 0U;
    if (ble_hs_mbuf_to_flat(ctxt->om, chunk, sizeof(chunk), &chunk_len) != 0 || chunk_len < 1U) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    const uint8_t flags = chunk[0];
    if ((flags & ~0x03U) != 0U) {
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
    if ((flags & 0x01U) != 0U) {
        s_ble_api_fragment_conn = conn_handle;
        s_ble_api_fragment_len = 0U;
    } else if (s_ble_api_fragment_conn != conn_handle || s_ble_api_fragment_len == 0U) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    const size_t payload_len = chunk_len - 1U;
    if (payload_len > BLE_API_REQUEST_MAX_LEN - s_ble_api_fragment_len) {
        s_ble_api_fragment_len = 0U;
        s_ble_api_fragment_conn = 0xFFFFU;
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    memcpy(s_ble_api_fragment + s_ble_api_fragment_len, chunk + 1U, payload_len);
    s_ble_api_fragment_len += payload_len;
    if ((flags & 0x02U) == 0U) {
        return 0;
    }
    runtime_ble_api_request_t request = { .conn_handle = conn_handle };
    memcpy(request.json, s_ble_api_fragment, s_ble_api_fragment_len);
    request.json[s_ble_api_fragment_len] = '\0';
    s_ble_api_fragment_len = 0U;
    s_ble_api_fragment_conn = 0xFFFFU;
    return xQueueSend(s_ble_api_request_queue, &request, 0) == pdTRUE
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_svc_def BLE_API_GATT_SERVICES[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &BLE_API_SERVICE_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &BLE_API_REQUEST_UUID.u,
                .access_cb = runtime_ble_api_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                .uuid = &BLE_API_RESPONSE_UUID.u,
                .val_handle = &s_ble_api_response_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC,
            },
            { 0 },
        },
    },
    { 0 },
};

static esp_err_t runtime_ble_api_register_gatt(void)
{
    int rc = ble_gatts_count_cfg(BLE_API_GATT_SERVICES);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(BLE_API_GATT_SERVICES);
    }
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

static bool runtime_ble_api_notify_json(uint16_t conn_handle, const char *json)
{
    if (!json || conn_handle != s_ble_api_subscribed_conn) {
        return false;
    }
    const size_t json_len = strlen(json);
    const size_t payload_max = (size_t)(ble_att_mtu(conn_handle) > 4U
                                            ? ble_att_mtu(conn_handle) - 4U
                                            : 1U);
    uint8_t chunk[CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU] = { 0 };
    size_t offset = 0U;
    while (offset < json_len) {
        const size_t payload_len = json_len - offset < payload_max
                                       ? json_len - offset
                                       : payload_max;
        chunk[0] = (offset == 0U ? 0x01U : 0U) |
                   (offset + payload_len == json_len ? 0x02U : 0U);
        memcpy(chunk + 1U, json + offset, payload_len);
        int rc = BLE_HS_ENOMEM;
        for (unsigned retry = 0U; retry < BLE_API_NOTIFY_RETRY_COUNT && rc != 0; retry++) {
            struct os_mbuf *om = ble_hs_mbuf_from_flat(chunk, payload_len + 1U);
            if (!om) {
                rc = BLE_HS_ENOMEM;
            } else {
                rc = ble_gatts_notify_custom(conn_handle, s_ble_api_response_handle, om);
            }
            if (rc != 0) {
                vTaskDelay(pdMS_TO_TICKS(BLE_API_NOTIFY_RETRY_MS));
            }
        }
        if (rc != 0) {
            ESP_LOGW(TAG, "[ble-api] response notify failed: conn=%u rc=%d", conn_handle, rc);
            return false;
        }
        offset += payload_len;
        vTaskDelay(pdMS_TO_TICKS(BLE_API_NOTIFY_RETRY_MS));
    }
    return true;
}

static bool runtime_ble_api_response(esp_bms_idf_runtime_t *runtime,
                                     const runtime_ble_api_request_t *request,
                                     char *response,
                                     size_t response_len)
{
    uint32_t id = 0U;
    uint32_t version = 0U;
    int status = 400;
    const char *error = "invalid request";
    static char body[HTTP_MANIFEST_JSON_MAX_LEN];
    static char config[HTTP_BODY_MAX_LEN];
    char method[8] = { 0 };
    char path[48] = { 0 };
    bool id_found = false;
    bool version_found = false;
    bool method_found = false;
    bool path_found = false;
    bool body_found = false;
    body[0] = '\0';
    config[0] = '\0';
    const bool valid = runtime_json_get_u32(request->json, "v", &version, &version_found) &&
                       runtime_json_get_u32(request->json, "id", &id, &id_found) &&
                       runtime_json_get_string(request->json, "method", method, sizeof(method), &method_found) &&
                       runtime_json_get_string(request->json, "path", path, sizeof(path), &path_found);
    if (valid && version_found && version == 1U && id_found && method_found && path_found) {
        if (strcmp(method, "GET") == 0 && strcmp(path, "/api/status") == 0) {
            status = runtime_status_json(runtime, body, sizeof(body)) ? 200 : 500;
            error = "status unavailable";
        } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/config") == 0) {
            status = runtime_config_json(runtime, body, sizeof(body)) ? 200 : 500;
            error = "config unavailable";
        } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/settings/manifest") == 0) {
            status = runtime_settings_manifest_json(runtime, body, sizeof(body)) ? 200 : 500;
            error = "manifest unavailable";
        } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/config") == 0) {
            if (runtime_json_get_object(request->json, "body", config, sizeof(config), &body_found) && body_found) {
                status = runtime_apply_config_json(runtime, config, &error);
            } else {
                status = 400;
                error = "invalid body";
            }
        } else {
            status = 404;
            error = "path not allowed";
        }
    }
    int written = -1;
    if (status == 204) {
        written = snprintf(response, response_len,
                           "{\"id\":%lu,\"status\":204,\"body\":null}",
                           (unsigned long)id);
    } else if (status == 200 && body[0] != '\0') {
        written = snprintf(response, response_len,
                           "{\"id\":%lu,\"status\":200,\"body\":%s}",
                           (unsigned long)id, body);
    } else {
        char escaped[96] = { 0 };
        const char *message = error ? error : "request failed";
        if (!runtime_json_escape(escaped, sizeof(escaped), message, strlen(message))) return false;
        written = snprintf(response, response_len,
                           "{\"id\":%lu,\"status\":%d,\"body\":\"%s\"}",
                           (unsigned long)id, status, escaped);
    }
    return written >= 0 && (size_t)written < response_len;
}

static void runtime_ble_api_worker(void *param)
{
    esp_bms_idf_runtime_t *runtime = param;
    runtime_ble_api_request_t request;
    static char response[BLE_API_RESPONSE_MAX_LEN + 1U];
    for (;;) {
        if (xQueueReceive(s_ble_api_request_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!runtime_ble_api_response(runtime, &request, response, sizeof(response)) ||
            !runtime_ble_api_notify_json(request.conn_handle, response)) {
            ESP_LOGW(TAG, "[ble-api] request failed or client disconnected");
        }
    }
}
#endif

static esp_err_t runtime_http_post_ap_password_handler(httpd_req_t *req, esp_bms_idf_runtime_t *runtime)
{
    static char body[HTTP_BODY_MAX_LEN];
    memset(body, 0, sizeof(body));
    esp_err_t ret = runtime_http_read_body(req, body, sizeof(body));
    if (ret != ESP_OK) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid body");
    }

    char parsed_password[sizeof(runtime->setup_ap_password)] = { 0 };
    bool found = false;
    if (!runtime_json_get_string(body, "password", parsed_password, sizeof(parsed_password), &found) ||
        !found ||
        !runtime_setup_ap_password_matches_policy(parsed_password)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid password");
    }

    if (!runtime_set_pending_http_ap_password(runtime, parsed_password)) {
        return runtime_http_send_text(req, "500 Internal Server Error", "password queue failed");
    }
    return runtime_http_send_no_content(req);
}

static esp_err_t runtime_http_post_bms_bind_handler(httpd_req_t *req, esp_bms_idf_runtime_t *runtime)
{
    static char body[HTTP_BODY_MAX_LEN];
    memset(body, 0, sizeof(body));
    esp_err_t ret = runtime_http_read_body(req, body, sizeof(body));
    if (ret != ESP_OK) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid body");
    }

    char parsed_mac[18] = { 0 };
    bool found = false;
    if (!runtime_json_get_string(body, "mac", parsed_mac, sizeof(parsed_mac), &found) || !found) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid mac");
    }

    char normalized_mac[18] = { 0 };
    if (!runtime_normalize_mac_text(parsed_mac, normalized_mac, sizeof(normalized_mac))) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid mac");
    }

    if (!runtime_set_pending_http_bms_bind(runtime, normalized_mac)) {
        return runtime_http_send_text(req, "500 Internal Server Error", "bms bind queue failed");
    }
    return runtime_http_send_no_content(req);
}

static esp_err_t runtime_http_post_bms_scan_handler(httpd_req_t *req, esp_bms_idf_runtime_t *runtime)
{
    if (req->content_len > 0U) {
        static char body[HTTP_BODY_MAX_LEN];
        memset(body, 0, sizeof(body));
        if (runtime_http_read_body(req, body, sizeof(body)) != ESP_OK) {
            return runtime_http_send_text(req, "400 Bad Request", "invalid body");
        }
    }

    ESP_LOGI(TAG, "[http] BMS scan requested");
    if (!runtime_set_pending_http_bms_scan(runtime)) {
        return runtime_http_send_text(req, "500 Internal Server Error", "bms scan queue failed");
    }
    return runtime_http_send_no_content(req);
}

static esp_err_t runtime_http_cast_info_handler(httpd_req_t *req, esp_bms_idf_runtime_t *runtime)
{
    uint16_t portrait_width = 0U;
    uint16_t portrait_height = 0U;
    uint16_t landscape_width = 0U;
    uint16_t landscape_height = 0U;
    if (runtime_cast_resolution(ESP_BMS_DISPLAY_ROTATION_PORTRAIT,
                                &portrait_width,
                                &portrait_height) != ESP_OK ||
        runtime_cast_resolution(ESP_BMS_DISPLAY_ROTATION_LANDSCAPE,
                                &landscape_width,
                                &landscape_height) != ESP_OK) {
        return runtime_http_send_text(req, "500 Internal Server Error", "cast resolution unavailable");
    }

    char json[320] = { 0 };
    const int written = snprintf(json,
                                 sizeof(json),
                                 "{\"protocol_version\":%u,\"physical_width\":%u,"
                                 "\"physical_height\":%u,\"codec\":\"jpeg\",\"jpeg_quality\":60,"
                                 "\"target_fps\":20,\"max_frame_bytes\":%u,\"orientations\":["
                                 "{\"rotation\":%u,\"width\":%u,\"height\":%u},"
                                 "{\"rotation\":%u,\"width\":%u,\"height\":%u}],\"active\":%s}",
                                 ESP_BMS_CAST_PROTOCOL_VERSION,
                                 portrait_width,
                                 portrait_height,
                                 ESP_BMS_CAST_MAX_FRAME_BYTES,
                                 ESP_BMS_DISPLAY_ROTATION_PORTRAIT,
                                 portrait_width,
                                 portrait_height,
                                 ESP_BMS_DISPLAY_ROTATION_LANDSCAPE,
                                 landscape_width,
                                 landscape_height,
                                 __atomic_load_n(&runtime->cast_active, __ATOMIC_RELAXED) ? "true" : "false");
    if (written < 0 || (size_t)written >= sizeof(json)) {
        return runtime_http_send_text(req, "500 Internal Server Error", "cast info format error");
    }
    return runtime_http_send_json(req, json);
}

esp_err_t esp_bms_idf_runtime_http_cast_accept(httpd_req_t *req)
{
    esp_bms_idf_runtime_t *runtime = req ? (esp_bms_idf_runtime_t *)req->user_ctx : NULL;
    if (!runtime) {
        return ESP_FAIL;
    }
    if (__atomic_load_n(&runtime->cast_active, __ATOMIC_RELAXED) ||
        __atomic_load_n(&runtime->cast_display_active, __ATOMIC_RELAXED)) {
        httpd_resp_set_status(req, "409 Conflict");
        (void)httpd_resp_send(req, NULL, 0);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t esp_bms_idf_runtime_http_cast_connected(httpd_req_t *req)
{
    esp_bms_idf_runtime_t *runtime = req ? (esp_bms_idf_runtime_t *)req->user_ctx : NULL;
    if (!runtime) {
        return ESP_FAIL;
    }
    bool expected = false;
    if (!__atomic_compare_exchange_n(&runtime->cast_active,
                                     &expected,
                                     true,
                                     false,
                                     __ATOMIC_ACQ_REL,
                                     __ATOMIC_RELAXED)) {
        return ESP_ERR_INVALID_STATE;
    }
    runtime->cast_frame_active = false;
    runtime->cast_socket_fd = httpd_req_to_sockfd(req);
    runtime->cast_rotation = (uint8_t)ESP_BMS_DISPLAY_ROTATION_PORTRAIT;
    runtime->cast_width = 0U;
    runtime->cast_height = 0U;
    runtime->cast_sequence = 0U;
    __atomic_store_n(&runtime->cast_heartbeat_elapsed_ms, 0U, __ATOMIC_RELAXED);
    if (!runtime->cast_receive_buffer) {
        runtime->cast_receive_buffer = heap_caps_malloc(ESP_BMS_CAST_MESSAGE_MAX_BYTES,
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!runtime->cast_receive_buffer) {
            esp_bms_idf_runtime_stop_cast(runtime, "receive buffer allocation failed");
            return ESP_ERR_NO_MEM;
        }
    }
    runtime->cast_metric_frames = 0U;
    runtime->cast_metric_bytes = 0U;
    runtime->cast_metric_decode_us = 0U;
    runtime->cast_metric_present_us = 0U;
    runtime->cast_metric_total_us = 0U;
    runtime->cast_metric_started_us = esp_timer_get_time();
    const esp_bms_display_service_command_t command = {
        .kind = ESP_BMS_DISPLAY_SERVICE_COMMAND_ENTER_CAST,
    };
    const esp_err_t ret = esp_bms_display_service_submit_command(&command, 1000U);
    if (ret != ESP_OK) {
        esp_bms_idf_runtime_stop_cast(runtime, "display enter failed");
        return ret;
    }
    __atomic_store_n(&runtime->cast_display_active, true, __ATOMIC_RELEASE);
    ESP_LOGI(TAG, "[cast] client connected fd=%d", runtime->cast_socket_fd);
    return ESP_OK;
}

static esp_err_t runtime_cast_send_ack(httpd_req_t *req, uint32_t sequence)
{
    uint8_t ack[ESP_BMS_CAST_ACK_BYTES] = { 0 };
    if (!esp_bms_cast_protocol_encode_ack(sequence, ack, sizeof(ack))) {
        return ESP_ERR_INVALID_ARG;
    }
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_BINARY,
        .payload = ack,
        .len = sizeof(ack),
    };
    return httpd_ws_send_frame(req, &frame);
}

static void runtime_cast_record_metrics(esp_bms_idf_runtime_t *runtime,
                                        size_t jpeg_bytes,
                                        uint32_t decode_us,
                                        uint32_t present_us,
                                        uint32_t total_us)
{
    ++runtime->cast_metric_frames;
    runtime->cast_metric_bytes += jpeg_bytes;
    runtime->cast_metric_decode_us += decode_us;
    runtime->cast_metric_present_us += present_us;
    runtime->cast_metric_total_us += total_us;

    const int64_t now_us = esp_timer_get_time();
    const int64_t elapsed_us = now_us - runtime->cast_metric_started_us;
    if (elapsed_us < CAST_METRICS_LOG_WINDOW_US) {
        return;
    }
    const uint32_t frames = runtime->cast_metric_frames;
    const uint32_t fps_centi = elapsed_us > 0
                                   ? (uint32_t)(((uint64_t)frames * UINT64_C(100000000)) /
                                                (uint64_t)elapsed_us)
                                   : 0U;
    ESP_LOGI(TAG,
             "[cast] frames=%u avg_bytes=%u avg_decode_ms=%u.%02u avg_present_ms=%u.%02u "
             "avg_total_ms=%u.%02u fps=%u.%02u",
             (unsigned)frames,
             (unsigned)(runtime->cast_metric_bytes / frames),
             (unsigned)(runtime->cast_metric_decode_us / frames / 1000U),
             (unsigned)(runtime->cast_metric_decode_us / frames / 10U % 100U),
             (unsigned)(runtime->cast_metric_present_us / frames / 1000U),
             (unsigned)(runtime->cast_metric_present_us / frames / 10U % 100U),
             (unsigned)(runtime->cast_metric_total_us / frames / 1000U),
             (unsigned)(runtime->cast_metric_total_us / frames / 10U % 100U),
             (unsigned)(fps_centi / 100U),
             (unsigned)(fps_centi % 100U));
    runtime->cast_metric_frames = 0U;
    runtime->cast_metric_bytes = 0U;
    runtime->cast_metric_decode_us = 0U;
    runtime->cast_metric_present_us = 0U;
    runtime->cast_metric_total_us = 0U;
    runtime->cast_metric_started_us = now_us;
}

esp_err_t esp_bms_idf_runtime_http_cast_ws_handler(httpd_req_t *req)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)req->user_ctx;
    if (!runtime) {
        return ESP_FAIL;
    }
    if (!__atomic_load_n(&runtime->cast_active, __ATOMIC_ACQUIRE) ||
        !__atomic_load_n(&runtime->cast_display_active, __ATOMIC_ACQUIRE) ||
        runtime->cast_socket_fd != httpd_req_to_sockfd(req)) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_ws_frame_t frame = { 0 };
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        esp_bms_idf_runtime_stop_cast(runtime, "socket closed");
        return ret;
    }
    if (frame.type != HTTPD_WS_TYPE_BINARY || frame.len == 0U ||
        frame.len > ESP_BMS_CAST_MESSAGE_MAX_BYTES) {
        esp_bms_idf_runtime_stop_cast(runtime, "invalid frame");
        return ESP_ERR_INVALID_SIZE;
    }
    if (!runtime->cast_receive_buffer) {
        esp_bms_idf_runtime_stop_cast(runtime, "receive buffer unavailable");
        return ESP_ERR_NO_MEM;
    }
    const int64_t frame_started_us = esp_timer_get_time();
    frame.payload = runtime->cast_receive_buffer;
    ret = httpd_ws_recv_frame(req, &frame, frame.len);
    if (ret != ESP_OK) {
        esp_bms_idf_runtime_stop_cast(runtime, "socket read failed");
        return ret;
    }
    __atomic_store_n(&runtime->cast_heartbeat_elapsed_ms, 0U, __ATOMIC_RELAXED);

    if (esp_bms_cast_protocol_is_heartbeat(runtime->cast_receive_buffer, frame.len)) {
        return ESP_OK;
    }
    esp_bms_cast_jpeg_frame_t jpeg_frame = { 0 };
    if (!esp_bms_cast_protocol_parse_jpeg_frame(runtime->cast_receive_buffer,
                                                frame.len,
                                                &jpeg_frame)) {
        esp_bms_idf_runtime_stop_cast(runtime, "invalid JPEG frame");
        return ESP_ERR_INVALID_SIZE;
    }
    esp_bms_lvgl_bridge_cast_metrics_t metrics = { 0 };
    const esp_bms_display_service_command_t command = {
        .kind = ESP_BMS_DISPLAY_SERVICE_COMMAND_PRESENT_JPEG,
        .data.jpeg = {
            .sequence = jpeg_frame.sequence,
            .rotation = (esp_bms_display_rotation_t)jpeg_frame.rotation,
            .bytes = jpeg_frame.jpeg,
            .byte_count = jpeg_frame.jpeg_bytes,
            .metrics = &metrics,
        },
    };
    /* The queued command borrows both the receive buffer and stack metrics. */
    ret = esp_bms_display_service_submit_command(&command, UINT32_MAX);
    if (ret != ESP_OK) {
        esp_bms_idf_runtime_stop_cast(runtime, "JPEG present failed");
        return ret;
    }
    const int64_t total_elapsed_us = esp_timer_get_time() - frame_started_us;
    const uint32_t total_us = total_elapsed_us <= 0
                                  ? 0U
                                  : total_elapsed_us > UINT32_MAX
                                        ? UINT32_MAX
                                        : (uint32_t)total_elapsed_us;
    runtime_cast_record_metrics(runtime,
                                jpeg_frame.jpeg_bytes,
                                metrics.decode_us,
                                metrics.present_us,
                                total_us);
    ret = runtime_cast_send_ack(req, jpeg_frame.sequence);
    if (ret != ESP_OK) {
        esp_bms_idf_runtime_stop_cast(runtime, "ack send failed");
    }
    return ret;
}

static bool runtime_http_uri_is(const char *uri, const char *path)
{
    if (!uri || !path) return false;
    const size_t len = strcspn(uri, "?");
    return strlen(path) == len && strncmp(uri, path, len) == 0;
}

static uint64_t runtime_http_query_u64(httpd_req_t *req, const char *key, uint64_t fallback)
{
    char value[24] = {0};
    char query[128] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, key, value, sizeof(value)) != ESP_OK) return fallback;
    char *end = NULL;
    const unsigned long long parsed = strtoull(value, &end, 10);
    return end == value ? fallback : (uint64_t)parsed;
}

static size_t runtime_http_query_limit(httpd_req_t *req)
{
    uint64_t value = runtime_http_query_u64(req, "limit", 200U);
    if (value == 0U) value = 200U;
    return value > 500U ? 500U : (size_t)value;
}

static esp_err_t runtime_http_history_sessions_handler(httpd_req_t *req, esp_bms_idf_runtime_t *runtime)
{
    static char json[768];
    size_t used = 0;
    used += (size_t)snprintf(json + used, sizeof(json) - used,
                             "{\"version\":1,\"ready\":%s,\"backend\":\"flash\",\"capacity_bytes\":%u,\"sessions\":[",
                             esp_bms_flashdb_ready() ? "true" : "false",
                             (unsigned)esp_bms_flashdb_capacity_bytes());
    bool first = true;
    for (size_t i = 0; i < ESP_BMS_FLASHDB_MAX_SESSIONS; ++i) {
        esp_bms_flashdb_session_t session;
        if (esp_bms_flashdb_get_session(i, &session) != ESP_OK) continue;
        used += (size_t)snprintf(json + used, sizeof(json) - used,
                                 "%s{\"id\":%llu,\"start_s\":%llu,\"end_s\":%llu,\"samples\":%u,\"capacity_samples\":%u,\"elapsed_s\":%u,\"calibrated\":%s,\"truncated\":%s,\"capacity_reached\":%s}",
                                 first ? "" : ",", (unsigned long long)session.session_id,
                                 (unsigned long long)session.start_time_s,
                                 (unsigned long long)session.end_time_s,
                                 (unsigned)session.sample_count, (unsigned)session.capacity_samples,
                                 (unsigned)session.elapsed_seconds, session.calibrated ? "true" : "false",
                                 session.truncated ? "true" : "false", session.capacity_reached ? "true" : "false");
        first = false;
        if (used + 160U >= sizeof(json)) break;
    }
    used += (size_t)snprintf(json + used, sizeof(json) - used, "]}");
    (void)runtime;
    return runtime_http_send_json(req, json);
}

typedef struct { httpd_req_t *req; uint64_t last; bool first; } history_http_query_t;
static bool runtime_http_sample_json_cb(uint64_t timestamp, const esp_bms_flashdb_sample_t *sample, void *ctx)
{
    history_http_query_t *query = ctx;
    char json[320];
    const int len = snprintf(json, sizeof(json),
                             "%s{\"t\":%llu,\"elapsed_s\":%u,\"flags\":%u,\"lat_e7\":%ld,\"lon_e7\":%ld,\"pack_voltage_mv\":%u,\"current_deci_amps\":%d,\"soc_percent\":%u,\"cell_min_mv\":%u,\"cell_avg_mv\":%u,\"cell_max_mv\":%u,\"cell_delta_mv\":%u,\"temperatures_c\":[%d,%d,%d,%d,%d,%d]}",
                             query->first ? "" : ",", (unsigned long long)timestamp, (unsigned)sample->elapsed_s,
                             (unsigned)sample->flags, (long)sample->latitude_e7, (long)sample->longitude_e7,
                             (unsigned)sample->pack_voltage_mv, (int)sample->current_deci_amps,
                             (unsigned)sample->soc_percent, (unsigned)sample->cell_min_mv, (unsigned)sample->cell_avg_mv,
                             (unsigned)sample->cell_max_mv, (unsigned)sample->cell_delta_mv,
                             sample->temperatures_c[0], sample->temperatures_c[1], sample->temperatures_c[2],
                             sample->temperatures_c[3], sample->temperatures_c[4], sample->temperatures_c[5]);
    if (len <= 0 || (size_t)len >= sizeof(json) || httpd_resp_send_chunk(query->req, json, len) != ESP_OK) return false;
    query->first = false;
    query->last = timestamp;
    return true;
}

static esp_err_t runtime_http_history_samples_handler(httpd_req_t *req, esp_bms_idf_runtime_t *runtime)
{
    const uint64_t session = runtime_http_query_u64(req, "session", 0U);
    if (session && !esp_bms_flashdb_has_session(session))
        return runtime_http_send_text(req, "404 Not Found", "session not found");
    uint64_t from = runtime_http_query_u64(req, "from", 0U);
    const uint64_t to = runtime_http_query_u64(req, "to", UINT64_MAX);
    const uint64_t cursor = runtime_http_query_u64(req, "cursor", UINT64_MAX);
    if (cursor < UINT64_MAX && cursor + 1U > from) from = cursor + 1U;
    history_http_query_t query = {.req = req, .first = true};
    ESP_RETURN_ON_ERROR(runtime_http_set_common_headers(req), TAG, "set headers failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "application/json"), TAG, "set type failed");
    ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(req, "{\"version\":1,\"samples\":[", HTTPD_RESP_USE_STRLEN), TAG, "send failed");
    size_t returned = 0;
    esp_err_t ret = session ? esp_bms_flashdb_query_session_samples(session, from, to, runtime_http_query_limit(req), runtime_http_sample_json_cb, &query, &returned)
                            : esp_bms_flashdb_query_samples(from, to, runtime_http_query_limit(req), runtime_http_sample_json_cb, &query, &returned);
    if (ret == ESP_ERR_NOT_FOUND) return runtime_http_send_text(req, "404 Not Found", "session not found");
    ESP_RETURN_ON_ERROR(ret, TAG, "sample query failed");
    char tail[96];
    if (returned) {
        snprintf(tail, sizeof(tail), "],\"returned\":%u,\"next_cursor\":%llu}",
                 (unsigned)returned, (unsigned long long)query.last);
    } else {
        snprintf(tail, sizeof(tail), "],\"returned\":0,\"next_cursor\":null}");
    }
    ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(req, tail, HTTPD_RESP_USE_STRLEN), TAG, "send failed");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static bool runtime_http_fault_json_cb(const esp_bms_flashdb_fault_t *fault, void *ctx)
{
    history_http_query_t *query = ctx;
    char json[160];
    const int len = snprintf(json, sizeof(json), "%s{\"t\":%llu,\"session\":%llu,\"active_mask\":%u,\"supported_mask\":%u,\"bms_type\":%u}",
                             query->first ? "" : ",", (unsigned long long)fault->timestamp,
                             (unsigned long long)fault->session_id,
                             (unsigned)fault->active_mask, (unsigned)fault->supported_mask, (unsigned)fault->bms_type);
    if (len <= 0 || (size_t)len >= sizeof(json) || httpd_resp_send_chunk(query->req, json, len) != ESP_OK) return false;
    query->first = false;
    query->last = fault->timestamp;
    return true;
}

static esp_err_t runtime_http_history_faults_handler(httpd_req_t *req, esp_bms_idf_runtime_t *runtime)
{
    const uint64_t session = runtime_http_query_u64(req, "session", 0U);
    if (session && !esp_bms_flashdb_has_session(session))
        return runtime_http_send_text(req, "404 Not Found", "session not found");
    uint64_t from = runtime_http_query_u64(req, "from", 0U);
    const uint64_t to = runtime_http_query_u64(req, "to", UINT64_MAX);
    const uint64_t cursor = runtime_http_query_u64(req, "cursor", UINT64_MAX);
    if (cursor < UINT64_MAX && cursor + 1U > from) from = cursor + 1U;
    history_http_query_t query = {.req = req, .first = true};
    ESP_RETURN_ON_ERROR(runtime_http_set_common_headers(req), TAG, "set headers failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "application/json"), TAG, "set type failed");
    ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(req, "{\"version\":1,\"faults\":[", HTTPD_RESP_USE_STRLEN), TAG, "send failed");
    size_t returned = 0;
    esp_err_t ret = esp_bms_flashdb_query_faults(session, from, to, runtime_http_query_limit(req),
                                                 runtime_http_fault_json_cb, &query, &returned);
    if (ret == ESP_ERR_NOT_FOUND) return runtime_http_send_text(req, "404 Not Found", "session not found");
    ESP_RETURN_ON_ERROR(ret, TAG, "fault query failed");
    char tail[96];
    if (returned) {
        snprintf(tail, sizeof(tail), "],\"returned\":%u,\"next_cursor\":%llu}",
                 (unsigned)returned, (unsigned long long)query.last);
    } else {
        snprintf(tail, sizeof(tail), "],\"returned\":0,\"next_cursor\":null}");
    }
    ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(req, tail, HTTPD_RESP_USE_STRLEN), TAG, "send failed");
    (void)runtime;
    return httpd_resp_send_chunk(req, NULL, 0);
}

esp_err_t esp_bms_idf_runtime_http_api_handler(httpd_req_t *req)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)req->user_ctx;
    if (!runtime) {
        return runtime_http_send_text(req, "500 Internal Server Error", "runtime missing");
    }

    if (req->method == HTTP_OPTIONS) {
        return runtime_http_send_no_content(req);
    }
    ESP_LOGI(TAG,
             "[http] api request: method=%d uri=%s clients=%u",
             req->method,
             req->uri,
             (unsigned)runtime->setup_ap_clients);
    runtime_log_heap_state("http_api");

    if (req->method == HTTP_GET && runtime_http_uri_is(req->uri, "/api/history/sessions")) {
        return runtime_http_history_sessions_handler(req, runtime);
    }
    if (req->method == HTTP_GET && runtime_http_uri_is(req->uri, "/api/history/samples")) {
        return runtime_http_history_samples_handler(req, runtime);
    }
    if (req->method == HTTP_GET && runtime_http_uri_is(req->uri, "/api/history/faults")) {
        return runtime_http_history_faults_handler(req, runtime);
    }
    if (req->method == HTTP_GET && strcmp(req->uri, "/api/status") == 0) {
        return runtime_http_status_handler(req, runtime);
    }
    if (req->method == HTTP_GET && strcmp(req->uri, "/api/bms/ride-records") == 0) {
        return runtime_http_ride_records_handler(req, runtime);
    }
    if (req->method == HTTP_GET && strcmp(req->uri, "/api/gps/track") == 0) {
        return runtime_http_gps_track_handler(req, runtime);
    }
    if (req->method == HTTP_GET && strcmp(req->uri, "/api/cast/info") == 0) {
        return runtime_http_cast_info_handler(req, runtime);
    }
    if (req->method == HTTP_GET && strcmp(req->uri, "/api/config") == 0) {
        return runtime_http_config_handler(req, runtime);
    }
    if (req->method == HTTP_GET && strcmp(req->uri, "/api/settings/manifest") == 0) {
        return runtime_http_settings_manifest_handler(req, runtime);
    }
    if (req->method == HTTP_GET && strcmp(req->uri, "/api/bms/candidates") == 0) {
        return runtime_http_bms_candidates_handler(req, runtime);
    }
    if (req->method == HTTP_POST && strcmp(req->uri, "/api/config") == 0) {
        return runtime_http_post_config_handler(req, runtime);
    }
    if (req->method == HTTP_POST && strcmp(req->uri, "/api/ap-password") == 0) {
        return runtime_http_post_ap_password_handler(req, runtime);
    }
    if (req->method == HTTP_POST && strcmp(req->uri, "/api/bms/scan") == 0) {
        return runtime_http_post_bms_scan_handler(req, runtime);
    }
    if (req->method == HTTP_POST && strcmp(req->uri, "/api/bms/bind") == 0) {
        return runtime_http_post_bms_bind_handler(req, runtime);
    }
    if (req->method == HTTP_GET && strcmp(req->uri, "/api/ota/progress") == 0) {
#if ESP_BMS_FEATURE_OTA
        return esp_bms_ota_handle_progress_request(req);
#else
        return runtime_http_send_text(req, "501 Not Implemented", "not implemented");
#endif
    }
    if (req->method == HTTP_POST && strcmp(req->uri, "/api/ota") == 0) {
#if ESP_BMS_FEATURE_OTA
        return esp_bms_ota_handle_http_request(req);
#else
        return runtime_http_send_text(req, "501 Not Implemented", "not implemented");
#endif
    }
    if (runtime->optional_http_handler) {
        const esp_err_t optional_result =
            runtime->optional_http_handler(req, runtime->optional_http_context);
        if (optional_result != ESP_ERR_NOT_FOUND) {
            return optional_result;
        }
    }
    ESP_LOGI(TAG, "[http] route not implemented: method=%d uri=%s", req->method, req->uri);
    return runtime_http_send_text(req, "501 Not Implemented", "not implemented");
}

static esp_err_t runtime_load_setup_ap_credentials(esp_bms_idf_runtime_t *runtime)
{
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(SETUP_AP_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    char ssid[sizeof(runtime->setup_ap_ssid)] = { 0 };
    char password[sizeof(runtime->setup_ap_password)] = { 0 };
    size_t ssid_len = sizeof(ssid);
    size_t password_len = sizeof(password);

    ret = nvs_get_str(handle, SETUP_AP_NVS_SSID_KEY, ssid, &ssid_len);
    if (ret == ESP_OK) {
        ret = nvs_get_str(handle, SETUP_AP_NVS_PASSWORD_KEY, password, &password_len);
    }
    nvs_close(handle);
    if (ret != ESP_OK) {
        return ret;
    }

    if (!runtime_setup_ap_ssid_matches_policy(ssid) ||
        !runtime_setup_ap_password_matches_policy(password)) {
        return ESP_ERR_INVALID_STATE;
    }

    runtime_copy_snapshot_text(runtime->setup_ap_ssid, sizeof(runtime->setup_ap_ssid), ssid);
    runtime_copy_snapshot_text(runtime->setup_ap_password, sizeof(runtime->setup_ap_password), password);
    runtime_update_setup_ap_snapshot(runtime);
    return ESP_OK;
}

static esp_err_t runtime_save_setup_ap_credentials(const esp_bms_idf_runtime_t *runtime)
{
    if (!runtime_setup_ap_ssid_matches_policy(runtime->setup_ap_ssid) ||
        !runtime_setup_ap_password_matches_policy(runtime->setup_ap_password)) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(SETUP_AP_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_str(handle, SETUP_AP_NVS_SSID_KEY, runtime->setup_ap_ssid);
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, SETUP_AP_NVS_PASSWORD_KEY, runtime->setup_ap_password);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

esp_err_t esp_bms_idf_runtime_load_bms_binding(esp_bms_idf_runtime_t *runtime)
{
    esp_err_t ret = runtime_init_nvs(runtime);
    if (ret != ESP_OK) {
        return ret;
    }

    nvs_handle_t handle = 0;
    ret = nvs_open(SETUP_AP_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    char mac[sizeof(runtime->bms_bound_mac)] = { 0 };
    char name[sizeof(runtime->bms_bound_name)] = { 0 };
    size_t mac_len = sizeof(mac);
    ret = nvs_get_str(handle, BMS_NVS_BOUND_MAC_KEY, mac, &mac_len);
    size_t name_len = sizeof(name);
    const esp_err_t name_ret = nvs_get_str(handle, BMS_NVS_BOUND_NAME_KEY, name, &name_len);
    nvs_close(handle);
    if (ret != ESP_OK) {
        return ret;
    }

    char normalized_mac[sizeof(runtime->bms_bound_mac)] = { 0 };
    if (!runtime_normalize_mac_text(mac, normalized_mac, sizeof(normalized_mac))) {
        runtime->bms_bound_mac[0] = '\0';
        return ESP_ERR_INVALID_STATE;
    }

    runtime_copy_snapshot_text(runtime->bms_bound_mac, sizeof(runtime->bms_bound_mac), normalized_mac);
    if (name_ret == ESP_OK) {
        (void)runtime_bms_name_copy(runtime->bms_bound_name,
                                    sizeof(runtime->bms_bound_name),
                                    (const uint8_t *)name,
                                    strlen(name));
    } else {
        runtime->bms_bound_name[0] = '\0';
    }
    runtime_copy_snapshot_text(runtime->snapshot.bms_bound_name,
                               sizeof(runtime->snapshot.bms_bound_name),
                               runtime->bms_bound_name);
    return ESP_OK;
}

static esp_err_t runtime_save_bms_binding(esp_bms_idf_runtime_t *runtime)
{
    if (runtime->bms_bound_mac[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    char normalized_mac[sizeof(runtime->bms_bound_mac)] = { 0 };
    if (!runtime_normalize_mac_text(runtime->bms_bound_mac, normalized_mac, sizeof(normalized_mac))) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = runtime_init_nvs(runtime);
    if (ret != ESP_OK) {
        return ret;
    }

    nvs_handle_t handle = 0;
    ret = nvs_open(SETUP_AP_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_str(handle, BMS_NVS_BOUND_MAC_KEY, normalized_mac);
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, BMS_NVS_BOUND_NAME_KEY, runtime->bms_bound_name);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static bool runtime_get_pending_http_ap_password(esp_bms_idf_runtime_t *runtime,
                                                 char *password,
                                                 size_t password_len)
{
    if (!runtime->http_pending_lock) {
        return false;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, 0) != pdTRUE) {
        return false;
    }
    const bool pending = RUNTIME_FLAG(runtime, HTTP_SETUP_AP_PASSWORD_PENDING);
    if (pending) {
        runtime_copy_snapshot_text(password, password_len, runtime->http_pending_setup_ap_password);
    }
    xSemaphoreGive(runtime->http_pending_lock);
    return pending;
}

static void runtime_clear_pending_http_ap_password(esp_bms_idf_runtime_t *runtime, const char *password)
{
    if (!runtime->http_pending_lock) {
        return;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    if (RUNTIME_FLAG(runtime, HTTP_SETUP_AP_PASSWORD_PENDING) &&
        strcmp(runtime->http_pending_setup_ap_password, password) == 0) {
        runtime->http_pending_setup_ap_password[0] = '\0';
        RUNTIME_SET_FLAG(runtime, HTTP_SETUP_AP_PASSWORD_PENDING, false);
    }
    xSemaphoreGive(runtime->http_pending_lock);
}

static bool runtime_apply_pending_http_ap_password(esp_bms_idf_runtime_t *runtime)
{
    char password[sizeof(runtime->setup_ap_password)] = { 0 };
    if (!runtime_get_pending_http_ap_password(runtime, password, sizeof(password))) {
        return false;
    }

    char previous_password[sizeof(runtime->setup_ap_password)] = { 0 };
    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    runtime_copy_snapshot_text(previous_password, sizeof(previous_password), runtime->setup_ap_password);
    if (strcmp(password, runtime->setup_ap_password) == 0) {
        xSemaphoreGive(runtime->http_pending_lock);
        runtime_clear_pending_http_ap_password(runtime, password);
        RUNTIME_SET_FLAG(runtime, HTTP_CONFIG_APPLIED, true);
        return false;
    }
    runtime_copy_snapshot_text(runtime->setup_ap_password, sizeof(runtime->setup_ap_password), password);
    xSemaphoreGive(runtime->http_pending_lock);
    runtime_update_setup_ap_snapshot(runtime);

    esp_err_t ret = ESP_OK;
    if (RUNTIME_FLAG(runtime, SETUP_AP_STARTED)) {
        ret = runtime->network_driver && runtime->network_driver->refresh_setup_ap_config
                  ? runtime->network_driver->refresh_setup_ap_config(runtime)
                  : ESP_ERR_NOT_SUPPORTED;
    }
    if (ret == ESP_OK) {
        ret = runtime_save_setup_ap_credentials(runtime);
    }
    if (ret == ESP_OK) {
        runtime_clear_pending_http_ap_password(runtime, password);
        RUNTIME_SET_FLAG(runtime, HTTP_CONFIG_APPLIED, true);
        runtime_set_error(runtime, "AP PW SET");
        ESP_LOGI(TAG, "[wifi] setup AP password updated: ssid='%s' ap_pw_len=%u",
                 runtime->setup_ap_ssid, (unsigned)strlen(runtime->setup_ap_password));
        return true;
    }

    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        runtime_copy_snapshot_text(runtime->setup_ap_password,
                                   sizeof(runtime->setup_ap_password),
                                   previous_password);
        xSemaphoreGive(runtime->http_pending_lock);
    }
    runtime_update_setup_ap_snapshot(runtime);
    if (RUNTIME_FLAG(runtime, SETUP_AP_STARTED) && runtime->network_driver &&
        runtime->network_driver->refresh_setup_ap_config) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(runtime->network_driver->refresh_setup_ap_config(runtime));
    }
    runtime_set_error(runtime, "AP PW FAIL");
    ESP_LOGW(TAG, "[wifi] setup AP password update failed: %s", esp_err_to_name(ret));
    return true;
}

static bool runtime_get_pending_http_bms_scan(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime->http_pending_lock) {
        return false;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, 0) != pdTRUE) {
        return false;
    }
    const bool pending = RUNTIME_FLAG(runtime, HTTP_BMS_SCAN_PENDING);
    RUNTIME_SET_FLAG(runtime, HTTP_BMS_SCAN_PENDING, false);
    xSemaphoreGive(runtime->http_pending_lock);
    return pending;
}

static bool runtime_apply_pending_http_bms_scan(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime_get_pending_http_bms_scan(runtime)) {
        return false;
    }

    ESP_LOGI(TAG, "[bms] consume pending BLE scan request");
    RUNTIME_SET_FLAG(runtime, BMS_BIND_ACTIVE, false);
    const esp_err_t ret = runtime->bms_ble_driver && runtime->bms_ble_driver->start_for_bind
                              ? runtime->bms_ble_driver->start_for_bind(runtime)
                              : ESP_ERR_NOT_SUPPORTED;
    if (ret == ESP_OK) {
        runtime_log_heap_state("bms_scan_started");
        return true;
    }

    runtime_set_bms_info(runtime, ret == ESP_ERR_NO_MEM ? "BLE MEM" : "BLE FAIL");
    ESP_LOGW(TAG, "[bms] BLE bind scan start failed: %s", esp_err_to_name(ret));
    runtime_log_heap_state("bms_scan_failed");
    return true;
}

static bool runtime_get_pending_http_bms_bind(esp_bms_idf_runtime_t *runtime,
                                              char *mac,
                                              size_t mac_len)
{
    if (!runtime->http_pending_lock) {
        return false;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, 0) != pdTRUE) {
        return false;
    }
    const bool pending = RUNTIME_FLAG(runtime, HTTP_BMS_BIND_PENDING);
    if (pending) {
        runtime_copy_snapshot_text(mac, mac_len, runtime->http_pending_bms_bound_mac);
    }
    xSemaphoreGive(runtime->http_pending_lock);
    return pending;
}

static void runtime_clear_pending_http_bms_bind(esp_bms_idf_runtime_t *runtime, const char *mac)
{
    if (!runtime->http_pending_lock) {
        return;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    if (RUNTIME_FLAG(runtime, HTTP_BMS_BIND_PENDING) &&
        strcmp(runtime->http_pending_bms_bound_mac, mac) == 0) {
        runtime->http_pending_bms_bound_mac[0] = '\0';
        RUNTIME_SET_FLAG(runtime, HTTP_BMS_BIND_PENDING, false);
    }
    xSemaphoreGive(runtime->http_pending_lock);
}

static bool runtime_apply_pending_http_bms_bind(esp_bms_idf_runtime_t *runtime)
{
    char mac[sizeof(runtime->bms_bound_mac)] = { 0 };
    if (!runtime_get_pending_http_bms_bind(runtime, mac, sizeof(mac))) {
        return false;
    }

    char previous_mac[sizeof(runtime->bms_bound_mac)] = { 0 };
    char previous_name[sizeof(runtime->bms_bound_name)] = { 0 };
    char selected_name[sizeof(runtime->bms_bound_name)] = { 0 };
    if (!runtime->bms_scan_lock ||
        xSemaphoreTake(runtime->bms_scan_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        const size_t candidate_index = runtime_bms_scan_find_candidate(runtime, mac);
        if (candidate_index < ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES &&
            runtime->bms_scan_candidates[candidate_index].has_name) {
            runtime_copy_snapshot_text(selected_name,
                                       sizeof(selected_name),
                                       runtime->bms_scan_candidates[candidate_index].name);
        }
        if (runtime->bms_scan_lock) {
            xSemaphoreGive(runtime->bms_scan_lock);
        }
    }
    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    runtime_copy_snapshot_text(previous_mac, sizeof(previous_mac), runtime->bms_bound_mac);
    runtime_copy_snapshot_text(previous_name, sizeof(previous_name), runtime->bms_bound_name);
    if (strcmp(mac, runtime->bms_bound_mac) != 0) {
        runtime_copy_snapshot_text(runtime->bms_bound_mac, sizeof(runtime->bms_bound_mac), mac);
        runtime_copy_snapshot_text(runtime->bms_bound_name,
                                   sizeof(runtime->bms_bound_name),
                                   selected_name);
        runtime->snapshot.bms_capacity_estimate_mah = 0U;
    } else if (selected_name[0] != '\0') {
        runtime_copy_snapshot_text(runtime->bms_bound_name,
                                   sizeof(runtime->bms_bound_name),
                                   selected_name);
    }
    runtime_copy_snapshot_text(runtime->snapshot.bms_bound_name,
                               sizeof(runtime->snapshot.bms_bound_name),
                               runtime->bms_bound_name);
    xSemaphoreGive(runtime->http_pending_lock);

    esp_err_t ret = runtime_save_bms_binding(runtime);
    if (ret == ESP_OK) {
        runtime_clear_pending_http_bms_bind(runtime, mac);
        RUNTIME_SET_FLAG(runtime, BMS_BIND_ACTIVE, true);
        ESP_LOGI(TAG, "[bms] bound MAC saved: mac=%s", mac);
        const esp_err_t scan_ret = runtime->bms_ble_driver &&
                                           runtime->bms_ble_driver->start_for_bind
                                       ? runtime->bms_ble_driver->start_for_bind(runtime)
                                       : ESP_ERR_NOT_SUPPORTED;
        if (scan_ret != ESP_OK) {
            runtime_set_bms_info(runtime, "BLE FAIL");
            ESP_LOGW(TAG, "[bms] scan after bind start failed: %s", esp_err_to_name(scan_ret));
        }
        return true;
    }

    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        runtime_copy_snapshot_text(runtime->bms_bound_mac,
                                   sizeof(runtime->bms_bound_mac),
                                   previous_mac);
        runtime_copy_snapshot_text(runtime->bms_bound_name,
                                   sizeof(runtime->bms_bound_name),
                                   previous_name);
        runtime_copy_snapshot_text(runtime->snapshot.bms_bound_name,
                                   sizeof(runtime->snapshot.bms_bound_name),
                                   previous_name);
        xSemaphoreGive(runtime->http_pending_lock);
    }
    runtime_set_bms_info(runtime, "BMS SAVE");
    ESP_LOGW(TAG, "[bms] bound MAC save failed: %s", esp_err_to_name(ret));
    return true;
}

static void runtime_ensure_setup_ap_credentials(esp_bms_idf_runtime_t *runtime)
{
    esp_err_t nvs_ret = runtime_init_nvs(runtime);
    if (nvs_ret == ESP_OK) {
        const esp_err_t load_ret = runtime_load_setup_ap_credentials(runtime);
        if (load_ret == ESP_OK) {
            ESP_LOGI(TAG, "[wifi] setup AP credentials loaded: ssid='%s' ap_pw_len=%u",
                     runtime->setup_ap_ssid, (unsigned)strlen(runtime->setup_ap_password));
            return;
        }
        if (load_ret == ESP_ERR_NVS_NOT_FOUND || load_ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGI(TAG, "[wifi] setup AP credentials missing or stale; regenerating");
        } else {
            ESP_LOGW(TAG, "[wifi] setup AP credential load failed: %s", esp_err_to_name(load_ret));
        }
    } else {
        ESP_LOGW(TAG, "[wifi] NVS init for setup AP credentials failed: %s", esp_err_to_name(nvs_ret));
    }

    runtime_generate_setup_ap_credentials(runtime);
    if (nvs_ret != ESP_OK) {
        ESP_LOGW(TAG, "[wifi] using volatile setup AP credentials: ssid='%s' ap_pw_len=%u",
                 runtime->setup_ap_ssid, (unsigned)strlen(runtime->setup_ap_password));
        return;
    }

    const esp_err_t save_ret = runtime_save_setup_ap_credentials(runtime);
    if (save_ret == ESP_OK) {
        ESP_LOGI(TAG, "[wifi] setup AP credentials regenerated and saved: ssid='%s' ap_pw_len=%u",
                 runtime->setup_ap_ssid, (unsigned)strlen(runtime->setup_ap_password));
    } else {
        ESP_LOGW(TAG, "[wifi] setup AP credential save failed: %s", esp_err_to_name(save_ret));
    }
}

#if ESP_BMS_FEATURE_BLE
static int runtime_bluetooth_gap_event(struct ble_gap_event *event, void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || !event) {
        return 0;
    }

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            runtime->bluetooth_conn_handle = event->connect.conn_handle;
#if ESP_BMS_FEATURE_BLE_MEDIA_HID
            runtime->bluetooth_pair_deadline_us =
                esp_timer_get_time() + (int64_t)LOCAL_BLUETOOTH_PAIR_TIMEOUT_MS * 1000;
            runtime->bluetooth_pair_initiate_at_us =
                esp_timer_get_time() +
                (int64_t)LOCAL_BLUETOOTH_PAIR_INITIATE_DELAY_MS * 1000;
#else
            runtime->bluetooth_pair_deadline_us =
                esp_timer_get_time() + (int64_t)LOCAL_BLUETOOTH_PAIR_TIMEOUT_MS * 1000;
            runtime->bluetooth_pair_initiate_at_us =
                esp_timer_get_time() +
                (int64_t)LOCAL_BLUETOOTH_PAIR_INITIATE_DELAY_MS * 1000;
#endif
#if !ESP_BMS_FEATURE_BLE_MEDIA_HID
            esp_bms_idf_runtime_request_coded_phy(event->connect.conn_handle, "local");
#else
            ESP_LOGI(TAG, "[bt] HID pairing keeps default PHY: conn=%u",
                     event->connect.conn_handle);
#endif
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_CONNECTED, false);
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, false);
#if ESP_BMS_FEATURE_BLE_MEDIA_HID
            runtime_ble_media_hid_snapshot_clear(runtime);
#endif
            (void)runtime_project_bluetooth_snapshot(runtime);
            runtime_set_error(runtime, "BT PAIR");
#if ESP_BMS_FEATURE_BLE_MEDIA_HID
            ESP_LOGI(TAG,
                     "[bt] local Bluetooth connected; HID pairing will start after %u ms: conn=%u",
                     (unsigned)LOCAL_BLUETOOTH_PAIR_INITIATE_DELAY_MS,
                     event->connect.conn_handle);
#else
            /* Delay the security initiate by a short window: some Android
             * system stacks start their own pairing on connect and react
             * badly to an immediate Slave Security Request. The deferred
             * initiate still guarantees pairing for app-driven GATT
             * connections that never initiate themselves. */
            ESP_LOGI(TAG, "[bt] local Bluetooth connected; pairing will start after %u ms: conn=%u",
                     (unsigned)LOCAL_BLUETOOTH_PAIR_INITIATE_DELAY_MS,
                     event->connect.conn_handle);
#endif
        } else {
            runtime->bluetooth_conn_handle = 0xFFFFU;
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_CONNECTED, false);
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, false);
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, true);
#if ESP_BMS_FEATURE_BLE_MEDIA_HID
            runtime_ble_media_hid_snapshot_clear(runtime);
#endif
            (void)runtime_project_bluetooth_snapshot(runtime);
            ESP_LOGW(TAG, "[bt] local Bluetooth connection failed: status=%d", event->connect.status);
        }
        return 0;
#if CONFIG_BT_NIMBLE_LL_CFG_FEAT_LE_CODED_PHY
    case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
        if (event->phy_updated.conn_handle == runtime->bluetooth_conn_handle &&
            event->phy_updated.status != 0) {
            ESP_LOGW(TAG, "[ble] local Coded PHY unavailable: conn=%u status=%d",
                     event->phy_updated.conn_handle, event->phy_updated.status);
        }
        return 0;
#endif
    case BLE_GAP_EVENT_DISCONNECT:
        if (event->disconnect.conn.conn_handle == s_ble_api_subscribed_conn) {
            s_ble_api_subscribed_conn = 0xFFFFU;
            s_ble_api_fragment_conn = 0xFFFFU;
            s_ble_api_fragment_len = 0U;
        }
        if (event->disconnect.conn.conn_handle == runtime->bluetooth_conn_handle) {
            const bool start_bms_scan = RUNTIME_FLAG(runtime, BMS_SCAN_REQUESTED);
            const bool resume_advertising =
                RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED) && !start_bms_scan;
            runtime->bluetooth_conn_handle = 0xFFFFU;
            runtime->bluetooth_pair_deadline_us = 0;
            runtime->bluetooth_pair_initiate_at_us = 0;
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_CONNECTED, false);
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, false);
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, resume_advertising);
#if ESP_BMS_FEATURE_BLE_MEDIA_HID
            runtime_ble_media_hid_snapshot_clear(runtime);
#endif
            (void)runtime_project_bluetooth_snapshot(runtime);
            runtime_set_error(runtime, "BT OFF");
            ESP_LOGI(TAG, "[bt] local Bluetooth disconnected: reason=%d", event->disconnect.reason);
            if (start_bms_scan) {
                const esp_err_t ret = runtime->bms_ble_driver &&
                                              runtime->bms_ble_driver->resume_scan
                                          ? runtime->bms_ble_driver->resume_scan(runtime)
                                          : ESP_ERR_NOT_SUPPORTED;
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "[bms] scan after local Bluetooth disconnect failed: %s",
                             esp_err_to_name(ret));
                }
            }
        }
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.conn_handle == runtime->bluetooth_conn_handle) {
            runtime->bluetooth_pair_deadline_us = 0;
            runtime->bluetooth_pair_initiate_at_us = 0;
            struct ble_gap_conn_desc desc = { 0 };
            const int find_rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
            const bool paired = event->enc_change.status == 0 && find_rc == 0 &&
                                desc.sec_state.encrypted;
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_CONNECTED, paired);
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, false);
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, !paired);
#if ESP_BMS_FEATURE_BLE_MEDIA_HID
            if (paired) {
                runtime->snapshot.ble_media_hid_connected = true;
                runtime->snapshot.ble_media_hid_suspended = false;
            } else {
                runtime_ble_media_hid_snapshot_clear(runtime);
            }
#endif
            (void)runtime_project_bluetooth_snapshot(runtime);
            if (paired) {
                runtime_set_error(runtime, "BT CONN");
                ESP_LOGI(TAG,
                         "[bt] pairing complete: conn=%u encrypted=%u bonded=%u",
                         event->enc_change.conn_handle,
                         desc.sec_state.encrypted,
                         desc.sec_state.bonded);
            } else {
                runtime_set_error(runtime, "BT PAIR FAIL");
                ESP_LOGW(TAG, "[bt] pairing failed: conn=%u status=%d find_rc=%d",
                         event->enc_change.conn_handle,
                         event->enc_change.status,
                         find_rc);
#if ESP_BMS_FEATURE_BLE_MEDIA_HID
                RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, false);
                RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, true);
                if (find_rc != 0 || event->enc_change.status == BLE_HS_ENOTCONN) {
                    runtime->bluetooth_conn_handle = 0xFFFFU;
                }
#else
                (void)ble_gap_terminate(event->enc_change.conn_handle,
                                        BLE_ERR_REM_USER_CONN_TERM);
#endif
            }
        }
        return 0;
    case BLE_GAP_EVENT_CONN_UPDATE:
        if (event->conn_update.conn_handle == runtime->bluetooth_conn_handle) {
            ESP_LOGI(TAG, "[bt] connection updated: conn=%u status=%d",
                     event->conn_update.conn_handle, event->conn_update.status);
        }
        return 0;
    case BLE_GAP_EVENT_MTU:
        if (event->mtu.conn_handle == runtime->bluetooth_conn_handle) {
            ESP_LOGI(TAG, "[bt] MTU updated: conn=%u mtu=%u",
                     event->mtu.conn_handle, event->mtu.value);
        }
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_ble_api_response_handle) {
            s_ble_api_subscribed_conn = event->subscribe.cur_notify != 0
                                            ? event->subscribe.conn_handle
                                            : 0xFFFFU;
            ESP_LOGI(TAG, "[ble-api] response notifications %s",
                     s_ble_api_subscribed_conn != 0xFFFFU ? "enabled" : "disabled");
        }
#if ESP_BMS_FEATURE_BLE_MEDIA_HID
        if (event->subscribe.conn_handle == runtime->bluetooth_conn_handle &&
            event->subscribe.attr_handle == s_ble_media_hid_input_report_handle) {
            runtime->ble_media_hid_input_report_subscribed = event->subscribe.cur_notify != 0;
            ESP_LOGI(TAG, "[hid] input report notifications %s",
                     runtime->ble_media_hid_input_report_subscribed ? "enabled" : "disabled");
        }
#endif
        return 0;
    case BLE_GAP_EVENT_PARING_COMPLETE:
        if (event->pairing_complete.conn_handle == runtime->bluetooth_conn_handle) {
            runtime->bluetooth_pair_initiate_at_us = 0;
            if (event->pairing_complete.status == 0) {
                ESP_LOGI(TAG, "[bt] SMP pairing completed: conn=%u",
                         event->pairing_complete.conn_handle);
            } else {
                ESP_LOGW(TAG, "[bt] SMP pairing failed: conn=%u status=0x%04x",
                         event->pairing_complete.conn_handle,
                         event->pairing_complete.status);
            }
        }
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc = { 0 };
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            (void)ble_store_util_delete_peer(&desc.peer_id_addr);
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    }
    case BLE_GAP_EVENT_ADV_COMPLETE:
    {
        const bool should_resume_advertising = RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED);
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, false);
        if (should_resume_advertising &&
            !RUNTIME_FLAG(runtime, BLUETOOTH_CONNECTED) &&
            !RUNTIME_FLAG(runtime, BMS_SCAN_REQUESTED) &&
            !RUNTIME_FLAG(runtime, BMS_SCAN_ACTIVE)) {
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, true);
        }
        (void)runtime_project_bluetooth_snapshot(runtime);
        ESP_LOGI(TAG, "[bt] local Bluetooth advertising complete: reason=%d",
                 event->adv_complete.reason);
        return 0;
    }
    default:
        return 0;
    }
}

static esp_err_t runtime_bluetooth_start_advertising_now(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!RUNTIME_FLAG(runtime, BMS_BLE_READY) || !RUNTIME_FLAG(runtime, BMS_BLE_SYNCED)) {
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, true);
        (void)runtime_project_bluetooth_snapshot(runtime);
        return ESP_ERR_INVALID_STATE;
    }
    if (RUNTIME_FLAG(runtime, BLUETOOTH_CONNECTED) || RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISING) || ble_gap_adv_active()) {
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, ble_gap_adv_active() != 0);
        (void)runtime_project_bluetooth_snapshot(runtime);
        return ESP_OK;
    }
    if (RUNTIME_FLAG(runtime, BMS_SCAN_ACTIVE) || ble_gap_disc_active()) {
        return ESP_ERR_INVALID_STATE;
    }

    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        return ESP_FAIL;
    }

    uint8_t own_addr_type = 0;
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        return ESP_FAIL;
    }

    rc = ble_svc_gap_device_name_set(runtime->bluetooth_name[0] != '\0'
                                         ? runtime->bluetooth_name
                                         : LOCAL_BLUETOOTH_NAME);
    if (rc != 0) {
        return ESP_FAIL;
    }

    struct ble_hs_adv_fields fields = { 0 };
    const char *name = ble_svc_gap_device_name();
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = &BLE_API_SERVICE_UUID;
    fields.num_uuids128 = 1U;
    fields.uuids128_is_complete = 1U;
#if ESP_BMS_FEATURE_BLE_MEDIA_HID
    fields.uuids16 = &BLE_MEDIA_HID_SERVICE_UUID;
    fields.num_uuids16 = 1U;
    fields.uuids16_is_complete = 1U;
    fields.appearance = BLE_MEDIA_HID_APPEARANCE;
    fields.appearance_is_present = 1U;
#else
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.tx_pwr_lvl_is_present = 1;
#endif
    const int fields_rc = ble_gap_adv_set_fields(&fields);
    if (fields_rc != 0) {
        return ESP_FAIL;
    }
    struct ble_hs_adv_fields response_fields = { 0 };
    response_fields.name = (uint8_t *)name;
    response_fields.name_len = strlen(name);
    response_fields.name_is_complete = 1U;
    if (ble_gap_adv_rsp_set_fields(&response_fields) != 0) {
        return ESP_FAIL;
    }

    struct ble_gap_adv_params params = { 0 };
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = BLE_GAP_ADV_ITVL_MS(LOCAL_BLUETOOTH_ADV_INTERVAL_MS);
    params.itvl_max = BLE_GAP_ADV_ITVL_MS(LOCAL_BLUETOOTH_ADV_INTERVAL_MS + 10U);

    rc = ble_gap_adv_start(own_addr_type,
                           NULL,
                           BLE_HS_FOREVER,
                           &params,
                           runtime_bluetooth_gap_event,
                           runtime);
    if (rc != 0) {
        return ESP_FAIL;
    }

    runtime->bluetooth_own_addr_type = own_addr_type;
    RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, true);
    RUNTIME_SET_FLAG(runtime, BLUETOOTH_CONNECTED, false);
    runtime_project_bluetooth_snapshot(runtime);
    runtime_set_error(runtime, "BT ON");
    ESP_LOGI(TAG, "[bt] local Bluetooth advertising started: name='%s'", runtime->snapshot.bluetooth_name);
    return ESP_OK;
}

static void runtime_ble_host_on_reset(int reason)
{
    esp_bms_idf_runtime_t *runtime = s_ble_host_runtime;
    if (runtime) {
        RUNTIME_SET_FLAG(runtime, BLE_HOST_SYNCED, false);
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, false);
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_CONNECTED, false);
        runtime->bluetooth_conn_handle = 0xFFFFU;
#if ESP_BMS_FEATURE_BLE_MEDIA_HID
        runtime_ble_media_hid_snapshot_clear(runtime);
#endif
        if (runtime->bms_ble_driver && runtime->bms_ble_driver->on_ble_reset) {
            runtime->bms_ble_driver->on_ble_reset(runtime);
        }
        if (runtime->controller_ble_driver && runtime->controller_ble_driver->on_ble_reset) {
            runtime->controller_ble_driver->on_ble_reset(runtime);
        }
        (void)runtime_project_bluetooth_snapshot(runtime);
    }
    ESP_LOGW(TAG, "[ble] NimBLE reset: reason=%d", reason);
}

static void runtime_ble_host_on_sync(void)
{
    esp_bms_idf_runtime_t *runtime = s_ble_host_runtime;
    if (!runtime) {
        return;
    }
    RUNTIME_SET_FLAG(runtime, BLE_HOST_SYNCED, true);
    ESP_LOGI(TAG, "[ble] NimBLE synced");
    if (RUNTIME_FLAG(runtime, CONTROLLER_SCAN_REQUESTED)) {
        const esp_err_t ret = esp_bms_idf_runtime_start_controller_scan(runtime);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "[controller] deferred scan failed: %s", esp_err_to_name(ret));
        }
    } else if (RUNTIME_FLAG(runtime, BMS_SCAN_REQUESTED)) {
        const esp_err_t ret = runtime->bms_ble_driver && runtime->bms_ble_driver->resume_scan
                                  ? runtime->bms_ble_driver->resume_scan(runtime)
                                  : ESP_ERR_NOT_SUPPORTED;
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "[bms] deferred BLE scan start failed: %s", esp_err_to_name(ret));
        }
    } else if (RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED)) {
        const esp_err_t ret = runtime_bluetooth_start_advertising_now(runtime);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "[bt] deferred local Bluetooth advertising failed: %s",
                     esp_err_to_name(ret));
        }
    }
}

static void runtime_ble_host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "[ble] NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static esp_err_t runtime_init_ble_host(esp_bms_idf_runtime_t *runtime)
{
    if (RUNTIME_FLAG(runtime, BLE_HOST_READY)) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(runtime_init_nvs(runtime), TAG, "NVS init failed");

    const uint32_t internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const size_t internal_free = heap_caps_get_free_size(internal_caps);
    if (internal_free < BLE_HOST_MIN_INTERNAL_FREE_BYTES) {
        ESP_LOGW(TAG,
                 "[ble] host init skipped: internal8_free=%u required=%u",
                 (unsigned)internal_free,
                 (unsigned)BLE_HOST_MIN_INTERNAL_FREE_BYTES);
        runtime_log_heap_state("ble_init_rejected");
        return ESP_ERR_NO_MEM;
    }

    runtime_log_heap_state("ble_init_pre");
    esp_err_t ret = nimble_port_init();
    runtime_log_heap_state(ret == ESP_OK ? "ble_init_post" : "ble_init_port_failed");
#if ESP_BMS_FEATURE_BLE_MEDIA_HID
    bool hid_queue_created = false;
#endif
    if (ret != ESP_OK) {
        return ret;
    }
    runtime_set_ble_tx_power();

    s_ble_host_runtime = runtime;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int gap_rc = ble_svc_gap_device_name_set(runtime->bluetooth_name[0] != '\0'
                                                 ? runtime->bluetooth_name
                                                 : LOCAL_BLUETOOTH_NAME);
    if (gap_rc != 0) {
        ret = ESP_FAIL;
        goto deinit_nimble;
    }
    gap_rc = ble_svc_gap_device_appearance_set(
#if ESP_BMS_FEATURE_BLE_MEDIA_HID
        BLE_MEDIA_HID_APPEARANCE
#else
        0x0200U
#endif
    );
    if (gap_rc != 0) {
        ret = ESP_FAIL;
        goto deinit_nimble;
    }
    if (!s_ble_api_request_queue) {
        s_ble_api_request_queue = xQueueCreate(BLE_API_QUEUE_LEN, sizeof(runtime_ble_api_request_t));
    }
    if (!s_ble_api_request_queue) {
        ret = ESP_ERR_NO_MEM;
        goto deinit_nimble;
    }
    ret = runtime_ble_api_register_gatt();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE API GATT service registration failed");
        goto deinit_nimble;
    }
#if ESP_BMS_FEATURE_BLE_MEDIA_HID
    if (!runtime->ble_media_hid_usage_queue) {
        runtime->ble_media_hid_usage_queue =
            xQueueCreate(BLE_MEDIA_HID_USAGE_QUEUE_LEN, sizeof(esp_bms_ble_media_hid_usage_t));
        hid_queue_created = true;
    }
    if (!runtime->ble_media_hid_usage_queue) {
        ret = ESP_ERR_NO_MEM;
        goto deinit_nimble;
    }
    ret = runtime_ble_media_hid_register_gatt();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE media HID GATT service registration failed");
        goto deinit_nimble;
    }
#endif

    ble_hs_cfg.reset_cb = runtime_ble_host_on_reset;
    ble_hs_cfg.sync_cb = runtime_ble_host_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_bonding = 1;
    /* Just Works pairing: encrypted and bonded without PIN or confirmation. */
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();

#if ESP_BMS_FEATURE_BLE_MEDIA_HID
    if (!runtime->ble_media_hid_worker_started &&
        xTaskCreate(runtime_ble_media_hid_worker,
                    "ble-hid-media",
                    BLE_MEDIA_HID_WORKER_STACK,
                    runtime,
                    BLE_MEDIA_HID_WORKER_PRIORITY,
                    NULL) != pdPASS) {
        ret = ESP_ERR_NO_MEM;
        goto deinit_nimble;
    }
    runtime->ble_media_hid_worker_started = true;
#endif

    if (xTaskCreate(runtime_ble_api_worker,
                    "ble-api",
                    BLE_API_WORKER_STACK,
                    runtime,
                    BLE_API_WORKER_PRIORITY,
                    NULL) != pdPASS) {
        ret = ESP_ERR_NO_MEM;
        goto deinit_nimble;
    }

    RUNTIME_SET_FLAG(runtime, BLE_HOST_READY, true);
    RUNTIME_SET_FLAG(runtime, BLE_HOST_STARTED, true);
    ESP_LOGI(TAG, "[ble] NimBLE initialized");
    nimble_port_freertos_init(runtime_ble_host_task);
    return ESP_OK;

deinit_nimble: {
    runtime_log_heap_state("ble_init_failed");
    const esp_err_t deinit_ret = nimble_port_deinit();
    if (deinit_ret != ESP_OK) {
        ESP_LOGW(TAG, "[ble] NimBLE deinit after failed init: %s", esp_err_to_name(deinit_ret));
    }
    s_ble_host_runtime = NULL;
    RUNTIME_SET_FLAG(runtime, BLE_HOST_READY, false);
    RUNTIME_SET_FLAG(runtime, BLE_HOST_SYNCED, false);
    RUNTIME_SET_FLAG(runtime, BLE_HOST_STARTED, false);
    if (s_ble_api_request_queue) {
        vQueueDelete(s_ble_api_request_queue);
        s_ble_api_request_queue = NULL;
    }
#if ESP_BMS_FEATURE_BLE_MEDIA_HID
    if (hid_queue_created) {
        vQueueDelete(runtime->ble_media_hid_usage_queue);
        runtime->ble_media_hid_usage_queue = NULL;
    }
#endif
    return ret;
}
}

esp_err_t esp_bms_idf_runtime_ensure_ble_host(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    return runtime_init_ble_host(runtime);
}

void esp_bms_idf_runtime_request_coded_phy(uint16_t conn_handle, const char *source)
{
#if CONFIG_BT_NIMBLE_LL_CFG_FEAT_LE_CODED_PHY
    const int rc = ble_gap_set_prefered_le_phy(conn_handle,
                                               BLE_GAP_LE_PHY_CODED_MASK,
                                               BLE_GAP_LE_PHY_CODED_MASK,
                                               BLE_GAP_LE_PHY_CODED_S8);
    if (rc != 0) {
        ESP_LOGW(TAG, "[ble] %s Coded PHY request failed: conn=%u rc=%d",
                 source ? source : "link", conn_handle, rc);
    }
#else
    (void)conn_handle;
    (void)source;
#endif
}
#else
esp_err_t esp_bms_idf_runtime_ensure_ble_host(esp_bms_idf_runtime_t *runtime)
{
    return runtime ? ESP_ERR_NOT_SUPPORTED : ESP_ERR_INVALID_ARG;
}

void esp_bms_idf_runtime_request_coded_phy(uint16_t conn_handle, const char *source)
{
    (void)conn_handle;
    (void)source;
}
#endif

void esp_bms_idf_runtime_register_bms_frame_handler(
    esp_bms_idf_runtime_t *runtime,
    esp_bms_idf_runtime_bms_frame_handler_t handler)
{
    if (runtime) {
        runtime->bms_frame_handler = handler;
    }
}

void esp_bms_idf_runtime_register_bms_ble_driver(
    esp_bms_idf_runtime_t *runtime,
    const esp_bms_idf_runtime_bms_ble_driver_t *driver)
{
    if (runtime) {
        runtime->bms_ble_driver = driver;
    }
}

void esp_bms_idf_runtime_register_controller_ble_driver(
    esp_bms_idf_runtime_t *runtime,
    const esp_bms_idf_runtime_controller_ble_driver_t *driver)
{
    if (runtime) {
        runtime->controller_ble_driver = driver;
    }
}

void esp_bms_idf_runtime_register_network_driver(
    esp_bms_idf_runtime_t *runtime,
    const esp_bms_idf_runtime_network_driver_t *driver)
{
    if (runtime) {
        runtime->network_driver = driver;
    }
}

static bool runtime_sample_battery(esp_bms_idf_runtime_t *runtime)
{
    if (!RUNTIME_FLAG(runtime, BATTERY_ADC_READY) || !runtime->battery_adc) {
        return false;
    }

    int raw = 0;
    esp_err_t ret = adc_oneshot_read(runtime->battery_adc, runtime->battery_adc_channel, &raw);
    if (ret != ESP_OK) {
        runtime->battery_read_failures++;
        if (runtime->battery_read_failures == 1 || (runtime->battery_read_failures % 32U) == 0U) {
            ESP_LOGW(TAG, "battery ADC read failed: %s", esp_err_to_name(ret));
        }
        return false;
    }

    runtime->battery_read_failures = 0;
    if (raw < 0) {
        raw = 0;
    } else if ((uint32_t)raw > BATTERY_ADC_MAX) {
        raw = BATTERY_ADC_MAX;
    }

    const uint32_t battery_mv = runtime_battery_mv_from_raw((uint16_t)raw);
    const bool changed =
        !RUNTIME_SNAPSHOT_FLAG(runtime, LOCAL_BATTERY_VALID) || runtime->snapshot.local_battery_mv != battery_mv;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, LOCAL_BATTERY_VALID, true);
    runtime->snapshot.local_battery_mv = battery_mv;
    runtime->battery_samples_seen++;
    return changed;
}

void esp_bms_idf_runtime_init(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->cast_socket_fd = -1;
    runtime->http_pending_lock = xSemaphoreCreateMutex();
    if (!runtime->http_pending_lock) {
        ESP_LOGW(TAG, "[http] pending config mutex allocation failed");
    }
    runtime->bms_scan_lock = xSemaphoreCreateMutex();
    if (!runtime->bms_scan_lock) {
        ESP_LOGW(TAG, "[bms] scan candidate mutex allocation failed");
    }
    runtime->ride_records_lock = xSemaphoreCreateMutex();
    if (!runtime->ride_records_lock) {
        ESP_LOGW(TAG, "[bms] ride record mutex allocation failed");
    }
    runtime->gps_track_lock = xSemaphoreCreateMutex();
    if (!runtime->gps_track_lock) {
        ESP_LOGW(TAG, "[gps] track mutex allocation failed");
    }
    runtime->capacity_estimate_lock = xSemaphoreCreateMutex();
    if (!runtime->capacity_estimate_lock) {
        ESP_LOGW(TAG, "[bms] capacity estimate mutex allocation failed");
    }
    runtime_reset_state(runtime);
    const esp_err_t flashdb_ret = esp_bms_flashdb_init();
    if (flashdb_ret != ESP_OK) {
        ESP_LOGW(TAG, "[history] FlashDB unavailable: %s", esp_err_to_name(flashdb_ret));
    }
    runtime_ensure_setup_ap_credentials(runtime);
    const esp_err_t ride_records_ret = runtime_load_ride_records(runtime);
    if (ride_records_ret != ESP_OK) {
        ESP_LOGW(TAG, "[bms] ride record load failed: %s", esp_err_to_name(ride_records_ret));
    }
    const esp_err_t gps_track_ret = runtime_load_gps_track(runtime);
    if (gps_track_ret != ESP_OK) {
        ESP_LOGW(TAG, "[gps] track load failed: %s", esp_err_to_name(gps_track_ret));
    } else {
        const esp_err_t migration_ret = runtime_migrate_gps_track(runtime);
        if (migration_ret != ESP_OK) {
            ESP_LOGW(TAG, "[history] legacy GPS migration deferred: %s",
                     esp_err_to_name(migration_ret));
        }
    }
    const esp_err_t capacity_ret = runtime_load_capacity_estimate(runtime);
    if (capacity_ret != ESP_OK) {
        ESP_LOGW(TAG, "[bms] capacity estimate load failed: %s", esp_err_to_name(capacity_ret));
    }
    runtime_init_battery_adc(runtime);
    (void)runtime_sample_battery(runtime);
}

esp_err_t esp_bms_idf_runtime_start_setup_ap(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    return runtime->network_driver && runtime->network_driver->start_setup_ap
               ? runtime->network_driver->start_setup_ap(runtime)
               : ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_bms_idf_runtime_start_http_server(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    return runtime->network_driver && runtime->network_driver->start_http_server
               ? runtime->network_driver->start_http_server(runtime)
               : ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_bms_idf_runtime_stop_setup_services(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }

    return runtime->network_driver && runtime->network_driver->stop_setup_services
               ? runtime->network_driver->stop_setup_services(runtime)
               : ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_bms_idf_runtime_start_bluetooth_advertising(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
#if !ESP_BMS_FEATURE_BLE
    RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, false);
    runtime_project_bluetooth_snapshot(runtime);
    runtime_set_error(runtime, "BT N/A");
    return ESP_ERR_NOT_SUPPORTED;
#else

    if (runtime->bluetooth_name[0] == '\0') {
        runtime_copy_snapshot_text(runtime->bluetooth_name,
                                   sizeof(runtime->bluetooth_name),
                                   LOCAL_BLUETOOTH_NAME);
    }
    RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, true);
    runtime_project_bluetooth_snapshot(runtime);

    esp_err_t ret = esp_bms_idf_runtime_ensure_ble_host(runtime);
    if (ret != ESP_OK) {
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, false);
        runtime_project_bluetooth_snapshot(runtime);
        runtime_set_error(runtime, "BT FAIL");
        return ret;
    }
    if (!RUNTIME_FLAG(runtime, BMS_BLE_SYNCED)) {
        runtime_set_error(runtime, "BT WAIT");
        return ESP_OK;
    }

    ret = runtime_bluetooth_start_advertising_now(runtime);
    if (ret == ESP_ERR_INVALID_STATE) {
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, false);
        runtime_project_bluetooth_snapshot(runtime);
        runtime_set_error(runtime, "BT BUSY");
    } else if (ret != ESP_OK) {
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, false);
        runtime_project_bluetooth_snapshot(runtime);
        runtime_set_error(runtime, "BT FAIL");
    }
    return ret;
#endif
}

esp_err_t esp_bms_idf_runtime_start_controller_ble_if_enabled(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!runtime->controller_ble_driver || !runtime->controller_ble_driver->start_if_enabled) {
        runtime_project_controller_snapshot(runtime);
        return ESP_OK;
    }
    return runtime->controller_ble_driver->start_if_enabled(runtime);
}

esp_err_t esp_bms_idf_runtime_start_controller_scan(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!runtime->controller_ble_driver || !runtime->controller_ble_driver->start_scan) {
        runtime_project_controller_snapshot(runtime);
        return ESP_ERR_NOT_SUPPORTED;
    }
    return runtime->controller_ble_driver->start_scan(runtime);
}

esp_err_t esp_bms_idf_runtime_resume_bms_scan(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!runtime->bms_ble_driver || !runtime->bms_ble_driver->resume_scan) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return runtime->bms_ble_driver->resume_scan(runtime);
}

void esp_bms_idf_runtime_stop_controller_ble(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return;
    }
    if (runtime->controller_ble_driver && runtime->controller_ble_driver->stop) {
        runtime->controller_ble_driver->stop(runtime);
    } else {
        runtime_project_controller_snapshot(runtime);
    }
}

#if !ESP_BMS_FEATURE_CLASSIC_MEDIA_HID
static esp_err_t runtime_bluetooth_stop_advertising(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }

    RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, false);
#if ESP_BMS_FEATURE_BLE
    if (runtime->bluetooth_conn_handle != 0xFFFFU) {
        (void)ble_gap_terminate(runtime->bluetooth_conn_handle,
                                BLE_ERR_REM_USER_CONN_TERM);
    }
    if (RUNTIME_FLAG(runtime, BMS_BLE_READY) &&
        RUNTIME_FLAG(runtime, BMS_BLE_SYNCED) &&
        ble_gap_adv_active()) {
        const int rc = ble_gap_adv_stop();
        if (rc != 0 && rc != BLE_HS_EALREADY) {
            runtime_project_bluetooth_snapshot(runtime);
            runtime_set_error(runtime, "BT FAIL");
            return ESP_FAIL;
        }
    }
#endif
    RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, false);
    runtime_project_bluetooth_snapshot(runtime);
    runtime_set_error(runtime, "BT HIDE");
    return ESP_OK;
}
#endif

void esp_bms_idf_runtime_set_active_data_source(esp_bms_idf_runtime_t *runtime,
                                                esp_bms_lvgl_data_source_t source)
{
    if (!runtime || runtime->active_data_source == source) {
        return;
    }
    runtime->active_data_source = source;
    const bool bms_collection_active = source == ESP_BMS_LVGL_DATA_SOURCE_BMS ||
                                       source == ESP_BMS_LVGL_DATA_SOURCE_SPEED_DASHBOARD ||
                                       runtime->trip_efficiency.started;
    runtime->bms_status_poll_elapsed_ms = bms_collection_active ? BMS_STATUS_POLL_PERIOD_MS : 0U;
}

void esp_bms_idf_runtime_register_optional_http_handler(
    esp_bms_idf_runtime_t *runtime,
    esp_bms_idf_runtime_optional_http_handler_t handler,
    void *context)
{
    if (!runtime) {
        return;
    }
    runtime->optional_http_handler = handler;
    runtime->optional_http_context = context;
}

static void runtime_flashdb_sample(esp_bms_idf_runtime_t *runtime)
{
    const esp_bms_dashboard_snapshot_t *s = &runtime->snapshot;
    const bool gps_valid = runtime->gps_last_fix_valid && RUNTIME_SNAPSHOT_FLAG(runtime, GPS_FIX_VALID);
    const bool bms_valid = RUNTIME_SNAPSHOT_FLAG(runtime, BMS_ONLINE);
    if (!runtime->history_session_started && (gps_valid || bms_valid)) {
        if (esp_bms_flashdb_start_session(&runtime->history_session_id) != ESP_OK) return;
        runtime->history_session_started = true;
        runtime->history_elapsed_s = 0;
        runtime->history_time_anchored = false;
        if (runtime->gps_utc_valid &&
            esp_bms_flashdb_set_session_anchor(runtime->history_session_id, 0U,
                                               runtime->gps_utc_epoch_s) == ESP_OK) {
            runtime->history_time_anchored = true;
        }
    }
    if (!runtime->history_session_started || esp_bms_flashdb_session_full()) return;
    esp_bms_flashdb_sample_t sample = { .version = ESP_BMS_FLASHDB_SAMPLE_VERSION,
                                        .flags = (gps_valid ? ESP_BMS_FLASHDB_FLAG_GPS_VALID : 0) |
                                                 (bms_valid ? ESP_BMS_FLASHDB_FLAG_BMS_VALID : 0),
                                        .bms_type = s->bms_type,
                                        .temperature_count = 0,
                                        .elapsed_s = (uint16_t)(runtime->history_elapsed_s > UINT16_MAX ? UINT16_MAX : runtime->history_elapsed_s),
                                        .latitude_e7 = runtime->gps_last_latitude_e7,
                                        .longitude_e7 = runtime->gps_last_longitude_e7,
                                        .pack_voltage_mv = s->pack_voltage_mv > UINT16_MAX ? UINT16_MAX : (uint16_t)s->pack_voltage_mv,
                                        .current_deci_amps = s->current_deci_amps,
                                        .soc_percent = s->soc_percent > 100U ? 100U : (uint8_t)s->soc_percent,
                                        .cell_delta_mv = s->delta_cell_voltage_mv > UINT8_MAX ? UINT8_MAX : (uint8_t)s->delta_cell_voltage_mv,
                                        .cell_min_mv = s->min_cell_voltage_mv,
                                        .cell_max_mv = s->max_cell_voltage_mv,
                                        .cell_avg_mv = s->average_cell_voltage_mv };
    for (size_t i = 0; i < 6U; ++i) {
        if (esp_bms_dashboard_snapshot_temperature_valid(s, i)) {
            int16_t temp = s->bms_temperature_celsius[i];
            sample.temperatures_c[sample.temperature_count++] =
                (int8_t)(temp < INT8_MIN ? INT8_MIN : temp > INT8_MAX ? INT8_MAX : temp);
        }
    }
    const uint32_t sample_elapsed_s = runtime->history_elapsed_s;
    const uint64_t key = (runtime->history_session_id << 32) | sample_elapsed_s;
    if (esp_bms_flashdb_append_sample(key, &sample) == ESP_OK) ++runtime->history_elapsed_s;
    const uint16_t fault_mask = s->bms_safety_active_mask;
    const uint16_t supported_mask = s->bms_safety_supported_mask;
    if (fault_mask != runtime->history_fault_mask ||
        supported_mask != runtime->history_fault_supported_mask) {
        const esp_bms_flashdb_fault_t fault = { .timestamp = key,
                                                .session_id = runtime->history_session_id,
                                                .elapsed_s = sample_elapsed_s,
                                                .active_mask = fault_mask,
                                                .supported_mask = supported_mask,
                                                .bms_type = s->bms_type,
                                                .flags = 1U };
        if (esp_bms_flashdb_append_fault(&fault) == ESP_OK) {
            runtime->history_fault_mask = fault_mask;
            runtime->history_fault_supported_mask = supported_mask;
        }
    }
}

bool esp_bms_idf_runtime_tick(esp_bms_idf_runtime_t *runtime, uint32_t elapsed_ms)
{
    if (!runtime) {
        return false;
    }

    bool cast_active = __atomic_load_n(&runtime->cast_active, __ATOMIC_ACQUIRE);
    if (cast_active) {
        const uint32_t elapsed =
            __atomic_add_fetch(&runtime->cast_heartbeat_elapsed_ms, elapsed_ms, __ATOMIC_RELAXED);
        if (elapsed >= CAST_HEARTBEAT_TIMEOUT_MS) {
            esp_bms_idf_runtime_stop_cast(runtime, "heartbeat timeout");
            cast_active = false;
        }

        bool changed = false;
        if (runtime->snapshot.cast_active != cast_active) {
            runtime->snapshot.cast_active = cast_active;
            changed = true;
        }
        return changed;
    }

    bool changed = RUNTIME_FLAG(runtime, BLUETOOTH_SNAPSHOT_DIRTY) ||
                   RUNTIME_FLAG(runtime, BMS_SNAPSHOT_DIRTY) ||
                   RUNTIME_FLAG(runtime, CONTROLLER_SNAPSHOT_DIRTY);
    if (runtime->snapshot.cast_active != cast_active) {
        runtime->snapshot.cast_active = cast_active;
        changed = true;
    }
    RUNTIME_SET_FLAG(runtime, BLUETOOTH_SNAPSHOT_DIRTY, false);
    RUNTIME_SET_FLAG(runtime, BMS_SNAPSHOT_DIRTY, false);
    RUNTIME_SET_FLAG(runtime, CONTROLLER_SNAPSHOT_DIRTY, false);
    if (!runtime->bms_ble_driver && RUNTIME_FLAG(runtime, BMS_SCAN_SNAPSHOT_DIRTY)) {
        RUNTIME_SET_FLAG(runtime, BMS_SCAN_SNAPSHOT_DIRTY, false);
        changed = true;
    }
    changed = runtime_apply_pending_http_ap_password(runtime) || changed;
    changed = runtime_apply_pending_http_bms_scan(runtime) || changed;
    changed = runtime_apply_pending_http_bms_bind(runtime) || changed;
#if ESP_BMS_FEATURE_BLE
    if (runtime->bluetooth_pair_initiate_at_us != 0 &&
        !RUNTIME_FLAG(runtime, BLUETOOTH_CONNECTED) &&
        runtime->bluetooth_conn_handle != 0xFFFFU &&
        esp_timer_get_time() >= runtime->bluetooth_pair_initiate_at_us) {
        runtime->bluetooth_pair_initiate_at_us = 0;
        const int security_rc =
            ble_gap_security_initiate(runtime->bluetooth_conn_handle);
        if (security_rc != 0 && security_rc != BLE_HS_EALREADY) {
            runtime_set_error(runtime, "BT PAIR FAIL");
            ESP_LOGW(TAG, "[bt] pairing start failed: conn=%u rc=%d",
                     runtime->bluetooth_conn_handle, security_rc);
            (void)ble_gap_terminate(runtime->bluetooth_conn_handle,
                                    BLE_ERR_REM_USER_CONN_TERM);
            changed = true;
        } else {
            ESP_LOGI(TAG, "[bt] pairing started: conn=%u",
                     runtime->bluetooth_conn_handle);
        }
    }
    if (runtime->bluetooth_pair_deadline_us != 0) {
        if (RUNTIME_FLAG(runtime, BLUETOOTH_CONNECTED) ||
            runtime->bluetooth_conn_handle == 0xFFFFU) {
            runtime->bluetooth_pair_deadline_us = 0;
        } else if (esp_timer_get_time() >= runtime->bluetooth_pair_deadline_us) {
            runtime->bluetooth_pair_deadline_us = 0;
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, true);
            runtime_log_heap_state("bt_pair_timeout");
            ESP_LOGW(TAG,
                     "[bt] pairing timed out (%u ms); terminating conn=%u",
                     (unsigned)LOCAL_BLUETOOTH_PAIR_TIMEOUT_MS,
                     runtime->bluetooth_conn_handle);
            (void)ble_gap_terminate(runtime->bluetooth_conn_handle,
                                    BLE_ERR_REM_USER_CONN_TERM);
            changed = true;
        }
    }
    if (RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED) &&
        !RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISING) &&
        !RUNTIME_FLAG(runtime, BLUETOOTH_CONNECTED) &&
        runtime->bluetooth_conn_handle == 0xFFFFU &&
        !RUNTIME_FLAG(runtime, BMS_SCAN_REQUESTED)) {
        const esp_err_t ret = esp_bms_idf_runtime_start_bluetooth_advertising(runtime);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "[bt] local Bluetooth advertising start failed: %s",
                     esp_err_to_name(ret));
        }
        changed = true;
    }
#endif
    if (runtime->bms_ble_driver && runtime->bms_ble_driver->tick) {
        changed = runtime->bms_ble_driver->tick(runtime, elapsed_ms) || changed;
    }
    if (runtime->controller_ble_driver && runtime->controller_ble_driver->tick) {
        changed = runtime->controller_ble_driver->tick(runtime, elapsed_ms) || changed;
    }
    runtime->battery_sample_elapsed_ms += elapsed_ms;
    if (runtime->battery_sample_elapsed_ms >= BATTERY_SAMPLE_PERIOD_MS) {
        runtime->battery_sample_elapsed_ms = 0;
        changed = runtime_sample_battery(runtime) || changed;
    }
    runtime->elapsed_ms += elapsed_ms;
    while (runtime->elapsed_ms >= 1000) {
        runtime->elapsed_ms -= 1000;
        runtime->tick_count++;
    }
    runtime->history_sample_elapsed_ms += elapsed_ms;
    while (runtime->history_sample_elapsed_ms >= 1000U) {
        runtime->history_sample_elapsed_ms -= 1000U;
        runtime_flashdb_sample(runtime);
    }
    const uint64_t uptime_seconds = (uint64_t)esp_timer_get_time() / UINT64_C(1000000);
    const uint32_t displayed_uptime = uptime_seconds > UINT32_MAX
                                          ? UINT32_MAX
                                          : (uint32_t)uptime_seconds;
    if (runtime->snapshot.uptime_seconds != displayed_uptime) {
        runtime->snapshot.uptime_seconds = displayed_uptime;
        changed = true;
    }
    runtime_persist_ride_records(runtime);
    /* FlashDB is the durable history; keep the legacy NVS blob read-only for migration. */
    runtime_persist_capacity_estimate(runtime);
    return changed;
}

uint8_t esp_bms_idf_runtime_take_connection_audio_events(esp_bms_idf_runtime_t *runtime)
{
    return runtime ? __atomic_exchange_n(&runtime->pending_audio_events, 0U, __ATOMIC_RELAXED) : 0U;
}

static bool runtime_action_feature_enabled(esp_bms_lvgl_action_t action)
{
    switch (action) {
    case ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING:
        return ESP_BMS_FEATURE_NETWORK;
    case ESP_BMS_LVGL_ACTION_START_BMS_BIND:
    case ESP_BMS_LVGL_ACTION_CANCEL_BMS_CONNECTION:
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_ANT:
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_JK:
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_JBD:
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_DALY:
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_YANYANG:
    case ESP_BMS_LVGL_ACTION_SET_PRESET_RANGE:
        return ESP_BMS_FEATURE_BMS;
    case ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_CONNECTION:
    case ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_PAGE:
    case ESP_BMS_LVGL_ACTION_START_CONTROLLER_BIND:
    case ESP_BMS_LVGL_ACTION_ADJUST_CONTROLLER_WHEEL:
    case ESP_BMS_LVGL_ACTION_ADJUST_CONTROLLER_RATIO:
    case ESP_BMS_LVGL_ACTION_SET_CONTROLLER_TIRE:
    case ESP_BMS_LVGL_ACTION_SET_CONTROLLER_RATIO:
        return ESP_BMS_FEATURE_CONTROLLER;
    case ESP_BMS_LVGL_ACTION_ENABLE_BLUETOOTH_ADVERTISING:
        return ESP_BMS_FEATURE_BMS || ESP_BMS_FEATURE_CONTROLLER || ESP_BMS_FEATURE_BLE_MEDIA_HID ||
               ESP_BMS_FEATURE_CLASSIC_MEDIA_HID;
    case ESP_BMS_LVGL_ACTION_PHONE_MEDIA_PREVIOUS:
    case ESP_BMS_LVGL_ACTION_PHONE_MEDIA_NEXT:
    case ESP_BMS_LVGL_ACTION_PHONE_MEDIA_VOLUME_DOWN:
    case ESP_BMS_LVGL_ACTION_PHONE_MEDIA_VOLUME_UP:
        return ESP_BMS_FEATURE_BLE_MEDIA_HID || ESP_BMS_FEATURE_CLASSIC_MEDIA_HID;
    case ESP_BMS_LVGL_ACTION_MEDIA_PLAY_PAUSE:
        return ESP_BMS_FEATURE_BLE_MEDIA_HID || ESP_BMS_FEATURE_CLASSIC_MEDIA_HID;
    case ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_UNIT:
    case ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_SOURCE:
    case ESP_BMS_LVGL_ACTION_SET_SPEED_SOURCE:
        return ESP_BMS_FEATURE_GPS || ESP_BMS_FEATURE_CONTROLLER;
    default:
        return true;
    }
}

bool esp_bms_idf_runtime_apply_action_event(esp_bms_idf_runtime_t *runtime,
                                            const esp_bms_lvgl_action_event_t *event)
{
    if (!runtime || !event || event->action == ESP_BMS_LVGL_ACTION_NONE ||
        !runtime_action_feature_enabled(event->action)) {
        return false;
    }

    switch (event->action) {
    case ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING:
        if (RUNTIME_SNAPSHOT_FLAG(runtime, SETUP_AP_ENABLED)) {
            RUNTIME_SET_SNAPSHOT_FLAG(runtime, SETUP_AP_ENABLED, false);
            runtime->snapshot.wifi = ESP_BMS_WIFI_OFFLINE;
            runtime_set_error(runtime, "AP OFF");
        } else {
            RUNTIME_SET_SNAPSHOT_FLAG(runtime, SETUP_AP_ENABLED, true);
            runtime->snapshot.wifi = ESP_BMS_WIFI_SETUP_AP;
            runtime_set_error(runtime, "SETUP AP");
        }
        return true;
    case ESP_BMS_LVGL_ACTION_CYCLE_BRIGHTNESS:
        (void)runtime_set_brightness_percent(runtime,
                                             runtime->brightness_percent >= 85 ? 30 :
                                             runtime->brightness_percent >= 60 ? 85 : 60);
        runtime_set_error(runtime, runtime->brightness_percent >= 85 ? "BRIGHT 85" :
                                   runtime->brightness_percent >= 60 ? "BRIGHT 60" : "BRIGHT 30");
        return true;
    case ESP_BMS_LVGL_ACTION_SET_BRIGHTNESS:
        if (!ACTION_EVENT_FLAG(event, BRIGHTNESS_PERCENT_VALID) ||
            !runtime_brightness_matches_policy(event->brightness_percent)) {
            return false;
        }
        if (runtime->brightness_percent == event->brightness_percent) {
            return false;
        }
        (void)runtime_set_brightness_percent(runtime, event->brightness_percent);
        runtime_set_error(runtime, "BRIGHT SET");
        return true;
    case ESP_BMS_LVGL_ACTION_SET_VOLUME:
        if (!ACTION_EVENT_FLAG(event, VOLUME_PERCENT_VALID) ||
            !runtime_volume_matches_policy(event->volume_percent)) {
            return false;
        }
        if (runtime->volume_percent == event->volume_percent) {
            return false;
        }
        (void)runtime_set_volume_percent(runtime, event->volume_percent);
        runtime_set_error(runtime, "VOL SET");
        return true;
    case ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY:
        runtime->display_rotation = runtime_next_rotation(runtime->display_rotation);
        runtime_set_error(runtime, runtime_rotation_text(runtime->display_rotation));
        return true;
    case ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_UNIT:
        runtime->snapshot.speed_unit = runtime->snapshot.speed_unit == ESP_BMS_SPEED_UNIT_KMH
                                           ? ESP_BMS_SPEED_UNIT_MPH
                                           : ESP_BMS_SPEED_UNIT_KMH;
        runtime_update_snapshot_speed(runtime);
        runtime_set_error(runtime, runtime->snapshot.speed_unit == ESP_BMS_SPEED_UNIT_MPH ? "SPEED MPH" : "SPEED KMH");
        return true;
    case ESP_BMS_LVGL_ACTION_TOGGLE_LANGUAGE:
        RUNTIME_SET_FLAG(runtime, LANGUAGE_ZH, !RUNTIME_FLAG(runtime, LANGUAGE_ZH));
        runtime_set_error(runtime, RUNTIME_FLAG(runtime, LANGUAGE_ZH) ? "LANG ZH" : "LANG EN");
        return true;
    case ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_CONNECTION:
        runtime->controller_connection_enabled = !runtime->controller_connection_enabled;
        if (!runtime->controller_connection_enabled) {
            RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_REQUESTED, false);
            RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_ACTIVE, false);
            esp_bms_idf_runtime_stop_controller_ble(runtime);
        } else if (runtime->controller_connection_enabled) {
            (void)esp_bms_idf_runtime_start_controller_ble_if_enabled(runtime);
        }
        runtime_project_controller_snapshot(runtime);
        return true;
    case ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_PAGE:
        if (!esp_bms_display_service_speed_dashboard_style_available(
                ESP_BMS_SPEED_DASHBOARD_STYLE_CONTROLLER)) {
            return false;
        }
        runtime->controller_page_enabled = !runtime->controller_page_enabled;
        runtime->snapshot.speed_dashboard_style =
            runtime->controller_page_enabled
                ? ESP_BMS_SPEED_DASHBOARD_STYLE_CONTROLLER
                : esp_bms_display_service_default_speed_dashboard_style();
        if (runtime->controller_page_enabled) {
            runtime->controller_connection_enabled = true;
            (void)esp_bms_idf_runtime_start_controller_ble_if_enabled(runtime);
        }
        runtime_project_controller_snapshot(runtime);
        return true;
    case ESP_BMS_LVGL_ACTION_SET_SPEED_DASHBOARD_STYLE:
        if (!ACTION_EVENT_FLAG(event, NUMERIC_DELTA_VALID) ||
            !runtime_speed_dashboard_style_matches_policy(event->numeric_delta)) {
            return false;
        }
        {
            const esp_bms_speed_dashboard_style_t style =
                (esp_bms_speed_dashboard_style_t)event->numeric_delta;
            if (runtime->snapshot.speed_dashboard_style == style) {
                return false;
            }
            runtime->snapshot.speed_dashboard_style = style;
            runtime->controller_page_enabled =
                style == ESP_BMS_SPEED_DASHBOARD_STYLE_CONTROLLER;
            if (style != ESP_BMS_SPEED_DASHBOARD_STYLE_S1000RR) {
                runtime->controller_connection_enabled = true;
                (void)esp_bms_idf_runtime_start_controller_ble_if_enabled(runtime);
            }
        }
        runtime_project_controller_snapshot(runtime);
        return true;
    case ESP_BMS_LVGL_ACTION_SET_BOOT_ANIMATION_STYLE:
        if (!ACTION_EVENT_FLAG(event, NUMERIC_DELTA_VALID) ||
            !runtime_boot_animation_style_matches_policy(event->numeric_delta) ||
            runtime->snapshot.boot_animation_style == (uint8_t)event->numeric_delta) {
            return false;
        }
        runtime->snapshot.boot_animation_style = (uint8_t)event->numeric_delta;
        switch ((esp_bms_boot_animation_style_t)runtime->snapshot.boot_animation_style) {
        case ESP_BMS_BOOT_ANIMATION_GAUGE_HONDA_FIREBLADE:
            runtime_set_error(runtime, "BOOT HONDA");
            break;
        case ESP_BMS_BOOT_ANIMATION_GAUGE_S1000RR:
            runtime_set_error(runtime, "BOOT BMW");
            break;
        case ESP_BMS_BOOT_ANIMATION_CHARGE:
        default:
            runtime_set_error(runtime, "BOOT CHARGE");
            break;
        }
        return true;
    case ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_SOURCE:
        {
            const esp_bms_speed_source_t target =
                runtime->snapshot.speed_source == ESP_BMS_SPEED_SOURCE_GPS
                    ? ESP_BMS_SPEED_SOURCE_CONTROLLER
                    : ESP_BMS_SPEED_SOURCE_GPS;
            if (target == ESP_BMS_SPEED_SOURCE_GPS &&
                runtime->snapshot.gps_module_state !=
                    (uint8_t)ESP_BMS_GPS_MODULE_AVAILABLE) {
                runtime_set_error(runtime, "GPS OFFLINE");
                return false;
            }
            runtime->snapshot.speed_source = target;
        }
        if (runtime->snapshot.speed_source == ESP_BMS_SPEED_SOURCE_CONTROLLER &&
            runtime->controller_connection_enabled) {
            (void)esp_bms_idf_runtime_start_controller_ble_if_enabled(runtime);
        }
        runtime_project_controller_snapshot(runtime);
        return true;
    case ESP_BMS_LVGL_ACTION_SET_SPEED_SOURCE:
        if (!ACTION_EVENT_FLAG(event, NUMERIC_DELTA_VALID) ||
            (event->numeric_delta != (int16_t)ESP_BMS_SPEED_SOURCE_GPS &&
             event->numeric_delta != (int16_t)ESP_BMS_SPEED_SOURCE_CONTROLLER)) {
            return false;
        }
        {
            const esp_bms_speed_source_t target = (esp_bms_speed_source_t)event->numeric_delta;
            if (target == ESP_BMS_SPEED_SOURCE_GPS &&
                runtime->snapshot.gps_module_state !=
                    (uint8_t)ESP_BMS_GPS_MODULE_AVAILABLE) {
                runtime_set_error(runtime, "GPS OFFLINE");
                return false;
            }
            if (runtime->snapshot.speed_source == target) {
                return false;
            }
            runtime->snapshot.speed_source = target;
        }
        if (runtime->snapshot.speed_source == ESP_BMS_SPEED_SOURCE_CONTROLLER &&
            runtime->controller_connection_enabled) {
            (void)esp_bms_idf_runtime_start_controller_ble_if_enabled(runtime);
        }
        runtime_project_controller_snapshot(runtime);
        return true;
    case ESP_BMS_LVGL_ACTION_SET_PRESET_RANGE:
        if (!ACTION_EVENT_FLAG(event, NUMERIC_DELTA_VALID) ||
            event->numeric_delta < 0 ||
            event->numeric_delta > (int16_t)ESP_BMS_REMAINING_RANGE_MAX_KM) {
            return false;
        }
        if (runtime->snapshot.preset_range_km == (uint16_t)event->numeric_delta) {
            return false;
        }
        runtime->snapshot.preset_range_km = (uint16_t)event->numeric_delta;
        runtime_update_snapshot_speed(runtime);
        runtime_set_error(runtime, "RANGE SET");
        return true;
    case ESP_BMS_LVGL_ACTION_START_CONTROLLER_BIND:
        if (ACTION_EVENT_FLAG(event, CONTROLLER_MAC_VALID)) {
            char normalized_mac[sizeof(runtime->controller_bound_mac)] = { 0 };
            if (!runtime_normalize_mac_text(event->controller_mac,
                                            normalized_mac,
                                            sizeof(normalized_mac))) {
                return false;
            }
            const bool binding_changed = strcmp(runtime->controller_bound_mac, normalized_mac) != 0;
            runtime_copy_snapshot_text(runtime->controller_bound_mac,
                                       sizeof(runtime->controller_bound_mac),
                                       normalized_mac);
            for (uint8_t index = 0; index < runtime->controller_scan_candidate_count; ++index) {
                if (strcmp(runtime->controller_scan_candidates[index].mac, normalized_mac) == 0) {
                    runtime_copy_snapshot_text(runtime->controller_bound_name,
                                               sizeof(runtime->controller_bound_name),
                                               runtime->controller_scan_candidates[index].name);
                    break;
                }
            }
            runtime->controller_connection_enabled = true;
            if (binding_changed && runtime->controller_conn_handle != 0xFFFFU) {
                RUNTIME_SET_FLAG(runtime, CONTROLLER_SCAN_REQUESTED, true);
#if ESP_BMS_FEATURE_BLE
                (void)ble_gap_terminate(runtime->controller_conn_handle,
                                        BLE_ERR_REM_USER_CONN_TERM);
#endif
            } else {
                (void)esp_bms_idf_runtime_start_controller_ble_if_enabled(runtime);
            }
        } else {
            (void)esp_bms_idf_runtime_start_controller_scan(runtime);
        }
        runtime_project_controller_snapshot(runtime);
        return true;
    case ESP_BMS_LVGL_ACTION_ADJUST_CONTROLLER_WHEEL:
        if (!ACTION_EVENT_FLAG(event, NUMERIC_DELTA_VALID)) {
            return false;
        }
        {
            int32_t value = (int32_t)runtime->controller_state.fallback_wheel_circumference_mm +
                            event->numeric_delta;
            value = value < 0 ? 0 : value > 4000 ? 4000 : value;
            runtime->controller_state.fallback_wheel_circumference_mm = (uint16_t)value;
            runtime->controller_fallback_tire_rim_inch = 0U;
            runtime->controller_fallback_tire_aspect_percent = 0U;
            runtime->controller_fallback_tire_width_mm = 0U;
        }
        esp_fardriver_refresh_derived(&runtime->controller_state);
        runtime_project_controller_snapshot(runtime);
        return true;
    case ESP_BMS_LVGL_ACTION_ADJUST_CONTROLLER_RATIO:
        if (!ACTION_EVENT_FLAG(event, NUMERIC_DELTA_VALID)) {
            return false;
        }
        {
            int32_t value = (int32_t)runtime->controller_state.fallback_gear_ratio_centi +
                            event->numeric_delta;
            value = value < (int32_t)CONTROLLER_RATIO_CENTI_MIN
                        ? (int32_t)CONTROLLER_RATIO_CENTI_MIN
                        : value > (int32_t)CONTROLLER_RATIO_CENTI_MAX
                              ? (int32_t)CONTROLLER_RATIO_CENTI_MAX
                              : value;
            runtime->controller_state.fallback_gear_ratio_centi = (uint16_t)value;
        }
        esp_fardriver_refresh_derived(&runtime->controller_state);
        runtime_project_controller_snapshot(runtime);
        return true;
    case ESP_BMS_LVGL_ACTION_SET_CONTROLLER_TIRE:
        if (!ACTION_EVENT_FLAG(event, CONTROLLER_SETTING_VALID) ||
            !runtime_controller_tire_matches_policy(event->controller_tire_rim_inch,
                                                    event->controller_tire_aspect_percent,
                                                    event->controller_tire_width_mm)) {
            return false;
        }
        {
            uint16_t circumference_mm = 0U;
            if (!esp_fardriver_tire_circumference_mm(event->controller_tire_rim_inch,
                                                      event->controller_tire_aspect_percent,
                                                      event->controller_tire_width_mm,
                                                      &circumference_mm)) {
                return false;
            }
            runtime->controller_fallback_tire_rim_inch = event->controller_tire_rim_inch;
            runtime->controller_fallback_tire_aspect_percent =
                event->controller_tire_aspect_percent;
            runtime->controller_fallback_tire_width_mm = event->controller_tire_width_mm;
            runtime->controller_state.fallback_wheel_circumference_mm = circumference_mm;
        }
        esp_fardriver_refresh_derived(&runtime->controller_state);
        runtime_project_controller_snapshot(runtime);
        return true;
    case ESP_BMS_LVGL_ACTION_SET_CONTROLLER_RATIO:
        if (!ACTION_EVENT_FLAG(event, CONTROLLER_SETTING_VALID) ||
            !runtime_controller_ratio_matches_policy(event->controller_gear_ratio_centi)) {
            return false;
        }
        runtime->controller_state.fallback_gear_ratio_centi =
            event->controller_gear_ratio_centi;
        esp_fardriver_refresh_derived(&runtime->controller_state);
        runtime_project_controller_snapshot(runtime);
        return true;
    case ESP_BMS_LVGL_ACTION_START_BMS_BIND:
        if (ACTION_EVENT_FLAG(event, BMS_MAC_VALID)) {
            char normalized_mac[sizeof(runtime->bms_bound_mac)] = { 0 };
            if (!runtime_normalize_mac_text(event->bms_mac, normalized_mac, sizeof(normalized_mac)) ||
                !runtime_set_pending_http_bms_bind(runtime, normalized_mac)) {
                runtime_set_bms_info(runtime, "BMS BIND FAIL");
                ESP_LOGW(TAG, "[bms] bind action queue failed: mac=%s", event->bms_mac);
                return true;
            }
            runtime_set_bms_info(runtime, "BMS BIND");
            ESP_LOGI(TAG, "[bms] bind action queued: mac=%s", normalized_mac);
            return true;
        }
        if (!runtime_set_pending_http_bms_scan(runtime)) {
            runtime_set_bms_info(runtime, "BMS Q FAIL");
            ESP_LOGW(TAG, "[bms] BLE scan action queue failed");
        } else {
            ESP_LOGI(TAG, "[bms] BLE scan action queued");
        }
        return true;
    case ESP_BMS_LVGL_ACTION_CANCEL_BMS_CONNECTION:
        return runtime->bms_ble_driver && runtime->bms_ble_driver->stop
                   ? runtime->bms_ble_driver->stop(runtime)
                   : false;
    case ESP_BMS_LVGL_ACTION_PHONE_MEDIA_PREVIOUS:
#if ESP_BMS_FEATURE_BLE_MEDIA_HID && ESP_BMS_FEATURE_BLE
        return runtime_ble_media_hid_enqueue(runtime, ESP_BMS_BLE_MEDIA_HID_USAGE_PREVIOUS_TRACK);
#elif ESP_BMS_FEATURE_CLASSIC_MEDIA_HID
        return esp_bms_classic_media_hid_send_usage(
                   ESP_BMS_BLE_MEDIA_HID_USAGE_PREVIOUS_TRACK) == ESP_OK;
#else
        return false;
#endif
    case ESP_BMS_LVGL_ACTION_PHONE_MEDIA_NEXT:
#if ESP_BMS_FEATURE_BLE_MEDIA_HID && ESP_BMS_FEATURE_BLE
        return runtime_ble_media_hid_enqueue(runtime, ESP_BMS_BLE_MEDIA_HID_USAGE_NEXT_TRACK);
#elif ESP_BMS_FEATURE_CLASSIC_MEDIA_HID
        return esp_bms_classic_media_hid_send_usage(
                   ESP_BMS_BLE_MEDIA_HID_USAGE_NEXT_TRACK) == ESP_OK;
#else
        return false;
#endif
    case ESP_BMS_LVGL_ACTION_PHONE_MEDIA_VOLUME_DOWN:
#if ESP_BMS_FEATURE_BLE_MEDIA_HID && ESP_BMS_FEATURE_BLE
        return runtime_ble_media_hid_enqueue(runtime, ESP_BMS_BLE_MEDIA_HID_USAGE_VOLUME_DECREMENT);
#elif ESP_BMS_FEATURE_CLASSIC_MEDIA_HID
        return esp_bms_classic_media_hid_send_usage(
                   ESP_BMS_BLE_MEDIA_HID_USAGE_VOLUME_DECREMENT) == ESP_OK;
#else
        return false;
#endif
    case ESP_BMS_LVGL_ACTION_PHONE_MEDIA_VOLUME_UP:
#if ESP_BMS_FEATURE_BLE_MEDIA_HID && ESP_BMS_FEATURE_BLE
        return runtime_ble_media_hid_enqueue(runtime, ESP_BMS_BLE_MEDIA_HID_USAGE_VOLUME_INCREMENT);
#elif ESP_BMS_FEATURE_CLASSIC_MEDIA_HID
        return esp_bms_classic_media_hid_send_usage(
                   ESP_BMS_BLE_MEDIA_HID_USAGE_VOLUME_INCREMENT) == ESP_OK;
#else
        return false;
#endif
    case ESP_BMS_LVGL_ACTION_MEDIA_PLAY_PAUSE:
#if ESP_BMS_FEATURE_BLE_MEDIA_HID && ESP_BMS_FEATURE_BLE
        return runtime_ble_media_hid_enqueue(runtime, ESP_BMS_BLE_MEDIA_HID_USAGE_PLAY_PAUSE);
#elif ESP_BMS_FEATURE_CLASSIC_MEDIA_HID
        return esp_bms_classic_media_hid_send_usage(
                   ESP_BMS_BLE_MEDIA_HID_USAGE_PLAY_PAUSE) == ESP_OK;
#else
        return false;
#endif
    case ESP_BMS_LVGL_ACTION_ENABLE_BLUETOOTH_ADVERTISING:
#if ESP_BMS_FEATURE_CLASSIC_MEDIA_HID
        if (RUNTIME_FLAG(runtime, BLUETOOTH_CONNECTED)) {
            (void)esp_bms_classic_media_hid_set_discoverable(false);
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, false);
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, false);
            runtime_project_bluetooth_snapshot(runtime);
            runtime_set_error(runtime, "BT CONN");
            return true;
        }
        if (RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED) ||
            RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISING)) {
            const esp_err_t ret = esp_bms_classic_media_hid_set_discoverable(false);
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, false);
            RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING, false);
            runtime_project_bluetooth_snapshot(runtime);
            runtime_set_error(runtime, "BT HIDE");
            return ret == ESP_OK || ret == ESP_ERR_INVALID_STATE;
        }
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, true);
        const esp_err_t ret = esp_bms_classic_media_hid_set_discoverable(true);
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISING,
                         ret == ESP_OK && !RUNTIME_FLAG(runtime, BLUETOOTH_CONNECTED));
        runtime_project_bluetooth_snapshot(runtime);
        runtime_set_error(runtime,
                          ret == ESP_OK || ret == ESP_ERR_INVALID_STATE ? "BT ON" : "BT FAIL");
        return ret == ESP_OK || ret == ESP_ERR_INVALID_STATE;
#else
        if (RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED) ||
            RUNTIME_FLAG(runtime, BLUETOOTH_ADVERTISING)) {
            return runtime_bluetooth_stop_advertising(runtime) == ESP_OK;
        }
        RUNTIME_SET_FLAG(runtime, BLUETOOTH_ADVERTISE_REQUESTED, true);
        runtime_project_bluetooth_snapshot(runtime);
        runtime_set_error(runtime, "BT ON");
        return true;
#endif
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_ANT:
        return runtime_select_bms_type(runtime, ESP_BMS_IDF_BMS_TYPE_ANT);
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_JK:
        return runtime_select_bms_type(runtime, ESP_BMS_IDF_BMS_TYPE_JK);
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_JBD:
        return runtime_select_bms_type(runtime, ESP_BMS_IDF_BMS_TYPE_JBD);
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_DALY:
        return runtime_select_bms_type(runtime, ESP_BMS_IDF_BMS_TYPE_DALY);
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_YANYANG:
        return runtime_select_bms_type(runtime, ESP_BMS_IDF_BMS_TYPE_YANYANG);
    case ESP_BMS_LVGL_ACTION_RESTORE_DEFAULTS:
        esp_bms_idf_runtime_stop_controller_ble(runtime);
        runtime_reset_state(runtime);
        (void)runtime_sample_battery(runtime);
        runtime_set_error(runtime, "RESTORED");
        return true;
    case ESP_BMS_LVGL_ACTION_SHOW_DASHBOARD:
    case ESP_BMS_LVGL_ACTION_SHOW_QUICK_MENU:
    case ESP_BMS_LVGL_ACTION_SHOW_SETTINGS:
    case ESP_BMS_LVGL_ACTION_CYCLE_LEVEL_POSITION:
    case ESP_BMS_LVGL_ACTION_START_TOUCH_CALIBRATION:
    case ESP_BMS_LVGL_ACTION_ADD_TOUCH_CALIBRATION_SAMPLE:
    case ESP_BMS_LVGL_ACTION_CANCEL_TOUCH_CALIBRATION:
        return false;
    case ESP_BMS_LVGL_ACTION_NONE:
    default:
        return false;
    }
}

bool esp_bms_idf_runtime_apply_action(esp_bms_idf_runtime_t *runtime, esp_bms_lvgl_action_t action)
{
    const esp_bms_lvgl_action_event_t event = {
        .action = action,
    };
    return esp_bms_idf_runtime_apply_action_event(runtime, &event);
}

const char *esp_bms_idf_runtime_action_name(esp_bms_lvgl_action_t action)
{
    switch (action) {
    case ESP_BMS_LVGL_ACTION_SHOW_DASHBOARD:
        return "show-dashboard";
    case ESP_BMS_LVGL_ACTION_SHOW_QUICK_MENU:
        return "show-quick-menu";
    case ESP_BMS_LVGL_ACTION_SHOW_SETTINGS:
        return "show-settings";
    case ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING:
        return "enable-wifi-reprovisioning";
    case ESP_BMS_LVGL_ACTION_CYCLE_BRIGHTNESS:
        return "cycle-brightness";
    case ESP_BMS_LVGL_ACTION_SET_BRIGHTNESS:
        return "set-brightness";
    case ESP_BMS_LVGL_ACTION_SET_VOLUME:
        return "set-volume";
    case ESP_BMS_LVGL_ACTION_PHONE_MEDIA_PREVIOUS:
        return "phone-media-previous";
    case ESP_BMS_LVGL_ACTION_PHONE_MEDIA_NEXT:
        return "phone-media-next";
    case ESP_BMS_LVGL_ACTION_PHONE_MEDIA_VOLUME_DOWN:
        return "phone-media-volume-down";
    case ESP_BMS_LVGL_ACTION_PHONE_MEDIA_VOLUME_UP:
        return "phone-media-volume-up";
    case ESP_BMS_LVGL_ACTION_MEDIA_PLAY_PAUSE:
        return "media-play-pause";
    case ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY:
        return "rotate-display";
    case ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_UNIT:
        return "toggle-speed-unit";
    case ESP_BMS_LVGL_ACTION_TOGGLE_LANGUAGE:
        return "toggle-language";
    case ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_CONNECTION:
        return "toggle-controller-connection";
    case ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_PAGE:
        return "toggle-controller-page";
    case ESP_BMS_LVGL_ACTION_SET_SPEED_DASHBOARD_STYLE:
        return "set-speed-dashboard-style";
    case ESP_BMS_LVGL_ACTION_SET_BOOT_ANIMATION_STYLE:
        return "set-boot-animation-style";
    case ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_SOURCE:
        return "toggle-speed-source";
    case ESP_BMS_LVGL_ACTION_SET_SPEED_SOURCE:
        return "set-speed-source";
    case ESP_BMS_LVGL_ACTION_START_CONTROLLER_BIND:
        return "start-controller-bind";
    case ESP_BMS_LVGL_ACTION_ADJUST_CONTROLLER_WHEEL:
        return "adjust-controller-wheel";
    case ESP_BMS_LVGL_ACTION_ADJUST_CONTROLLER_RATIO:
        return "adjust-controller-ratio";
    case ESP_BMS_LVGL_ACTION_SET_CONTROLLER_TIRE:
        return "set-controller-tire";
    case ESP_BMS_LVGL_ACTION_SET_CONTROLLER_RATIO:
        return "set-controller-ratio";
    case ESP_BMS_LVGL_ACTION_SET_PRESET_RANGE:
        return "set-preset-range";
    case ESP_BMS_LVGL_ACTION_START_BMS_BIND:
        return "start-bms-bind";
    case ESP_BMS_LVGL_ACTION_CANCEL_BMS_CONNECTION:
        return "cancel-bms-connection";
    case ESP_BMS_LVGL_ACTION_ENABLE_BLUETOOTH_ADVERTISING:
        return "enable-bluetooth-advertising";
    case ESP_BMS_LVGL_ACTION_CYCLE_LEVEL_POSITION:
        return "cycle-level-position";
    case ESP_BMS_LVGL_ACTION_START_TOUCH_CALIBRATION:
        return "start-touch-calibration";
    case ESP_BMS_LVGL_ACTION_ADD_TOUCH_CALIBRATION_SAMPLE:
        return "add-touch-calibration-sample";
    case ESP_BMS_LVGL_ACTION_CANCEL_TOUCH_CALIBRATION:
        return "cancel-touch-calibration";
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_ANT:
        return "select-bms-ant";
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_JK:
        return "select-bms-jk";
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_JBD:
        return "select-bms-jbd";
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_DALY:
        return "select-bms-daly";
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_YANYANG:
        return "select-bms-yanyang";
    case ESP_BMS_LVGL_ACTION_RESTORE_DEFAULTS:
        return "restore-defaults";
    case ESP_BMS_LVGL_ACTION_NONE:
    default:
        return "none";
    }
}
