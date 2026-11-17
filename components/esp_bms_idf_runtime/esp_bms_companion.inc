static bool runtime_companion_info(char *json, size_t length)
{
    if (esp_bms_lvgl_bridge_lock(1000) != ESP_OK) return false;
    lv_display_t *display = lv_display_get_default();
    const int width = display ? lv_display_get_horizontal_resolution(display) : 0;
    const int height = display ? lv_display_get_vertical_resolution(display) : 0;
    const int page = esp_bms_lvgl_ui_companion_page();
    uint32_t action_id = 0, action = 0;
    static uint32_t boot_id;
    if (!boot_id) boot_id = esp_random() | 1U;
    esp_bms_lvgl_ui_companion_action(&action_id, &action);
    esp_bms_lvgl_bridge_unlock();
    const int n = snprintf(json, length,
        "{\"version\":1,\"boot_id\":%lu,\"action_id\":%lu,\"action\":%lu,\"page\":%d,\"width\":%d,\"height\":%d,\"music_height\":%d,\"encoding\":\"rle4\",\"chunk_bytes\":128}",
        (unsigned long)boot_id, (unsigned long)action_id, (unsigned long)action, page, width, height, height > 120 ? height - 120 : 0);
    return n > 0 && (size_t)n < length;
}

static int runtime_companion_post(const char *body)
{
    char operation[12] = {0}, hex[257] = {0};
    uint32_t slot = 0, frame = 0, width = 0, height = 0, offset = 0;
    bool found = false;
    if (!runtime_json_get_string(body, "op", operation, sizeof(operation), &found) || !found ||
        !runtime_json_get_u32(body, "slot", &slot, &found) || !found || slot > 1 ||
        !runtime_json_get_u32(body, "frame", &frame, &found) ||
        !runtime_json_get_u32(body, "width", &width, &found) || width > 480 ||
        !runtime_json_get_u32(body, "height", &height, &found) || height > 480 ||
        !runtime_json_get_u32(body, "offset", &offset, &found) ||
        !runtime_json_get_string(body, "data", hex, sizeof(hex), &found)) return 400;
    const size_t n = strlen(hex);
    uint8_t data[128];
    if (n & 1U) return 400;
    for (size_t i = 0; i < n; ++i) {
        const char c = hex[i];
        const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (v < 0) return 400;
        if (!(i & 1U)) data[i / 2] = v << 4;
        else data[i / 2] |= v;
    }
    if (esp_bms_lvgl_bridge_lock(1000) != ESP_OK) return 503;
    const esp_err_t result = esp_bms_lvgl_ui_companion(slot, operation, frame,
        (uint16_t)width, (uint16_t)height, offset, data, n / 2U);
    esp_bms_lvgl_bridge_unlock();
    return result == ESP_OK ? 204 : result == ESP_ERR_NO_MEM ? 503 : 400;
}
