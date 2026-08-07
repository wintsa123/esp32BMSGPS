#!/usr/bin/env python3
"""esp_bms_lvgl_ui.c 拆分脚本：计算归属 + 导出分析 + 生成文件"""
import re, os, sys
from collections import Counter

SRC = '.trellis/esp_bms_lvgl_ui_orig.c'
OUT = sys.argv[1] if len(sys.argv) > 1 else '.trellis/ui_out'
lines = open(SRC, encoding='utf-8').read().split('\n')

# ---------- 函数定义提取（括号匹配） ----------
def func_body_end(start_ln):
    depth = 0
    state = 'code'
    for i in range(start_ln - 1, len(lines)):
        t = lines[i]
        j = 0
        while j < len(t):
            ch = t[j]
            nxt = t[j+1] if j+1 < len(t) else ''
            if state == 'code':
                if ch == '/' and nxt == '/': state = 'line_comment'; j += 2; continue
                if ch == '/' and nxt == '*': state = 'block_comment'; j += 2; continue
                if ch == '"': state = 'str'; j += 1; continue
                if ch == "'": state = 'char'; j += 1; continue
                if ch == '{': depth += 1
                elif ch == '}':
                    depth -= 1
                    if depth == 0: return i + 1
                j += 1
            elif state == 'str':
                if ch == '\\': j += 2; continue
                if ch == '"': state = 'code'
                j += 1
            elif state == 'char':
                if ch == '\\': j += 2; continue
                if ch == "'": state = 'code'
                j += 1
            elif state == 'line_comment':
                j = len(t)
            elif state == 'block_comment':
                if ch == '*' and nxt == '/': state = 'code'; j += 2; continue
                j += 1
        if state == 'line_comment': state = 'code'
    return None

def extract_sig(start_ln):
    """拼接签名直到 '{' 所在行，返回纯签名文本（无 static、无 {）"""
    sig = ''
    for i in range(start_ln - 1, min(start_ln + 25, len(lines))):
        t = lines[i].strip()
        sig += t + ' '
        if '{' in t:
            break
    sig = sig.strip()
    if sig.endswith('{'):
        sig = sig[:-1].rstrip()
    sig = re.sub(r'^static\s+', '', sig)
    return sig

# 条件栈
def build_cond():
    stack, line_cond = [], {}
    for i, l in enumerate(lines, 1):
        t = l.strip()
        if t.startswith('#if '): stack.append(t[3:].strip())
        elif t.startswith('#ifdef'): stack.append('defined(' + t[6:].strip() + ')')
        elif t.startswith('#ifndef'): stack.append('!defined(' + t[7:].strip() + ')')
        elif t.startswith('#elif'): stack[-1] = '!((' + stack[-1] + ')) && (' + t[5:].strip() + ')'
        elif t.startswith('#else'): stack[-1] = '!((' + stack[-1] + '))'
        elif t.startswith('#endif') and stack: stack.pop()
        line_cond[i] = list(stack)
    return line_cond

line_cond = build_cond()

PREFIX = re.compile(r'^(?:static\s+)?(?:esp_err_t|bool|esp_bms_lvgl_data_source_t|uint8_t|uint32_t|int32_t)\b|^lv_obj_t \*|^static ')

def is_definition(start_ln):
    """向后拼接：先遇 '{' 为定义，先遇 ';'（括号平衡）为声明"""
    depth = 0
    for i in range(start_ln - 1, min(start_ln + 25, len(lines))):
        t = lines[i]
        depth += t.count('(') - t.count(')')
        if '{' in t:
            return True
        if ';' in t and depth <= 0:
            return False
    return False

def find_defs():
    defs, seen = [], set()
    for i, l in enumerate(lines, 1):
        t = l.strip()
        if not t or t.startswith(('#', 'typedef', 'extern')) or '(' not in t: continue
        if not PREFIX.match(t): continue
        m = re.search(r'([a-z_]\w*)\s*\(', t)
        if not m: continue
        name = m.group(1)
        if name in seen: continue
        if '=' in t[:m.start(1)] or '(' in t[:m.start(1)]: continue
        if not is_definition(i): continue   # 前向声明（; 结尾）跳过
        end = func_body_end(i)
        if end is None: continue   # 括号不匹配，视为非函数
        # 判别：签名中函数名后是 '('，且前面有类型（名字前至少一个空格）
        seen.add(name)
        defs.append((name, i, end, list(line_cond.get(i, []))))
    return defs

defs = find_defs()
print("定义函数数:", len(defs))

# ---------- 函数 -> 文件映射 ----------
SPECIAL = {
    'controller_gear_text':'core','clear_style':'core','dashboard_native_landscape_enabled':'core',
    'bms_native_landscape_enabled':'core','bms_native_portrait_enabled':'core',
    'dashboard_static_cache_alloc':'core','dashboard_static_cache_free':'core',
    'dashboard_static_cache_enabled':'core','dashboard_static_cache_release_one':'core',
    'dashboard_static_cache_release':'core','dashboard_static_cache_finalize':'core',
    'label_set_text_if_changed':'core','bms_label_set':'core','bms_native_set_safety_status':'core',
    'label_set_text_fmt_if_changed':'core','label_set_text_color_if_changed':'core','set_obj_hidden':'core',
    'settings_view_is_visible':'core','quick_pull_start_allowed':'core','return_home_start_allowed':'core',
    'show_dashboard_view':'core','show_settings_view':'core',
    'queue_action_with_commit':'core','queue_action':'core','queue_bms_bind_action':'core',
    'queue_controller_bind_action':'core','queue_touch_calibration_sample':'core',
    'clamp_brightness_percent':'core','clamp_volume_percent':'core',
    'get_active_pointer':'core','ui_flag_bit':'core','ui_flag_get':'core','ui_flag_set':'core',
    'ui_state_flag_get':'core','ui_state_flag_set':'core',
    'perform_ui_action':'core','action_event_cb':'core','process_return_swipe_event':'core',
    'return_swipe_event_cb':'core','quick_pull_event_cb':'core',
    'apply_dashboard_snapshot':'core','defer_dashboard_snapshot':'core',
    'flush_deferred_dashboard_snapshot':'core','rebuild_screen_if_needed':'core',
    'panel':'bms_dash','label':'bms_dash','dashboard_viewport':'bms_dash','dashboard_separator':'bms_dash',
    'dashboard_panel':'bms_dash','dashboard_battery_icon':'bms_dash','bms_native_static_label':'bms_dash',
    'update_dashboard_battery_icon':'bms_dash','dashboard_thermometer_icon':'bms_dash',
    'bms_native_layout':'bms_dash','bms_native_safety_icon':'bms_dash','bms_native_safety_check':'bms_dash',
    'create_native_bms_dashboard':'bms_dash','dashboard_native_layer':'bms_dash',
    'create_native_bms_portrait_dashboard':'bms_dash','dashboard_cell_key_draw_buf':'bms_dash',
    'dashboard_cell_key_draw':'bms_dash','dashboard_cell_key':'bms_dash',
    'dashboard_soc_fill_color':'bms_dash',
    'abs_i32':'quick','clamp_i32':'quick',
    'set_quick_brightness_value':'quick','set_quick_volume_value':'quick',
    'refresh_quick_level_layouts':'quick','set_quick_panel_open':'quick','set_quick_edit_mode':'quick',
    'update_quick_item_colors':'quick',
    'format_mv':'pages_common','format_deci_amps':'pages_common','format_cell_v':'pages_common',
    'format_temp_c':'pages_common','set_header':'pages_common','set_setup_ap':'pages_common',
    'set_cast_page':'pages_common','music_control_set_enabled':'pages_common',
    'set_music_page':'pages_common','set_dashboard':'pages_common',
    'set_controller_dashboard':'controller_dash','create_controller_dashboard':'controller_dash',
    'speed_dashboard_style_apply':'controller_dash',
    'controller_label_set':'controller_dash','controller_dashboard_panel':'controller_dash',
    'controller_dashboard_label':'controller_dash','controller_dashboard_vertical_separator':'controller_dash',
    'esp_bms_lvgl_ui_speed_dashboard_style_available':'settings_pickers',
    'speed_dashboard_style_from_snapshot':'settings_pickers','settings_dashboard_style_label':'settings_pickers',
    'settings_show_controller_style_picker':'settings_pickers','settings_show_speed_unit_picker':'settings_pickers',
    'settings_show_speed_source_picker':'settings_pickers',
    'boot_animation_style_is_available':'boot_ota','boot_animation_style_is_gauge':'boot_ota',
    'esp_bms_lvgl_ui_boot_start':'boot_ota','esp_bms_lvgl_ui_boot_update':'boot_ota',
    'esp_bms_lvgl_ui_boot_finish':'boot_ota','esp_bms_lvgl_ui_ota_update':'boot_ota',
    'esp_bms_lvgl_ui_ota_finish':'boot_ota',
    'settings_show_detail':'settings_system','settings_option_event_cb':'settings_system',
    'settings_show_boot_animation_picker':'settings_system','settings_show_system_view':'settings_system',
    'set_gps_dashboard':'speed','create_gps_dashboard':'speed','speed_page_sync':'speed',
    'speed_dashboard_point':'speed','gps_label_set':'speed',
    'set_fireblade_dashboard':'fireblade','create_fireblade_dashboard':'fireblade',
    'create_gps_page_content':'screen','create_cast_page_content':'screen',
    'create_music_page_content':'screen','create_battery_page_content':'screen',
    'invalidate_dashboard_viewport':'screen','finish_page_scroll_state':'screen','move_to_page':'screen',
    'dashboard_battery_pointers_reset':'screen','dashboard_gps_pointers_reset':'screen',
    'dashboard_pages_release_except':'screen',
    'bluetooth_status_text':'settings',
}
PREFIX_RULES = [
    ('esp_bms_lvgl_ui_simulator_', 'simulator'), ('esp_bms_lvgl_ui_', 'core'),
    ('simulator_', 'simulator'),
    ('settings_restore_', 'settings_system'), ('settings_detail_action_', 'settings_system'),
    ('settings_detail_switch', 'settings_system'), ('settings_bms_type_', 'settings_system'),
    ('settings_bms_bind_', 'settings_system'), ('settings_bms_ble_candidate_', 'settings_system'),
    ('settings_bms_ble_refresh_', 'settings_system'), ('settings_system_', 'settings_system'),
    ('settings_calibration_', 'settings_system'), ('settings_show_touch_calibration', 'settings_system'),
    ('settings_boot_preview_', 'settings_system'), ('settings_boot_animation_option_', 'settings_system'),
    ('settings_controller_style_', 'settings_pickers'), ('settings_speed_unit_', 'settings_pickers'),
    ('settings_speed_source_', 'settings_pickers'),
    ('settings_', 'settings'), ('quick_', 'quick'), ('speed_dashboard_', 'speed'),
    ('fireblade_', 'fireblade'), ('boot_', 'boot_ota'), ('ota_', 'boot_ota'),
    ('screen_', 'screen'), ('page_', 'screen'), ('dashboard_page_', 'screen'),
    ('create_screen', 'screen'), ('native_', 'core'), ('format_', 'pages_common'),
]
def target_file(name):
    if name in SPECIAL: return SPECIAL[name]
    for p, f in PREFIX_RULES:
        if name.startswith(p): return f
    return None

unmatched = [d[0] for d in defs if target_file(d[0]) is None]
if unmatched:
    print("!! 未匹配:", unmatched); sys.exit(1)

# ---------- 行归属 ----------
STATIC = [
    (1, 18, 'internal'), (19, 20, 'internal'),
    (37, 60, 'internal'), (61, 753, 'internal'),
    (754, 758, 'core'), (759, 803, 'core'),
    (809, 826, 'bms_dash'), (827, 853, 'DROP'),
    (855, 919, 'core'), (920, 922, 'internal'), (923, 923, 'core'), (924, 955, 'bms_dash'), (956, 986, 'core'),
    (14497, 14497, 'internal'),
]
def static_file(ln):
    for a, b, f in STATIC:
        if a <= ln <= b: return f
    return None

func_ranges = sorted([(n, a, b) for n, a, b, c in defs], key=lambda x: x[1])
def func_file(ln):
    for n, a, b in func_ranges:
        if a <= ln <= b: return target_file(n)
    return None

def prev_func(ln):
    prev = None
    for n, a, b in func_ranges:
        if a > ln: break
        prev = (n, a, b)
    return prev
def next_func(ln):
    for n, a, b in func_ranges:
        if b >= ln: return (n, a, b)
    return None

# typedef 块（含跨行）强制归 internal.h
def typedef_block_lines():
    tl = set()
    for i, l in enumerate(lines, 1):
        t = l.strip()
        if not t.startswith('typedef '):
            continue
        if t.endswith(';') and '{' not in t:
            tl.add(i)
            continue
        depth = 0
        for j in range(i, min(i + 80, len(lines) + 1)):
            tl.add(j)
            s = lines[j - 1]
            depth += s.count('{') - s.count('}')
            if depth == 0 and s.rstrip().endswith(';'):
                break
    return tl

TYPEDEF_LINES = typedef_block_lines()

def is_fwd_decl(ln):
    """函数前向声明（含多行，以 ; 结束）→ 丢弃，脚本会重新生成。
    判别：行首是 static/类型 + 函数名后紧跟 '(' + 遇 ';' 结束（遇 '{' 则视为定义）"""
    t = lines[ln-1].strip()
    if '{' in t or t.startswith('#') or not PREFIX.match(t):
        return False
    m = re.search(r'([a-z_]\w*)\s*\(', t)
    if not m or '=' in t[:m.start(1)] or '(' in t[:m.start(1)]:
        return False
    sig = t
    for i in range(ln, min(ln + 12, len(lines) + 1)):
        if sig.rstrip().endswith(';'):
            return True
        if '{' in sig:
            return False
        sig += ' ' + lines[i-1].strip()
    return False

# 前向声明整块行（含续行）→ 丢弃
FWD_DROP_LINES = set()
for _ln in range(1, len(lines) + 1):
    if is_fwd_decl(_ln):
        _sig = lines[_ln-1]
        _end = _ln
        for _i in range(_ln, min(_ln + 12, len(lines) + 1)):
            if _sig.rstrip().endswith(';'):
                break
            _sig += lines[_i-1]
            _end = _i
        for _j in range(_ln, _end + 1):
            FWD_DROP_LINES.add(_j)

# 条件指令配对：#endif/#else/#elif -> 所属 #if 行
IF_ANCHOR = {}
_cond_stack = []
for _ln in range(1, len(lines) + 1):
    _t = lines[_ln-1].strip()
    if _t.startswith(('#if ', '#ifdef', '#ifndef')):
        _cond_stack.append(_ln)
    elif _t.startswith(('#elif', '#else')):
        if _cond_stack:
            IF_ANCHOR[_ln] = _cond_stack[-1]
    elif _t.startswith('#endif'):
        if _cond_stack:
            IF_ANCHOR[_ln] = _cond_stack.pop()

def file_of_line(ln):
    if ln in FWD_DROP_LINES:
        return None
    if ln in TYPEDEF_LINES:
        return 'internal'
    ff = func_file(ln)
    if ff: return ff
    sf = static_file(ln)
    if sf: return None if sf == 'DROP' else sf
    t = lines[ln-1].strip()
    COND = ('#if ', '#ifdef', '#ifndef', '#else', '#elif', '#endif')
    if t.startswith(('#if ', '#ifdef', '#ifndef', '#else', '#elif', '#endif')):
        anchor = IF_ANCHOR.get(ln, ln)
        for j in range(anchor + 1, len(lines) + 1):
            tj = lines[j-1].strip()
            if not tj.startswith(COND):
                return file_of_line(j)
        return None
    if is_fwd_decl(ln):
        return None
    p = prev_func(ln); return target_file(p[0]) if p else None

line_file = {ln: file_of_line(ln) for ln in range(1, len(lines) + 1)}

# ---------- 导出分析 ----------
exports = {}
for name, a, b, cond in defs:
    refs = set()
    for i, l in enumerate(lines, 1):
        if a <= i <= b: continue
        if re.search(r'\b' + re.escape(name) + r'\b', l):
            f = line_file.get(i)
            if f and f != target_file(name): refs.add(f)
    if refs: exports[name] = sorted(refs)

FORCE_EXPORT = {'ui_state_flag_get', 'ui_state_flag_set'}
for name in FORCE_EXPORT:
    exports.setdefault(name, ['internal'])
print("导出函数数:", len(exports))
c = Counter(target_file(n) for n in exports)
for f, n in sorted(c.items()): print(f"  {f}: {n}")

# ---------- 生成文件 ----------
os.makedirs(OUT, exist_ok=True)

def emit(fname, header_note, body_lines):
    with open(os.path.join(OUT, fname), 'w', encoding='utf-8') as f:
        f.write("/*\n * %s\n * 由 ui_split.py 从 esp_bms_lvgl_ui.c 拆分生成（按功能模块）。\n */\n" % header_note)
        f.write('#include "esp_bms_lvgl_ui_internal.h"\n\n')
        f.write('\n'.join(body_lines))
        f.write('\n')

# 每个文件的函数（按行号排序）
file_funcs = {}   # file -> [(name, a, b, cond)]
for name, a, b, cond in defs:
    file_funcs.setdefault(target_file(name), []).append((name, a, b, cond))

def is_exported(name):
    return name in exports

# 收集每个文件的静态区间行 + 函数行
def file_lines(f):
    """返回 (行号, 文本) 列表"""
    out = []
    for ln in range(1, len(lines) + 1):
        if line_file.get(ln) == f:
            out.append((ln, lines[ln-1]))
    return out

def needs_forward_decl(f, name, a):
    """文件 f 内是否有引用 name 的行号 < a"""
    for ln, l in enumerate(lines, 1):
        if ln >= a: break
        if line_file.get(ln) != f: continue
        if re.search(r'\b' + re.escape(name) + r'\b', l):
            return True
    return False

# 文件级 static const 表（core/bms_dash 定义）-> extern 声明
extern_names = []
for ln in range(1, len(lines) + 1):
    if func_file(ln):
        continue
    f = line_file.get(ln)
    if f not in ('core', 'bms_dash'):
        continue
    t = lines[ln-1].strip()
    m = re.match(r'static\s+const\s+([\w\s\*]+?)\s+([A-Z][A-Z0-9_]+)\s*(\[[^;]*?\])?\s*=', t)
    if m:
        ty, name, dim = m.group(1).strip(), m.group(2), m.group(3) or ''
        extern_names.append((ty, name, dim, ln))
EXTERN_DEF_LINES = set(ln for _, _, _, ln in extern_names)
EXTERN_VAR_LINES = {755, 756, 757}

# 生成各模块文件
for f in ['bms_dash', 'quick', 'settings', 'settings_pickers', 'settings_system',
          'pages_common', 'controller_dash', 'fireblade', 'speed', 'screen',
          'boot_ota', 'simulator']:
    body = []
    # 前向声明（模块内 static 函数，先使用后定义）
    fwd = []
    for name, a, b, cond in file_funcs.get(f, []):
        if not is_exported(name) and needs_forward_decl(f, name, a):
            fwd.append((name, a))
    if fwd:
        body.append('/* 文件内前向声明 */')
        for name, a in sorted(fwd, key=lambda x: x[1]):
            body.append('static %s;' % extract_sig(a))
        body.append('')
    # 主体：静态区间行 + 函数行（按行号）
    prev_ln = 0
    for ln, text in file_lines(f):
        # 函数定义行去 static（导出函数）
        ff = func_file(ln)
        if ff == f and ln in [a for n, a, b, c in file_funcs.get(f, [])]:
            name = next(n for n, a0, b0, c0 in file_funcs[f] if a0 == ln)
            if is_exported(name):
                text = re.sub(r'^static\s+', '', text)
        elif ln in EXTERN_DEF_LINES or ln in EXTERN_VAR_LINES:
            text = re.sub(r'^static\s+', '', text)
        body.append(text)
    fname = {'bms_dash':'ui_bms_dashboard.c','quick':'ui_quick_panel.c',
             'settings':'ui_settings.c','settings_pickers':'ui_settings_pickers.c',
             'settings_system':'ui_settings_system.c','pages_common':'ui_dashboard_pages.c',
             'controller_dash':'ui_dashboard_controller.c','fireblade':'ui_dashboard_fireblade.c',
             'speed':'ui_dashboard_speed.c','screen':'ui_screen.c',
             'boot_ota':'ui_boot_ota.c','simulator':'ui_simulator.c'}[f]
    emit(fname, 'UI 模块: ' + f, body)
    print("生成", fname, len(body), "行")

# ---------- internal.h ----------
hdr = []
hdr.append('#pragma once')
hdr.append('')
# 全部 internal 行（include、TAG、字体声明、宏、类型、typedef 块）按原顺序输出
for ln in range(1, len(lines) + 1):
    if line_file.get(ln) == 'internal':
        hdr.append(lines[ln-1])
hdr.append('')

# extern 变量
hdr.append('/* ---- 跨模块共享状态（定义见 esp_bms_lvgl_ui.c / ui_bms_dashboard.c） ---- */')
hdr.append('extern esp_bms_lvgl_ui_t s_ui;')
hdr.append('extern bool s_touch_calibration_supported;')
hdr.append('extern bool s_native_gestures_supported;')
hdr.append('')
# 文件级 static const 表（core/bms_dash 定义）-> extern 声明
def array_dim(ln):
    """解析数组初始化列表的顶层元素个数。

    按顶层 '{...}' 初始化器计数，不受结尾逗号影响。若列表内出现
    #if/#endif 等预处理指令（条件编译会改变最终元素个数，无法静态
    求值），返回 None 并打印警告，由人工核对。
    """
    depth = 0
    count = 0
    started = False
    in_str = False
    warned = False
    for i in range(ln - 1, len(lines)):
        t = lines[i]
        if t.lstrip().startswith('#') and not t.lstrip().startswith('#include'):
            if depth == 1 and not warned:
                print("WARN: array at line %d contains preprocessor "
                      "directives; size cannot be inferred safely" % ln)
                warned = True
            continue
        j = 0
        while j < len(t):
            ch = t[j]
            if in_str:
                if ch == '\\':
                    j += 2
                    continue
                if ch == '"':
                    in_str = False
                j += 1
                continue
            if ch == '"':
                in_str = True
                j += 1
                continue
            if not started:
                if ch == '{':
                    started = True
                    depth = 1
            elif ch == '{':
                if depth == 1:
                    count += 1
                depth += 1
            elif ch == '}':
                depth -= 1
                if depth == 0:
                    if warned:
                        return None
                    return count
            j += 1
    if warned:
        return None
    return count

for ty, name, dim, ln in extern_names:
    if dim == '[]':
        n = array_dim(ln)
        dim = '[%d]' % n if n else '[]'
    hdr.append('extern const %s %s%s;' % (ty, name, dim))
hdr.append('')
# 宏 UI_FLAG（core 801-802 移到头）
hdr.append('#define UI_FLAG(name) ui_state_flag_get(UI_STATE_FLAG_##name)')
hdr.append('#define UI_SET_FLAG(name, enabled) ui_state_flag_set(UI_STATE_FLAG_##name, (enabled))')
hdr.append('')

# 函数声明（导出函数，按文件分组）
for f in ['core', 'bms_dash', 'quick', 'settings', 'settings_pickers', 'settings_system',
          'pages_common', 'controller_dash', 'fireblade', 'speed', 'screen',
          'boot_ota', 'simulator']:
    group = [(n, a, b, c) for n, a, b, c in defs if target_file(n) == f and n in exports]
    if not group: continue
    hdr.append('/* ---- %s ---- */' % f)
    for name, a, b, cond in sorted(group, key=lambda x: x[1]):
        sig = extract_sig(a)
        if cond:
            hdr.append('#if %s' % ' && '.join(cond))
            hdr.append('%s;' % sig)
            hdr.append('#endif')
        else:
            hdr.append('%s;' % sig)
    hdr.append('')

with open(os.path.join(OUT, 'esp_bms_lvgl_ui_internal.h'), 'w', encoding='utf-8') as f:
    f.write("/*\n * esp_bms_lvgl_ui 组件内部共享头（拆分自 esp_bms_lvgl_ui.c）。\n * 仅供组件内 .c 文件包含，不属于公共 API。\n */\n")
    f.write('\n'.join(hdr))
    f.write('\n')
print("生成 esp_bms_lvgl_ui_internal.h", len(hdr), "行")

# ---------- 核心文件 ----------
core_body = []
# 文件头
core_head = ['#include "esp_bms_lvgl_ui.h"', '#include "esp_bms_lvgl_ui_internal.h"', '']
core_body.extend(core_head)
fwd = []
for name, a, b, cond in file_funcs.get('core', []):
    if not is_exported(name) and needs_forward_decl('core', name, a):
        fwd.append((name, a))
if fwd:
    core_body.append('/* 文件内前向声明 */')
    for name, a in sorted(fwd, key=lambda x: x[1]):
        core_body.append('static %s;' % extract_sig(a))
    core_body.append('')
for ln, text in file_lines('core'):
    ff = func_file(ln)
    if ff == 'core' and ln in [a for n, a, b, c in file_funcs['core']]:
        name = next(n for n, a0, b0, c0 in file_funcs['core'] if a0 == ln)
        if is_exported(name):
            text = re.sub(r'^static\s+', '', text)
    elif ln in EXTERN_DEF_LINES or ln in EXTERN_VAR_LINES:
        text = re.sub(r'^static\s+', '', text)
    core_body.append(text)
with open(os.path.join(OUT, 'esp_bms_lvgl_ui.c'), 'w', encoding='utf-8') as f:
    f.write("/*\n * esp_bms_lvgl_ui 核心：入口 API、全局状态、手势与页面管理。\n * 由 ui_split.py 从原单文件拆分生成。\n */\n")
    f.write('\n'.join(core_body))
    f.write('\n')
print("生成 esp_bms_lvgl_ui.c", len(core_body), "行")

# ---------- 统计 ----------
total = 0
for fn in os.listdir(OUT):
    sz = os.path.getsize(os.path.join(OUT, fn))
    n = sum(1 for _ in open(os.path.join(OUT, fn), encoding='utf-8'))
    print(f"  {fn}: {n} 行, {sz//1024}KB")
    total += n
print("合计行数:", total)
