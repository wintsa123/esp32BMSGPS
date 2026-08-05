#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t esp_bms_classic_media_hid_start(void);
esp_err_t esp_bms_classic_media_hid_set_discoverable(bool discoverable);
bool esp_bms_classic_media_hid_tick(bool *connected, bool *suspended, bool *discoverable);
esp_err_t esp_bms_classic_media_hid_send_usage(uint16_t consumer_usage);

#ifdef __cplusplus
}
#endif
