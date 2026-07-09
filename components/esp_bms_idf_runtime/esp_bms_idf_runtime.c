#include "esp_bms_idf_runtime.h"

#include "esp_event.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "os/os_mbuf.h"
#include "sdkconfig.h"
#include "services/gap/ble_svc_gap.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "bms_idf_runtime";

#define BATTERY_GPIO 34
#define BATTERY_SAMPLE_PERIOD_MS 2000U
#define BATTERY_ADC_MAX 4095U
#define BATTERY_REFERENCE_MV 3300U
#define BATTERY_DIVIDER_TOP_OHMS 100000U
#define BATTERY_DIVIDER_BOTTOM_OHMS 100000U
#define GPS_UART_PORT UART_NUM_0
#define GPS_UART_TX_GPIO 1
#define GPS_UART_RX_GPIO 3
#define GPS_UART_BAUD 9600
#define GPS_UART_RX_BUFFER_SIZE 1024
#define GPS_RMC_MAX_LINE 96U
#define SETUP_AP_SSID_PREFIX "fuckingBms_"
#define SETUP_AP_SSID_SUFFIX_LEN 6U
#define SETUP_AP_PASSWORD_LEN 8U
#define SETUP_AP_CHANNEL 1U
#define SETUP_AP_MAX_CONNECTIONS 1U
#define SETUP_AP_NVS_NAMESPACE "esp_bms"
#define SETUP_AP_NVS_SSID_KEY "setup_ssid"
#define SETUP_AP_NVS_PASSWORD_KEY "setup_pw"
#define EXTERNAL_WIFI_NVS_SSID_KEY "external_ssid"
#define EXTERNAL_WIFI_NVS_PASSWORD_KEY "external_pw"
#define EXTERNAL_WIFI_SSID_MAX_LEN 32U
#define EXTERNAL_WIFI_PASSWORD_MAX_LEN 64U
#define STATION_RECONNECT_MAX 5U
#define ANT_BMS_SERVICE_UUID_16 0xFFE0U
#define ANT_BMS_CHARACTERISTIC_UUID_16 0xFFE1U
#define BMS_SCAN_NAME_PREFIX "ANT-"
#define BMS_SCAN_DURATION_MS 10000
#define BMS_SCAN_HOST_TASK_STACK 4096U
#define BMS_SCAN_HOST_TASK_PRIORITY 5U
#define LOCAL_BLUETOOTH_NAME "ESP32 BMS GPS"
#define LOCAL_BLUETOOTH_ADV_INTERVAL_MS 500U
#define BMS_CONNECT_TIMEOUT_MS 30000
#define BMS_STATUS_POLL_PERIOD_MS 5000U
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
#define BMS_NVS_BOUND_MAC_KEY "bms_mac"
#define DISPLAY_NVS_BRIGHTNESS_KEY "disp_bright"
#define DISPLAY_NVS_VOLUME_KEY "disp_vol"
#define DISPLAY_NVS_ROTATION_KEY "disp_rot"
#define DISPLAY_NVS_SPEED_UNIT_KEY "speed_unit"
#define DISPLAY_NVS_LANGUAGE_KEY "lang"
#define DISPLAY_NVS_BMS_TYPE_KEY "bms_type"
#define HTTP_BODY_MAX_LEN 384U
#define HTTP_JSON_MAX_LEN 1024U
#define HTTP_AUTH_MAX_LEN 48U
#define HTTP_SETUP_USER "esp32"

extern const char web_index_html_start[] asm("_binary_index_html_start");
extern const char web_index_html_end[] asm("_binary_index_html_end");

typedef enum {
    GPS_PARSE_IGNORE,
    GPS_PARSE_ERROR,
    GPS_PARSE_FIX,
} gps_parse_result_t;

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

static esp_err_t runtime_apply_setup_ap_wifi_config(const esp_bms_idf_runtime_t *runtime);
static esp_err_t runtime_apply_external_wifi_config(esp_bms_idf_runtime_t *runtime);
static int runtime_bms_ble_gap_event(struct ble_gap_event *event, void *arg);
static int runtime_bluetooth_gap_event(struct ble_gap_event *event, void *arg);
static esp_err_t runtime_bms_ble_start_scan(esp_bms_idf_runtime_t *runtime);
static esp_err_t runtime_bluetooth_start_advertising_now(esp_bms_idf_runtime_t *runtime);
static esp_err_t runtime_bluetooth_stop_for_bms_scan(esp_bms_idf_runtime_t *runtime);
static esp_err_t runtime_bms_ble_send_poll_request(esp_bms_idf_runtime_t *runtime);
static esp_err_t runtime_init_bms_ble(esp_bms_idf_runtime_t *runtime);
static esp_err_t runtime_wifi_start_scan(esp_bms_idf_runtime_t *runtime);
static void runtime_wifi_scan_handle_done(esp_bms_idf_runtime_t *runtime);
static esp_err_t runtime_start_station_connect(esp_bms_idf_runtime_t *runtime);
static esp_err_t runtime_save_external_wifi_credentials(esp_bms_idf_runtime_t *runtime);
static esp_err_t runtime_save_bms_binding(esp_bms_idf_runtime_t *runtime);
static esp_err_t runtime_save_setup_ap_credentials(const esp_bms_idf_runtime_t *runtime);
static void runtime_ensure_setup_ap_credentials(esp_bms_idf_runtime_t *runtime);
static char runtime_hex_char(uint8_t value);

static esp_bms_idf_runtime_t *s_bms_ble_runtime;

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
}

static bool runtime_project_bluetooth_snapshot(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return false;
    }

    const bool enabled = runtime->bluetooth_advertise_requested ||
                         runtime->bluetooth_advertising ||
                         runtime->bluetooth_connected;
    const bool changed = runtime->snapshot.bluetooth_enabled != enabled ||
                         runtime->snapshot.bluetooth_advertising != runtime->bluetooth_advertising ||
                         runtime->snapshot.bluetooth_connected != runtime->bluetooth_connected ||
                         strcmp(runtime->snapshot.bluetooth_name, runtime->bluetooth_name) != 0;

    runtime->snapshot.bluetooth_enabled = enabled;
    runtime->snapshot.bluetooth_advertising = runtime->bluetooth_advertising;
    runtime->snapshot.bluetooth_connected = runtime->bluetooth_connected;
    runtime_copy_snapshot_text(runtime->snapshot.bluetooth_name,
                               sizeof(runtime->snapshot.bluetooth_name),
                               runtime->bluetooth_name);
    if (changed) {
        runtime->bluetooth_snapshot_dirty = true;
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

static bool runtime_external_wifi_text_is_json_safe(const char *text)
{
    for (const char *cursor = text; *cursor; cursor++) {
        const unsigned char value = (unsigned char)*cursor;
        if (value < 0x20U || *cursor == '"' || *cursor == '\\') {
            return false;
        }
    }
    return true;
}

static bool runtime_external_wifi_credentials_match_policy(const char *ssid, const char *password)
{
    if (!ssid || !password) {
        return false;
    }

    const size_t ssid_len = strlen(ssid);
    const size_t password_len = strlen(password);
    if (ssid_len == 0U || ssid_len > EXTERNAL_WIFI_SSID_MAX_LEN) {
        return false;
    }
    if (password_len > 0U &&
        (password_len < 8U || password_len > EXTERNAL_WIFI_PASSWORD_MAX_LEN)) {
        return false;
    }
    return runtime_external_wifi_text_is_json_safe(ssid) &&
           runtime_external_wifi_text_is_json_safe(password);
}

static bool runtime_has_external_wifi_credentials(const esp_bms_idf_runtime_t *runtime)
{
    return runtime_external_wifi_credentials_match_policy(runtime->external_ssid,
                                                          runtime->external_password);
}

static void runtime_wifi_scan_project_snapshot(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return;
    }

    if (runtime->wifi_scan_lock &&
        xSemaphoreTake(runtime->wifi_scan_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    memset(runtime->snapshot.wifi_scan_candidates,
           0,
           sizeof(runtime->snapshot.wifi_scan_candidates));
    runtime->snapshot.wifi_scan_count = runtime->wifi_scan_candidate_count;
    if (runtime->snapshot.wifi_scan_count > ESP_BMS_WIFI_SCAN_MAX_CANDIDATES) {
        runtime->snapshot.wifi_scan_count = ESP_BMS_WIFI_SCAN_MAX_CANDIDATES;
    }
    for (uint8_t index = 0; index < runtime->snapshot.wifi_scan_count; ++index) {
        runtime->snapshot.wifi_scan_candidates[index] = runtime->wifi_scan_candidates[index];
    }
    runtime->snapshot.wifi_scan_active = runtime->wifi_scan_active;
    runtime->snapshot.wifi_scan_complete = runtime->wifi_scan_complete;

    if (runtime->wifi_scan_lock) {
        xSemaphoreGive(runtime->wifi_scan_lock);
    }
}

static void runtime_wifi_scan_set_state(esp_bms_idf_runtime_t *runtime,
                                        bool active,
                                        bool complete)
{
    if (!runtime) {
        return;
    }

    if (runtime->wifi_scan_lock &&
        xSemaphoreTake(runtime->wifi_scan_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    runtime->wifi_scan_active = active;
    runtime->wifi_scan_complete = complete;
    runtime->snapshot.wifi_scan_generation++;
    if (!active && !complete) {
        runtime->wifi_scan_candidate_count = 0;
        memset(runtime->wifi_scan_candidates, 0, sizeof(runtime->wifi_scan_candidates));
    }
    if (runtime->wifi_scan_lock) {
        xSemaphoreGive(runtime->wifi_scan_lock);
    }
    runtime_wifi_scan_project_snapshot(runtime);
}

static void runtime_wifi_scan_clear_candidates(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return;
    }
    if (runtime->wifi_scan_lock &&
        xSemaphoreTake(runtime->wifi_scan_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    memset(runtime->wifi_scan_candidates, 0, sizeof(runtime->wifi_scan_candidates));
    runtime->wifi_scan_candidate_count = 0;
    memset(runtime->snapshot.wifi_scan_candidates,
           0,
           sizeof(runtime->snapshot.wifi_scan_candidates));
    runtime->snapshot.wifi_scan_count = 0;
    if (runtime->wifi_scan_lock) {
        xSemaphoreGive(runtime->wifi_scan_lock);
    }
}

static bool runtime_wifi_scan_ssid_copy(char *out, size_t out_len, const uint8_t *ssid)
{
    if (!out || out_len == 0U || !ssid) {
        return false;
    }
    out[0] = '\0';
    size_t copied = 0;
    while (copied + 1U < out_len && copied < EXTERNAL_WIFI_SSID_MAX_LEN && ssid[copied] != 0U) {
        out[copied] = (char)ssid[copied];
        copied++;
    }
    out[copied] = '\0';
    return copied > 0U;
}

static void runtime_wifi_scan_store_candidate(esp_bms_idf_runtime_t *runtime,
                                              const wifi_ap_record_t *record)
{
    if (!runtime || !record) {
        return;
    }

    char ssid[ESP_BMS_WIFI_SCAN_SSID_LEN + 1U] = { 0 };
    if (!runtime_wifi_scan_ssid_copy(ssid, sizeof(ssid), record->ssid)) {
        return;
    }

    if (runtime->wifi_scan_lock &&
        xSemaphoreTake(runtime->wifi_scan_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    size_t index = runtime->wifi_scan_candidate_count;
    for (size_t candidate = 0; candidate < runtime->wifi_scan_candidate_count; ++candidate) {
        if (strcmp(runtime->wifi_scan_candidates[candidate].ssid, ssid) == 0) {
            if (record->rssi <= runtime->wifi_scan_candidates[candidate].rssi) {
                if (runtime->wifi_scan_lock) {
                    xSemaphoreGive(runtime->wifi_scan_lock);
                }
                return;
            }
            index = candidate;
            break;
        }
    }

    if (index >= ESP_BMS_WIFI_SCAN_MAX_CANDIDATES) {
        index = 0;
        for (size_t candidate = 1; candidate < ESP_BMS_WIFI_SCAN_MAX_CANDIDATES; ++candidate) {
            if (runtime->wifi_scan_candidates[candidate].rssi < runtime->wifi_scan_candidates[index].rssi) {
                index = candidate;
            }
        }
        if (record->rssi <= runtime->wifi_scan_candidates[index].rssi) {
            if (runtime->wifi_scan_lock) {
                xSemaphoreGive(runtime->wifi_scan_lock);
            }
            return;
        }
    } else if (index == runtime->wifi_scan_candidate_count) {
        runtime->wifi_scan_candidate_count++;
    }

    runtime_copy_snapshot_text(runtime->wifi_scan_candidates[index].ssid,
                               sizeof(runtime->wifi_scan_candidates[index].ssid),
                               ssid);
    runtime->wifi_scan_candidates[index].rssi = record->rssi;
    runtime->wifi_scan_candidates[index].auth_mode = (uint8_t)record->authmode;
    runtime->wifi_scan_candidates[index].open = record->authmode == WIFI_AUTH_OPEN;
    if (runtime->wifi_scan_lock) {
        xSemaphoreGive(runtime->wifi_scan_lock);
    }
}

static void runtime_ble_addr_to_mac_text(const uint8_t addr[6], char *out, size_t out_len)
{
    if (!out || out_len < 18U) {
        return;
    }
    (void)snprintf(out,
                   out_len,
                   "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
                   runtime_hex_char(addr[5] >> 4),
                   runtime_hex_char(addr[5] & 0x0FU),
                   runtime_hex_char(addr[4] >> 4),
                   runtime_hex_char(addr[4] & 0x0FU),
                   runtime_hex_char(addr[3] >> 4),
                   runtime_hex_char(addr[3] & 0x0FU),
                   runtime_hex_char(addr[2] >> 4),
                   runtime_hex_char(addr[2] & 0x0FU),
                   runtime_hex_char(addr[1] >> 4),
                   runtime_hex_char(addr[1] & 0x0FU),
                   runtime_hex_char(addr[0] >> 4),
                   runtime_hex_char(addr[0] & 0x0FU));
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
    const size_t limit = name_len < ESP_BMS_IDF_BMS_SCAN_NAME_LEN
                             ? name_len
                             : ESP_BMS_IDF_BMS_SCAN_NAME_LEN;
    for (size_t index = 0; index < limit && copied + 1U < out_len; index++) {
        const unsigned char value = name[index];
        if (value < 0x20U || value > 0x7EU || value == '"' || value == '\\') {
            break;
        }
        out[copied++] = (char)value;
    }
    out[copied] = '\0';
    return copied > 0U;
}

static bool runtime_bms_name_matches_ant(const char *name)
{
    return name && strncmp(name, BMS_SCAN_NAME_PREFIX, strlen(BMS_SCAN_NAME_PREFIX)) == 0;
}

static bool runtime_bms_adv_has_ant_service(const struct ble_hs_adv_fields *fields)
{
    for (uint8_t index = 0; index < fields->num_uuids16; index++) {
        if (ble_uuid_u16(&fields->uuids16[index].u) == ANT_BMS_SERVICE_UUID_16) {
            return true;
        }
    }
    return false;
}

static void runtime_bms_scan_clear_candidates(esp_bms_idf_runtime_t *runtime)
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

static size_t runtime_bms_scan_weakest_candidate(const esp_bms_idf_runtime_t *runtime)
{
    size_t weakest = 0;
    for (size_t index = 1; index < ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES; index++) {
        if (runtime->bms_scan_candidates[index].rssi < runtime->bms_scan_candidates[weakest].rssi) {
            weakest = index;
        }
    }
    return weakest;
}

static void runtime_bms_scan_store_candidate(esp_bms_idf_runtime_t *runtime,
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

    size_t index = runtime_bms_scan_find_candidate(runtime, mac);
    if (index < ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES) {
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
        return;
    }

    if (runtime->bms_scan_candidate_count < ESP_BMS_IDF_BMS_SCAN_MAX_CANDIDATES) {
        index = runtime->bms_scan_candidate_count++;
    } else {
        index = runtime_bms_scan_weakest_candidate(runtime);
        if (rssi <= runtime->bms_scan_candidates[index].rssi) {
            if (runtime->bms_scan_lock) {
                xSemaphoreGive(runtime->bms_scan_lock);
            }
            return;
        }
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
}

static void runtime_clear_bms_telemetry(esp_bms_idf_runtime_t *runtime)
{
    runtime->snapshot.bms_online = false;
    runtime->snapshot.pack_voltage_valid = false;
    runtime->snapshot.pack_voltage_mv = 0;
    runtime->snapshot.current_valid = false;
    runtime->snapshot.current_deci_amps = 0;
    runtime->snapshot.soc_valid = false;
    runtime->snapshot.soc_percent = 0;
    runtime->snapshot.min_cell_valid = false;
    runtime->snapshot.min_cell_voltage_mv = 0;
    runtime->snapshot.average_cell_valid = false;
    runtime->snapshot.average_cell_voltage_mv = 0;
    runtime->snapshot.max_cell_valid = false;
    runtime->snapshot.max_cell_voltage_mv = 0;
    runtime->snapshot.delta_cell_valid = false;
    runtime->snapshot.delta_cell_voltage_mv = 0;
    runtime->snapshot.total_capacity_valid = false;
    runtime->snapshot.total_capacity_mah = 0;
    runtime->snapshot.capacity_remaining_valid = false;
    runtime->snapshot.capacity_remaining_mah = 0;
    runtime->snapshot.bms_protection_count = 0;
    memset(runtime->snapshot.bms_protection_codes, 0, sizeof(runtime->snapshot.bms_protection_codes));
    runtime->snapshot.bms_warning_count = 0;
    memset(runtime->snapshot.bms_warning_codes, 0, sizeof(runtime->snapshot.bms_warning_codes));
    memset(runtime->snapshot.bms_temperature_valid, 0, sizeof(runtime->snapshot.bms_temperature_valid));
    memset(runtime->snapshot.bms_temperature_celsius, 0, sizeof(runtime->snapshot.bms_temperature_celsius));
    runtime_copy_snapshot_text(runtime->snapshot.bms_info_text,
                               sizeof(runtime->snapshot.bms_info_text),
                               "BMS OFF");
}

static void runtime_append_bms_code(char codes[][ESP_BMS_BMS_CODE_TEXT_LEN],
                                    uint8_t *count,
                                    const char prefix,
                                    uint8_t bit)
{
    if (!codes || !count || *count >= ESP_BMS_BMS_CODE_MAX_COUNT) {
        return;
    }
    (void)snprintf(codes[*count], ESP_BMS_BMS_CODE_TEXT_LEN, "%c%02u", prefix, (unsigned)bit);
    (*count)++;
}

static void runtime_apply_bms_fault_masks(esp_bms_dashboard_snapshot_t *snapshot,
                                          uint64_t protection_mask,
                                          uint64_t warning_mask)
{
    snapshot->bms_protection_count = 0;
    memset(snapshot->bms_protection_codes, 0, sizeof(snapshot->bms_protection_codes));
    snapshot->bms_warning_count = 0;
    memset(snapshot->bms_warning_codes, 0, sizeof(snapshot->bms_warning_codes));

    for (uint8_t bit = 0; bit < 64U; bit++) {
        const uint64_t mask = 1ULL << bit;
        if ((protection_mask & mask) != 0ULL) {
            runtime_append_bms_code(snapshot->bms_protection_codes,
                                    &snapshot->bms_protection_count,
                                    'P',
                                    bit);
        }
        if ((warning_mask & mask) != 0ULL) {
            runtime_append_bms_code(snapshot->bms_warning_codes,
                                    &snapshot->bms_warning_count,
                                    'W',
                                    bit);
        }
    }
}

static uint16_t runtime_crc16_modbus(const uint8_t *bytes, size_t len)
{
    uint16_t crc = 0xFFFFU;
    for (size_t index = 0; index < len; index++) {
        crc ^= bytes[index];
        for (uint8_t bit = 0; bit < 8U; bit++) {
            crc = (crc & 0x0001U) ? (uint16_t)((crc >> 1) ^ 0xA001U)
                                  : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static bool runtime_read_u16_le(const uint8_t *data, size_t len, size_t index, uint16_t *out)
{
    if (!data || !out || index + 1U >= len) {
        return false;
    }
    *out = (uint16_t)data[index] | ((uint16_t)data[index + 1U] << 8);
    return true;
}

static bool runtime_read_i16_le(const uint8_t *data, size_t len, size_t index, int16_t *out)
{
    uint16_t value = 0;
    if (!runtime_read_u16_le(data, len, index, &value)) {
        return false;
    }
    *out = (int16_t)value;
    return true;
}

static bool runtime_read_u32_le(const uint8_t *data, size_t len, size_t index, uint32_t *out)
{
    if (!data || !out || index + 3U >= len) {
        return false;
    }
    *out = (uint32_t)data[index] |
           ((uint32_t)data[index + 1U] << 8) |
           ((uint32_t)data[index + 2U] << 16) |
           ((uint32_t)data[index + 3U] << 24);
    return true;
}

static bool runtime_read_u64_le(const uint8_t *data, size_t len, size_t index, uint64_t *out)
{
    if (!data || !out || index + 7U >= len) {
        return false;
    }
    uint64_t value = 0;
    for (uint8_t byte = 0; byte < 8U; byte++) {
        value |= ((uint64_t)data[index + byte]) << (byte * 8U);
    }
    *out = value;
    return true;
}

static bool runtime_validate_bms_frame(const uint8_t *data,
                                       size_t len,
                                       uint8_t *function,
                                       size_t *protocol_len)
{
    if (!data || !function || !protocol_len || len < BMS_FRAME_MIN_LEN ||
        len > ESP_BMS_IDF_BMS_FRAME_MAX_LEN ||
        data[0] != BMS_FRAME_START_1 ||
        data[1] != BMS_FRAME_START_2 ||
        data[len - 2U] != BMS_FRAME_END_1 ||
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
    const uint16_t expected_crc = runtime_crc16_modbus(&data[1], crc_offset - 1U);
    const uint16_t remote_crc = (uint16_t)data[crc_offset] |
                                ((uint16_t)data[crc_offset + 1U] << 8);
    return expected_crc == remote_crc;
}

static bool runtime_apply_bms_status_frame(esp_bms_idf_runtime_t *runtime,
                                           const uint8_t *data,
                                           size_t len)
{
    uint8_t function = 0;
    size_t protocol_len = 0;
    if (!runtime_validate_bms_frame(data, len, &function, &protocol_len) ||
        function != BMS_FRAME_TYPE_STATUS) {
        return false;
    }

    const uint8_t temperature_sensor_count = data[8];
    const uint8_t cell_count = data[9];
    if (cell_count > BMS_MAX_CELLS ||
        temperature_sensor_count > BMS_MAX_TEMPERATURE_SENSORS) {
        return false;
    }

    const size_t dynamic_offset = ((size_t)cell_count * 2U) +
                                  ((size_t)temperature_sensor_count * 2U);
    uint16_t pack_voltage_dv = 0;
    int16_t current_deci_amps = 0;
    uint16_t soc_percent = 0;
    uint32_t total_capacity_uah = 0;
    uint32_t capacity_remaining_uah = 0;
    uint64_t protection_mask = 0;
    uint64_t warning_mask = 0;
    uint16_t max_cell_mv = 0;
    uint16_t min_cell_mv = 0;
    uint16_t delta_cell_mv = 0;
    uint16_t average_cell_mv = 0;
    int16_t temperatures[ESP_BMS_BMS_TEMP_MAX_COUNT] = { 0 };
    bool temperature_valid[ESP_BMS_BMS_TEMP_MAX_COUNT] = { false };

    if (!runtime_read_u64_le(data, protocol_len, BMS_STATUS_PROTECTION_MASK_OFFSET, &protection_mask) ||
        !runtime_read_u64_le(data, protocol_len, BMS_STATUS_WARNING_MASK_OFFSET, &warning_mask) ||
        !runtime_read_u16_le(data, protocol_len, 38U + dynamic_offset, &pack_voltage_dv) ||
        !runtime_read_i16_le(data, protocol_len, 40U + dynamic_offset, &current_deci_amps) ||
        !runtime_read_u16_le(data, protocol_len, 42U + dynamic_offset, &soc_percent) ||
        !runtime_read_u32_le(data, protocol_len, 50U + dynamic_offset, &total_capacity_uah) ||
        !runtime_read_u32_le(data, protocol_len, 54U + dynamic_offset, &capacity_remaining_uah) ||
        !runtime_read_u16_le(data, protocol_len, 74U + dynamic_offset, &max_cell_mv) ||
        !runtime_read_u16_le(data, protocol_len, 78U + dynamic_offset, &min_cell_mv) ||
        !runtime_read_u16_le(data, protocol_len, 82U + dynamic_offset, &delta_cell_mv) ||
        !runtime_read_u16_le(data, protocol_len, 84U + dynamic_offset, &average_cell_mv)) {
        return false;
    }

    const size_t temperature_offset = BMS_STATUS_DYNAMIC_BASE_OFFSET + ((size_t)cell_count * 2U);
    const uint8_t temperature_count = temperature_sensor_count > ESP_BMS_BMS_TEMP_MAX_COUNT - 2U
                                          ? ESP_BMS_BMS_TEMP_MAX_COUNT - 2U
                                          : temperature_sensor_count;
    for (uint8_t index = 0; index < temperature_count; index++) {
        if (!runtime_read_i16_le(data, protocol_len, temperature_offset + ((size_t)index * 2U), &temperatures[index])) {
            return false;
        }
        temperature_valid[index] = true;
    }
    if (!runtime_read_i16_le(data, protocol_len, BMS_STATUS_DYNAMIC_BASE_OFFSET + dynamic_offset, &temperatures[4]) ||
        !runtime_read_i16_le(data, protocol_len, BMS_STATUS_DYNAMIC_BASE_OFFSET + dynamic_offset + 2U, &temperatures[5])) {
        return false;
    }
    temperature_valid[4] = true;
    temperature_valid[5] = true;

    runtime->snapshot.bms_online = true;
    runtime->snapshot.pack_voltage_valid = true;
    runtime->snapshot.pack_voltage_mv = (uint32_t)pack_voltage_dv * 10U;
    runtime->snapshot.current_valid = true;
    runtime->snapshot.current_deci_amps = current_deci_amps;
    runtime->snapshot.soc_valid = true;
    runtime->snapshot.soc_percent = soc_percent;
    runtime->snapshot.max_cell_valid = true;
    runtime->snapshot.max_cell_voltage_mv = max_cell_mv;
    runtime->snapshot.min_cell_valid = true;
    runtime->snapshot.min_cell_voltage_mv = min_cell_mv;
    runtime->snapshot.delta_cell_valid = true;
    runtime->snapshot.delta_cell_voltage_mv = delta_cell_mv;
    runtime->snapshot.average_cell_valid = true;
    runtime->snapshot.average_cell_voltage_mv = average_cell_mv;
    runtime->snapshot.total_capacity_valid = true;
    runtime->snapshot.total_capacity_mah = total_capacity_uah / 1000U;
    runtime->snapshot.capacity_remaining_valid = true;
    runtime->snapshot.capacity_remaining_mah = capacity_remaining_uah / 1000U;
    memcpy(runtime->snapshot.bms_temperature_valid,
           temperature_valid,
           sizeof(runtime->snapshot.bms_temperature_valid));
    memcpy(runtime->snapshot.bms_temperature_celsius,
           temperatures,
           sizeof(runtime->snapshot.bms_temperature_celsius));
    runtime_apply_bms_fault_masks(&runtime->snapshot, protection_mask, warning_mask);
    runtime_set_bms_info(runtime, "BMS OK");
    return true;
}

static bool runtime_apply_bms_frame(esp_bms_idf_runtime_t *runtime,
                                    const uint8_t *data,
                                    size_t len)
{
    uint8_t function = 0;
    size_t protocol_len = 0;
    if (!runtime_validate_bms_frame(data, len, &function, &protocol_len)) {
        return false;
    }

    if (function == BMS_FRAME_TYPE_STATUS) {
        return runtime_apply_bms_status_frame(runtime, data, len);
    }
    if (function == BMS_FRAME_TYPE_DEVICE_INFO) {
        runtime->bms_device_info_known = true;
        return true;
    }
    return false;
}

static bool runtime_bms_frame_push(esp_bms_idf_runtime_t *runtime,
                                   const uint8_t *chunk,
                                   size_t chunk_len)
{
    if (!runtime || !chunk || chunk_len == 0U) {
        return false;
    }

    if (chunk_len >= 2U && chunk[0] == BMS_FRAME_START_1 && chunk[1] == BMS_FRAME_START_2) {
        runtime->bms_frame_len = 0;
    } else if (runtime->bms_frame_len == 0U && chunk[0] != BMS_FRAME_START_1) {
        return false;
    }

    if ((size_t)runtime->bms_frame_len + chunk_len > sizeof(runtime->bms_frame)) {
        runtime->bms_frame_len = 0;
        return false;
    }

    memcpy(&runtime->bms_frame[runtime->bms_frame_len], chunk, chunk_len);
    runtime->bms_frame_len = (uint16_t)(runtime->bms_frame_len + chunk_len);

    if (runtime->bms_frame_len >= BMS_FRAME_MIN_LEN &&
        runtime->bms_frame[runtime->bms_frame_len - 2U] == BMS_FRAME_END_1 &&
        runtime->bms_frame[runtime->bms_frame_len - 1U] == BMS_FRAME_END_2) {
        const bool applied = runtime_apply_bms_frame(runtime,
                                                     runtime->bms_frame,
                                                     runtime->bms_frame_len);
        runtime->bms_frame_len = 0;
        return applied;
    }

    return true;
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

static bool runtime_parse_decimal_milli(const char *text, size_t len, uint32_t *out_milli)
{
    uint64_t whole = 0;
    uint32_t fraction = 0;
    uint32_t fraction_scale = 100;
    bool seen_digit = false;
    bool seen_decimal = false;

    for (size_t index = 0; index < len; index++) {
        const char value = text[index];
        if (value >= '0' && value <= '9') {
            seen_digit = true;
            if (seen_decimal) {
                if (fraction_scale > 0) {
                    fraction += (uint32_t)(value - '0') * fraction_scale;
                    fraction_scale /= 10;
                }
            } else {
                whole = (whole * 10U) + (uint32_t)(value - '0');
                if (whole > UINT32_MAX) {
                    return false;
                }
            }
        } else if (value == '.' && !seen_decimal) {
            seen_decimal = true;
        } else {
            return false;
        }
    }

    if (!seen_digit) {
        return false;
    }

    const uint64_t milli = (whole * 1000U) + fraction;
    if (milli > UINT32_MAX) {
        return false;
    }
    *out_milli = (uint32_t)milli;
    return true;
}

static bool runtime_field_equals(const char *field, size_t len, const char *expected)
{
    return strlen(expected) == len && memcmp(field, expected, len) == 0;
}

static bool runtime_is_rmc_kind(const char *field, size_t len)
{
    return runtime_field_equals(field, len, "GPRMC") ||
           runtime_field_equals(field, len, "GNRMC") ||
           runtime_field_equals(field, len, "GARMC") ||
           runtime_field_equals(field, len, "GLRMC") ||
           runtime_field_equals(field, len, "BDRMC");
}

static bool runtime_validate_nmea_checksum(const char *payload, size_t payload_len, const char *checksum)
{
    const int high = hex_value(checksum[0]);
    const int low = hex_value(checksum[1]);
    if (high < 0 || low < 0) {
        return false;
    }

    uint8_t actual = 0;
    for (size_t index = 0; index < payload_len; index++) {
        actual ^= (uint8_t)payload[index];
    }
    return actual == (uint8_t)((high << 4) | low);
}

static gps_parse_result_t runtime_parse_rmc(const uint8_t *line,
                                            size_t len,
                                            bool *fix_valid,
                                            uint32_t *speed_knots_milli)
{
    if (len == 0) {
        return GPS_PARSE_IGNORE;
    }

    const char *payload = (const char *)line;
    size_t payload_len = len;
    if (payload_len > 0 && payload[0] == '$') {
        payload++;
        payload_len--;
    }

    for (size_t index = 0; index < payload_len; index++) {
        if (payload[index] == '*') {
            if ((payload_len - index) < 3U ||
                !runtime_validate_nmea_checksum(payload, index, &payload[index + 1])) {
                return GPS_PARSE_ERROR;
            }
            payload_len = index;
            break;
        }
    }

    bool is_rmc = false;
    bool status_seen = false;
    bool speed_seen = false;
    uint8_t status = 'V';
    uint32_t parsed_speed_milli = 0;
    size_t field_index = 0;
    size_t field_start = 0;

    for (size_t index = 0; index <= payload_len; index++) {
        if (index != payload_len && payload[index] != ',') {
            continue;
        }

        const char *field = &payload[field_start];
        const size_t field_len = index - field_start;
        switch (field_index) {
        case 0:
            is_rmc = runtime_is_rmc_kind(field, field_len);
            if (!is_rmc) {
                return GPS_PARSE_IGNORE;
            }
            break;
        case 2:
            if (field_len == 0) {
                return GPS_PARSE_ERROR;
            }
            status = (uint8_t)field[0];
            status_seen = true;
            break;
        case 7:
            if (!runtime_parse_decimal_milli(field, field_len, &parsed_speed_milli)) {
                return GPS_PARSE_ERROR;
            }
            speed_seen = true;
            break;
        default:
            break;
        }

        field_index++;
        field_start = index + 1U;
    }

    if (!status_seen || !speed_seen) {
        return GPS_PARSE_ERROR;
    }

    *fix_valid = status == 'A';
    *speed_knots_milli = parsed_speed_milli;
    return GPS_PARSE_FIX;
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
    runtime->snapshot.speed_valid = runtime->snapshot.gps_fix_valid;
    runtime->snapshot.speed_deci_units = runtime->snapshot.speed_valid
                                             ? runtime_speed_deci_units(runtime->snapshot.speed_unit,
                                                                        runtime->gps_speed_knots_milli)
                                             : 0;
}

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

static const char *runtime_bms_type_config_text(esp_bms_idf_bms_type_t type)
{
    switch (type) {
    case ESP_BMS_IDF_BMS_TYPE_JK:
        return "jk";
    case ESP_BMS_IDF_BMS_TYPE_JBD:
        return "jbd";
    case ESP_BMS_IDF_BMS_TYPE_DALY:
        return "daly";
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
    case ESP_BMS_IDF_BMS_TYPE_ANT:
    default:
        return "BMS ANT";
    }
}

static const char *runtime_wifi_config_text(esp_bms_wifi_state_t wifi)
{
    switch (wifi) {
    case ESP_BMS_WIFI_CONNECTING:
        return "connecting";
    case ESP_BMS_WIFI_CONNECTED:
        return "connected";
    case ESP_BMS_WIFI_OFFLINE:
        return "offline";
    case ESP_BMS_WIFI_SETUP_AP:
    default:
        return "setup";
    }
}

static const char *runtime_ota_config_text(esp_bms_ota_state_t ota)
{
    switch (ota) {
    case ESP_BMS_OTA_CHECKING:
        return "checking";
    case ESP_BMS_OTA_AVAILABLE:
        return "available";
    case ESP_BMS_OTA_DOWNLOADING:
        return "downloading";
    case ESP_BMS_OTA_VERIFYING:
        return "verifying";
    case ESP_BMS_OTA_READY:
        return "ready";
    case ESP_BMS_OTA_FAILED:
        return "failed";
    case ESP_BMS_OTA_IDLE:
    default:
        return "idle";
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

static bool runtime_speed_unit_matches_policy(uint8_t speed_unit)
{
    return speed_unit == (uint8_t)ESP_BMS_SPEED_UNIT_KMH ||
           speed_unit == (uint8_t)ESP_BMS_SPEED_UNIT_MPH;
}

static bool runtime_language_matches_policy(uint8_t language)
{
    return language <= 1U;
}

static bool runtime_bms_type_matches_policy(uint8_t bms_type)
{
    return bms_type <= (uint8_t)ESP_BMS_IDF_BMS_TYPE_DALY;
}

static bool runtime_select_bms_type(esp_bms_idf_runtime_t *runtime, esp_bms_idf_bms_type_t bms_type)
{
    if (!runtime || !runtime_bms_type_matches_policy((uint8_t)bms_type)) {
        return false;
    }
    if (runtime->bms_type == (uint8_t)bms_type) {
        return false;
    }

    runtime->bms_type = (uint8_t)bms_type;
    runtime->snapshot.bms_type = runtime->bms_type;
    runtime_set_error(runtime, runtime_bms_type_status_text(bms_type));
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
    char pattern[32] = { 0 };
    const int written = snprintf(pattern, sizeof(pattern), "\"%s\"", field);
    if (written < 0 || (size_t)written >= sizeof(pattern)) {
        return false;
    }

    const char *cursor = strstr(body, pattern);
    if (!cursor) {
        return false;
    }
    cursor += strlen(pattern);
    while (*cursor && isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor != ':') {
        return false;
    }
    cursor++;
    while (*cursor && isspace((unsigned char)*cursor)) {
        cursor++;
    }
    *out_value = cursor;
    return true;
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
        if (*cursor == '\\' || (unsigned char)*cursor < 0x20U || len + 1U >= out_len) {
            return false;
        }
        out_value[len++] = *cursor++;
    }
    if (*cursor != '"') {
        return false;
    }
    out_value[len] = '\0';
    return true;
}

static void runtime_reset_state(esp_bms_idf_runtime_t *runtime)
{
    memset(&runtime->snapshot, 0, sizeof(runtime->snapshot));
    runtime->tick_count = 0;
    runtime->elapsed_ms = 0;
    runtime->battery_sample_elapsed_ms = 0;
    runtime->battery_samples_seen = 0;
    runtime->battery_read_failures = 0;
    runtime->gps_bytes_seen = 0;
    runtime->gps_parse_errors = 0;
    runtime->gps_speed_knots_milli = 0;
    runtime->gps_line_len = 0;
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
    runtime->bms_ble_phase = BMS_BLE_PHASE_IDLE;
    runtime->bms_write_in_flight = false;
    runtime->bms_device_info_requested = false;
    runtime->bms_device_info_known = false;
    runtime_copy_snapshot_text(runtime->bluetooth_name,
                               sizeof(runtime->bluetooth_name),
                               LOCAL_BLUETOOTH_NAME);

    runtime->snapshot.speed_unit = ESP_BMS_SPEED_UNIT_KMH;
    runtime->snapshot.setup_ap_enabled = false;
    runtime->snapshot.wifi = ESP_BMS_WIFI_OFFLINE;
    runtime->snapshot.ota = ESP_BMS_OTA_IDLE;
    runtime->bms_type = (uint8_t)ESP_BMS_IDF_BMS_TYPE_ANT;
    runtime->snapshot.bms_type = runtime->bms_type;
    (void)runtime_set_brightness_percent(runtime, 85U);
    (void)runtime_set_volume_percent(runtime, 65U);
    runtime->display_rotation = ESP_BMS_IDF_DISPLAY_ROTATION_LANDSCAPE;
    runtime->language_zh = true;
    runtime->bms_bind_active = false;
    runtime->station_retry_count = 0;
    runtime->station_connected = false;
    runtime->station_has_ip = false;
    runtime->station_connect_requested = false;
    runtime->http_bms_scan_pending = false;
    runtime->bms_scan_requested = false;
    runtime->bms_scan_active = false;
    runtime->wifi_scan_requested = false;
    runtime->wifi_scan_active = false;
    runtime->wifi_scan_complete = false;
    runtime->bluetooth_advertise_requested = false;
    runtime->bluetooth_advertising = false;
    runtime->bluetooth_connected = false;
    (void)runtime_project_bluetooth_snapshot(runtime);
    runtime_bms_scan_clear_candidates(runtime);
    runtime_wifi_scan_clear_candidates(runtime);
    runtime_clear_bms_telemetry(runtime);
    runtime_update_setup_ap_snapshot(runtime);
    runtime_set_bms_info(runtime, "BMS OFF");
    runtime_update_snapshot_speed(runtime);
}

static void runtime_init_battery_adc(esp_bms_idf_runtime_t *runtime)
{
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
    runtime->battery_adc_ready = true;
    ESP_LOGI(TAG, "battery ADC ready: gpio=%d unit=ADC1 channel=%d", BATTERY_GPIO, channel);
}

static void runtime_init_gps_uart(esp_bms_idf_runtime_t *runtime)
{
#if CONFIG_ESP_CONSOLE_UART
    if ((int)GPS_UART_PORT == CONFIG_ESP_CONSOLE_UART_NUM) {
        ESP_LOGW(TAG,
                 "GPS UART disabled: uart=%d shares ESP console at %d baud; use a non-console UART for GPS",
                 GPS_UART_PORT,
                 CONFIG_ESP_CONSOLE_UART_BAUDRATE);
        return;
    }
#endif

    uart_config_t config = {
        .baud_rate = GPS_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    runtime->gps_uart = GPS_UART_PORT;
    esp_err_t ret = uart_param_config(runtime->gps_uart, &config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "GPS UART config failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = uart_set_pin(runtime->gps_uart, GPS_UART_TX_GPIO, GPS_UART_RX_GPIO,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "GPS UART pin config failed: %s", esp_err_to_name(ret));
        return;
    }

    if (!uart_is_driver_installed(runtime->gps_uart)) {
        ret = uart_driver_install(runtime->gps_uart, GPS_UART_RX_BUFFER_SIZE, 0, 0, NULL, 0);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "GPS UART driver install failed: %s", esp_err_to_name(ret));
            return;
        }
    }

    runtime->gps_uart_ready = true;
    ESP_LOGI(TAG, "GPS UART ready: uart=%d rx=%d tx=%d baud=%d",
             runtime->gps_uart, GPS_UART_RX_GPIO, GPS_UART_TX_GPIO, GPS_UART_BAUD);
}

static esp_err_t runtime_init_nvs(esp_bms_idf_runtime_t *runtime)
{
    if (runtime && runtime->nvs_ready) {
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
        runtime->nvs_ready = true;
    }
    return ret;
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
    uint8_t speed_unit = 0;
    uint8_t language = 0;
    uint8_t bms_type = (uint8_t)ESP_BMS_IDF_BMS_TYPE_ANT;

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
    nvs_close(handle);
    if (ret != ESP_OK) {
        return ret;
    }

    if (!runtime_brightness_matches_policy(brightness_percent) ||
        !runtime_volume_matches_policy(volume_percent) ||
        !runtime_rotation_matches_policy(rotation) ||
        !runtime_speed_unit_matches_policy(speed_unit) ||
        !runtime_language_matches_policy(language) ||
        !runtime_bms_type_matches_policy(bms_type)) {
        return ESP_ERR_INVALID_STATE;
    }

    (void)runtime_set_brightness_percent(runtime, brightness_percent);
    (void)runtime_set_volume_percent(runtime, volume_percent);
    runtime->display_rotation = (esp_bms_idf_display_rotation_t)rotation;
    runtime->snapshot.speed_unit = (esp_bms_speed_unit_t)speed_unit;
    runtime->language_zh = language != 0U;
    runtime->bms_type = bms_type;
    runtime->snapshot.bms_type = runtime->bms_type;
    runtime_update_snapshot_speed(runtime);
    *loaded = true;
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
    ESP_RETURN_ON_FALSE(runtime_bms_type_matches_policy(runtime->bms_type),
                        ESP_ERR_INVALID_STATE, TAG, "invalid BMS type");

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
        ret = nvs_set_u8(handle, DISPLAY_NVS_SPEED_UNIT_KEY, (uint8_t)runtime->snapshot.speed_unit);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, DISPLAY_NVS_LANGUAGE_KEY, runtime->language_zh ? 1U : 0U);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(handle, DISPLAY_NVS_BMS_TYPE_KEY, runtime->bms_type);
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
    runtime->http_pending_language_zh = language_zh;
    runtime->http_pending_bms_type = (uint8_t)bms_type;
    runtime->http_config_pending = true;

    xSemaphoreGive(runtime->http_pending_lock);
    return true;
}

static bool runtime_apply_pending_http_config(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime->http_pending_lock) {
        return false;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, 0) != pdTRUE) {
        return false;
    }

    const bool pending = runtime->http_config_pending;
    const uint8_t brightness_percent = runtime->http_pending_brightness_percent;
    const uint8_t volume_percent = runtime->http_pending_volume_percent;
    const esp_bms_idf_display_rotation_t rotation = runtime->http_pending_display_rotation;
    const esp_bms_speed_unit_t speed_unit = runtime->http_pending_speed_unit;
    const bool language_zh = runtime->http_pending_language_zh;
    const uint8_t bms_type = runtime->http_pending_bms_type;
    runtime->http_config_pending = false;
    xSemaphoreGive(runtime->http_pending_lock);

    if (!pending) {
        return false;
    }

    const bool changed = runtime->brightness_percent != brightness_percent ||
                         runtime->volume_percent != volume_percent ||
                         runtime->display_rotation != rotation ||
                         runtime->snapshot.speed_unit != speed_unit ||
                         runtime->language_zh != language_zh ||
                         runtime->bms_type != bms_type;
    if (!changed) {
        return false;
    }

    (void)runtime_set_brightness_percent(runtime, brightness_percent);
    (void)runtime_set_volume_percent(runtime, volume_percent);
    runtime->display_rotation = rotation;
    runtime->snapshot.speed_unit = speed_unit;
    runtime->language_zh = language_zh;
    runtime->bms_type = bms_type;
    runtime->snapshot.bms_type = runtime->bms_type;
    runtime_update_snapshot_speed(runtime);
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
                                 "Content-Type, X-Setup-Password, Authorization");
    }
    if (ret == ESP_OK) {
        ret = httpd_resp_set_hdr(req, "Access-Control-Max-Age", "600");
    }
    if (ret == ESP_OK) {
        ret = httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    }
    return ret;
}

static size_t runtime_base64_encode(const uint8_t *input, size_t input_len, char *out, size_t out_len)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_index = 0;
    for (size_t index = 0; index < input_len; index += 3U) {
        const size_t remaining = input_len - index;
        const uint32_t triple = ((uint32_t)input[index] << 16) |
                                ((remaining > 1U ? (uint32_t)input[index + 1] : 0U) << 8) |
                                (remaining > 2U ? (uint32_t)input[index + 2] : 0U);
        if (out_index + 4U >= out_len) {
            return 0;
        }
        out[out_index++] = alphabet[(triple >> 18) & 0x3FU];
        out[out_index++] = alphabet[(triple >> 12) & 0x3FU];
        out[out_index++] = remaining > 1U ? alphabet[(triple >> 6) & 0x3FU] : '=';
        out[out_index++] = remaining > 2U ? alphabet[triple & 0x3FU] : '=';
    }
    if (out_index >= out_len) {
        return 0;
    }
    out[out_index] = '\0';
    return out_index;
}

static void runtime_copy_auth_passwords(const esp_bms_idf_runtime_t *runtime,
                                        char *current,
                                        size_t current_len,
                                        bool *has_pending,
                                        char *pending,
                                        size_t pending_len)
{
    if (current_len > 0) {
        current[0] = '\0';
    }
    *has_pending = false;
    if (pending_len > 0) {
        pending[0] = '\0';
    }

    if (!runtime->http_pending_lock) {
        runtime_copy_snapshot_text(current, current_len, runtime->setup_ap_password);
        return;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    runtime_copy_snapshot_text(current, current_len, runtime->setup_ap_password);
    if (runtime->http_setup_ap_password_pending) {
        runtime_copy_snapshot_text(pending, pending_len, runtime->http_pending_setup_ap_password);
        *has_pending = true;
    }
    xSemaphoreGive(runtime->http_pending_lock);
}

static bool runtime_http_basic_password_matches(const char *header, const char *password)
{
    if (!password || password[0] == '\0') {
        return false;
    }

    char credential[sizeof(HTTP_SETUP_USER) + SETUP_AP_PASSWORD_LEN + 1U] = { 0 };
    const int credential_len = snprintf(credential,
                                        sizeof(credential),
                                        HTTP_SETUP_USER ":%s",
                                        password);
    if (credential_len < 0 || (size_t)credential_len >= sizeof(credential)) {
        return false;
    }

    char encoded[32] = { 0 };
    if (runtime_base64_encode((const uint8_t *)credential,
                              (size_t)credential_len,
                              encoded,
                              sizeof(encoded)) == 0) {
        return false;
    }

    char expected[HTTP_AUTH_MAX_LEN] = { 0 };
    const int expected_len = snprintf(expected, sizeof(expected), "Basic %s", encoded);
    return expected_len > 0 && (size_t)expected_len < sizeof(expected) &&
           strcmp(header, expected) == 0;
}

static bool runtime_http_auth_ok(httpd_req_t *req, const esp_bms_idf_runtime_t *runtime)
{
    char current_password[sizeof(runtime->setup_ap_password)] = { 0 };
    char pending_password[sizeof(runtime->setup_ap_password)] = { 0 };
    bool has_pending_password = false;
    runtime_copy_auth_passwords(runtime,
                                current_password,
                                sizeof(current_password),
                                &has_pending_password,
                                pending_password,
                                sizeof(pending_password));
    if (current_password[0] == '\0' && !has_pending_password) {
        return false;
    }

    char header[HTTP_AUTH_MAX_LEN] = { 0 };
    if (httpd_req_get_hdr_value_str(req, "X-Setup-Password", header, sizeof(header)) == ESP_OK &&
        (strcmp(header, current_password) == 0 ||
         (has_pending_password && strcmp(header, pending_password) == 0))) {
        return true;
    }

    memset(header, 0, sizeof(header));
    if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) != ESP_OK) {
        return false;
    }

    return runtime_http_basic_password_matches(header, current_password) ||
           (has_pending_password && runtime_http_basic_password_matches(header, pending_password));
}

static esp_err_t runtime_http_send_text(httpd_req_t *req, const char *status, const char *text)
{
    ESP_RETURN_ON_ERROR(runtime_http_set_common_headers(req), TAG, "set HTTP headers failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_status(req, status), TAG, "set HTTP status failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "text/plain; charset=utf-8"), TAG, "set HTTP type failed");
    return httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t runtime_http_send_unauthorized(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(runtime_http_set_common_headers(req), TAG, "set HTTP headers failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(req,
                                           "WWW-Authenticate",
                                           "Basic realm=\"esp32-bms-gps\", charset=\"UTF-8\""),
                        TAG,
                        "set auth header failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_status(req, "401 Unauthorized"), TAG, "set HTTP status failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "text/plain; charset=utf-8"), TAG, "set HTTP type failed");
    return httpd_resp_send(req, "unauthorized", HTTPD_RESP_USE_STRLEN);
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

static esp_err_t runtime_http_index_handler(httpd_req_t *req)
{
    const size_t html_size = (size_t)(web_index_html_end - web_index_html_start);
    const size_t html_len = html_size > 0U && web_index_html_start[html_size - 1U] == '\0'
                                ? html_size - 1U
                                : html_size;
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "text/html; charset=utf-8"), TAG, "set HTTP type failed");
    return httpd_resp_send(req, web_index_html_start, (ssize_t)html_len);
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
        if (snapshot->bms_temperature_valid[index]) {
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

static esp_err_t runtime_http_status_handler(httpd_req_t *req, esp_bms_idf_runtime_t *runtime)
{
    char pack_voltage[16] = { 0 };
    char current[16] = { 0 };
    char soc[16] = { 0 };
    char local_battery[16] = { 0 };
    char protections[96] = { 0 };
    char warnings[96] = { 0 };
    char temperatures[80] = { 0 };
    if (!runtime_json_write_u32_or_null(pack_voltage,
                                        sizeof(pack_voltage),
                                        runtime->snapshot.pack_voltage_valid,
                                        runtime->snapshot.pack_voltage_mv) ||
        !runtime_json_write_i32_or_null(current,
                                        sizeof(current),
                                        runtime->snapshot.current_valid,
                                        runtime->snapshot.current_deci_amps) ||
        !runtime_json_write_u32_or_null(soc,
                                        sizeof(soc),
                                        runtime->snapshot.soc_valid,
                                        runtime->snapshot.soc_percent) ||
        !runtime_json_write_u32_or_null(local_battery,
                                        sizeof(local_battery),
                                        runtime->snapshot.local_battery_valid,
                                        runtime->snapshot.local_battery_mv) ||
        !runtime_json_write_bms_codes(protections,
                                      sizeof(protections),
                                      runtime->snapshot.bms_protection_codes,
                                      runtime->snapshot.bms_protection_count) ||
        !runtime_json_write_bms_codes(warnings,
                                      sizeof(warnings),
                                      runtime->snapshot.bms_warning_codes,
                                      runtime->snapshot.bms_warning_count) ||
        !runtime_json_write_bms_temperatures(temperatures, sizeof(temperatures), &runtime->snapshot)) {
        return runtime_http_send_text(req, "500 Internal Server Error", "json format error");
    }

    char speed[16] = "--";
    if (runtime->snapshot.speed_valid) {
        const unsigned value = runtime->snapshot.speed_deci_units;
        (void)snprintf(speed, sizeof(speed), "%u.%u", value / 10U, value % 10U);
    }

    char json[HTTP_JSON_MAX_LEN] = { 0 };
    const int written = snprintf(json,
                                 sizeof(json),
                                 "{\"version\":\"0.1.0\",\"speed\":\"%s\",\"speed_unit\":\"%s\","
                                 "\"gps_fix\":%s,\"bms\":\"%s\",\"pack_voltage_mv\":%s,"
                                 "\"current_deci_amps\":%s,\"soc_percent\":%s,"
                                 "\"local_battery_mv\":%s,\"bms_info\":\"%s\","
                                 "\"bms_protections\":%s,\"bms_warnings\":%s,"
                                 "\"bms_temperatures_c\":%s,\"wifi\":\"%s\","
                                 "\"setup_ap_enabled\":%s,\"ota\":\"%s\"}",
                                 speed,
                                 runtime_speed_unit_config_text(runtime->snapshot.speed_unit),
                                 runtime->snapshot.gps_fix_valid ? "true" : "false",
                                 runtime->snapshot.bms_online ? "online" : "offline",
                                 pack_voltage,
                                 current,
                                 soc,
                                 local_battery,
                                 runtime->snapshot.bms_info_text[0] != '\0' ? runtime->snapshot.bms_info_text : "BMS OFF",
                                 protections,
                                 warnings,
                                 temperatures,
                                 runtime_wifi_config_text(runtime->snapshot.wifi),
                                 runtime->snapshot.setup_ap_enabled ? "true" : "false",
                                 runtime_ota_config_text(runtime->snapshot.ota));
    if (written < 0 || (size_t)written >= sizeof(json)) {
        return runtime_http_send_text(req, "500 Internal Server Error", "json too large");
    }
    return runtime_http_send_json(req, json);
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

    char json[HTTP_JSON_MAX_LEN] = { 0 };
    size_t offset = 0;
    int written = snprintf(json, sizeof(json), "{\"candidates\":[");
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
            written = snprintf(json + offset,
                               sizeof(json) - offset,
                               ",\"name\":\"%s\"",
                               candidate->name);
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
        if (runtime->http_bms_bind_pending) {
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

static void runtime_config_external_ssid_copy(const esp_bms_idf_runtime_t *runtime,
                                              char *out,
                                              size_t out_len)
{
    if (runtime->http_pending_lock &&
        xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        runtime_copy_snapshot_text(out, out_len, runtime->external_ssid);
        if (runtime->http_external_wifi_pending) {
            runtime_copy_snapshot_text(out, out_len, runtime->http_pending_external_ssid);
        }
        xSemaphoreGive(runtime->http_pending_lock);
        return;
    }

    runtime_copy_snapshot_text(out, out_len, runtime->external_ssid);
}

static esp_err_t runtime_http_config_handler(httpd_req_t *req, esp_bms_idf_runtime_t *runtime)
{
    char bms_mac[24] = { 0 };
    runtime_config_bms_mac_json(runtime, bms_mac, sizeof(bms_mac));
    char external_ssid[sizeof(runtime->external_ssid)] = { 0 };
    runtime_config_external_ssid_copy(runtime, external_ssid, sizeof(external_ssid));

    char json[HTTP_JSON_MAX_LEN] = { 0 };
    const int written = snprintf(json,
                                 sizeof(json),
                                 "{\"brightness\":%u,\"volume\":%u,\"display_rotation\":\"%s\","
                                 "\"speed_unit\":\"%s\",\"language\":\"%s\","
                                 "\"setup_ap_ssid\":\"%s\",\"external_wifi_saved\":%s,"
                                 "\"external_ssid\":\"%s\",\"setup_ap_password_saved\":%s,"
                                 "\"setup_ap_state\":\"%s\",\"bms_mac\":%s,\"bms_type\":\"%s\"}",
                                 runtime->brightness_percent,
                                 runtime->volume_percent,
                                 runtime_rotation_config_text(runtime->display_rotation),
                                 runtime_speed_unit_config_text(runtime->snapshot.speed_unit),
                                 runtime->language_zh ? "zh" : "en",
                                 runtime->setup_ap_ssid,
                                 external_ssid[0] == '\0' ? "false" : "true",
                                 external_ssid,
                                 runtime->setup_ap_password[0] == '\0' ? "false" : "true",
                                 runtime->snapshot.setup_ap_enabled ? "enabled" : "disabled",
                                 bms_mac,
                                 runtime_bms_type_config_text((esp_bms_idf_bms_type_t)runtime->bms_type));
    if (written < 0 || (size_t)written >= sizeof(json)) {
        return runtime_http_send_text(req, "500 Internal Server Error", "json too large");
    }
    return runtime_http_send_json(req, json);
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
    runtime->http_setup_ap_password_pending = true;
    xSemaphoreGive(runtime->http_pending_lock);
    return true;
}

static bool runtime_set_pending_http_external_wifi(esp_bms_idf_runtime_t *runtime,
                                                  const char *ssid,
                                                  const char *password)
{
    if (!runtime->http_pending_lock) {
        return false;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    runtime_copy_snapshot_text(runtime->http_pending_external_ssid,
                               sizeof(runtime->http_pending_external_ssid),
                               ssid);
    runtime_copy_snapshot_text(runtime->http_pending_external_password,
                               sizeof(runtime->http_pending_external_password),
                               password);
    runtime->http_external_wifi_pending = true;
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
    runtime->http_bms_bind_pending = true;
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
    runtime->http_bms_scan_pending = true;
    xSemaphoreGive(runtime->http_pending_lock);
    return true;
}

static esp_err_t runtime_http_post_config_handler(httpd_req_t *req, esp_bms_idf_runtime_t *runtime)
{
    char body[HTTP_BODY_MAX_LEN] = { 0 };
    esp_err_t ret = runtime_http_read_body(req, body, sizeof(body));
    if (ret != ESP_OK) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid body");
    }

    uint8_t brightness_percent = runtime->brightness_percent;
    uint8_t volume_percent = runtime->volume_percent;
    esp_bms_idf_display_rotation_t rotation = runtime->display_rotation;
    esp_bms_speed_unit_t speed_unit = runtime->snapshot.speed_unit;
    bool language_zh = runtime->language_zh;
    esp_bms_idf_bms_type_t bms_type = (esp_bms_idf_bms_type_t)runtime->bms_type;

    bool found = false;
    uint8_t parsed_u8 = 0;
    if (!runtime_json_get_u8(body, "brightness", &parsed_u8, &found)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid brightness");
    }
    if (found) {
        if (!runtime_brightness_matches_policy(parsed_u8)) {
            return runtime_http_send_text(req, "400 Bad Request", "invalid brightness");
        }
        brightness_percent = parsed_u8;
    }

    if (!runtime_json_get_u8(body, "volume", &parsed_u8, &found)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid volume");
    }
    if (found) {
        if (!runtime_volume_matches_policy(parsed_u8)) {
            return runtime_http_send_text(req, "400 Bad Request", "invalid volume");
        }
        volume_percent = parsed_u8;
    }

    char parsed_text[32] = { 0 };
    if (!runtime_json_get_string(body, "display_rotation", parsed_text, sizeof(parsed_text), &found)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid rotation");
    }
    if (found && !runtime_parse_rotation_config_text(parsed_text, &rotation)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid rotation");
    }

    memset(parsed_text, 0, sizeof(parsed_text));
    if (!runtime_json_get_string(body, "speed_unit", parsed_text, sizeof(parsed_text), &found)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid speed unit");
    }
    if (found && !runtime_parse_speed_unit_config_text(parsed_text, &speed_unit)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid speed unit");
    }

    memset(parsed_text, 0, sizeof(parsed_text));
    if (!runtime_json_get_string(body, "language", parsed_text, sizeof(parsed_text), &found)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid language");
    }
    if (found) {
        if (strcmp(parsed_text, "zh") == 0) {
            language_zh = true;
        } else if (strcmp(parsed_text, "en") == 0) {
            language_zh = false;
        } else {
            return runtime_http_send_text(req, "400 Bad Request", "invalid language");
        }
    }

    memset(parsed_text, 0, sizeof(parsed_text));
    if (!runtime_json_get_string(body, "bms_type", parsed_text, sizeof(parsed_text), &found)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid BMS type");
    }
    if (found && !runtime_parse_bms_type_config_text(parsed_text, &bms_type)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid BMS type");
    }

    if (!runtime_set_pending_http_config(runtime, brightness_percent, volume_percent,
                                         rotation, speed_unit, language_zh, bms_type)) {
        return runtime_http_send_text(req, "500 Internal Server Error", "config queue failed");
    }
    return runtime_http_send_no_content(req);
}

static esp_err_t runtime_http_post_wifi_handler(httpd_req_t *req, esp_bms_idf_runtime_t *runtime)
{
    char body[HTTP_BODY_MAX_LEN] = { 0 };
    esp_err_t ret = runtime_http_read_body(req, body, sizeof(body));
    if (ret != ESP_OK) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid body");
    }

    char parsed_ssid[sizeof(runtime->external_ssid)] = { 0 };
    char parsed_password[sizeof(runtime->external_password)] = { 0 };
    bool found = false;
    if (!runtime_json_get_string(body, "ssid", parsed_ssid, sizeof(parsed_ssid), &found) ||
        !found) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid ssid");
    }

    bool password_found = false;
    if (!runtime_json_get_string(body,
                                 "password",
                                 parsed_password,
                                 sizeof(parsed_password),
                                 &password_found)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid password");
    }
    if (!password_found) {
        parsed_password[0] = '\0';
    }

    if (!runtime_external_wifi_credentials_match_policy(parsed_ssid, parsed_password)) {
        return runtime_http_send_text(req, "400 Bad Request", "invalid wifi credentials");
    }

    if (!runtime_set_pending_http_external_wifi(runtime, parsed_ssid, parsed_password)) {
        return runtime_http_send_text(req, "500 Internal Server Error", "wifi queue failed");
    }
    return runtime_http_send_no_content(req);
}

static esp_err_t runtime_http_post_ap_password_handler(httpd_req_t *req, esp_bms_idf_runtime_t *runtime)
{
    char body[HTTP_BODY_MAX_LEN] = { 0 };
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
    char body[HTTP_BODY_MAX_LEN] = { 0 };
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
        char body[HTTP_BODY_MAX_LEN] = { 0 };
        if (runtime_http_read_body(req, body, sizeof(body)) != ESP_OK) {
            return runtime_http_send_text(req, "400 Bad Request", "invalid body");
        }
    }

    if (!runtime_set_pending_http_bms_scan(runtime)) {
        return runtime_http_send_text(req, "500 Internal Server Error", "bms scan queue failed");
    }
    return runtime_http_send_no_content(req);
}

static esp_err_t runtime_http_post_ota_unavailable_handler(httpd_req_t *req,
                                                           esp_bms_idf_runtime_t *runtime,
                                                           const char *operation)
{
    runtime->snapshot.ota = ESP_BMS_OTA_FAILED;
    runtime_set_error(runtime, "OTA N/A");
    ESP_LOGI(TAG, "[ota] %s requested but OTA transport is not implemented", operation);

    char json[160] = { 0 };
    const int written = snprintf(json,
                                 sizeof(json),
                                 "{\"ota\":\"failed\",\"available\":false,"
                                 "\"message\":\"ota transport not implemented\"}");
    if (written < 0 || (size_t)written >= sizeof(json)) {
        return runtime_http_send_text(req, "500 Internal Server Error", "json too large");
    }
    return runtime_http_send_json(req, json);
}

static esp_err_t runtime_http_api_handler(httpd_req_t *req)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)req->user_ctx;
    if (!runtime) {
        return runtime_http_send_text(req, "500 Internal Server Error", "runtime missing");
    }

    if (req->method == HTTP_OPTIONS) {
        return runtime_http_send_no_content(req);
    }
    if (!runtime_http_auth_ok(req, runtime)) {
        return runtime_http_send_unauthorized(req);
    }

    if (req->method == HTTP_GET && strcmp(req->uri, "/api/status") == 0) {
        return runtime_http_status_handler(req, runtime);
    }
    if (req->method == HTTP_GET && strcmp(req->uri, "/api/config") == 0) {
        return runtime_http_config_handler(req, runtime);
    }
    if (req->method == HTTP_GET && strcmp(req->uri, "/api/bms/candidates") == 0) {
        return runtime_http_bms_candidates_handler(req, runtime);
    }
    if (req->method == HTTP_POST && strcmp(req->uri, "/api/config") == 0) {
        return runtime_http_post_config_handler(req, runtime);
    }
    if (req->method == HTTP_POST && strcmp(req->uri, "/api/wifi") == 0) {
        return runtime_http_post_wifi_handler(req, runtime);
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
    if (req->method == HTTP_POST && strcmp(req->uri, "/api/ota/check") == 0) {
        return runtime_http_post_ota_unavailable_handler(req, runtime, "check");
    }
    if (req->method == HTTP_POST && strcmp(req->uri, "/api/ota/start") == 0) {
        return runtime_http_post_ota_unavailable_handler(req, runtime, "start");
    }

    ESP_LOGI(TAG, "[http] route not implemented: method=%d uri=%s", req->method, req->uri);
    return runtime_http_send_text(req, "501 Not Implemented", "not implemented");
}

static esp_err_t runtime_start_http_server(esp_bms_idf_runtime_t *runtime)
{
    if (runtime->http_server_started) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 4;
    config.max_uri_handlers = 4;
    config.stack_size = 4096;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t ret = httpd_start(&runtime->http_server, &config);
    if (ret != ESP_OK) {
        return ret;
    }

    const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = runtime_http_index_handler,
        .user_ctx = runtime,
    };
    const httpd_uri_t api = {
        .uri = "/api/*",
        .method = HTTP_ANY,
        .handler = runtime_http_api_handler,
        .user_ctx = runtime,
    };

    ret = httpd_register_uri_handler(runtime->http_server, &root);
    if (ret == ESP_OK) {
        ret = httpd_register_uri_handler(runtime->http_server, &api);
    }
    if (ret != ESP_OK) {
        httpd_stop(runtime->http_server);
        runtime->http_server = NULL;
        return ret;
    }

    runtime->http_server_started = true;
    ESP_LOGI(TAG, "[http] server started: port=80 routes=/,/api/*");
    return ESP_OK;
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

static esp_err_t runtime_load_external_wifi_credentials(esp_bms_idf_runtime_t *runtime)
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

    char ssid[sizeof(runtime->external_ssid)] = { 0 };
    char password[sizeof(runtime->external_password)] = { 0 };
    size_t ssid_len = sizeof(ssid);
    size_t password_len = sizeof(password);

    ret = nvs_get_str(handle, EXTERNAL_WIFI_NVS_SSID_KEY, ssid, &ssid_len);
    if (ret == ESP_OK) {
        ret = nvs_get_str(handle, EXTERNAL_WIFI_NVS_PASSWORD_KEY, password, &password_len);
    }
    nvs_close(handle);
    if (ret != ESP_OK) {
        return ret;
    }

    if (!runtime_external_wifi_credentials_match_policy(ssid, password)) {
        runtime->external_ssid[0] = '\0';
        runtime->external_password[0] = '\0';
        return ESP_ERR_INVALID_STATE;
    }

    runtime_copy_snapshot_text(runtime->external_ssid, sizeof(runtime->external_ssid), ssid);
    runtime_copy_snapshot_text(runtime->external_password, sizeof(runtime->external_password), password);
    return ESP_OK;
}

static esp_err_t runtime_save_external_wifi_credentials(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime_external_wifi_credentials_match_policy(runtime->external_ssid,
                                                        runtime->external_password)) {
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

    ret = nvs_set_str(handle, EXTERNAL_WIFI_NVS_SSID_KEY, runtime->external_ssid);
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, EXTERNAL_WIFI_NVS_PASSWORD_KEY, runtime->external_password);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static esp_err_t runtime_load_bms_binding(esp_bms_idf_runtime_t *runtime)
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
    size_t mac_len = sizeof(mac);
    ret = nvs_get_str(handle, BMS_NVS_BOUND_MAC_KEY, mac, &mac_len);
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
    const bool pending = runtime->http_setup_ap_password_pending;
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
    if (runtime->http_setup_ap_password_pending &&
        strcmp(runtime->http_pending_setup_ap_password, password) == 0) {
        runtime->http_pending_setup_ap_password[0] = '\0';
        runtime->http_setup_ap_password_pending = false;
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
        return false;
    }
    runtime_copy_snapshot_text(runtime->setup_ap_password, sizeof(runtime->setup_ap_password), password);
    xSemaphoreGive(runtime->http_pending_lock);
    runtime_update_setup_ap_snapshot(runtime);

    esp_err_t ret = ESP_OK;
    if (runtime->setup_ap_started) {
        ret = runtime_apply_setup_ap_wifi_config(runtime);
    }
    if (ret == ESP_OK) {
        ret = runtime_save_setup_ap_credentials(runtime);
    }
    if (ret == ESP_OK) {
        runtime_clear_pending_http_ap_password(runtime, password);
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
    if (runtime->setup_ap_started) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(runtime_apply_setup_ap_wifi_config(runtime));
    }
    runtime_set_error(runtime, "AP PW FAIL");
    ESP_LOGW(TAG, "[wifi] setup AP password update failed: %s", esp_err_to_name(ret));
    return true;
}

static bool runtime_get_pending_http_external_wifi(esp_bms_idf_runtime_t *runtime,
                                                   char *ssid,
                                                   size_t ssid_len,
                                                   char *password,
                                                   size_t password_len)
{
    if (!runtime->http_pending_lock) {
        return false;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, 0) != pdTRUE) {
        return false;
    }
    const bool pending = runtime->http_external_wifi_pending;
    if (pending) {
        runtime_copy_snapshot_text(ssid, ssid_len, runtime->http_pending_external_ssid);
        runtime_copy_snapshot_text(password, password_len, runtime->http_pending_external_password);
    }
    xSemaphoreGive(runtime->http_pending_lock);
    return pending;
}

static void runtime_clear_pending_http_external_wifi(esp_bms_idf_runtime_t *runtime,
                                                    const char *ssid)
{
    if (!runtime->http_pending_lock) {
        return;
    }
    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    if (runtime->http_external_wifi_pending &&
        strcmp(runtime->http_pending_external_ssid, ssid) == 0) {
        runtime->http_pending_external_ssid[0] = '\0';
        runtime->http_pending_external_password[0] = '\0';
        runtime->http_external_wifi_pending = false;
    }
    xSemaphoreGive(runtime->http_pending_lock);
}

static bool runtime_apply_pending_http_external_wifi(esp_bms_idf_runtime_t *runtime)
{
    char ssid[sizeof(runtime->external_ssid)] = { 0 };
    char password[sizeof(runtime->external_password)] = { 0 };
    if (!runtime_get_pending_http_external_wifi(runtime,
                                                ssid,
                                                sizeof(ssid),
                                                password,
                                                sizeof(password))) {
        return false;
    }
    if (!runtime_external_wifi_credentials_match_policy(ssid, password)) {
        runtime_clear_pending_http_external_wifi(runtime, ssid);
        runtime_set_error(runtime, "WIFI BAD");
        return true;
    }

    char previous_ssid[sizeof(runtime->external_ssid)] = { 0 };
    char previous_password[sizeof(runtime->external_password)] = { 0 };
    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    runtime_copy_snapshot_text(previous_ssid, sizeof(previous_ssid), runtime->external_ssid);
    runtime_copy_snapshot_text(previous_password, sizeof(previous_password), runtime->external_password);
    runtime_copy_snapshot_text(runtime->external_ssid, sizeof(runtime->external_ssid), ssid);
    runtime_copy_snapshot_text(runtime->external_password, sizeof(runtime->external_password), password);
    xSemaphoreGive(runtime->http_pending_lock);

    esp_err_t ret = runtime_save_external_wifi_credentials(runtime);
    if (ret == ESP_OK) {
        ret = runtime_start_station_connect(runtime);
    }

    if (ret == ESP_OK) {
        runtime_clear_pending_http_external_wifi(runtime, ssid);
        runtime->snapshot.wifi = ESP_BMS_WIFI_CONNECTING;
        runtime_set_error(runtime, "WIFI SET");
        ESP_LOGI(TAG, "[wifi] external credentials saved: ssid='%s' sta_pw_len=%u",
                 runtime->external_ssid, (unsigned)strlen(runtime->external_password));
        return true;
    }

    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        runtime_copy_snapshot_text(runtime->external_ssid,
                                   sizeof(runtime->external_ssid),
                                   previous_ssid);
        runtime_copy_snapshot_text(runtime->external_password,
                                   sizeof(runtime->external_password),
                                   previous_password);
        xSemaphoreGive(runtime->http_pending_lock);
    }
    runtime_set_error(runtime, "WIFI SAVE");
    ESP_LOGW(TAG, "[wifi] external credential apply failed: %s", esp_err_to_name(ret));
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
    const bool pending = runtime->http_bms_scan_pending;
    runtime->http_bms_scan_pending = false;
    xSemaphoreGive(runtime->http_pending_lock);
    return pending;
}

static bool runtime_apply_pending_http_bms_scan(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime_get_pending_http_bms_scan(runtime)) {
        return false;
    }

    runtime->bms_bind_active = true;
    runtime_clear_bms_telemetry(runtime);
    runtime_bms_scan_clear_candidates(runtime);

    const esp_err_t ret = esp_bms_idf_runtime_start_bms_ble_for_bind(runtime);
    if (ret == ESP_OK) {
        return true;
    }

    runtime_set_bms_info(runtime, "BLE FAIL");
    ESP_LOGW(TAG, "[bms] BLE bind scan start failed: %s", esp_err_to_name(ret));
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
    const bool pending = runtime->http_bms_bind_pending;
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
    if (runtime->http_bms_bind_pending &&
        strcmp(runtime->http_pending_bms_bound_mac, mac) == 0) {
        runtime->http_pending_bms_bound_mac[0] = '\0';
        runtime->http_bms_bind_pending = false;
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
    if (xSemaphoreTake(runtime->http_pending_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    runtime_copy_snapshot_text(previous_mac, sizeof(previous_mac), runtime->bms_bound_mac);
    if (strcmp(mac, runtime->bms_bound_mac) != 0) {
        runtime_copy_snapshot_text(runtime->bms_bound_mac, sizeof(runtime->bms_bound_mac), mac);
    }
    xSemaphoreGive(runtime->http_pending_lock);

    esp_err_t ret = runtime_save_bms_binding(runtime);
    if (ret == ESP_OK) {
        runtime_clear_pending_http_bms_bind(runtime, mac);
        runtime->bms_bind_active = true;
        runtime_clear_bms_telemetry(runtime);
        ESP_LOGI(TAG, "[bms] bound MAC saved: mac=%s", mac);
        const esp_err_t scan_ret = esp_bms_idf_runtime_start_bms_ble_for_bind(runtime);
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
        xSemaphoreGive(runtime->http_pending_lock);
    }
    runtime_set_bms_info(runtime, "BMS SAVE");
    ESP_LOGW(TAG, "[bms] bound MAC save failed: %s", esp_err_to_name(ret));
    return true;
}

static bool runtime_apply_pending_wifi_scan(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || !runtime->wifi_scan_requested) {
        return false;
    }
    runtime->wifi_scan_requested = false;

    const esp_err_t ret = esp_bms_idf_runtime_start_wifi_scan(runtime);
    if (ret == ESP_OK) {
        return true;
    }

    runtime_wifi_scan_set_state(runtime, false, true);
    runtime_set_error(runtime, "WIFI SCAN");
    ESP_LOGW(TAG, "[wifi] scan start failed: %s", esp_err_to_name(ret));
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

static esp_err_t runtime_configure_setup_ap_ip(esp_netif_t *netif)
{
    esp_err_t ret = esp_netif_dhcps_stop(netif);
    if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        return ret;
    }

    esp_netif_ip_info_t ip_info = { 0 };
    esp_netif_set_ip4_addr(&ip_info.ip, 192, 168, 4, 1);
    esp_netif_set_ip4_addr(&ip_info.gw, 192, 168, 4, 1);
    esp_netif_set_ip4_addr(&ip_info.netmask, 255, 255, 255, 0);
    return esp_netif_set_ip_info(netif, &ip_info);
}

static void runtime_bms_ble_reset_connection_state(esp_bms_idf_runtime_t *runtime,
                                                   bms_ble_phase_t phase)
{
    runtime->bms_ble_phase = (uint8_t)phase;
    runtime->bms_conn_handle = 0xFFFFU;
    runtime->bms_service_start_handle = 0;
    runtime->bms_service_end_handle = 0;
    runtime->bms_char_val_handle = 0;
    runtime->bms_cccd_handle = 0;
    runtime->bms_frame_len = 0;
    runtime->bms_status_poll_elapsed_ms = 0;
    runtime->bms_write_in_flight = false;
    runtime->bms_device_info_requested = false;
    runtime->bms_device_info_known = false;
}

static int runtime_bms_ble_write_cb(uint16_t conn_handle,
                                    const struct ble_gatt_error *error,
                                    struct ble_gatt_attr *attr,
                                    void *arg)
{
    (void)attr;
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || conn_handle != runtime->bms_conn_handle) {
        return 0;
    }

    runtime->bms_write_in_flight = false;
    if (error && error->status != 0) {
        runtime_set_bms_info(runtime, "BMS WR");
        ESP_LOGW(TAG, "[bms] GATT write failed: conn=%u status=%u",
                 conn_handle, (unsigned)error->status);
        return 0;
    }

    if (runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_SUBSCRIBING) {
        runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_ONLINE;
        runtime->bms_status_poll_elapsed_ms = BMS_STATUS_POLL_PERIOD_MS;
        runtime_set_bms_info(runtime, "BMS ON");
        ESP_LOGI(TAG, "[bms] notifications subscribed: conn=%u", conn_handle);
    }
    return 0;
}

static esp_err_t runtime_bms_ble_write_frame(esp_bms_idf_runtime_t *runtime,
                                             const uint8_t *frame,
                                             size_t frame_len)
{
    if (!runtime || !frame || frame_len == 0U ||
        runtime->bms_conn_handle == 0xFFFFU ||
        runtime->bms_char_val_handle == 0U ||
        runtime->bms_write_in_flight) {
        return ESP_ERR_INVALID_STATE;
    }

    const int rc = ble_gattc_write_flat(runtime->bms_conn_handle,
                                        runtime->bms_char_val_handle,
                                        frame,
                                        (uint16_t)frame_len,
                                        runtime_bms_ble_write_cb,
                                        runtime);
    if (rc != 0) {
        return ESP_FAIL;
    }

    runtime->bms_write_in_flight = true;
    return ESP_OK;
}

static esp_err_t runtime_bms_ble_send_poll_request(esp_bms_idf_runtime_t *runtime)
{
    static const uint8_t status_request[] = {
        0x7E, 0xA1, 0x01, 0x00, 0x00, 0xBE, 0x18, 0x55, 0xAA, 0x55,
    };
    static const uint8_t device_info_request[] = {
        0x7E, 0xA1, 0x02, 0x6C, 0x02, 0x20, 0x58, 0xC4, 0xAA, 0x55,
    };

    if (!runtime || runtime->bms_ble_phase != (uint8_t)BMS_BLE_PHASE_ONLINE) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t *frame = status_request;
    size_t frame_len = sizeof(status_request);
    const bool send_device_info = !runtime->bms_device_info_requested;
    if (send_device_info) {
        frame = device_info_request;
        frame_len = sizeof(device_info_request);
    }

    const esp_err_t ret = runtime_bms_ble_write_frame(runtime, frame, frame_len);
    if (ret == ESP_OK) {
        runtime->bms_status_poll_elapsed_ms = 0;
        if (send_device_info) {
            runtime->bms_device_info_requested = true;
        }
    }
    return ret;
}

static esp_err_t runtime_bms_ble_subscribe(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || runtime->bms_conn_handle == 0xFFFFU || runtime->bms_cccd_handle == 0U) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t value[2] = { 1, 0 };
    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_SUBSCRIBING;
    runtime->bms_write_in_flight = true;
    const int rc = ble_gattc_write_flat(runtime->bms_conn_handle,
                                        runtime->bms_cccd_handle,
                                        value,
                                        sizeof(value),
                                        runtime_bms_ble_write_cb,
                                        runtime);
    if (rc != 0) {
        runtime->bms_write_in_flight = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

static int runtime_bms_ble_dsc_cb(uint16_t conn_handle,
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
        if (runtime->bms_cccd_handle == 0U) {
            runtime_set_bms_info(runtime, "BMS NO CCCD");
            ESP_LOGW(TAG, "[bms] characteristic lacks CCCD: conn=%u val_handle=%u",
                     conn_handle, runtime->bms_char_val_handle);
            (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        if (runtime_bms_ble_subscribe(runtime) != ESP_OK) {
            runtime_set_bms_info(runtime, "BMS SUB");
            ESP_LOGW(TAG, "[bms] subscribe request failed: conn=%u cccd=%u",
                     conn_handle, runtime->bms_cccd_handle);
            (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;
    }

    runtime_set_bms_info(runtime, "BMS DSC");
    ESP_LOGW(TAG, "[bms] descriptor discovery failed: conn=%u status=%u",
             conn_handle, error ? (unsigned)error->status : 0U);
    (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

static esp_err_t runtime_bms_ble_start_descriptor_discovery(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || runtime->bms_conn_handle == 0xFFFFU ||
        runtime->bms_char_val_handle == 0U ||
        runtime->bms_char_val_handle >= runtime->bms_service_end_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_DISCOVERING_CCCD;
    runtime->bms_cccd_handle = 0;
    const int rc = ble_gattc_disc_all_dscs(runtime->bms_conn_handle,
                                           runtime->bms_char_val_handle + 1U,
                                           runtime->bms_service_end_handle,
                                           runtime_bms_ble_dsc_cb,
                                           runtime);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

static int runtime_bms_ble_chr_cb(uint16_t conn_handle,
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
        if ((chr->properties & BLE_GATT_CHR_PROP_NOTIFY) == 0U) {
            ESP_LOGW(TAG, "[bms] characteristic has no notify property: props=0x%02x",
                     chr->properties);
        }
        if ((chr->properties & (BLE_GATT_CHR_PROP_WRITE | BLE_GATT_CHR_PROP_WRITE_NO_RSP)) == 0U) {
            ESP_LOGW(TAG, "[bms] characteristic has no write property: props=0x%02x",
                     chr->properties);
        }
        return 0;
    }

    if (error && error->status == BLE_HS_EDONE) {
        if (runtime->bms_char_val_handle == 0U ||
            runtime_bms_ble_start_descriptor_discovery(runtime) != ESP_OK) {
            runtime_set_bms_info(runtime, "BMS NO CHR");
            ESP_LOGW(TAG, "[bms] characteristic discovery failed or missing: conn=%u",
                     conn_handle);
            (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;
    }

    runtime_set_bms_info(runtime, "BMS CHR");
    ESP_LOGW(TAG, "[bms] characteristic discovery failed: conn=%u status=%u",
             conn_handle, error ? (unsigned)error->status : 0U);
    (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

static esp_err_t runtime_bms_ble_start_characteristic_discovery(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || runtime->bms_conn_handle == 0xFFFFU ||
        runtime->bms_service_start_handle == 0U ||
        runtime->bms_service_end_handle == 0U) {
        return ESP_ERR_INVALID_STATE;
    }

    ble_uuid16_t characteristic_uuid = BLE_UUID16_INIT(ANT_BMS_CHARACTERISTIC_UUID_16);
    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_DISCOVERING_CHARACTERISTIC;
    runtime->bms_char_val_handle = 0;
    const int rc = ble_gattc_disc_chrs_by_uuid(runtime->bms_conn_handle,
                                               runtime->bms_service_start_handle,
                                               runtime->bms_service_end_handle,
                                               &characteristic_uuid.u,
                                               runtime_bms_ble_chr_cb,
                                               runtime);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

static int runtime_bms_ble_service_cb(uint16_t conn_handle,
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
            runtime_bms_ble_start_characteristic_discovery(runtime) != ESP_OK) {
            runtime_set_bms_info(runtime, "BMS NO SVC");
            ESP_LOGW(TAG, "[bms] service discovery failed or missing: conn=%u",
                     conn_handle);
            (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;
    }

    runtime_set_bms_info(runtime, "BMS SVC");
    ESP_LOGW(TAG, "[bms] service discovery failed: conn=%u status=%u",
             conn_handle, error ? (unsigned)error->status : 0U);
    (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

static esp_err_t runtime_bms_ble_start_service_discovery(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime || runtime->bms_conn_handle == 0xFFFFU) {
        return ESP_ERR_INVALID_STATE;
    }

    ble_uuid16_t service_uuid = BLE_UUID16_INIT(ANT_BMS_SERVICE_UUID_16);
    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_DISCOVERING_SERVICE;
    runtime->bms_service_start_handle = 0;
    runtime->bms_service_end_handle = 0;
    const int rc = ble_gattc_disc_svc_by_uuid(runtime->bms_conn_handle,
                                              &service_uuid.u,
                                              runtime_bms_ble_service_cb,
                                              runtime);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t runtime_bms_ble_connect_to_disc(esp_bms_idf_runtime_t *runtime,
                                                 const struct ble_gap_disc_desc *disc,
                                                 const char *mac)
{
    if (!runtime || !disc || !mac || runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_CONNECTING ||
        runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_ONLINE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (ble_gap_disc_active()) {
        const int cancel_rc = ble_gap_disc_cancel();
        if (cancel_rc != 0) {
            return ESP_FAIL;
        }
    }

    uint8_t own_addr_type = 0;
    const int addr_rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (addr_rc != 0) {
        return ESP_FAIL;
    }

    const int rc = ble_gap_connect(own_addr_type,
                                   &disc->addr,
                                   BMS_CONNECT_TIMEOUT_MS,
                                   NULL,
                                   runtime_bms_ble_gap_event,
                                   runtime);
    if (rc != 0) {
        return ESP_FAIL;
    }

    runtime->bms_own_addr_type = own_addr_type;
    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_CONNECTING;
    runtime->bms_scan_active = false;
    runtime_set_bms_info(runtime, "BMS CONN");
    ESP_LOGI(TAG, "[bms] connecting to bound BMS: mac=%s addr_type=%u",
             mac, disc->addr.type);
    return ESP_OK;
}

static void runtime_bms_ble_handle_notification(esp_bms_idf_runtime_t *runtime,
                                                const struct ble_gap_event *event)
{
    if (!runtime || !event || event->notify_rx.conn_handle != runtime->bms_conn_handle ||
        event->notify_rx.attr_handle != runtime->bms_char_val_handle) {
        return;
    }

    const int len = OS_MBUF_PKTLEN(event->notify_rx.om);
    if (len <= 0 || len > (int)sizeof(runtime->bms_frame)) {
        runtime_set_bms_info(runtime, "BMS RX LEN");
        ESP_LOGW(TAG, "[bms] invalid notification length: len=%d", len);
        return;
    }

    uint8_t chunk[ESP_BMS_IDF_BMS_FRAME_MAX_LEN] = { 0 };
    const int rc = os_mbuf_copydata(event->notify_rx.om, 0, len, chunk);
    if (rc != 0 || !runtime_bms_frame_push(runtime, chunk, (size_t)len)) {
        runtime_set_bms_info(runtime, "BMS RX");
        ESP_LOGW(TAG, "[bms] notification parse failed: len=%d rc=%d", len, rc);
    }
}

static int runtime_bms_ble_gap_event(struct ble_gap_event *event, void *arg)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime) {
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
        runtime_ble_addr_to_mac_text(event->disc.addr.val, mac, sizeof(mac));
        const bool has_name = runtime_bms_name_copy(name, sizeof(name), fields.name, fields.name_len);
        const bool matches_binding = runtime->bms_bound_mac[0] != '\0' &&
                                     strcmp(mac, runtime->bms_bound_mac) == 0;
        const bool looks_like_ant = has_name && runtime_bms_name_matches_ant(name);
        const bool has_ant_service = runtime_bms_adv_has_ant_service(&fields);
        if (matches_binding || looks_like_ant || has_ant_service) {
            const int8_t rssi = event->disc.rssi == 127 ? INT8_MIN : event->disc.rssi;
            runtime_bms_scan_store_candidate(runtime, mac, has_name ? name : NULL, rssi);
        }
        if (matches_binding) {
            const esp_err_t ret = runtime_bms_ble_connect_to_disc(runtime, &event->disc, mac);
            if (ret != ESP_OK) {
                runtime_set_bms_info(runtime, "BMS CONN ERR");
                ESP_LOGW(TAG, "[bms] connect request failed: mac=%s ret=%s",
                         mac, esp_err_to_name(ret));
            }
        }
        return 0;
    }
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            runtime->bms_conn_handle = event->connect.conn_handle;
            runtime->bms_scan_active = false;
            runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_DISCOVERING_SERVICE;
            runtime_set_bms_info(runtime, "BMS DISC");
            ESP_LOGI(TAG, "[bms] connected: conn=%u", event->connect.conn_handle);
            if (runtime_bms_ble_start_service_discovery(runtime) != ESP_OK) {
                runtime_set_bms_info(runtime, "BMS SVC");
                ESP_LOGW(TAG, "[bms] service discovery request failed: conn=%u",
                         event->connect.conn_handle);
                (void)ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
        } else {
            runtime_bms_ble_reset_connection_state(runtime, BMS_BLE_PHASE_BACKOFF);
            runtime_clear_bms_telemetry(runtime);
            runtime_set_bms_info(runtime, "BMS CONN FAIL");
            ESP_LOGW(TAG, "[bms] connection failed: status=%d", event->connect.status);
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        if (event->disconnect.conn.conn_handle == runtime->bms_conn_handle ||
            runtime->bms_ble_phase != (uint8_t)BMS_BLE_PHASE_SCANNING) {
            const bool scan_requested = runtime->bms_scan_requested;
            runtime_bms_ble_reset_connection_state(runtime, BMS_BLE_PHASE_BACKOFF);
            runtime_clear_bms_telemetry(runtime);
            runtime_set_bms_info(runtime, "BMS OFF");
            ESP_LOGW(TAG, "[bms] disconnected: reason=%d", event->disconnect.reason);
            if (scan_requested) {
                runtime->bms_scan_requested = true;
                const esp_err_t ret = runtime_bms_ble_start_scan(runtime);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "[bms] deferred scan after disconnect failed: %s",
                             esp_err_to_name(ret));
                }
            }
        }
        return 0;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_SCANNING) {
            runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_IDLE;
            runtime->bms_scan_active = false;
            runtime_set_bms_info(runtime, "BMS DONE");
        }
        ESP_LOGI(TAG, "[bms] BLE scan complete: reason=%d candidates=%u",
                 event->disc_complete.reason, runtime->bms_scan_candidate_count);
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX:
        runtime_bms_ble_handle_notification(runtime, event);
        return 0;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "[bms] MTU updated: conn=%u mtu=%u",
                 event->mtu.conn_handle, event->mtu.value);
        return 0;
    default:
        return 0;
    }
}

static esp_err_t runtime_bms_ble_start_scan(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime->bms_ble_ready || !runtime->bms_ble_synced) {
        runtime->bms_scan_requested = true;
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t local_bt_ret = runtime_bluetooth_stop_for_bms_scan(runtime);
    if (local_bt_ret != ESP_OK) {
        runtime->bms_scan_requested = true;
        return local_bt_ret;
    }

    if (runtime->bms_conn_handle != 0xFFFFU) {
        runtime->bms_scan_requested = true;
        (void)ble_gap_terminate(runtime->bms_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return ESP_ERR_INVALID_STATE;
    }
    if (ble_gap_disc_active()) {
        (void)ble_gap_disc_cancel();
    }

    uint8_t own_addr_type = 0;
    const int addr_rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (addr_rc != 0) {
        runtime->bms_scan_requested = true;
        return ESP_FAIL;
    }

    runtime->bms_own_addr_type = own_addr_type;
    struct ble_gap_disc_params disc_params = { 0 };
    disc_params.filter_duplicates = 0;
    disc_params.passive = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    const int rc = ble_gap_disc(own_addr_type,
                                BMS_SCAN_DURATION_MS,
                                &disc_params,
                                runtime_bms_ble_gap_event,
                                runtime);
    if (rc != 0) {
        runtime->bms_scan_requested = true;
        runtime_bms_ble_reset_connection_state(runtime, BMS_BLE_PHASE_BACKOFF);
        return ESP_FAIL;
    }

    runtime->bms_scan_requested = false;
    runtime->bms_scan_active = true;
    runtime->bms_ble_phase = (uint8_t)BMS_BLE_PHASE_SCANNING;
    runtime_set_bms_info(runtime, "BMS SCAN");
    ESP_LOGI(TAG, "[bms] BLE scan started: duration_ms=%u", (unsigned)BMS_SCAN_DURATION_MS);
    return ESP_OK;
}

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
            runtime->bluetooth_connected = true;
            runtime->bluetooth_advertising = false;
            runtime->bluetooth_advertise_requested = false;
            (void)runtime_project_bluetooth_snapshot(runtime);
            runtime_set_error(runtime, "BT CONN");
            ESP_LOGI(TAG, "[bt] local Bluetooth connected: conn=%u", event->connect.conn_handle);
        } else {
            runtime->bluetooth_conn_handle = 0xFFFFU;
            runtime->bluetooth_connected = false;
            runtime->bluetooth_advertising = false;
            runtime->bluetooth_advertise_requested = true;
            (void)runtime_project_bluetooth_snapshot(runtime);
            ESP_LOGW(TAG, "[bt] local Bluetooth connection failed: status=%d", event->connect.status);
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        if (event->disconnect.conn.conn_handle == runtime->bluetooth_conn_handle) {
            const bool start_bms_scan = runtime->bms_scan_requested;
            runtime->bluetooth_conn_handle = 0xFFFFU;
            runtime->bluetooth_connected = false;
            runtime->bluetooth_advertising = false;
            runtime->bluetooth_advertise_requested = !start_bms_scan;
            (void)runtime_project_bluetooth_snapshot(runtime);
            runtime_set_error(runtime, "BT OFF");
            ESP_LOGI(TAG, "[bt] local Bluetooth disconnected: reason=%d", event->disconnect.reason);
            if (start_bms_scan) {
                const esp_err_t ret = runtime_bms_ble_start_scan(runtime);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "[bms] scan after local Bluetooth disconnect failed: %s",
                             esp_err_to_name(ret));
                }
            }
        }
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        runtime->bluetooth_advertising = false;
        if (!runtime->bluetooth_connected &&
            !runtime->bms_scan_requested &&
            !runtime->bms_scan_active &&
            runtime->bms_conn_handle == 0xFFFFU) {
            runtime->bluetooth_advertise_requested = true;
        }
        (void)runtime_project_bluetooth_snapshot(runtime);
        ESP_LOGI(TAG, "[bt] local Bluetooth advertising complete: reason=%d",
                 event->adv_complete.reason);
        return 0;
    default:
        return 0;
    }
}

static esp_err_t runtime_bluetooth_start_advertising_now(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!runtime->bms_ble_ready || !runtime->bms_ble_synced) {
        runtime->bluetooth_advertise_requested = true;
        (void)runtime_project_bluetooth_snapshot(runtime);
        return ESP_ERR_INVALID_STATE;
    }
    if (runtime->bluetooth_connected || runtime->bluetooth_advertising || ble_gap_adv_active()) {
        runtime->bluetooth_advertise_requested = false;
        runtime->bluetooth_advertising = ble_gap_adv_active() != 0;
        (void)runtime_project_bluetooth_snapshot(runtime);
        return ESP_OK;
    }
    if (runtime->bms_scan_active || runtime->bms_conn_handle != 0xFFFFU || ble_gap_disc_active()) {
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
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.tx_pwr_lvl_is_present = 1;
    const int fields_rc = ble_gap_adv_set_fields(&fields);
    if (fields_rc != 0) {
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
    runtime->bluetooth_advertise_requested = false;
    runtime->bluetooth_advertising = true;
    runtime->bluetooth_connected = false;
    runtime_project_bluetooth_snapshot(runtime);
    runtime_set_error(runtime, "BT ON");
    ESP_LOGI(TAG, "[bt] local Bluetooth advertising started: name='%s'", runtime->snapshot.bluetooth_name);
    return ESP_OK;
}

static esp_err_t runtime_bluetooth_stop_for_bms_scan(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }

    runtime->bluetooth_advertise_requested = false;
    if (runtime->bluetooth_conn_handle != 0xFFFFU) {
        (void)ble_gap_terminate(runtime->bluetooth_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        runtime->bms_scan_requested = true;
        runtime_project_bluetooth_snapshot(runtime);
        return ESP_ERR_INVALID_STATE;
    }
    if (runtime->bluetooth_advertising || ble_gap_adv_active()) {
        const int rc = ble_gap_adv_stop();
        if (rc != 0 && rc != BLE_HS_EALREADY) {
            return ESP_FAIL;
        }
        runtime->bluetooth_advertising = false;
        runtime_project_bluetooth_snapshot(runtime);
    }
    return ESP_OK;
}

static esp_err_t runtime_bms_ble_start_scan_or_defer(esp_bms_idf_runtime_t *runtime)
{
    const esp_err_t ret = runtime_bms_ble_start_scan(runtime);
    if (ret == ESP_ERR_INVALID_STATE && runtime && runtime->bms_scan_requested) {
        return ESP_OK;
    }
    return ret;
}

static void runtime_bms_ble_on_reset(int reason)
{
    esp_bms_idf_runtime_t *runtime = s_bms_ble_runtime;
    if (runtime) {
        runtime->bms_ble_synced = false;
        runtime->bms_scan_active = false;
        runtime->bluetooth_advertising = false;
        runtime->bluetooth_connected = false;
        runtime->bluetooth_conn_handle = 0xFFFFU;
        (void)runtime_project_bluetooth_snapshot(runtime);
    }
    ESP_LOGW(TAG, "[bms] NimBLE reset: reason=%d", reason);
}

static void runtime_bms_ble_on_sync(void)
{
    esp_bms_idf_runtime_t *runtime = s_bms_ble_runtime;
    if (!runtime) {
        return;
    }
    runtime->bms_ble_synced = true;
    ESP_LOGI(TAG, "[bms] NimBLE synced");
    if (runtime->bms_scan_requested) {
        const esp_err_t ret = runtime_bms_ble_start_scan(runtime);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "[bms] deferred BLE scan start failed: %s", esp_err_to_name(ret));
        }
    } else if (runtime->bluetooth_advertise_requested) {
        const esp_err_t ret = runtime_bluetooth_start_advertising_now(runtime);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "[bt] deferred local Bluetooth advertising failed: %s",
                     esp_err_to_name(ret));
        }
    }
}

static void runtime_bms_ble_host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "[bms] NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static esp_err_t runtime_init_bms_ble(esp_bms_idf_runtime_t *runtime)
{
    if (runtime->bms_ble_ready) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(runtime_init_nvs(runtime), TAG, "NVS init failed");

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        return ret;
    }

    s_bms_ble_runtime = runtime;
    ble_svc_gap_init();
    int gap_rc = ble_svc_gap_device_name_set(runtime->bluetooth_name[0] != '\0'
                                                 ? runtime->bluetooth_name
                                                 : LOCAL_BLUETOOTH_NAME);
    if (gap_rc != 0) {
        return ESP_FAIL;
    }
    gap_rc = ble_svc_gap_device_appearance_set(0x0200U);
    if (gap_rc != 0) {
        return ESP_FAIL;
    }

    ble_hs_cfg.reset_cb = runtime_bms_ble_on_reset;
    ble_hs_cfg.sync_cb = runtime_bms_ble_on_sync;

    BaseType_t task_ret = xTaskCreate(runtime_bms_ble_host_task,
                                      "bms-nimble",
                                      BMS_SCAN_HOST_TASK_STACK,
                                      NULL,
                                      BMS_SCAN_HOST_TASK_PRIORITY,
                                      NULL);
    if (task_ret != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    runtime->bms_ble_ready = true;
    runtime->bms_ble_host_started = true;
    ESP_LOGI(TAG, "[bms] NimBLE initialized");
    return ESP_OK;
}

static void runtime_wifi_event_handler(void *arg,
                                       esp_event_base_t event_base,
                                       int32_t event_id,
                                       void *event_data)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || event_base != WIFI_EVENT) {
        return;
    }

    if (event_id == WIFI_EVENT_AP_START) {
        runtime->setup_ap_started = true;
        ESP_LOGI(TAG, "[wifi] AP started: ip=192.168.4.1 dhcp=on");
        return;
    }
    if (event_id == WIFI_EVENT_AP_STOP) {
        runtime->setup_ap_started = false;
        runtime->setup_ap_clients = 0;
        ESP_LOGI(TAG, "[wifi] AP stopped");
        return;
    }
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *event = (const wifi_event_ap_staconnected_t *)event_data;
        if (runtime->setup_ap_clients < UINT8_MAX) {
            runtime->setup_ap_clients++;
        }
        ESP_LOGI(TAG, "[wifi] AP client connected: clients=%u first_mac=" MACSTR,
                 runtime->setup_ap_clients, MAC2STR(event->mac));
        return;
    }
    if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *event = (const wifi_event_ap_stadisconnected_t *)event_data;
        if (runtime->setup_ap_clients > 0) {
            runtime->setup_ap_clients--;
        }
        ESP_LOGI(TAG, "[wifi] AP client disconnected: clients=%u mac=" MACSTR " reason=%u",
                 runtime->setup_ap_clients, MAC2STR(event->mac), event->reason);
        return;
    }
    if (event_id == WIFI_EVENT_SCAN_DONE) {
        runtime_wifi_scan_handle_done(runtime);
        return;
    }
    if (event_id == WIFI_EVENT_STA_START) {
        runtime->station_started = true;
        if (runtime->station_connect_requested && runtime_has_external_wifi_credentials(runtime)) {
            runtime->snapshot.wifi = ESP_BMS_WIFI_CONNECTING;
            const esp_err_t ret = esp_wifi_connect();
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "[wifi] STA connect request failed: %s", esp_err_to_name(ret));
            }
        }
        return;
    }
    if (event_id == WIFI_EVENT_STA_STOP) {
        runtime->station_started = false;
        runtime->station_connected = false;
        runtime->station_has_ip = false;
        ESP_LOGI(TAG, "[wifi] STA stopped");
        return;
    }
    if (event_id == WIFI_EVENT_STA_CONNECTED) {
        runtime->station_connected = true;
        runtime->station_has_ip = false;
        runtime->snapshot.wifi = ESP_BMS_WIFI_CONNECTING;
        ESP_LOGI(TAG, "[wifi] STA connected: ssid='%s'", runtime->external_ssid);
        return;
    }
    if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = (const wifi_event_sta_disconnected_t *)event_data;
        runtime->station_connected = false;
        runtime->station_has_ip = false;
        if (runtime->station_connect_requested &&
            runtime_has_external_wifi_credentials(runtime) &&
            runtime->station_retry_count < STATION_RECONNECT_MAX) {
            runtime->station_retry_count++;
            runtime->snapshot.wifi = ESP_BMS_WIFI_CONNECTING;
            ESP_LOGW(TAG, "[wifi] STA disconnected: ssid='%s' reason=%u retry=%u/%u",
                     runtime->external_ssid,
                     event ? event->reason : 0U,
                     runtime->station_retry_count,
                     STATION_RECONNECT_MAX);
            const esp_err_t ret = esp_wifi_connect();
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "[wifi] STA reconnect request failed: %s", esp_err_to_name(ret));
            }
            return;
        }

        runtime->snapshot.wifi = runtime_has_external_wifi_credentials(runtime)
                                     ? ESP_BMS_WIFI_OFFLINE
                                     : ESP_BMS_WIFI_SETUP_AP;
        runtime_set_error(runtime, "STA FAIL");
        ESP_LOGW(TAG, "[wifi] STA disconnected: ssid='%s' reason=%u retries_exhausted=%s",
                 runtime->external_ssid,
                 event ? event->reason : 0U,
                 runtime->station_retry_count >= STATION_RECONNECT_MAX ? "true" : "false");
    }
}

static void runtime_ip_event_handler(void *arg,
                                     esp_event_base_t event_base,
                                     int32_t event_id,
                                     void *event_data)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (event_base != IP_EVENT) {
        return;
    }

    if (event_id == IP_EVENT_AP_STAIPASSIGNED) {
        const ip_event_ap_staipassigned_t *event = (const ip_event_ap_staipassigned_t *)event_data;
        char ip[16] = { 0 };
        ESP_LOGI(TAG, "[wifi] DHCP lease assigned: ip=%s mac=" MACSTR,
                 esp_ip4addr_ntoa(&event->ip, ip, sizeof(ip)), MAC2STR(event->mac));
        return;
    }
    if (event_id == IP_EVENT_STA_GOT_IP && runtime) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        char ip[16] = { 0 };
        runtime->station_connected = true;
        runtime->station_has_ip = true;
        runtime->station_retry_count = 0;
        runtime->snapshot.wifi = ESP_BMS_WIFI_CONNECTED;
        runtime_set_error(runtime, "STA IP");
        ESP_LOGI(TAG, "[wifi] STA got IP: ssid='%s' ip=%s",
                 runtime->external_ssid,
                 esp_ip4addr_ntoa(&event->ip_info.ip, ip, sizeof(ip)));
    }
}

static esp_err_t runtime_register_wifi_handlers(esp_bms_idf_runtime_t *runtime)
{
    if (runtime->wifi_handlers_registered) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            runtime_wifi_event_handler,
                                                            runtime,
                                                            NULL),
                        TAG,
                        "Wi-Fi event handler registration failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            runtime_ip_event_handler,
                                                            runtime,
                                                            NULL),
                        TAG,
                        "IP event handler registration failed");
    runtime->wifi_handlers_registered = true;
    return ESP_OK;
}

static esp_err_t runtime_init_wifi_stack(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime->wifi_stack_ready) {
        ESP_RETURN_ON_ERROR(runtime_init_nvs(runtime), TAG, "NVS init failed");

        esp_err_t ret = esp_netif_init();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(ret));
            return ret;
        }

        ret = esp_event_loop_create_default();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "event loop init failed: %s", esp_err_to_name(ret));
            return ret;
        }

        runtime->setup_ap_netif = esp_netif_create_default_wifi_ap();
        if (!runtime->setup_ap_netif) {
            return ESP_ERR_NO_MEM;
        }
        ESP_RETURN_ON_ERROR(runtime_configure_setup_ap_ip(runtime->setup_ap_netif),
                            TAG,
                            "setup AP IP config failed");

        runtime->station_netif = esp_netif_create_default_wifi_sta();
        if (!runtime->station_netif) {
            return ESP_ERR_NO_MEM;
        }

        runtime->wifi_stack_ready = true;
    }

    if (!runtime->wifi_driver_ready) {
        wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
        ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "esp_wifi_init failed");
        runtime->wifi_driver_ready = true;
    }

    return runtime_register_wifi_handlers(runtime);
}

static esp_err_t runtime_apply_setup_ap_wifi_config(const esp_bms_idf_runtime_t *runtime)
{
    if (!runtime_setup_ap_ssid_matches_policy(runtime->setup_ap_ssid) ||
        !runtime_setup_ap_password_matches_policy(runtime->setup_ap_password)) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_config_t wifi_config = { 0 };
    const size_t ssid_len = strlen(runtime->setup_ap_ssid);
    const size_t password_len = strlen(runtime->setup_ap_password);
    memcpy(wifi_config.ap.ssid, runtime->setup_ap_ssid, ssid_len);
    memcpy(wifi_config.ap.password, runtime->setup_ap_password, password_len);
    wifi_config.ap.ssid_len = (uint8_t)ssid_len;
    wifi_config.ap.channel = SETUP_AP_CHANNEL;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.ap.max_connection = SETUP_AP_MAX_CONNECTIONS;
    wifi_config.ap.pmf_cfg.required = false;
    return esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
}

static esp_err_t runtime_apply_external_wifi_config(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime_external_wifi_credentials_match_policy(runtime->external_ssid,
                                                        runtime->external_password)) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_config_t wifi_config = { 0 };
    const size_t ssid_len = strlen(runtime->external_ssid);
    const size_t password_len = strlen(runtime->external_password);
    memcpy(wifi_config.sta.ssid, runtime->external_ssid, ssid_len);
    memcpy(wifi_config.sta.password, runtime->external_password, password_len);
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.threshold.authmode = password_len == 0U ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    const esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret == ESP_OK) {
        runtime->station_connect_requested = true;
        runtime->station_retry_count = 0;
        runtime->station_connected = false;
        runtime->station_has_ip = false;
        runtime->snapshot.wifi = ESP_BMS_WIFI_CONNECTING;
        ESP_LOGI(TAG, "[wifi] STA config applied: ssid='%s' sta_pw_len=%u",
                 runtime->external_ssid, (unsigned)password_len);
    }
    return ret;
}

static bool runtime_wifi_started(const esp_bms_idf_runtime_t *runtime)
{
    return runtime && (runtime->setup_ap_started || runtime->station_started);
}

static esp_err_t runtime_start_station_connect(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!runtime_has_external_wifi_credentials(runtime)) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(runtime_init_wifi_stack(runtime), TAG, "Wi-Fi stack init failed");

    const bool wifi_was_started = runtime_wifi_started(runtime);
    esp_err_t ret = esp_wifi_set_mode(runtime->setup_ap_started ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    if (ret == ESP_OK && runtime->setup_ap_started) {
        ret = runtime_apply_setup_ap_wifi_config(runtime);
    }
    if (ret == ESP_OK) {
        ret = runtime_apply_external_wifi_config(runtime);
    }
    if (ret == ESP_OK && !wifi_was_started) {
        ret = esp_wifi_start();
    } else if (ret == ESP_OK && runtime->station_started) {
        (void)esp_wifi_disconnect();
        ret = esp_wifi_connect();
    }
    return ret;
}

static esp_err_t runtime_wifi_start_scan(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    if (runtime->wifi_scan_active) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(runtime_init_wifi_stack(runtime), TAG, "Wi-Fi stack init failed");

    const bool wifi_was_started = runtime_wifi_started(runtime);
    esp_err_t ret = esp_wifi_set_mode(runtime->setup_ap_started ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    if (ret != ESP_OK) {
        return ret;
    }
    if (runtime->setup_ap_started) {
        ret = runtime_apply_setup_ap_wifi_config(runtime);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    if (!wifi_was_started) {
        ret = esp_wifi_start();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    runtime_wifi_scan_clear_candidates(runtime);
    runtime_wifi_scan_set_state(runtime, true, false);

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 80,
        .scan_time.active.max = 180,
    };
    ret = esp_wifi_scan_start(&scan_config, false);
    if (ret != ESP_OK) {
        runtime_wifi_scan_set_state(runtime, false, true);
        return ret;
    }

    runtime_set_error(runtime, "SCAN WIFI");
    ESP_LOGI(TAG, "[wifi] STA scan started");
    return ESP_OK;
}

static void runtime_wifi_scan_handle_done(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return;
    }

    uint16_t ap_count = 0;
    esp_err_t ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK) {
        runtime_wifi_scan_set_state(runtime, false, true);
        runtime_set_error(runtime, "SCAN FAIL");
        ESP_LOGW(TAG, "[wifi] scan count failed: %s", esp_err_to_name(ret));
        return;
    }

    runtime_wifi_scan_clear_candidates(runtime);
    uint16_t read_count = ap_count;
    if (read_count > ESP_BMS_WIFI_SCAN_MAX_CANDIDATES) {
        read_count = ESP_BMS_WIFI_SCAN_MAX_CANDIDATES;
    }

    wifi_ap_record_t records[ESP_BMS_WIFI_SCAN_MAX_CANDIDATES] = { 0 };
    if (read_count > 0U) {
        ret = esp_wifi_scan_get_ap_records(&read_count, records);
        if (ret != ESP_OK) {
            runtime_wifi_scan_set_state(runtime, false, true);
            runtime_set_error(runtime, "SCAN FAIL");
            ESP_LOGW(TAG, "[wifi] scan records failed: %s", esp_err_to_name(ret));
            return;
        }

        for (uint16_t index = 0; index < read_count; ++index) {
            runtime_wifi_scan_store_candidate(runtime, &records[index]);
        }
    }

    runtime_wifi_scan_set_state(runtime, false, true);
    runtime_wifi_scan_project_snapshot(runtime);
    runtime_set_error(runtime, read_count > 0U ? "SCAN DONE" : "SCAN EMPTY");
    ESP_LOGI(TAG, "[wifi] STA scan done: aps=%u shown=%u",
             (unsigned)ap_count,
             (unsigned)runtime->snapshot.wifi_scan_count);
}

static bool runtime_sample_battery(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime->battery_adc_ready || !runtime->battery_adc) {
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
        !runtime->snapshot.local_battery_valid || runtime->snapshot.local_battery_mv != battery_mv;
    runtime->snapshot.local_battery_valid = true;
    runtime->snapshot.local_battery_mv = battery_mv;
    runtime->battery_samples_seen++;
    return changed;
}

static bool runtime_apply_gps_line(esp_bms_idf_runtime_t *runtime)
{
    if (runtime->gps_line_len == 0) {
        return false;
    }

    bool fix_valid = false;
    uint32_t speed_knots_milli = 0;
    const gps_parse_result_t result =
        runtime_parse_rmc(runtime->gps_line, runtime->gps_line_len, &fix_valid, &speed_knots_milli);
    if (result == GPS_PARSE_IGNORE) {
        return false;
    }
    if (result == GPS_PARSE_ERROR) {
        runtime->gps_parse_errors++;
        return false;
    }

    runtime->snapshot.gps_fix_valid = fix_valid;
    runtime->gps_speed_knots_milli = speed_knots_milli;
    runtime->snapshot.gps_sentences_seen++;
    runtime_update_snapshot_speed(runtime);
    return true;
}

static bool runtime_feed_gps_byte(esp_bms_idf_runtime_t *runtime, uint8_t byte)
{
    if (byte == '\r') {
        return false;
    }
    if (byte == '$') {
        runtime->gps_line[0] = byte;
        runtime->gps_line_len = 1;
        return false;
    }
    if (byte == '\n') {
        const bool changed = runtime_apply_gps_line(runtime);
        runtime->gps_line_len = 0;
        return changed;
    }
    if (runtime->gps_line_len >= GPS_RMC_MAX_LINE) {
        runtime->gps_line_len = 0;
        runtime->gps_parse_errors++;
        return false;
    }

    runtime->gps_line[runtime->gps_line_len++] = byte;
    return false;
}

static bool runtime_poll_gps_uart(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime->gps_uart_ready) {
        return false;
    }

    size_t available = 0;
    esp_err_t ret = uart_get_buffered_data_len(runtime->gps_uart, &available);
    if (ret != ESP_OK || available == 0) {
        return false;
    }

    uint8_t bytes[GPS_RMC_MAX_LINE];
    if (available > sizeof(bytes)) {
        available = sizeof(bytes);
    }

    const int read = uart_read_bytes(runtime->gps_uart, bytes, (uint32_t)available, 0);
    if (read <= 0) {
        return false;
    }

    bool changed = false;
    runtime->gps_bytes_seen += (uint32_t)read;
    for (int index = 0; index < read; index++) {
        changed = runtime_feed_gps_byte(runtime, bytes[index]) || changed;
    }
    return changed;
}

void esp_bms_idf_runtime_init(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->http_pending_lock = xSemaphoreCreateMutex();
    if (!runtime->http_pending_lock) {
        ESP_LOGW(TAG, "[http] pending config mutex allocation failed");
    }
    runtime->bms_scan_lock = xSemaphoreCreateMutex();
    if (!runtime->bms_scan_lock) {
        ESP_LOGW(TAG, "[bms] scan candidate mutex allocation failed");
    }
    runtime->wifi_scan_lock = xSemaphoreCreateMutex();
    if (!runtime->wifi_scan_lock) {
        ESP_LOGW(TAG, "[wifi] scan candidate mutex allocation failed");
    }
    runtime_reset_state(runtime);
    runtime_ensure_setup_ap_credentials(runtime);
    runtime_init_battery_adc(runtime);
    (void)runtime_sample_battery(runtime);
    runtime_init_gps_uart(runtime);
}

esp_err_t esp_bms_idf_runtime_start_setup_ap(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    if (runtime->setup_ap_started) {
        return ESP_OK;
    }
    runtime_ensure_setup_ap_credentials(runtime);
    ESP_LOGI(TAG, "[wifi] starting setup AP: ssid='%s' ap_pw_len=%u",
             runtime->setup_ap_ssid,
             (unsigned)strlen(runtime->setup_ap_password));

    esp_err_t ret = runtime_init_wifi_stack(runtime);
    if (ret != ESP_OK) {
        runtime->snapshot.wifi = ESP_BMS_WIFI_OFFLINE;
        runtime->snapshot.setup_ap_enabled = false;
        runtime_set_error(runtime, "AP FAIL");
        return ret;
    }

    const bool wifi_was_started = runtime_wifi_started(runtime);
    const bool keep_station = runtime->station_started || runtime->station_connect_requested;
    ret = esp_wifi_set_mode(keep_station ? WIFI_MODE_APSTA : WIFI_MODE_AP);
    if (ret == ESP_OK) {
        ret = runtime_apply_setup_ap_wifi_config(runtime);
    }
    if (ret == ESP_OK && !wifi_was_started) {
        ret = esp_wifi_start();
    }
    if (ret != ESP_OK) {
        runtime->snapshot.wifi = ESP_BMS_WIFI_OFFLINE;
        runtime->snapshot.setup_ap_enabled = false;
        runtime_set_error(runtime, "AP FAIL");
        ESP_LOGE(TAG, "[wifi] AP start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    runtime->setup_ap_started = true;
    runtime->snapshot.setup_ap_enabled = true;
    runtime->snapshot.wifi = runtime->station_has_ip
                                  ? ESP_BMS_WIFI_CONNECTED
                                  : (runtime->station_connect_requested ? ESP_BMS_WIFI_CONNECTING
                                                                        : ESP_BMS_WIFI_SETUP_AP);
    runtime_set_error(runtime, "AP READY");
    return ESP_OK;
}

esp_err_t esp_bms_idf_runtime_start_http_server(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!runtime->setup_ap_started) {
        return ESP_ERR_INVALID_STATE;
    }
    runtime_ensure_setup_ap_credentials(runtime);

    const esp_err_t ret = runtime_start_http_server(runtime);
    if (ret == ESP_OK) {
        runtime_set_error(runtime, "HTTP ON");
    }
    return ret;
}

esp_err_t esp_bms_idf_runtime_start_bms_ble_if_bound(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t load_ret = runtime_load_bms_binding(runtime);
    if (load_ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "[bms] no bound MAC; NimBLE stays off");
        return ESP_OK;
    }
    if (load_ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "[bms] bound MAC in NVS is invalid; NimBLE stays off");
        return ESP_OK;
    }
    if (load_ret != ESP_OK) {
        return load_ret;
    }

    ESP_LOGI(TAG, "[bms] bound MAC loaded: mac=%s", runtime->bms_bound_mac);
    ESP_RETURN_ON_ERROR(runtime_init_bms_ble(runtime), TAG, "BMS BLE init failed");
    return runtime_bms_ble_start_scan_or_defer(runtime);
}

esp_err_t esp_bms_idf_runtime_start_bms_ble_for_bind(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(runtime_init_bms_ble(runtime), TAG, "BMS BLE init failed");
    return runtime_bms_ble_start_scan_or_defer(runtime);
}

esp_err_t esp_bms_idf_runtime_start_wifi_scan(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t load_ret = runtime_load_external_wifi_credentials(runtime);
    if (load_ret == ESP_OK) {
        ESP_LOGI(TAG, "[wifi] external credentials loaded: ssid='%s' sta_pw_len=%u",
                 runtime->external_ssid,
                 (unsigned)strlen(runtime->external_password));
    } else if (load_ret == ESP_ERR_NVS_NOT_FOUND) {
        runtime->external_ssid[0] = '\0';
        runtime->external_password[0] = '\0';
    } else if (load_ret != ESP_ERR_INVALID_STATE) {
        runtime->external_ssid[0] = '\0';
        runtime->external_password[0] = '\0';
        ESP_LOGW(TAG, "[wifi] external credential load failed: %s", esp_err_to_name(load_ret));
    }

    return runtime_wifi_start_scan(runtime);
}

esp_err_t esp_bms_idf_runtime_start_bluetooth_advertising(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }

    if (runtime->bluetooth_name[0] == '\0') {
        runtime_copy_snapshot_text(runtime->bluetooth_name,
                                   sizeof(runtime->bluetooth_name),
                                   LOCAL_BLUETOOTH_NAME);
    }
    runtime->bluetooth_advertise_requested = true;
    runtime_project_bluetooth_snapshot(runtime);

    esp_err_t ret = runtime_init_bms_ble(runtime);
    if (ret != ESP_OK) {
        runtime->bluetooth_advertise_requested = false;
        runtime_project_bluetooth_snapshot(runtime);
        runtime_set_error(runtime, "BT FAIL");
        return ret;
    }
    if (!runtime->bms_ble_synced) {
        runtime_set_error(runtime, "BT WAIT");
        return ESP_OK;
    }

    ret = runtime_bluetooth_start_advertising_now(runtime);
    if (ret == ESP_ERR_INVALID_STATE) {
        runtime->bluetooth_advertise_requested = false;
        runtime_project_bluetooth_snapshot(runtime);
        runtime_set_error(runtime, "BT BUSY");
    } else if (ret != ESP_OK) {
        runtime->bluetooth_advertise_requested = false;
        runtime_project_bluetooth_snapshot(runtime);
        runtime_set_error(runtime, "BT FAIL");
    }
    return ret;
}

bool esp_bms_idf_runtime_tick(esp_bms_idf_runtime_t *runtime, uint32_t elapsed_ms)
{
    if (!runtime) {
        return false;
    }

    bool changed = runtime->bluetooth_snapshot_dirty;
    runtime->bluetooth_snapshot_dirty = false;
    changed = runtime_apply_pending_http_config(runtime) || changed;
    changed = runtime_apply_pending_http_ap_password(runtime) || changed;
    changed = runtime_apply_pending_http_external_wifi(runtime) || changed;
    changed = runtime_apply_pending_http_bms_scan(runtime) || changed;
    changed = runtime_apply_pending_http_bms_bind(runtime) || changed;
    changed = runtime_apply_pending_wifi_scan(runtime) || changed;
    if (runtime->bluetooth_advertise_requested && !runtime->bms_scan_requested) {
        const esp_err_t ret = esp_bms_idf_runtime_start_bluetooth_advertising(runtime);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "[bt] local Bluetooth advertising start failed: %s",
                     esp_err_to_name(ret));
        }
        changed = true;
    }
    if (runtime->bms_ble_phase == (uint8_t)BMS_BLE_PHASE_ONLINE) {
        runtime->bms_status_poll_elapsed_ms += elapsed_ms;
        if (runtime->bms_status_poll_elapsed_ms >= BMS_STATUS_POLL_PERIOD_MS &&
            !runtime->bms_write_in_flight) {
            const esp_err_t poll_ret = runtime_bms_ble_send_poll_request(runtime);
            if (poll_ret != ESP_OK) {
                runtime_set_bms_info(runtime, "BMS POLL");
                ESP_LOGW(TAG, "[bms] poll request failed: %s", esp_err_to_name(poll_ret));
                changed = true;
            }
        }
    }
    runtime->battery_sample_elapsed_ms += elapsed_ms;
    if (runtime->battery_sample_elapsed_ms >= BATTERY_SAMPLE_PERIOD_MS) {
        runtime->battery_sample_elapsed_ms = 0;
        changed = runtime_sample_battery(runtime) || changed;
    }
    changed = runtime_poll_gps_uart(runtime) || changed;

    runtime->elapsed_ms += elapsed_ms;
    while (runtime->elapsed_ms >= 1000) {
        runtime->elapsed_ms -= 1000;
        runtime->tick_count++;
    }
    return changed;
}

bool esp_bms_idf_runtime_apply_action_event(esp_bms_idf_runtime_t *runtime,
                                            const esp_bms_lvgl_action_event_t *event)
{
    if (!runtime || !event || event->action == ESP_BMS_LVGL_ACTION_NONE) {
        return false;
    }

    switch (event->action) {
    case ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING:
        runtime->snapshot.setup_ap_enabled = true;
        runtime->snapshot.wifi = ESP_BMS_WIFI_SETUP_AP;
        runtime_set_error(runtime, "SETUP AP");
        return true;
    case ESP_BMS_LVGL_ACTION_CYCLE_BRIGHTNESS:
        (void)runtime_set_brightness_percent(runtime,
                                             runtime->brightness_percent >= 85 ? 30 :
                                             runtime->brightness_percent >= 60 ? 85 : 60);
        runtime_set_error(runtime, runtime->brightness_percent >= 85 ? "BRIGHT 85" :
                                   runtime->brightness_percent >= 60 ? "BRIGHT 60" : "BRIGHT 30");
        return true;
    case ESP_BMS_LVGL_ACTION_SET_BRIGHTNESS:
        if (!event->brightness_percent_valid ||
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
        if (!event->volume_percent_valid ||
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
        runtime->language_zh = !runtime->language_zh;
        runtime_set_error(runtime, runtime->language_zh ? "LANG ZH" : "LANG EN");
        return true;
    case ESP_BMS_LVGL_ACTION_START_BMS_BIND:
        if (!runtime_set_pending_http_bms_scan(runtime)) {
            runtime_set_bms_info(runtime, "BMS Q FAIL");
        }
        return true;
    case ESP_BMS_LVGL_ACTION_ENABLE_BLUETOOTH_ADVERTISING:
        runtime->bluetooth_advertise_requested = true;
        runtime_project_bluetooth_snapshot(runtime);
        runtime_set_error(runtime, "BT ON");
        return true;
    case ESP_BMS_LVGL_ACTION_SCAN_WIFI:
        runtime->wifi_scan_requested = true;
        runtime->wifi_scan_complete = false;
        runtime->snapshot.wifi_scan_active = true;
        runtime->snapshot.wifi_scan_complete = false;
        runtime->snapshot.wifi_scan_generation++;
        runtime_set_error(runtime, "SCAN WIFI");
        return true;
    case ESP_BMS_LVGL_ACTION_CONNECT_WIFI:
        if (!event->wifi_ssid_valid ||
            !event->wifi_password_valid ||
            !runtime_external_wifi_credentials_match_policy(event->wifi_ssid, event->wifi_password)) {
            runtime_set_error(runtime, "WIFI BAD");
            return true;
        }
        if (!runtime_set_pending_http_external_wifi(runtime, event->wifi_ssid, event->wifi_password)) {
            runtime_set_error(runtime, "WIFI Q FAIL");
            return true;
        }
        runtime->snapshot.wifi = ESP_BMS_WIFI_CONNECTING;
        runtime_set_error(runtime, "WIFI SET");
        return true;
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_ANT:
        return runtime_select_bms_type(runtime, ESP_BMS_IDF_BMS_TYPE_ANT);
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_JK:
        return runtime_select_bms_type(runtime, ESP_BMS_IDF_BMS_TYPE_JK);
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_JBD:
        return runtime_select_bms_type(runtime, ESP_BMS_IDF_BMS_TYPE_JBD);
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_DALY:
        return runtime_select_bms_type(runtime, ESP_BMS_IDF_BMS_TYPE_DALY);
    case ESP_BMS_LVGL_ACTION_RESTORE_DEFAULTS:
        runtime_reset_state(runtime);
        (void)runtime_sample_battery(runtime);
        runtime_set_error(runtime, "RESTORED");
        return true;
    case ESP_BMS_LVGL_ACTION_SHOW_DASHBOARD:
    case ESP_BMS_LVGL_ACTION_SHOW_QUICK_MENU:
    case ESP_BMS_LVGL_ACTION_SHOW_SETTINGS:
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
    case ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY:
        return "rotate-display";
    case ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_UNIT:
        return "toggle-speed-unit";
    case ESP_BMS_LVGL_ACTION_TOGGLE_LANGUAGE:
        return "toggle-language";
    case ESP_BMS_LVGL_ACTION_START_BMS_BIND:
        return "start-bms-bind";
    case ESP_BMS_LVGL_ACTION_ENABLE_BLUETOOTH_ADVERTISING:
        return "enable-bluetooth-advertising";
    case ESP_BMS_LVGL_ACTION_SCAN_WIFI:
        return "scan-wifi";
    case ESP_BMS_LVGL_ACTION_CONNECT_WIFI:
        return "connect-wifi";
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_ANT:
        return "select-bms-ant";
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_JK:
        return "select-bms-jk";
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_JBD:
        return "select-bms-jbd";
    case ESP_BMS_LVGL_ACTION_SELECT_BMS_DALY:
        return "select-bms-daly";
    case ESP_BMS_LVGL_ACTION_RESTORE_DEFAULTS:
        return "restore-defaults";
    case ESP_BMS_LVGL_ACTION_NONE:
    default:
        return "none";
    }
}
