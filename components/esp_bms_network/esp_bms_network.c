#include "esp_bms_network.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "esp_bms_idf_runtime.h"

static const char *TAG = "esp_bms_network";

#define SETUP_AP_SSID_PREFIX "fuckingBms_"
#define SETUP_AP_SSID_SUFFIX_LEN 6U
#define SETUP_AP_PASSWORD_LEN 8U
#define SETUP_AP_CHANNEL 1U
#define SETUP_AP_MAX_CONNECTIONS 1U
#define HTTP_SERVER_TASK_PRIORITY 3U

#define RUNTIME_FLAG(runtime, name) \
    esp_bms_idf_runtime_flag_get((runtime), ESP_BMS_IDF_RUNTIME_FLAG_##name)
#define RUNTIME_SET_FLAG(runtime, name, enabled) \
    esp_bms_idf_runtime_flag_set((runtime), ESP_BMS_IDF_RUNTIME_FLAG_##name, (enabled))
#define RUNTIME_SET_SNAPSHOT_FLAG(runtime, name, enabled) \
    esp_bms_dashboard_snapshot_flag_set(&(runtime)->snapshot, \
                                         ESP_BMS_DASHBOARD_FLAG_##name, \
                                         (enabled))

static esp_netif_t *s_setup_ap_netif;
static httpd_handle_t s_http_server;

extern const char web_index_html_start[] asm("_binary_index_html_start");
extern const char web_index_html_end[] asm("_binary_index_html_end");

static void network_set_error(esp_bms_idf_runtime_t *runtime, const char *text)
{
    if (!runtime || !text) {
        return;
    }
    strncpy(runtime->snapshot.bms_error_text, text, sizeof(runtime->snapshot.bms_error_text) - 1U);
    runtime->snapshot.bms_error_text[sizeof(runtime->snapshot.bms_error_text) - 1U] = '\0';
}

static bool network_credentials_valid(const esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return false;
    }

    const size_t prefix_len = strlen(SETUP_AP_SSID_PREFIX);
    const size_t ssid_len = strlen(runtime->setup_ap_ssid);
    if (ssid_len != prefix_len + SETUP_AP_SSID_SUFFIX_LEN ||
        memcmp(runtime->setup_ap_ssid, SETUP_AP_SSID_PREFIX, prefix_len) != 0 ||
        strlen(runtime->setup_ap_password) != SETUP_AP_PASSWORD_LEN) {
        return false;
    }
    for (size_t index = prefix_len; index < ssid_len; ++index) {
        const char value = runtime->setup_ap_ssid[index];
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) {
            return false;
        }
    }
    for (size_t index = 0U; index < SETUP_AP_PASSWORD_LEN; ++index) {
        if (runtime->setup_ap_password[index] < '0' || runtime->setup_ap_password[index] > '9') {
            return false;
        }
    }
    return true;
}

static esp_err_t network_configure_setup_ap_ip(esp_netif_t *netif)
{
    esp_err_t ret = esp_netif_dhcps_stop(netif);
    if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        return ret;
    }

    esp_netif_ip_info_t ip_info = { 0 };
    esp_netif_set_ip4_addr(&ip_info.ip, 192, 168, 4, 1);
    esp_netif_set_ip4_addr(&ip_info.gw, 192, 168, 4, 1);
    esp_netif_set_ip4_addr(&ip_info.netmask, 255, 255, 255, 0);
    ESP_RETURN_ON_ERROR(esp_netif_set_ip_info(netif, &ip_info), TAG, "setup AP IP config failed");

    ret = esp_netif_dhcps_start(netif);
    return ret == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED ? ESP_OK : ret;
}

static void network_wifi_event_handler(void *arg,
                                       esp_event_base_t event_base,
                                       int32_t event_id,
                                       void *event_data)
{
    esp_bms_idf_runtime_t *runtime = (esp_bms_idf_runtime_t *)arg;
    if (!runtime || event_base != WIFI_EVENT) {
        return;
    }

    if (event_id == WIFI_EVENT_AP_START) {
        RUNTIME_SET_FLAG(runtime, SETUP_AP_STARTED, true);
        ESP_LOGI(TAG, "[wifi] AP started: ip=192.168.4.1 dhcp=on");
    } else if (event_id == WIFI_EVENT_AP_STOP) {
        RUNTIME_SET_FLAG(runtime, SETUP_AP_STARTED, false);
        runtime->setup_ap_clients = 0U;
        ESP_LOGI(TAG, "[wifi] AP stopped");
    } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *event = (const wifi_event_ap_staconnected_t *)event_data;
        if (runtime->setup_ap_clients < UINT8_MAX) {
            runtime->setup_ap_clients++;
        }
        ESP_LOGI(TAG, "[wifi] AP client connected: clients=%u first_mac=" MACSTR,
                 runtime->setup_ap_clients, MAC2STR(event->mac));
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *event = (const wifi_event_ap_stadisconnected_t *)event_data;
        if (runtime->setup_ap_clients > 0U) {
            runtime->setup_ap_clients--;
        }
        ESP_LOGI(TAG, "[wifi] AP client disconnected: clients=%u mac=" MACSTR " reason=%u",
                 runtime->setup_ap_clients, MAC2STR(event->mac), event->reason);
    }
}

static void network_ip_event_handler(void *arg,
                                     esp_event_base_t event_base,
                                     int32_t event_id,
                                     void *event_data)
{
    (void)arg;
    if (event_base == IP_EVENT && event_id == IP_EVENT_ASSIGNED_IP_TO_CLIENT) {
        const ip_event_assigned_ip_to_client_t *event = (const ip_event_assigned_ip_to_client_t *)event_data;
        char ip[16] = { 0 };
        ESP_LOGI(TAG, "[wifi] DHCP lease assigned: ip=%s mac=" MACSTR,
                 esp_ip4addr_ntoa(&event->ip, ip, sizeof(ip)), MAC2STR(event->mac));
    }
}

static esp_err_t network_register_wifi_handlers(esp_bms_idf_runtime_t *runtime)
{
    if (RUNTIME_FLAG(runtime, WIFI_HANDLERS_REGISTERED)) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            network_wifi_event_handler,
                                                            runtime,
                                                            NULL),
                        TAG,
                        "Wi-Fi event handler registration failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            network_ip_event_handler,
                                                            runtime,
                                                            NULL),
                        TAG,
                        "IP event handler registration failed");
    RUNTIME_SET_FLAG(runtime, WIFI_HANDLERS_REGISTERED, true);
    return ESP_OK;
}

static esp_err_t network_init_wifi_stack(esp_bms_idf_runtime_t *runtime)
{
    if (!RUNTIME_FLAG(runtime, WIFI_STACK_READY)) {
        esp_err_t ret = esp_netif_init();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            return ret;
        }
        ret = esp_event_loop_create_default();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            return ret;
        }
        s_setup_ap_netif = esp_netif_create_default_wifi_ap();
        if (!s_setup_ap_netif) {
            return ESP_ERR_NO_MEM;
        }
        ESP_RETURN_ON_ERROR(network_configure_setup_ap_ip(s_setup_ap_netif), TAG, "setup AP IP config failed");
        RUNTIME_SET_FLAG(runtime, WIFI_STACK_READY, true);
    }
    if (!RUNTIME_FLAG(runtime, WIFI_DRIVER_READY)) {
        wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
        ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "esp_wifi_init failed");
        RUNTIME_SET_FLAG(runtime, WIFI_DRIVER_READY, true);
    }
    return network_register_wifi_handlers(runtime);
}

static esp_err_t network_apply_setup_ap_wifi_config(esp_bms_idf_runtime_t *runtime)
{
    if (!network_credentials_valid(runtime)) {
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

static esp_err_t network_root_handler(httpd_req_t *req)
{
    const size_t html_size = (size_t)(web_index_html_end - web_index_html_start);
    const size_t html_len = html_size > 0U && web_index_html_start[html_size - 1U] == '\0'
                                ? html_size - 1U
                                : html_size;
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "text/html; charset=utf-8"), TAG, "set HTTP type failed");
    return httpd_resp_send(req, web_index_html_start, (ssize_t)html_len);
}

static esp_err_t network_start_setup_ap(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    if (RUNTIME_FLAG(runtime, SETUP_AP_STARTED)) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "[wifi] starting setup AP: ssid='%s' ap_pw_len=%u",
             runtime->setup_ap_ssid, (unsigned)strlen(runtime->setup_ap_password));
    esp_err_t ret = network_init_wifi_stack(runtime);
    if (ret == ESP_OK) {
        ret = esp_wifi_set_mode(WIFI_MODE_AP);
    }
    if (ret == ESP_OK) {
        ret = network_apply_setup_ap_wifi_config(runtime);
    }
    if (ret == ESP_OK) {
        ret = esp_wifi_start();
    }
    if (ret != ESP_OK) {
        runtime->snapshot.wifi = ESP_BMS_WIFI_OFFLINE;
        RUNTIME_SET_SNAPSHOT_FLAG(runtime, SETUP_AP_ENABLED, false);
        network_set_error(runtime, "AP FAIL");
        ESP_LOGE(TAG, "[wifi] AP start failed: %s", esp_err_to_name(ret));
        return ret;
    }
    RUNTIME_SET_FLAG(runtime, SETUP_AP_STARTED, true);
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, SETUP_AP_ENABLED, true);
    runtime->snapshot.wifi = ESP_BMS_WIFI_SETUP_AP;
    network_set_error(runtime, "AP READY");
    return ESP_OK;
}

static esp_err_t network_start_http_server(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!RUNTIME_FLAG(runtime, SETUP_AP_STARTED)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (RUNTIME_FLAG(runtime, HTTP_SERVER_STARTED)) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 4;
    config.max_uri_handlers = 5;
    config.stack_size = 4096;
    config.task_priority = HTTP_SERVER_TASK_PRIORITY;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t ret = httpd_start(&s_http_server, &config);
    if (ret != ESP_OK) {
        return ret;
    }
    const httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = network_root_handler, .user_ctx = runtime };
    const httpd_uri_t api = { .uri = "/api/*", .method = HTTP_ANY,
                              .handler = esp_bms_idf_runtime_http_api_handler, .user_ctx = runtime };
    const httpd_uri_t cast = { .uri = "/cast", .method = HTTP_GET,
                               .handler = esp_bms_idf_runtime_http_cast_ws_handler,
                               .user_ctx = runtime, .is_websocket = true };
    ret = httpd_register_uri_handler(s_http_server, &root);
    if (ret == ESP_OK) {
        ret = httpd_register_uri_handler(s_http_server, &api);
    }
    if (ret == ESP_OK) {
        ret = httpd_register_uri_handler(s_http_server, &cast);
    }
    if (ret != ESP_OK) {
        httpd_stop(s_http_server);
        s_http_server = NULL;
        return ret;
    }
    RUNTIME_SET_FLAG(runtime, HTTP_SERVER_STARTED, true);
    network_set_error(runtime, "HTTP ON");
    ESP_LOGI(TAG, "[http] server started: port=80 routes=/,/api/*,/cast");
    return ESP_OK;
}

static esp_err_t network_stop_setup_services(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = ESP_OK;
    esp_bms_idf_runtime_stop_cast(runtime, "setup AP stopped");
    if (s_http_server) {
        const esp_err_t http_ret = httpd_stop(s_http_server);
        if (http_ret != ESP_OK) {
            result = http_ret;
        }
        s_http_server = NULL;
    }
    RUNTIME_SET_FLAG(runtime, HTTP_SERVER_STARTED, false);
    if (RUNTIME_FLAG(runtime, SETUP_AP_STARTED)) {
        const esp_err_t wifi_ret = esp_wifi_stop();
        if (wifi_ret != ESP_OK && result == ESP_OK) {
            result = wifi_ret;
        }
    }
    RUNTIME_SET_FLAG(runtime, SETUP_AP_STARTED, false);
    runtime->setup_ap_clients = 0U;
    RUNTIME_SET_SNAPSHOT_FLAG(runtime, SETUP_AP_ENABLED, false);
    runtime->snapshot.wifi = ESP_BMS_WIFI_OFFLINE;
    network_set_error(runtime, "AP OFF");
    return result;
}

static const esp_bms_idf_runtime_network_driver_t s_network_driver = {
    .start_setup_ap = network_start_setup_ap,
    .start_http_server = network_start_http_server,
    .stop_setup_services = network_stop_setup_services,
    .refresh_setup_ap_config = network_apply_setup_ap_wifi_config,
};

esp_err_t esp_bms_network_init(esp_bms_idf_runtime_t *runtime)
{
    if (!runtime) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_bms_idf_runtime_register_network_driver(runtime, &s_network_driver);
    return ESP_OK;
}
