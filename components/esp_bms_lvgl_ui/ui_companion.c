#include "esp_bms_lvgl_ui_internal.h"
#include "esp_bms_companion_codec.h"

#define COMPANION_SLOT_COUNT 2U
#define COMPANION_TTL_MS 45000U

typedef struct {
    lv_image_dsc_t image;
    uint8_t *shown;
    uint8_t *pending;
    esp_bms_companion_decoder_t decoder;
    uint32_t frame;
    uint32_t seen;
    uint32_t started;
    uint16_t width, height;
    lv_obj_t *view;
} companion_slot_t;

static companion_slot_t slots[COMPANION_SLOT_COUNT];
static lv_timer_t *companion_timer;
static uint32_t last_contact;
static bool contact_seen;
static uint32_t action_sequence;
static struct { uint32_t id, action, time; } actions[8];
static unsigned action_count;

bool esp_bms_lvgl_ui_companion_connected(void)
{
    return contact_seen && lv_tick_get() - last_contact < COMPANION_TTL_MS;
}

void esp_bms_lvgl_ui_companion_action(uint32_t *id, uint32_t *action)
{
    while (action_count && lv_tick_get() - actions[0].time > 10000U) {
        --action_count;
        memmove(actions, actions + 1, action_count * sizeof(actions[0]));
    }
    *id = action_count ? actions[0].id : 0;
    *action = action_count ? actions[0].action : 0;
}

void companion_music_control_event(lv_event_t *event)
{
    if (s_ui.last_snapshot.ble_media_hid_connected && !s_ui.last_snapshot.ble_media_hid_suspended) {
        action_event_cb(event);
        return;
    }
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || process_return_swipe_event(LV_EVENT_CLICKED, false) ||
        !esp_bms_lvgl_ui_companion_connected() || action_count == 8) return;
    const esp_bms_lvgl_action_t value = (esp_bms_lvgl_action_t)(uintptr_t)lv_event_get_user_data(event);
    uint32_t action = 0;
    switch (value) {
        case ESP_BMS_LVGL_ACTION_PHONE_MEDIA_PREVIOUS: action = 1; break;
        case ESP_BMS_LVGL_ACTION_MEDIA_PLAY_PAUSE: action = 2; break;
        case ESP_BMS_LVGL_ACTION_PHONE_MEDIA_NEXT: action = 3; break;
        case ESP_BMS_LVGL_ACTION_PHONE_MEDIA_VOLUME_DOWN: action = 4; break;
        case ESP_BMS_LVGL_ACTION_PHONE_MEDIA_VOLUME_UP: action = 5; break;
        default: return;
    }
    if (++action_sequence == 0) ++action_sequence;
    actions[action_count].id = action_sequence;
    actions[action_count].action = action;
    actions[action_count].time = lv_tick_get();
    ++action_count;
}
/* Shared with CompanionFrame.kt; opaque BGRA palette for LVGL I4. */
static const uint32_t palette[16] = {
    0xff080a0e, 0xff162029, 0xff303c49, 0xff556578,
    0xff8091a5, 0xffa9b4c8, 0xffd5e0ee, 0xffffffff,
    0xff00e5ff, 0xff0066ff, 0xff74d6b5, 0xff20e87b,
    0xffffc857, 0xffff6b6b, 0xff4a90d9, 0xff5bbf5b,
};

static void companion_view_deleted(lv_event_t *event)
{
    companion_slot_t *slot = lv_event_get_user_data(event);
    slot->view = NULL;
}

static void companion_refresh(lv_timer_t *timer)
{
    (void)timer;
    const uint32_t now = lv_tick_get();
    set_music_page(&s_ui.last_snapshot);
    for (size_t i = 0; i < COMPANION_SLOT_COUNT; ++i) {
        companion_slot_t *s = &slots[i];
        if (s->pending && now - s->started > 15000U) {
            free(s->pending);
            s->pending = NULL;
        }
        if (s->shown && now - s->seen > COMPANION_TTL_MS) {
            if (s->view) lv_obj_delete(s->view);
            lv_image_cache_drop(&s->image);
            free(s->shown);
            s->shown = NULL;
        }
        lv_obj_t *parent = i == 0 ? s_ui.music_page : s_ui.gps_page;
        const esp_bms_lvgl_page_t page = i == 0 ? ESP_BMS_LVGL_PAGE_MUSIC : ESP_BMS_LVGL_PAGE_GPS;
        if (!s->shown || !parent || !dashboard_page_content_ready(page)) continue;
        if (!s->view) {
            s->view = lv_image_create(parent);
            lv_obj_remove_flag(s->view, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(s->view, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
            lv_obj_add_event_cb(s->view, companion_view_deleted, LV_EVENT_DELETE, s);
            lv_image_set_src(s->view, &s->image);
            lv_image_set_inner_align(s->view, LV_IMAGE_ALIGN_STRETCH);
        }
        const int32_t h = i == 0 ? s_ui.height - 120 : s_ui.height;
        lv_obj_set_size(s->view, s_ui.width, h);
        lv_obj_set_pos(s->view, 0, 0);
        lv_obj_move_foreground(s->view);
    }
}

int esp_bms_lvgl_ui_companion_page(void)
{
    return s_ui.page == ESP_BMS_LVGL_PAGE_MUSIC ? 0 : s_ui.page == ESP_BMS_LVGL_PAGE_GPS ? 1 : -1;
}

esp_err_t esp_bms_lvgl_ui_companion(unsigned slot, const char *operation,
                                    uint32_t frame, uint16_t width, uint16_t height,
                                    size_t offset, const uint8_t *data, size_t length)
{
    if (slot >= COMPANION_SLOT_COUNT || !operation) return ESP_ERR_INVALID_ARG;
    if (!companion_timer) companion_timer = lv_timer_create(companion_refresh, 250, NULL);
    if (!companion_timer) return ESP_ERR_NO_MEM;
    companion_slot_t *s = &slots[slot];
    if (strcmp(operation, "ack") == 0) {
        if (action_count && actions[0].id == frame) {
            --action_count;
            memmove(actions, actions + 1, action_count * sizeof(actions[0]));
        }
        return ESP_OK;
    }
    if (strcmp(operation, "select") == 0) {
        return esp_bms_lvgl_ui_set_page(slot == 0 ? ESP_BMS_LVGL_PAGE_MUSIC : ESP_BMS_LVGL_PAGE_GPS, true);
    }
    if (strcmp(operation, "clear") == 0) {
        if (s->view) lv_obj_delete(s->view);
        lv_image_cache_drop(&s->image);
        free(s->shown); free(s->pending);
        memset(s, 0, sizeof(*s));
        return ESP_OK;
    }
    if (strcmp(operation, "ping") == 0) {
        s->seen = lv_tick_get();
        last_contact = s->seen;
        contact_seen = true;
        return ESP_OK;
    }
    if (strcmp(operation, "begin") == 0) {
        if (width < 2 || (width & 1U) || width > 480 || height < 1 || height > 480 ||
            (size_t)width * height > 153600U) return ESP_ERR_INVALID_ARG;
        /* Retain only the visible companion page on devices without PSRAM. */
        companion_slot_t *other = &slots[slot == 0 ? 1 : 0];
        if (other->view) lv_obj_delete(other->view);
        lv_image_cache_drop(&other->image);
        free(other->shown); free(other->pending);
        memset(other, 0, sizeof(*other));
        free(s->pending);
        s->pending = calloc(1, 64U + (size_t)width * height / 2U);
        if (!s->pending) return ESP_ERR_NO_MEM;
        memcpy(s->pending, palette, sizeof(palette));
        s->width = width; s->height = height; s->frame = frame;
        s->started = lv_tick_get();
        s->decoder = (esp_bms_companion_decoder_t){
            .pixels = s->pending + 64, .capacity = (size_t)width * height / 2U,
            .count = (size_t)width * height,
        };
        return ESP_OK;
    }
    if (!s->pending || frame != s->frame) return ESP_ERR_INVALID_STATE;
    if (strcmp(operation, "chunk") == 0) {
        if (!esp_bms_companion_decode(&s->decoder, offset, data, length)) return ESP_ERR_INVALID_ARG;
        s->started = lv_tick_get();
        return ESP_OK;
    }
    if (strcmp(operation, "commit") != 0 || (s->decoder.written != s->decoder.count || s->decoder.run_state != 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s->view) lv_obj_delete(s->view);
    lv_image_cache_drop(&s->image);
    free(s->shown);
    s->shown = s->pending; s->pending = NULL; s->seen = lv_tick_get();
    s->image = (lv_image_dsc_t){
        .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_I4,
                    .w = s->width, .h = s->height, .stride = s->width / 2U },
        .data_size = 64U + (uint32_t)s->width * s->height / 2U,
        .data = s->shown,
    };
    last_contact = s->seen;
    contact_seen = true;
    companion_refresh(NULL);
    return ESP_OK;
}
