"""编译真实 GPS 详情函数，验证定位文案、增量刷新和标签生命周期。"""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_ble_scan_source_contract import function_body


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "components/esp_bms_lvgl_ui/ui_settings.c"

STUBS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define SETTINGS_LIST_MARGIN_X 12
#define SETTINGS_DETAIL_ROW_H_PORTRAIT 60
#define SETTINGS_DETAIL_ROW_H_LANDSCAPE 50
#define SNAPSHOT_FLAG(s, flag) ((s)->fix_valid)
enum { ESP_BMS_LVGL_ACTION_NONE, SETTINGS_SYSTEM_VIEW_ROOT, SETTINGS_DETAIL_GPS,
       LV_OBJ_FLAG_CLICKABLE, LV_STATE_DISABLED, LV_PART_MAIN, LV_OPA_40,
       LV_ANIM_OFF, LV_EVENT_DELETE };
typedef struct lv_obj lv_obj_t;
typedef struct { int unused; } lv_event_t;
struct lv_obj {
    char text[128];
    lv_obj_t *children[6];
    unsigned child_count;
    void (*on_delete)(lv_event_t *);
};
typedef struct {
    bool fix_valid, gps_satellite_info_valid, gps_hdop_valid;
    uint8_t gps_fix_dimension, gps_constellation_mask;
    uint8_t gps_satellites_used, gps_satellites_visible, gps_average_cn0;
    uint16_t gps_hdop_centi;
} esp_bms_dashboard_snapshot_t;
typedef struct {
    const char *title, *title_en, *subtitle, *subtitle_en;
    int action, system_view;
} settings_detail_row_t;
static struct {
    int width, height;
    uint8_t settings_detail_id;
    lv_obj_t *settings_detail, *settings_detail_title, *settings_bms_ble_status;
    lv_obj_t *settings_gps_values[5];
} s_ui;
static esp_bms_dashboard_snapshot_t snapshot;
static lv_obj_t objects[64], detail, title;
static unsigned created, cleaned, scrolled, text_writes;
static bool chinese;
static const char *ui_t(const char *zh, const char *en) { return chinese ? zh : en; }
static const esp_bms_dashboard_snapshot_t *settings_current_snapshot(void) { return &snapshot; }
static lv_obj_t *create(lv_obj_t *parent) {
    assert(created < ARRAY_SIZE(objects));
    lv_obj_t *obj = &objects[created++];
    memset(obj, 0, sizeof(*obj));
    if (parent) parent->children[parent->child_count++] = obj;
    return obj;
}
static void label_set_text_if_changed(lv_obj_t *obj, const char *text) {
    assert(obj);
    if (strcmp(obj->text, text)) {
        snprintf(obj->text, sizeof(obj->text), "%s", text);
        text_writes++;
    }
}
static void destroy(lv_obj_t *obj) {
    if (obj->on_delete) { lv_event_t event = {0}; obj->on_delete(&event); }
    for (unsigned i = 0; i < obj->child_count; i++) destroy(obj->children[i]);
    obj->child_count = 0;
}
static void lv_obj_clean(lv_obj_t *obj) { cleaned++; destroy(obj); }
static lv_obj_t *settings_list_card(lv_obj_t *p, int x, int y, int w, int h, size_t n) {
    (void)x; (void)y; (void)w; (void)h; (void)n;
    return create(p);
}
static lv_obj_t *settings_detail_row(lv_obj_t *p, int x, int y, int w, int h,
                                    const settings_detail_row_t *row) {
    (void)x; (void)y; (void)w; (void)h;
    lv_obj_t *box = create(p);
    label_set_text_if_changed(create(box), ui_t(row->title, row->title_en));
    label_set_text_if_changed(create(box), ui_t(row->subtitle, row->subtitle_en));
    return box;
}
static void lv_obj_add_event_cb(lv_obj_t *obj, void (*cb)(lv_event_t *), int event, void *data) {
    (void)event; (void)data; obj->on_delete = cb;
}
static lv_obj_t *lv_obj_get_child(lv_obj_t *obj, int index) { return obj->children[index]; }
#define lv_obj_remove_flag(...) ((void)0)
#define lv_obj_add_state(...) ((void)0)
#define lv_obj_set_style_text_opa(...) ((void)0)
#define lv_obj_update_layout(...) ((void)0)
#define lv_obj_scroll_to_y(...) ((void)scrolled++)
"""

CASES = r"""
int main(void) {
    s_ui.width = 480; s_ui.height = 320;
    s_ui.settings_detail = &detail; s_ui.settings_detail_title = &title;
    snapshot.fix_valid = true;
    settings_show_gps_detail();
    assert(created == 16 && cleaned == 1 && scrolled == 1);
    assert(!strcmp(s_ui.settings_gps_values[0]->text, "Fix acquired"));
    assert(!strcmp(s_ui.settings_gps_values[2]->text, "--"));
    lv_obj_t *value = s_ui.settings_gps_values[0];
    unsigned writes = text_writes;
    settings_show_gps_detail();
    assert(created == 16 && cleaned == 1 && scrolled == 1 && text_writes == writes);
    snapshot.gps_satellite_info_valid = true;
    snapshot.gps_fix_dimension = 3; snapshot.gps_constellation_mask = 7;
    snapshot.gps_satellites_visible = 12; snapshot.gps_satellites_used = 8;
    snapshot.gps_average_cn0 = 30; snapshot.gps_hdop_valid = true;
    snapshot.gps_hdop_centi = 125;
    for (unsigned i = 0; i < 1000; i++) {
        snapshot.gps_satellites_visible = (uint8_t)(10 + i % 5);
        settings_show_gps_detail();
        assert(s_ui.settings_gps_values[0] == value);
    }
    assert(created == 16 && cleaned == 1 && scrolled == 1);
    assert(!strcmp(value->text, "3D fix, accurate speed"));
    snapshot.gps_constellation_mask = 5;
    settings_show_gps_detail();
    assert(!strcmp(s_ui.settings_gps_values[1]->text, "GPS+GLONASS"));
    snapshot.gps_constellation_mask = 6;
    settings_show_gps_detail();
    assert(!strcmp(s_ui.settings_gps_values[1]->text, "BDS+GLONASS"));
    snapshot.gps_satellite_info_valid = false;
    settings_show_gps_detail();
    assert(!strcmp(value->text, "Fix acquired"));
    snapshot.fix_valid = false;
    settings_show_gps_detail();
    assert(!strcmp(value->text, "No fix"));
    /* 返回设置、切换详情或语言时，卡片删除必须使全部引用失效。 */
    lv_obj_clean(&detail);
    for (unsigned i = 0; i < 5; i++) assert(!s_ui.settings_gps_values[i]);
    chinese = true; snapshot.fix_valid = true;
    settings_show_gps_detail();
    assert(created == 32 && !strcmp(s_ui.settings_gps_values[0]->text, "已定位"));
    assert(!strcmp(s_ui.settings_gps_values[1]->text, "等待卫星信息"));
    lv_obj_clean(&detail);
    for (unsigned i = 0; i < 5; i++) assert(!s_ui.settings_gps_values[i]);
    return 0;
}
"""


class GpsSettingsUiTest(unittest.TestCase):
    def test_gps_chinese_text_has_real_glyphs_at_both_subtitle_sizes(self):
        import re

        body = function_body(SOURCE, "settings_show_gps_detail")
        required = {character for text in re.findall(r'"([^"\n]*)"', body)
                    for character in text if ord(character) > 127}
        self.assertIn("双", required)
        for size in (13, 16):
            with self.subTest(size=size):
                font = SOURCE.with_name(f"settings_zh_{size}.c").read_text(encoding="utf-8")
                sparse = re.search(r"unicode_list_1\[\]\s*=\s*\{(.*?)\};", font, re.S).group(1)
                offsets = [int(value, 16) for value in re.findall(r"0x[0-9a-fA-F]+", sparse)]
                cmap = re.search(r"\.range_start = (\d+),[^{}]+\.glyph_id_start = (\d+),"
                                 r"[^{}]+\.unicode_list = unicode_list_1", font)
                start, first = map(int, cmap.groups())
                descriptors = re.findall(r"\{\.bitmap_index = (\d+), \.adv_w = (\d+), "
                                         r"\.box_w = (\d+), \.box_h = (\d+),[^}]+\}", font)
                glyphs = {start + offset: descriptors[first + index]
                          for index, offset in enumerate(offsets)}
                self.assertEqual(offsets, sorted(set(offsets)))
                self.assertEqual({char for char in required if ord(char) not in glyphs}, set())
                for char in required:
                    _, advance, width, height = map(int, glyphs[ord(char)])
                    self.assertGreater(min(advance, width, height), 0, char)

    def test_real_c_updates_preserve_objects_scroll_and_fix_status(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("需要主机 gcc 或 clang")
        functions = "\n".join(
            signature + function_body(SOURCE, name)
            for name, signature in {
                "settings_gps_card_delete_event_cb": "static void settings_gps_card_delete_event_cb(lv_event_t *event)",
                "settings_show_gps_detail": "void settings_show_gps_detail(void)",
            }.items()
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            source = path / "gps_ui.c"
            executable = path / "gps_ui.exe"
            source.write_text(STUBS + functions + CASES, encoding="utf-8")
            result = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-finput-charset=UTF-8", "-fexec-charset=UTF-8",
                 str(source), "-o", str(executable)],
                capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
