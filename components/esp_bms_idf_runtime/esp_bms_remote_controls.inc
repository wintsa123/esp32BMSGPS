/* Remote requests are validated here and executed by the existing main-loop action queue. */
static bool runtime_action_feature_enabled(esp_bms_lvgl_action_t action);
typedef struct {
    const char *group, *id, *zh, *en;
    esp_bms_lvgl_action_t action;
    int min, max, step;
    bool confirm;
} remote_control_t;
static const remote_control_t REMOTE_CONTROLS[] = {
    {"display", "dashboard_style", "仪表样式（0 宝马 / 1 控制器 / 2 本田）", "Dashboard style (0 BMW / 1 controller / 2 Honda)", ESP_BMS_LVGL_ACTION_SET_SPEED_DASHBOARD_STYLE, 0, 2, 1, false},
    {"display", "boot_style", "开机动画（0 充电 / 1 宝马 / 2 本田）", "Boot animation (0 charge / 1 BMW / 2 Honda)", ESP_BMS_LVGL_ACTION_SET_BOOT_ANIMATION_STYLE, 0, 2, 1, false},
    {"vehicle", "range", "满电预估续航 (km)", "Full-charge range (km)", ESP_BMS_LVGL_ACTION_SET_PRESET_RANGE, 0, 9999, 1, false},
    {"vehicle", "tire_rim", "轮毂直径 (英寸)", "Rim diameter (inch)", ESP_BMS_LVGL_ACTION_SET_CONTROLLER_TIRE, 8, 24, 1, false},
    {"vehicle", "tire_width", "轮胎宽度 (mm)", "Tire width (mm)", ESP_BMS_LVGL_ACTION_SET_CONTROLLER_TIRE, 50, 200, 5, false},
    {"vehicle", "tire_aspect", "轮胎扁平比 (%)", "Tire aspect (%)", ESP_BMS_LVGL_ACTION_SET_CONTROLLER_TIRE, 30, 100, 5, false},
    {"vehicle", "ratio", "传动比 × 100", "Gear ratio × 100", ESP_BMS_LVGL_ACTION_SET_CONTROLLER_RATIO, 50, 1000, 1, false},
    {"vehicle", "controller_connection", "切换控制器连接", "Toggle controller connection", ESP_BMS_LVGL_ACTION_TOGGLE_CONTROLLER_CONNECTION, 0, 0, 0, false},
    {"vehicle", "controller_scan", "扫描控制器", "Scan controller", ESP_BMS_LVGL_ACTION_START_CONTROLLER_BIND, 0, 0, 0, false},
    {"vehicle", "bms_disconnect", "断开保护板", "Disconnect BMS", ESP_BMS_LVGL_ACTION_CANCEL_BMS_CONNECTION, 0, 0, 0, true},
    {"connection", "wifi", "开关设备热点", "Toggle hotspot", ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING, 0, 0, 0, true},
    {"connection", "bluetooth", "开启蓝牙广播", "Enable Bluetooth advertising", ESP_BMS_LVGL_ACTION_ENABLE_BLUETOOTH_ADVERTISING, 0, 0, 0, false},
    {"media", "previous", "上一曲", "Previous", ESP_BMS_LVGL_ACTION_PHONE_MEDIA_PREVIOUS, 0, 0, 0, false},
    {"media", "play_pause", "播放 / 暂停 / 通话接挂", "Play / pause / answer / hang up", ESP_BMS_LVGL_ACTION_MEDIA_PLAY_PAUSE, 0, 0, 0, false},
    {"media", "next", "下一曲", "Next", ESP_BMS_LVGL_ACTION_PHONE_MEDIA_NEXT, 0, 0, 0, false},
    {"media", "volume_down", "手机音量减", "Phone volume down", ESP_BMS_LVGL_ACTION_PHONE_MEDIA_VOLUME_DOWN, 0, 0, 0, false},
    {"media", "volume_up", "手机音量加", "Phone volume up", ESP_BMS_LVGL_ACTION_PHONE_MEDIA_VOLUME_UP, 0, 0, 0, false},
    {"system", "restore", "恢复默认设置", "Restore defaults", ESP_BMS_LVGL_ACTION_RESTORE_DEFAULTS, 0, 0, 0, true},
    {"display", "dashboard", "显示仪表首页", "Show dashboard", ESP_BMS_LVGL_ACTION_SHOW_DASHBOARD, 0, 0, 0, false},
    {"display", "settings", "显示设备设置", "Show device settings", ESP_BMS_LVGL_ACTION_SHOW_SETTINGS, 0, 0, 0, false},
    {"display", "quick_menu", "开关快捷菜单", "Toggle quick menu", ESP_BMS_LVGL_ACTION_SHOW_QUICK_MENU, 0, 0, 0, false},
    {"display", "level_position", "切换调节条位置", "Cycle slider position", ESP_BMS_LVGL_ACTION_CYCLE_LEVEL_POSITION, 0, 0, 0, false},
    {"system", "calibrate", "开始触屏校准（在设备上点选）", "Start calibration (tap device targets)", ESP_BMS_LVGL_ACTION_START_TOUCH_CALIBRATION, 0, 0, 0, false},
    {"system", "calibrate_cancel", "取消触屏校准", "Cancel calibration", ESP_BMS_LVGL_ACTION_CANCEL_TOUCH_CALIBRATION, 0, 0, 0, false},
};
static int runtime_remote_value(esp_bms_idf_runtime_t *runtime, const char *id)
{
    if (strcmp(id, "dashboard_style") == 0) return runtime->snapshot.speed_dashboard_style;
    if (strcmp(id, "boot_style") == 0) return runtime->snapshot.boot_animation_style;
    if (strcmp(id, "range") == 0) return runtime->snapshot.preset_range_km;
    if (strcmp(id, "tire_rim") == 0) return runtime->controller_fallback_tire_rim_inch;
    if (strcmp(id, "tire_width") == 0) return runtime->controller_fallback_tire_width_mm;
    if (strcmp(id, "tire_aspect") == 0) return runtime->controller_fallback_tire_aspect_percent;
    if (strcmp(id, "ratio") == 0) return runtime->controller_state.fallback_gear_ratio_centi;
    return 0;
}
static bool runtime_remote_controls_json(esp_bms_idf_runtime_t *runtime, const char *path, char *json, size_t len)
{
    const char *group = strchr(path, '?');
    if (!group || strncmp(group, "?group=", 7) != 0) return false;
    group += 7;
    size_t offset = 0;
    bool first = true;
    bool ok = runtime_json_append(json, len, &offset, "{\"controls\":[");
    for (size_t i = 0; i < sizeof(REMOTE_CONTROLS) / sizeof(REMOTE_CONTROLS[0]); ++i) {
        const remote_control_t *c = &REMOTE_CONTROLS[i];
        if (strcmp(c->group, group) != 0 || !runtime_action_feature_enabled(c->action)) continue;
        ok = ok && runtime_json_append(json, len, &offset,
            "%s{\"id\":\"%s\",\"zh\":\"%s\",\"en\":\"%s\",\"confirm\":%s",
            first ? "" : ",", c->id, c->zh, c->en, c->confirm ? "true" : "false");
        first = false;
        if (c->step) ok = ok && runtime_json_append(json, len, &offset,
            ",\"value\":%d,\"min\":%d,\"max\":%d,\"step\":%d",
            runtime_remote_value(runtime, c->id), c->min, c->max, c->step);
        ok = ok && runtime_json_append(json, len, &offset, "}");
    }
    return ok && runtime_json_append(json, len, &offset, "]}");
}
static int runtime_remote_control(esp_bms_idf_runtime_t *runtime, const char *body, const char **error)
{
    char id[32] = {0};
    bool found = false;
    *error = "invalid control";
    if (!runtime_json_get_string(body, "action", id, sizeof(id), &found) || !found) return 400;
    const remote_control_t *control = NULL;
    for (size_t i = 0; i < sizeof(REMOTE_CONTROLS) / sizeof(REMOTE_CONTROLS[0]); ++i) {
        if (strcmp(id, REMOTE_CONTROLS[i].id) == 0) control = &REMOTE_CONTROLS[i];
    }
    if (!control || !runtime_action_feature_enabled(control->action)) return 400;
    uint32_t value = 0;
    if (control->step && (!runtime_json_get_u32(body, "value", &value, &found) || !found ||
        value < (uint32_t)control->min || value > (uint32_t)control->max ||
        (value - control->min) % control->step != 0)) return 400;
    if (control->action == ESP_BMS_LVGL_ACTION_SET_SPEED_DASHBOARD_STYLE && !runtime_speed_dashboard_style_matches_policy(value)) return 400;
    if (control->action == ESP_BMS_LVGL_ACTION_SET_BOOT_ANIMATION_STYLE && !runtime_boot_animation_style_matches_policy(value)) return 400;
    esp_bms_display_service_command_t command = { .kind = ESP_BMS_DISPLAY_SERVICE_COMMAND_REMOTE_ACTION };
    esp_bms_lvgl_action_event_t *event = &command.data.remote_action;
    event->action = control->action;
    event->flags = ESP_BMS_LVGL_ACTION_EVENT_FLAG_COMMITTED | ESP_BMS_LVGL_ACTION_EVENT_FLAG_NUMERIC_DELTA_VALID;
    event->numeric_delta = (int16_t)value;
    if (control->action == ESP_BMS_LVGL_ACTION_SET_CONTROLLER_TIRE || control->action == ESP_BMS_LVGL_ACTION_SET_CONTROLLER_RATIO) {
        event->flags |= ESP_BMS_LVGL_ACTION_EVENT_FLAG_CONTROLLER_SETTING_VALID;
        event->controller_tire_rim_inch = strcmp(id, "tire_rim") == 0 ? value : runtime->controller_fallback_tire_rim_inch;
        event->controller_tire_width_mm = strcmp(id, "tire_width") == 0 ? value : runtime->controller_fallback_tire_width_mm;
        event->controller_tire_aspect_percent = strcmp(id, "tire_aspect") == 0 ? value : runtime->controller_fallback_tire_aspect_percent;
        event->controller_gear_ratio_centi = value;
        if (control->action == ESP_BMS_LVGL_ACTION_SET_CONTROLLER_TIRE && !runtime_controller_tire_matches_policy(event->controller_tire_rim_inch, event->controller_tire_aspect_percent, event->controller_tire_width_mm)) return 400;
    }
    if (esp_bms_display_service_submit_command(&command, 1000U) != ESP_OK) {
        *error = "device busy; stop casting and retry";
        return 503;
    }
    *error = NULL;
    return 204;
}
