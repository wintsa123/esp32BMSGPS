#include "esp_bms_classic_media_hid.h"

#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_check.h"
#include "esp_gap_bt_api.h"
#include "esp_hid_common.h"
#include "esp_hidd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <inttypes.h>
#include <string.h>

static const char *TAG = "classic_media_hid";

#define CLASSIC_MEDIA_HID_REPORT_ID 1U
#define CLASSIC_MEDIA_HID_REPORT_LEN 2U
#define CLASSIC_MEDIA_HID_RELEASE_MS 30U

enum {
    CLASSIC_MEDIA_USAGE_SCAN_NEXT_TRACK = 0x00B5U,
    CLASSIC_MEDIA_USAGE_SCAN_PREVIOUS_TRACK = 0x00B6U,
    CLASSIC_MEDIA_USAGE_PLAY_PAUSE = 0x00CDU,
    CLASSIC_MEDIA_USAGE_VOLUME_INCREMENT = 0x00E9U,
    CLASSIC_MEDIA_USAGE_VOLUME_DECREMENT = 0x00EAU,
};

static const uint8_t CLASSIC_MEDIA_HID_REPORT_MAP[] = {
    0x05U, 0x0CU,
    0x09U, 0x01U,
    0xA1U, 0x01U,
    0x85U, CLASSIC_MEDIA_HID_REPORT_ID,
    0x15U, 0x00U,
    0x26U, 0xFFU, 0x03U,
    0x19U, 0x00U,
    0x2AU, 0xFFU, 0x03U,
    0x75U, 0x10U,
    0x95U, 0x01U,
    0x81U, 0x00U,
    0xC0U,
};

static esp_hid_raw_report_map_t s_report_maps[] = {
    {
        .data = CLASSIC_MEDIA_HID_REPORT_MAP,
        .len = sizeof(CLASSIC_MEDIA_HID_REPORT_MAP),
    },
};

static const esp_hid_device_config_t s_hid_config = {
    .vendor_id = 0x16C0,
    .product_id = 0x05DF,
    .version = 0x0100,
    .device_name = "ESP32 BMS GPS Media",
    .manufacturer_name = "Espressif",
    .serial_number = "classic-media-spike",
    .report_maps = s_report_maps,
    .report_maps_len = 1,
};

static esp_hidd_dev_t *s_hid_dev;
static bool s_started;
static bool s_connected;
static bool s_suspended;
static bool s_discoverable;
static bool s_discoverable_requested;
static bool s_state_dirty = true;

static esp_err_t classic_media_hid_apply_scan_mode(bool discoverable)
{
    const esp_err_t ret = esp_bt_gap_set_scan_mode(
        discoverable ? ESP_BT_CONNECTABLE : ESP_BT_NON_CONNECTABLE,
        discoverable ? ESP_BT_GENERAL_DISCOVERABLE : ESP_BT_NON_DISCOVERABLE);
    if (ret == ESP_OK && s_discoverable != discoverable) {
        s_discoverable = discoverable;
        s_state_dirty = true;
    }
    return ret;
}

static esp_err_t classic_media_hid_init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase failed");
        ret = nvs_flash_init();
    }
    return ret == ESP_ERR_INVALID_STATE ? ESP_OK : ret;
}

static bool classic_media_hid_report_from_usage(uint16_t usage,
                                                uint8_t report[CLASSIC_MEDIA_HID_REPORT_LEN])
{
    if (!report) {
        return false;
    }
    switch (usage) {
    case CLASSIC_MEDIA_USAGE_SCAN_NEXT_TRACK:
    case CLASSIC_MEDIA_USAGE_SCAN_PREVIOUS_TRACK:
    case CLASSIC_MEDIA_USAGE_PLAY_PAUSE:
    case CLASSIC_MEDIA_USAGE_VOLUME_INCREMENT:
    case CLASSIC_MEDIA_USAGE_VOLUME_DECREMENT:
        report[0] = (uint8_t)(usage & UINT8_C(0xFF));
        report[1] = (uint8_t)(usage >> 8U);
        return true;
    default:
        report[0] = 0U;
        report[1] = 0U;
        return false;
    }
}

static void classic_media_hid_mark_state(bool connected, bool suspended)
{
    if (s_connected != connected || s_suspended != suspended) {
        s_state_dirty = true;
    }
    s_connected = connected;
    s_suspended = suspended;
}

static void classic_media_hid_gap_callback(esp_bt_gap_cb_event_t event,
                                           esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "pairing complete: %s", param->auth_cmpl.device_name);
        } else {
            ESP_LOGW(TAG, "pairing failed: status=%d", param->auth_cmpl.stat);
        }
        break;
    case ESP_BT_GAP_PIN_REQ_EVT: {
        esp_bt_pin_code_t pin_code = { 0 };
        /* Standard HID practice: answer legacy PIN requests with "0000" so the
         * phone pairs without user interaction (like a consumer headset). */
        pin_code[0] = '0';
        pin_code[1] = '0';
        pin_code[2] = '0';
        pin_code[3] = '0';
        ESP_LOGI(TAG,
                 "legacy PIN requested; replying 0000 (no-PIN pairing): " ESP_BD_ADDR_STR,
                 ESP_BD_ADDR_HEX(param->pin_req.bda));
        (void)esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        break;
    }
    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(TAG, "SSP confirmation: value=%06" PRIu32, param->cfm_req.num_val);
        (void)esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(TAG, "SSP passkey notification: %06" PRIu32, param->key_notif.passkey);
        break;
    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGW(TAG, "SSP passkey entry requested; no keyboard is available");
        break;
    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI(TAG, "scan mode changed: mode=%d", param->mode_chg.mode);
        break;
    default:
        break;
    }
}

static void classic_media_hid_event_callback(void *handler_args,
                                             esp_event_base_t base,
                                             int32_t id,
                                             void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;
    switch ((esp_hidd_event_t)id) {
    case ESP_HIDD_START_EVENT:
        if (param->start.status == ESP_OK) {
            ESP_LOGI(TAG, "Classic HID started; manual discoverable control ready");
            s_started = true;
            (void)classic_media_hid_apply_scan_mode(s_discoverable_requested);
        } else {
            ESP_LOGE(TAG, "Classic HID start failed: %s", esp_err_to_name(param->start.status));
        }
        break;
    case ESP_HIDD_CONNECT_EVENT:
        if (param->connect.status == ESP_OK) {
            ESP_LOGI(TAG, "Classic HID connected");
            s_discoverable_requested = false;
            (void)classic_media_hid_apply_scan_mode(false);
            classic_media_hid_mark_state(true, false);
        } else {
            ESP_LOGW(TAG, "Classic HID connect failed: %s", esp_err_to_name(param->connect.status));
        }
        break;
    case ESP_HIDD_CONTROL_EVENT:
        classic_media_hid_mark_state(s_connected, param->control.control == ESP_HID_CONTROL_SUSPEND);
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        if (param->disconnect.status == ESP_OK) {
            ESP_LOGI(TAG, "Classic HID disconnected");
            classic_media_hid_mark_state(false, false);
            (void)classic_media_hid_apply_scan_mode(s_discoverable_requested);
        } else {
            ESP_LOGW(TAG, "Classic HID disconnect failed: %s",
                     esp_err_to_name(param->disconnect.status));
        }
        break;
    case ESP_HIDD_STOP_EVENT:
        ESP_LOGI(TAG, "Classic HID stopped");
        s_started = false;
        classic_media_hid_mark_state(false, false);
        break;
    default:
        break;
    }
}

esp_err_t esp_bms_classic_media_hid_start(void)
{
    if (s_started || s_hid_dev) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(classic_media_hid_init_nvs(), TAG, "NVS init failed");
    esp_err_t ret = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(ret, TAG, "BLE memory release failed");
    }

    esp_bt_controller_config_t bt_config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
#if CONFIG_IDF_TARGET_ESP32
    bt_config.mode = ESP_BT_MODE_CLASSIC_BT;
#endif
    ESP_RETURN_ON_ERROR(esp_bt_controller_init(&bt_config), TAG, "BT controller init failed");
    ESP_RETURN_ON_ERROR(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT),
                        TAG,
                        "BT controller enable failed");

    esp_bluedroid_config_t bluedroid_config = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    bluedroid_config.ssp_en = true;
    ESP_RETURN_ON_ERROR(esp_bluedroid_init_with_cfg(&bluedroid_config),
                        TAG,
                        "Bluedroid init failed");
    ESP_RETURN_ON_ERROR(esp_bluedroid_enable(), TAG, "Bluedroid enable failed");
    ESP_RETURN_ON_ERROR(esp_bt_gap_register_callback(classic_media_hid_gap_callback),
                        TAG,
                        "GAP callback register failed");

    esp_bt_io_cap_t io_capability = ESP_BT_IO_CAP_NONE;
    ESP_RETURN_ON_ERROR(esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE,
                                                       &io_capability,
                                                       sizeof(io_capability)),
                        TAG,
                        "SSP IO capability failed");
    esp_bt_pin_code_t pin_code = { 0 };
    ESP_RETURN_ON_ERROR(esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_VARIABLE, 0, pin_code),
                        TAG,
                        "legacy PIN policy failed");
    ESP_RETURN_ON_ERROR(esp_bt_gap_set_device_name(s_hid_config.device_name),
                        TAG,
                        "device name failed");

    esp_bt_cod_t cod = { 0 };
    cod.major = ESP_BT_COD_MAJOR_DEV_PERIPHERAL;
    cod.minor = ESP_BT_COD_MINOR_PERIPHERAL_REMOTE_CONTROL;
    ESP_RETURN_ON_ERROR(esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_MAJOR_MINOR),
                        TAG,
                        "class of device failed");

    ESP_RETURN_ON_ERROR(esp_hidd_dev_init(&s_hid_config,
                                          ESP_HID_TRANSPORT_BT,
                                          classic_media_hid_event_callback,
                                          &s_hid_dev),
                        TAG,
                        "HID device init failed");
    ESP_LOGI(TAG, "Classic HID manual media controls ready");
    ESP_LOG_BUFFER_HEX(TAG, esp_bt_dev_get_address(), ESP_BD_ADDR_LEN);
    return ESP_OK;
}

esp_err_t esp_bms_classic_media_hid_set_discoverable(bool discoverable)
{
    s_discoverable_requested = discoverable;
    if (s_connected) {
        s_discoverable_requested = false;
        return classic_media_hid_apply_scan_mode(false);
    }
    if (!s_started) {
        s_state_dirty = true;
        return ESP_ERR_INVALID_STATE;
    }
    return classic_media_hid_apply_scan_mode(discoverable);
}

bool esp_bms_classic_media_hid_tick(bool *connected, bool *suspended, bool *discoverable)
{
    if (connected) {
        *connected = s_connected;
    }
    if (suspended) {
        *suspended = s_suspended;
    }
    if (discoverable) {
        *discoverable = s_discoverable;
    }
    const bool dirty = s_state_dirty;
    s_state_dirty = false;
    return dirty;
}

esp_err_t esp_bms_classic_media_hid_send_usage(uint16_t consumer_usage)
{
    uint8_t report[CLASSIC_MEDIA_HID_REPORT_LEN] = { 0 };
    uint8_t release[CLASSIC_MEDIA_HID_REPORT_LEN] = { 0 };
    if (!classic_media_hid_report_from_usage(consumer_usage, report)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_hid_dev || !s_connected || s_suspended) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(esp_hidd_dev_input_set(s_hid_dev,
                                               0,
                                               CLASSIC_MEDIA_HID_REPORT_ID,
                                               report,
                                               sizeof(report)),
                        TAG,
                        "Classic HID press failed");
    vTaskDelay(pdMS_TO_TICKS(CLASSIC_MEDIA_HID_RELEASE_MS));
    return esp_hidd_dev_input_set(s_hid_dev,
                                  0,
                                  CLASSIC_MEDIA_HID_REPORT_ID,
                                  release,
                                  sizeof(release));
}
