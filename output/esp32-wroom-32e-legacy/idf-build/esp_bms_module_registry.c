#include "esp_bms_module_registry.h"

#define ESP_BMS_FEATURE_AUDIO 0
#define ESP_BMS_FEATURE_BMS 1
#define ESP_BMS_FEATURE_CONTROLLER 1
#define ESP_BMS_FEATURE_GPS 0
#define ESP_BMS_FEATURE_NETWORK 1

#include "esp_bms_idf_runtime.h"

#if ESP_BMS_FEATURE_AUDIO
#include "esp_bms_audio_feedback.h"
#endif

#if ESP_BMS_FEATURE_BMS
#include "esp_bms_bms_ble.h"
#endif

#if ESP_BMS_FEATURE_CONTROLLER
#include "esp_bms_controller_ble.h"
#endif

#if ESP_BMS_FEATURE_GPS
#include "esp_bms_gps.h"
#endif

#if ESP_BMS_FEATURE_NETWORK
#include "esp_bms_network.h"
#endif

esp_err_t esp_bms_module_registry_init(esp_bms_idf_runtime_t *runtime)
{
    esp_err_t first_error = ESP_OK;
#if ESP_BMS_FEATURE_AUDIO
    const esp_err_t audio_ret = esp_bms_audio_feedback_init();
    if (audio_ret != ESP_OK) {
        first_error = audio_ret;
    }
#endif
#if ESP_BMS_FEATURE_BMS
    const esp_err_t bms_ret = esp_bms_bms_ble_init(runtime);
    if (first_error == ESP_OK) {
        first_error = bms_ret;
    }
#endif
#if ESP_BMS_FEATURE_CONTROLLER
    const esp_err_t controller_ret = esp_bms_controller_ble_init(runtime);
    if (first_error == ESP_OK) {
        first_error = controller_ret;
    }
#endif
#if ESP_BMS_FEATURE_GPS
    const esp_err_t gps_ret = esp_bms_gps_init(runtime);
    if (first_error == ESP_OK) {
        first_error = gps_ret;
    }
#else
    (void)runtime;
#endif
#if ESP_BMS_FEATURE_NETWORK
    const esp_err_t network_ret = esp_bms_network_init(runtime);
    if (first_error == ESP_OK) {
        first_error = network_ret;
    }
#endif
    return first_error;
}

esp_err_t esp_bms_module_registry_start(esp_bms_idf_runtime_t *runtime)
{
    esp_err_t first_error = ESP_OK;
#if ESP_BMS_FEATURE_CONTROLLER
    const esp_err_t controller_ret = esp_bms_controller_ble_start(runtime);
    if (first_error == ESP_OK) {
        first_error = controller_ret;
    }
#endif
#if ESP_BMS_FEATURE_BMS
    const esp_err_t bms_ret = esp_bms_bms_ble_start(runtime);
    if (first_error == ESP_OK) {
        first_error = bms_ret;
    }
#endif
    (void)runtime;
    return first_error;
}

bool esp_bms_module_registry_tick(esp_bms_idf_runtime_t *runtime, uint32_t elapsed_ms)
{
#if ESP_BMS_FEATURE_AUDIO
    esp_bms_audio_feedback_tick();
#endif
#if ESP_BMS_FEATURE_GPS
    return esp_bms_gps_tick(runtime, elapsed_ms);
#else
    (void)runtime;
    (void)elapsed_ms;
    return false;
#endif
}

void esp_bms_module_registry_stop(esp_bms_idf_runtime_t *runtime)
{
#if ESP_BMS_FEATURE_GPS
    esp_bms_gps_stop(runtime);
#else
    (void)runtime;
#endif
}

esp_err_t esp_bms_module_registry_start_setup_ap(esp_bms_idf_runtime_t *runtime)
{
#if ESP_BMS_FEATURE_NETWORK
    return esp_bms_idf_runtime_start_setup_ap(runtime);
#else
    (void)runtime;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t esp_bms_module_registry_start_http_server(esp_bms_idf_runtime_t *runtime)
{
#if ESP_BMS_FEATURE_NETWORK
    return esp_bms_idf_runtime_start_http_server(runtime);
#else
    (void)runtime;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t esp_bms_module_registry_stop_setup_services(esp_bms_idf_runtime_t *runtime)
{
#if ESP_BMS_FEATURE_NETWORK
    return esp_bms_idf_runtime_stop_setup_services(runtime);
#else
    (void)runtime;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool esp_bms_module_registry_gps_enabled(void)
{
    return ESP_BMS_FEATURE_GPS != 0;
}

bool esp_bms_module_registry_gps_finish_startup_probe(esp_bms_idf_runtime_t *runtime)
{
#if ESP_BMS_FEATURE_GPS
    return esp_bms_gps_finish_startup_probe(runtime);
#else
    (void)runtime;
    return false;
#endif
}

bool esp_bms_module_registry_gps_is_available(const esp_bms_idf_runtime_t *runtime)
{
#if ESP_BMS_FEATURE_GPS
    return esp_bms_gps_module_state(runtime) == ESP_BMS_GPS_MODULE_AVAILABLE;
#else
    (void)runtime;
    return false;
#endif
}

void esp_bms_module_registry_play_connection_audio(uint8_t events, uint8_t volume_percent)
{
#if ESP_BMS_FEATURE_AUDIO
    if ((events & ESP_BMS_IDF_RUNTIME_AUDIO_EVENT_BMS_CONNECTED) != 0U) {
        esp_bms_audio_feedback_play_voice(ESP_BMS_AUDIO_VOICE_BMS_CONNECTED, volume_percent);
    }
    if ((events & ESP_BMS_IDF_RUNTIME_AUDIO_EVENT_CONTROLLER_CONNECTED) != 0U) {
        esp_bms_audio_feedback_play_voice(ESP_BMS_AUDIO_VOICE_CONTROLLER_CONNECTED, volume_percent);
    }
#else
    (void)events;
    (void)volume_percent;
#endif
}

void esp_bms_module_registry_play_volume_audio(uint8_t volume_percent)
{
#if ESP_BMS_FEATURE_AUDIO
    esp_bms_audio_feedback_play_volume(volume_percent);
#else
    (void)volume_percent;
#endif
}
