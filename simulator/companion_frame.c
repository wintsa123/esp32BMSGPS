#include "companion_frame.h"
#include "esp_bms_lvgl_ui.h"
#include <stdio.h>
#include <stdlib.h>

/* Optional test input produced by the Android instrumentation renderer. */
bool companion_frame_load(void)
{
    const char *path = getenv("ESP_BMS_COMPANION_FRAME");
    if (!path || !*path) return true;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    uint8_t header[8], bytes[128];
    bool ok = fread(header, 1, sizeof(header), file) == sizeof(header);
    const unsigned slot = ok ? header[4] : 2;
    const uint16_t width = ok ? header[0] | ((uint16_t)header[1] << 8) : 0;
    const uint16_t height = ok ? header[2] | ((uint16_t)header[3] << 8) : 0;
    ok = ok && esp_bms_lvgl_ui_companion(slot, "select", 1, 0, 0, 0, NULL, 0) == ESP_OK;
    ok = ok && esp_bms_lvgl_ui_companion(slot, "begin", 1, width, height, 0, NULL, 0) == ESP_OK;
    size_t offset = 0, count;
    while (ok && (count = fread(bytes, 1, sizeof(bytes), file)) != 0) {
        ok = offset + count <= 200000U &&
             esp_bms_lvgl_ui_companion(slot, "chunk", 1, 0, 0, offset, bytes, count) == ESP_OK;
        offset += count;
    }
    ok = ok && !ferror(file) && esp_bms_lvgl_ui_companion(slot, "commit", 1, 0, 0, 0, NULL, 0) == ESP_OK;
    fclose(file);
    return ok;
}
