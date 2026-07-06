#include "esp_bms_lvgl_ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_bms_lvgl_contract.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "bms_lvgl_ui";

_Static_assert(sizeof(esp_bms_dashboard_snapshot_t) == 256,
               "esp_bms_dashboard_snapshot_t ABI size changed; update C snapshot consumers too");
_Static_assert(sizeof(esp_bms_lvgl_action_t) == 4,
               "esp_bms_lvgl_action_t ABI size changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_NONE == 0,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_SHOW_DASHBOARD == 1,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_SHOW_QUICK_MENU == 2,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_SHOW_SETTINGS == 3,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING == 4,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_CYCLE_BRIGHTNESS == 5,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY == 6,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_UNIT == 7,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_TOGGLE_LANGUAGE == 8,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_START_BMS_BIND == 9,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");
_Static_assert(ESP_BMS_LVGL_ACTION_RESTORE_DEFAULTS == 10,
               "esp_bms_lvgl_action_t value changed; update C action consumers too");

typedef struct {
    lv_display_t *display;
    lv_obj_t *root;
    lv_obj_t *header;
    lv_obj_t *pages;
    lv_obj_t *battery_page;
    lv_obj_t *gps_page;
    lv_obj_t *settings_page;
    lv_obj_t *settings_button;

    lv_obj_t *speed;
    lv_obj_t *gps_state;
    lv_obj_t *bms_state;
    lv_obj_t *wifi_state;
    lv_obj_t *ota_state;
    lv_obj_t *soc;
    lv_obj_t *pack_voltage;
    lv_obj_t *current;
    lv_obj_t *capacity;
    lv_obj_t *cell_stats;
    lv_obj_t *bms_error;
    lv_obj_t *local_battery;
    lv_obj_t *gps_detail;
    lv_obj_t *setup_ap_info;
    lv_obj_t *setup_ap_qr;

    int32_t width;
    int32_t height;
    bool dragging;
    bool settling;
    int32_t drag_start_pages_x;
    uint32_t drag_last_sample_log_ms;
    esp_bms_lvgl_page_t page;
    esp_bms_lvgl_action_t pending_action;
    bool deferred_snapshot_valid;
    esp_bms_dashboard_snapshot_t deferred_snapshot;
    char current_setup_ap_qr_payload[sizeof(((esp_bms_dashboard_snapshot_t *)0)->setup_ap_qr_payload)];
    bool initialized;
} esp_bms_lvgl_ui_t;

static esp_bms_lvgl_ui_t s_ui;

static void finish_page_scroll_state(bool flush_snapshot);

static const lv_color_t COLOR_BG = LV_COLOR_MAKE(0x08, 0x0a, 0x0e);
static const lv_color_t COLOR_PANEL = LV_COLOR_MAKE(0x12, 0x18, 0x20);
static const lv_color_t COLOR_PANEL_ALT = LV_COLOR_MAKE(0x16, 0x20, 0x29);
static const lv_color_t COLOR_TEXT = LV_COLOR_MAKE(0xe8, 0xf1, 0xff);
static const lv_color_t COLOR_MUTED = LV_COLOR_MAKE(0xa9, 0xb4, 0xc8);
static const lv_color_t COLOR_ACCENT = LV_COLOR_MAKE(0x74, 0xd6, 0xb5);
static const lv_color_t COLOR_WARN = LV_COLOR_MAKE(0xff, 0xc8, 0x57);
static const lv_color_t COLOR_BAD = LV_COLOR_MAKE(0xff, 0x6b, 0x6b);

static void clear_style(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *panel(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    clear_style(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 4, LV_PART_MAIN);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, const lv_font_t *font)
{
    lv_obj_t *obj = lv_label_create(parent);
    clear_style(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(obj, COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    return obj;
}

static void label_set_text_if_changed(lv_obj_t *obj, const char *text)
{
    const char *current = lv_label_get_text(obj);
    if (!current || strcmp(current, text) != 0) {
        lv_label_set_text(obj, text);
    }
}

static void label_set_text_fmt_if_changed(lv_obj_t *obj, const char *fmt, ...)
{
    char text[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    label_set_text_if_changed(obj, text);
}

static void label_set_text_color_if_changed(lv_obj_t *obj, lv_color_t color)
{
    const lv_color_t current = lv_obj_get_style_text_color(obj, LV_PART_MAIN);
    if (!lv_color_eq(current, color)) {
        lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
    }
}

static lv_obj_t *panel_label(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                             lv_color_t bg, const lv_font_t *font)
{
    lv_obj_t *box = panel(parent, x, y, w, h, bg);
    return label(box, 4, 4, w - 8, h - 8, font);
}

#if LV_USE_QRCODE
static lv_obj_t *setup_ap_qr(lv_obj_t *parent, int32_t x, int32_t y, int32_t size)
{
    lv_obj_t *obj = lv_qrcode_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_qrcode_set_size(obj, size);
    lv_qrcode_set_dark_color(obj, lv_color_black());
    lv_qrcode_set_light_color(obj, lv_color_white());
    lv_qrcode_set_quiet_zone(obj, true);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    return obj;
}
#else
static lv_obj_t *setup_ap_qr(lv_obj_t *parent, int32_t x, int32_t y, int32_t size)
{
    (void)parent;
    (void)x;
    (void)y;
    (void)size;
    return NULL;
}
#endif

static void show_dashboard_view(void)
{
    finish_page_scroll_state(true);
    lv_obj_clear_flag(s_ui.pages, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.settings_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_ui.header);
}

static void show_settings_view(void)
{
    finish_page_scroll_state(true);
    lv_obj_add_flag(s_ui.pages, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_ui.settings_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_ui.settings_page);
    lv_obj_move_foreground(s_ui.header);
}

static void queue_action(esp_bms_lvgl_action_t action)
{
    if (action != ESP_BMS_LVGL_ACTION_NONE) {
        s_ui.pending_action = action;
    }
}

static void action_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_CLICKED) {
        return;
    }

    esp_bms_lvgl_action_t action = (esp_bms_lvgl_action_t)(uintptr_t)lv_event_get_user_data(event);
    queue_action(action);

    if (action == ESP_BMS_LVGL_ACTION_SHOW_SETTINGS) {
        show_settings_view();
    } else if (action == ESP_BMS_LVGL_ACTION_SHOW_DASHBOARD) {
        show_dashboard_view();
    }
}

static lv_obj_t *action_panel(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                              const char *text, esp_bms_lvgl_action_t action)
{
    lv_obj_t *box = panel(parent, x, y, w, h, COLOR_PANEL);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(box, action_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)action);

    lv_obj_t *text_label = label(box, 4, 3, w - 8, h - 6, &lv_font_montserrat_14);
    lv_label_set_text(text_label, text);
    lv_obj_add_flag(text_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(text_label, action_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)action);
    return box;
}

static const char *speed_unit_text(esp_bms_speed_unit_t unit)
{
    return unit == ESP_BMS_SPEED_UNIT_MPH ? "MPH" : "KMH";
}

static const char *wifi_text(esp_bms_wifi_state_t state)
{
    switch (state) {
    case ESP_BMS_WIFI_SETUP_AP:
        return "AP";
    case ESP_BMS_WIFI_CONNECTING:
        return "WIFI...";
    case ESP_BMS_WIFI_CONNECTED:
        return "WIFI";
    case ESP_BMS_WIFI_OFFLINE:
    default:
        return "OFF";
    }
}

static const char *ota_text(esp_bms_ota_state_t state)
{
    switch (state) {
    case ESP_BMS_OTA_CHECKING:
        return "OTA CHK";
    case ESP_BMS_OTA_AVAILABLE:
        return "OTA NEW";
    case ESP_BMS_OTA_DOWNLOADING:
        return "OTA DL";
    case ESP_BMS_OTA_VERIFYING:
        return "OTA VFY";
    case ESP_BMS_OTA_READY:
        return "OTA RDY";
    case ESP_BMS_OTA_FAILED:
        return "OTA ERR";
    case ESP_BMS_OTA_IDLE:
    default:
        return "OTA IDLE";
    }
}

static void format_mv(char *out, size_t len, bool valid, uint32_t mv)
{
    if (!valid) {
        snprintf(out, len, "--");
        return;
    }
    snprintf(out, len, "%lu.%02luV", (unsigned long)(mv / 1000), (unsigned long)((mv % 1000) / 10));
}

static void format_deci_amps(char *out, size_t len, bool valid, int16_t deci_amps)
{
    if (!valid) {
        snprintf(out, len, "--");
        return;
    }
    const char sign = deci_amps < 0 ? '-' : '+';
    uint16_t abs_value = deci_amps < 0 ? (uint16_t)(-deci_amps) : (uint16_t)deci_amps;
    snprintf(out, len, "%c%u.%uA", sign, abs_value / 10, abs_value % 10);
}

static void format_ah(char *out, size_t len, bool valid, uint32_t mah)
{
    if (!valid) {
        snprintf(out, len, "--");
        return;
    }
    snprintf(out, len, "%lu.%01luAh", (unsigned long)(mah / 1000), (unsigned long)((mah % 1000) / 100));
}

static void format_cell_v(char *out, size_t len, bool valid, uint16_t mv)
{
    if (!valid) {
        snprintf(out, len, "--");
        return;
    }
    snprintf(out, len, "%u.%03u", mv / 1000, mv % 1000);
}

static void set_header(const esp_bms_dashboard_snapshot_t *snapshot)
{
    label_set_text_color_if_changed(s_ui.gps_state, snapshot->gps_fix_valid ? COLOR_ACCENT : COLOR_WARN);
    label_set_text_if_changed(s_ui.gps_state, snapshot->gps_fix_valid ? "GPS OK" : "GPS --");

    label_set_text_color_if_changed(s_ui.bms_state, snapshot->bms_online ? COLOR_ACCENT : COLOR_BAD);
    label_set_text_if_changed(s_ui.bms_state, snapshot->bms_online ? "BMS OK" : "BMS OFF");

    label_set_text_color_if_changed(s_ui.wifi_state,
                                    snapshot->wifi == ESP_BMS_WIFI_OFFLINE ? COLOR_WARN : COLOR_ACCENT);
    label_set_text_if_changed(s_ui.wifi_state, wifi_text(snapshot->wifi));

    label_set_text_color_if_changed(s_ui.ota_state, snapshot->ota == ESP_BMS_OTA_FAILED ? COLOR_BAD : COLOR_MUTED);
    label_set_text_if_changed(s_ui.ota_state, ota_text(snapshot->ota));
}

static void set_setup_ap(const esp_bms_dashboard_snapshot_t *snapshot)
{
    const char *ssid = snapshot->setup_ap_ssid[0] != '\0' ? snapshot->setup_ap_ssid : "--";
    const char *password = snapshot->setup_ap_password[0] != '\0' ? snapshot->setup_ap_password : "--";
    label_set_text_fmt_if_changed(s_ui.setup_ap_info, "SETUP %s\nSSID %.31s\nPW %.8s",
                                  snapshot->setup_ap_enabled ? "ON" : "OFF",
                                  ssid,
                                  password);

#if LV_USE_QRCODE
    if (s_ui.setup_ap_qr) {
        if (snapshot->setup_ap_qr_payload[0] == '\0') {
            s_ui.current_setup_ap_qr_payload[0] = '\0';
            lv_obj_add_flag(s_ui.setup_ap_qr, LV_OBJ_FLAG_HIDDEN);
        } else if (strcmp(s_ui.current_setup_ap_qr_payload, snapshot->setup_ap_qr_payload) == 0) {
            lv_obj_clear_flag(s_ui.setup_ap_qr, LV_OBJ_FLAG_HIDDEN);
        } else if (lv_qrcode_update(s_ui.setup_ap_qr,
                                    snapshot->setup_ap_qr_payload,
                                    strlen(snapshot->setup_ap_qr_payload)) == LV_RESULT_OK) {
            snprintf(s_ui.current_setup_ap_qr_payload, sizeof(s_ui.current_setup_ap_qr_payload), "%s",
                     snapshot->setup_ap_qr_payload);
            lv_obj_clear_flag(s_ui.setup_ap_qr, LV_OBJ_FLAG_HIDDEN);
        } else {
            s_ui.current_setup_ap_qr_payload[0] = '\0';
            lv_obj_add_flag(s_ui.setup_ap_qr, LV_OBJ_FLAG_HIDDEN);
        }
    }
#endif
}

static void set_dashboard(const esp_bms_dashboard_snapshot_t *snapshot)
{
    char voltage[24];
    char current[24];
    char total[24];
    char left[24];
    char min_cell[16];
    char avg_cell[16];
    char max_cell[16];

    if (snapshot->speed_valid) {
        label_set_text_fmt_if_changed(s_ui.speed, "%u.%u\n%s",
                                      snapshot->speed_deci_units / 10,
                                      snapshot->speed_deci_units % 10,
                                      speed_unit_text(snapshot->speed_unit));
    } else {
        label_set_text_fmt_if_changed(s_ui.speed, "--\n%s", speed_unit_text(snapshot->speed_unit));
    }

    if (snapshot->soc_valid) {
        label_set_text_fmt_if_changed(s_ui.soc, "SOC\n%u%%",
                                      snapshot->soc_percent > 100 ? 100 : snapshot->soc_percent);
    } else {
        label_set_text_if_changed(s_ui.soc, "SOC\n--");
    }

    format_mv(voltage, sizeof(voltage), snapshot->pack_voltage_valid, snapshot->pack_voltage_mv);
    label_set_text_fmt_if_changed(s_ui.pack_voltage, "PACK\n%s", voltage);

    format_deci_amps(current, sizeof(current), snapshot->current_valid, snapshot->current_deci_amps);
    label_set_text_fmt_if_changed(s_ui.current, "CUR\n%s", current);

    format_ah(total, sizeof(total), snapshot->total_capacity_valid, snapshot->total_capacity_mah);
    format_ah(left, sizeof(left), snapshot->capacity_remaining_valid, snapshot->capacity_remaining_mah);
    label_set_text_fmt_if_changed(s_ui.capacity, "TOTAL %s\nLEFT  %s", total, left);

    format_cell_v(min_cell, sizeof(min_cell), snapshot->min_cell_valid, snapshot->min_cell_voltage_mv);
    format_cell_v(avg_cell, sizeof(avg_cell), snapshot->average_cell_valid, snapshot->average_cell_voltage_mv);
    format_cell_v(max_cell, sizeof(max_cell), snapshot->max_cell_valid, snapshot->max_cell_voltage_mv);
    if (snapshot->delta_cell_valid) {
        label_set_text_fmt_if_changed(s_ui.cell_stats, "MN %s AV %s\nMX %s DF %umV",
                                      min_cell, avg_cell, max_cell, snapshot->delta_cell_voltage_mv);
    } else {
        label_set_text_fmt_if_changed(s_ui.cell_stats, "MN %s AV %s\nMX %s DF --",
                                      min_cell, avg_cell, max_cell);
    }

    if (snapshot->bms_error_text[0] != '\0') {
        label_set_text_color_if_changed(s_ui.bms_error, COLOR_WARN);
        label_set_text_fmt_if_changed(s_ui.bms_error, "ERR\n%.28s", snapshot->bms_error_text);
    } else {
        label_set_text_color_if_changed(s_ui.bms_error, COLOR_MUTED);
        label_set_text_if_changed(s_ui.bms_error, "ERR\n--");
    }

    format_mv(voltage, sizeof(voltage), snapshot->local_battery_valid, snapshot->local_battery_mv);
    label_set_text_fmt_if_changed(s_ui.local_battery, "LOCAL %s\nSETUP %s", voltage,
                                  snapshot->setup_ap_enabled ? "ON" : "OFF");

    label_set_text_fmt_if_changed(s_ui.gps_detail, "FIX %s\nNMEA %lu\nPAGE GPS",
                                  snapshot->gps_fix_valid ? "YES" : "NO",
                                  (unsigned long)snapshot->gps_sentences_seen);
    set_setup_ap(snapshot);
}

static void apply_dashboard_snapshot(const esp_bms_dashboard_snapshot_t *snapshot)
{
    set_header(snapshot);
    set_dashboard(snapshot);
}

static void defer_dashboard_snapshot(const esp_bms_dashboard_snapshot_t *snapshot)
{
    memcpy(&s_ui.deferred_snapshot, snapshot, sizeof(s_ui.deferred_snapshot));
    s_ui.deferred_snapshot_valid = true;
}

static void flush_deferred_dashboard_snapshot(void)
{
    if (!s_ui.deferred_snapshot_valid) {
        return;
    }

    esp_bms_dashboard_snapshot_t snapshot;
    memcpy(&snapshot, &s_ui.deferred_snapshot, sizeof(snapshot));
    s_ui.deferred_snapshot_valid = false;
    apply_dashboard_snapshot(&snapshot);
}

static void invalidate_dashboard_viewport(void)
{
#if CONFIG_ESP_BMS_LVGL_UI_DRAG_FULL_INVALIDATE
    if (!s_ui.root) {
        return;
    }

    const lv_area_t area = {
        .x1 = 0,
        .y1 = 20,
        .x2 = s_ui.width - 1,
        .y2 = s_ui.height - 1,
    };
    lv_obj_invalidate_area(s_ui.root, &area);
#endif
}

static int32_t page_target_scroll_x(esp_bms_lvgl_page_t page)
{
    return page == ESP_BMS_LVGL_PAGE_GPS ? s_ui.width : 0;
}

static esp_bms_lvgl_page_t page_from_scroll_x(int32_t scroll_x)
{
    return scroll_x >= (s_ui.width / 2) ? ESP_BMS_LVGL_PAGE_GPS : ESP_BMS_LVGL_PAGE_BATTERY;
}

static void finish_page_scroll_state(bool flush_snapshot)
{
    if (s_ui.pages) {
        lv_obj_stop_scroll_anim(s_ui.pages);
        s_ui.page = page_from_scroll_x(lv_obj_get_scroll_x(s_ui.pages));
        lv_obj_scroll_to_x(s_ui.pages, page_target_scroll_x(s_ui.page), LV_ANIM_OFF);
        invalidate_dashboard_viewport();
    }

    s_ui.dragging = false;
    s_ui.settling = false;
    if (flush_snapshot) {
        flush_deferred_dashboard_snapshot();
    }
}

static void move_to_page(esp_bms_lvgl_page_t page, bool animated)
{
    lv_obj_stop_scroll_anim(s_ui.pages);
    const int32_t target_x = page_target_scroll_x(page);
    const int32_t current_x = lv_obj_get_scroll_x(s_ui.pages);
    if (current_x == target_x) {
        s_ui.page = page;
        s_ui.dragging = false;
        s_ui.settling = false;
        flush_deferred_dashboard_snapshot();
        return;
    }

    s_ui.page = page;
    s_ui.dragging = false;
    s_ui.settling = animated;
    lv_obj_scroll_to_x(s_ui.pages, target_x, animated ? LV_ANIM_ON : LV_ANIM_OFF);
    if (!animated) {
        flush_deferred_dashboard_snapshot();
    }
}

static void page_scroll_event_cb(lv_event_t *event)
{
    if (lv_event_get_target(event) != s_ui.pages) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        s_ui.drag_start_pages_x = lv_obj_get_scroll_x(s_ui.pages);
#if CONFIG_ESP_BMS_LVGL_UI_DRAG_DIAGNOSTICS
        ESP_LOGI(TAG, "[drag] press scroll_x=%ld page=%d",
                 (long)s_ui.drag_start_pages_x,
                 (int)s_ui.page);
#endif
        return;
    }

    if (code == LV_EVENT_SCROLL_BEGIN) {
        s_ui.dragging = true;
        s_ui.drag_start_pages_x = lv_obj_get_scroll_x(s_ui.pages);
        s_ui.drag_last_sample_log_ms = lv_tick_get();
#if CONFIG_ESP_BMS_LVGL_UI_DRAG_DIAGNOSTICS
        ESP_LOGI(TAG, "[drag] scroll_begin scroll_x=%ld page=%d",
                 (long)s_ui.drag_start_pages_x,
                 (int)s_ui.page);
#endif
        return;
    }

    if (code == LV_EVENT_SCROLL) {
        invalidate_dashboard_viewport();
#if CONFIG_ESP_BMS_LVGL_UI_DRAG_SAMPLE_DIAGNOSTICS
        if (lv_tick_elaps(s_ui.drag_last_sample_log_ms) >= CONFIG_ESP_BMS_LVGL_UI_DRAG_SAMPLE_PERIOD_MS) {
            s_ui.drag_last_sample_log_ms = lv_tick_get();
            ESP_LOGI(TAG, "[drag] sample scroll_x=%ld from=%ld page=%d",
                     (long)lv_obj_get_scroll_x(s_ui.pages),
                     (long)s_ui.drag_start_pages_x,
                     (int)s_ui.page);
        }
#endif
        return;
    }

    if (code == LV_EVENT_SCROLL_END) {
        const int32_t scroll_x = lv_obj_get_scroll_x(s_ui.pages);
        const esp_bms_lvgl_page_t target = page_from_scroll_x(scroll_x);
        s_ui.dragging = false;
        s_ui.settling = false;
        s_ui.page = target;
#if CONFIG_ESP_BMS_LVGL_UI_DRAG_DIAGNOSTICS
        ESP_LOGI(TAG, "[drag] scroll_end scroll_x=%ld target=%d",
                 (long)scroll_x,
                 (int)target);
#endif
        flush_deferred_dashboard_snapshot();
    }
}

static void create_screen(lv_display_t *display)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    clear_style(screen);
    lv_obj_set_style_bg_color(screen, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_display_set_default(display);
    lv_screen_load(screen);

    s_ui.display = display;
    s_ui.root = screen;
    s_ui.width = lv_display_get_horizontal_resolution(display);
    s_ui.height = lv_display_get_vertical_resolution(display);
    const bool portrait = s_ui.width < s_ui.height;
    const int32_t page_h = s_ui.height - 20;
    const int32_t content_w = s_ui.width - 16;

    s_ui.header = panel(screen, 0, 0, s_ui.width, 20, COLOR_BG);
    lv_obj_set_style_radius(s_ui.header, 0, LV_PART_MAIN);
    s_ui.settings_button = label(s_ui.header, 6, 2, 34, 16, &lv_font_montserrat_14);
    lv_label_set_text(s_ui.settings_button, "SET");
    lv_obj_set_style_text_color(s_ui.settings_button, COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.settings_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.settings_button, action_event_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)ESP_BMS_LVGL_ACTION_SHOW_SETTINGS);
    if (portrait) {
        s_ui.gps_state = label(s_ui.header, 42, 2, 46, 16, &lv_font_montserrat_14);
        s_ui.bms_state = label(s_ui.header, 92, 2, 56, 16, &lv_font_montserrat_14);
        s_ui.wifi_state = label(s_ui.header, 152, 2, 40, 16, &lv_font_montserrat_14);
        s_ui.ota_state = label(s_ui.header, 196, 2, 42, 16, &lv_font_montserrat_14);
    } else {
        s_ui.gps_state = label(s_ui.header, 48, 2, 50, 16, &lv_font_montserrat_14);
        s_ui.bms_state = label(s_ui.header, 104, 2, 54, 16, &lv_font_montserrat_14);
        s_ui.wifi_state = label(s_ui.header, 166, 2, 54, 16, &lv_font_montserrat_14);
        s_ui.ota_state = label(s_ui.header, 222, 2, 92, 16, &lv_font_montserrat_14);
    }

    s_ui.pages = lv_obj_create(screen);
    clear_style(s_ui.pages);
    lv_obj_set_pos(s_ui.pages, 0, 20);
    lv_obj_set_size(s_ui.pages, s_ui.width, page_h);
    lv_obj_set_style_bg_color(s_ui.pages, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.pages, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui.pages, LV_OBJ_FLAG_SCROLL_ELASTIC |
                                  LV_OBJ_FLAG_SCROLL_MOMENTUM |
                                  LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_flag(s_ui.pages, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_set_scroll_dir(s_ui.pages, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(s_ui.pages, LV_SCROLL_SNAP_START);
    lv_obj_set_scroll_snap_y(s_ui.pages, LV_SCROLL_SNAP_NONE);
    lv_obj_set_scrollbar_mode(s_ui.pages, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_ui.pages, page_scroll_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_ui.pages, page_scroll_event_cb, LV_EVENT_SCROLL_BEGIN, NULL);
    lv_obj_add_event_cb(s_ui.pages, page_scroll_event_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(s_ui.pages, page_scroll_event_cb, LV_EVENT_SCROLL_END, NULL);

    s_ui.battery_page = lv_obj_create(s_ui.pages);
    clear_style(s_ui.battery_page);
    lv_obj_set_pos(s_ui.battery_page, 0, 0);
    lv_obj_set_size(s_ui.battery_page, s_ui.width, page_h);
    lv_obj_set_style_bg_color(s_ui.battery_page, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.battery_page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.battery_page, LV_OBJ_FLAG_SNAPPABLE);

    s_ui.gps_page = lv_obj_create(s_ui.pages);
    clear_style(s_ui.gps_page);
    lv_obj_set_pos(s_ui.gps_page, s_ui.width, 0);
    lv_obj_set_size(s_ui.gps_page, s_ui.width, page_h);
    lv_obj_set_style_bg_color(s_ui.gps_page, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.gps_page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_ui.gps_page, LV_OBJ_FLAG_SNAPPABLE);

    if (portrait) {
        s_ui.speed = panel_label(s_ui.battery_page, 8, 8, 68, 56, COLOR_PANEL_ALT, &lv_font_montserrat_14);
        s_ui.soc = panel_label(s_ui.battery_page, 84, 8, 68, 56, COLOR_PANEL_ALT, &lv_font_montserrat_14);
        s_ui.local_battery = panel_label(s_ui.battery_page, 160, 8, 72, 56, COLOR_PANEL, &lv_font_montserrat_14);
        s_ui.pack_voltage = panel_label(s_ui.battery_page, 8, 72, 108, 54, COLOR_PANEL, &lv_font_montserrat_14);
        s_ui.current = panel_label(s_ui.battery_page, 124, 72, 108, 54, COLOR_PANEL, &lv_font_montserrat_14);
        s_ui.capacity = panel_label(s_ui.battery_page, 8, 134, 108, 58, COLOR_PANEL, &lv_font_montserrat_14);
        s_ui.bms_error = panel_label(s_ui.battery_page, 124, 134, 108, 58, COLOR_PANEL, &lv_font_montserrat_14);
        s_ui.cell_stats = panel_label(s_ui.battery_page, 8, 200, content_w, 88, COLOR_PANEL, &lv_font_montserrat_14);

        s_ui.gps_detail = panel_label(s_ui.gps_page, 8, 8, content_w, 76, COLOR_PANEL, &lv_font_montserrat_14);
        lv_obj_t *hint = panel_label(s_ui.gps_page, 8, 92, content_w, 58, COLOR_PANEL_ALT, &lv_font_montserrat_14);
        lv_label_set_text(hint, "SAT --\nHDOP --");
        lv_obj_t *adapter = panel_label(s_ui.gps_page, 8, 158, content_w, 64, COLOR_PANEL, &lv_font_montserrat_14);
        lv_label_set_text(adapter, "LVGL 9.5.0\nESP LVGL ADAPTER");
    } else {
        s_ui.speed = panel_label(s_ui.battery_page, 8, 8, 72, 68, COLOR_PANEL_ALT, &lv_font_montserrat_14);
        s_ui.soc = panel_label(s_ui.battery_page, 88, 8, 58, 68, COLOR_PANEL_ALT, &lv_font_montserrat_14);
        s_ui.pack_voltage = panel_label(s_ui.battery_page, 154, 8, 80, 68, COLOR_PANEL, &lv_font_montserrat_14);
        s_ui.current = panel_label(s_ui.battery_page, 242, 8, 70, 68, COLOR_PANEL, &lv_font_montserrat_14);
        s_ui.capacity = panel_label(s_ui.battery_page, 8, 84, 108, 60, COLOR_PANEL, &lv_font_montserrat_14);
        s_ui.cell_stats = panel_label(s_ui.battery_page, 124, 84, 108, 60, COLOR_PANEL, &lv_font_montserrat_14);
        s_ui.bms_error = panel_label(s_ui.battery_page, 240, 84, 72, page_h - 92, COLOR_PANEL, &lv_font_montserrat_14);
        const int32_t local_battery_y = 152;
        const int32_t local_battery_h = page_h - local_battery_y - 8;
        s_ui.local_battery = panel_label(s_ui.battery_page, 8, local_battery_y, 224, local_battery_h,
                                         COLOR_PANEL, &lv_font_montserrat_14);

        s_ui.gps_detail = panel_label(s_ui.gps_page, 8, 8, 160, 84, COLOR_PANEL, &lv_font_montserrat_14);
        lv_obj_t *hint = panel_label(s_ui.gps_page, 176, 8, 136, 84, COLOR_PANEL_ALT, &lv_font_montserrat_14);
        lv_label_set_text(hint, "SAT --\nHDOP --");
        lv_obj_t *adapter = panel_label(s_ui.gps_page, 8, 104, 304, 64, COLOR_PANEL, &lv_font_montserrat_14);
        lv_label_set_text(adapter, "LVGL 9.5.0\nESP LVGL ADAPTER");
    }
    lv_obj_set_style_text_align(s_ui.speed, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_ui.soc, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    s_ui.settings_page = lv_obj_create(screen);
    clear_style(s_ui.settings_page);
    lv_obj_set_pos(s_ui.settings_page, 0, 20);
    lv_obj_set_size(s_ui.settings_page, s_ui.width, page_h);
    lv_obj_set_style_bg_color(s_ui.settings_page, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.settings_page, LV_OPA_COVER, LV_PART_MAIN);
    const int32_t action_w = portrait ? 88 : 84;
    const int32_t setup_info_x = action_w + 16;
    const int32_t setup_info_w = s_ui.width - setup_info_x - 8;
    const int32_t setup_qr_size = setup_info_w < 128 ? setup_info_w : 128;
    action_panel(s_ui.settings_page, 8, 6, action_w, 28, "BACK", ESP_BMS_LVGL_ACTION_SHOW_DASHBOARD);
    action_panel(s_ui.settings_page, 8, 40, action_w, 22, "SETUP AP", ESP_BMS_LVGL_ACTION_ENABLE_WIFI_REPROVISIONING);
    action_panel(s_ui.settings_page, 8, 66, action_w, 22, "BRIGHT", ESP_BMS_LVGL_ACTION_CYCLE_BRIGHTNESS);
    action_panel(s_ui.settings_page, 8, 92, action_w, 22, "ROTATE", ESP_BMS_LVGL_ACTION_ROTATE_DISPLAY);
    action_panel(s_ui.settings_page, 8, 118, action_w, 22, "SPEED", ESP_BMS_LVGL_ACTION_TOGGLE_SPEED_UNIT);
    action_panel(s_ui.settings_page, 8, 144, action_w, 22, "LANG", ESP_BMS_LVGL_ACTION_TOGGLE_LANGUAGE);
    action_panel(s_ui.settings_page, 8, 170, action_w, 22, "BMS", ESP_BMS_LVGL_ACTION_START_BMS_BIND);
    action_panel(s_ui.settings_page, 8, 196, action_w, 22, "RESTORE", ESP_BMS_LVGL_ACTION_RESTORE_DEFAULTS);
    s_ui.setup_ap_info = panel_label(s_ui.settings_page, setup_info_x, 6, setup_info_w, 68,
                                     COLOR_PANEL_ALT, &lv_font_montserrat_14);
    s_ui.setup_ap_qr = setup_ap_qr(s_ui.settings_page, setup_info_x, 82, setup_qr_size);
    lv_obj_add_flag(s_ui.settings_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_ui.header);
}

static esp_err_t rebuild_screen_if_resolution_changed(void)
{
    ESP_RETURN_ON_FALSE(s_ui.display, ESP_ERR_INVALID_STATE, TAG, "display is not initialized");

    const int32_t width = lv_display_get_horizontal_resolution(s_ui.display);
    const int32_t height = lv_display_get_vertical_resolution(s_ui.display);
    if (width == s_ui.width && height == s_ui.height) {
        return ESP_OK;
    }

    lv_obj_t *old_root = s_ui.root;
    const esp_bms_lvgl_page_t page = s_ui.page;
    const esp_bms_lvgl_action_t pending_action = s_ui.pending_action;
    const bool settings_visible = s_ui.settings_page && !lv_obj_has_flag(s_ui.settings_page, LV_OBJ_FLAG_HIDDEN);
    lv_display_t *display = s_ui.display;

    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.display = display;
    s_ui.pending_action = pending_action;
    s_ui.initialized = true;
    create_screen(display);
    move_to_page(page, false);
    if (settings_visible) {
        show_settings_view();
    } else {
        show_dashboard_view();
    }
    if (old_root) {
        lv_obj_delete(old_root);
    }
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_ui_init(lv_display_t *display)
{
    ESP_RETURN_ON_FALSE(display, ESP_ERR_INVALID_ARG, TAG, "display is required");
    ESP_RETURN_ON_FALSE(!s_ui.initialized, ESP_ERR_INVALID_STATE, TAG, "UI already initialized");
    create_screen(display);
    s_ui.initialized = true;
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_ui_update(const esp_bms_dashboard_snapshot_t *snapshot)
{
    ESP_RETURN_ON_FALSE(snapshot, ESP_ERR_INVALID_ARG, TAG, "snapshot is required");
    ESP_RETURN_ON_FALSE(s_ui.initialized, ESP_ERR_INVALID_STATE, TAG, "UI is not initialized");
    ESP_RETURN_ON_ERROR(rebuild_screen_if_resolution_changed(), TAG, "rebuild UI after resolution change failed");
    if (s_ui.dragging || s_ui.settling) {
        defer_dashboard_snapshot(snapshot);
        return ESP_OK;
    }

    apply_dashboard_snapshot(snapshot);
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_ui_set_page(esp_bms_lvgl_page_t page, bool animated)
{
    ESP_RETURN_ON_FALSE(s_ui.initialized, ESP_ERR_INVALID_STATE, TAG, "UI is not initialized");
    ESP_RETURN_ON_FALSE(page == ESP_BMS_LVGL_PAGE_BATTERY || page == ESP_BMS_LVGL_PAGE_GPS,
                        ESP_ERR_INVALID_ARG, TAG, "invalid page");

    move_to_page(page, animated);
    return ESP_OK;
}

esp_err_t esp_bms_lvgl_ui_take_action(esp_bms_lvgl_action_t *action)
{
    ESP_RETURN_ON_FALSE(action, ESP_ERR_INVALID_ARG, TAG, "action output is required");
    ESP_RETURN_ON_FALSE(s_ui.initialized, ESP_ERR_INVALID_STATE, TAG, "UI is not initialized");

    *action = s_ui.pending_action;
    s_ui.pending_action = ESP_BMS_LVGL_ACTION_NONE;
    return ESP_OK;
}
