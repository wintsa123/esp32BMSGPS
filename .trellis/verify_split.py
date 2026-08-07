#!/usr/bin/env python3
"""验证 ui_split.py 生成的各 .c 文件与原文件逐行一致（去 static / 前向声明除外）"""
import re, sys

src_code = open('.trellis/ui_split.py', encoding='utf-8').read()
calc = src_code.split('# ---------- 生成文件')[0].replace(
    'OUT = sys.argv[1] if len(sys.argv) > 1 else \'.trellis/ui_out\'', 'OUT = ".trellis/ui_out"')
exec(calc)

file_funcs = {}
for name, a, b, cond in defs:
    file_funcs.setdefault(target_file(name), []).append((name, a, b, cond))

EXTERN_VAR_LINES = {755, 756, 757}

# 复用 ui_split.py 的 extern 表收集
import re as _re
EXTERN_DEF_LINES = set()
for ln in range(1, len(lines) + 1):
    if func_file(ln):
        continue
    f = line_file.get(ln)
    if f not in ('core', 'bms_dash'):
        continue
    t = lines[ln-1].strip()
    if _re.match(r'static\s+const\s+([\w\s\*]+?)\s+[A-Z][A-Z0-9_]+\s*(\[[^;]*?\])?\s*=', t):
        EXTERN_DEF_LINES.add(ln)

is_exported = lambda name: name in exports


def needs_forward_decl(f, name, a):
    for ln, l in enumerate(lines, 1):
        if ln >= a:
            break
        if line_file.get(ln) != f:
            continue
        if re.search(r'\b' + re.escape(name) + r'\b', l):
            return True
    return False


def strip_static_for_export(text, f, ln):
    ff = func_file(ln)
    if ff == f and ln in [a for n, a, b, c in file_funcs.get(f, [])]:
        name = next(n for n, a0, b0, c0 in file_funcs[f] if a0 == ln)
        if name in exports:
            return re.sub(r'^static\s+', '', text)
    if ln in EXTERN_DEF_LINES or ln in EXTERN_VAR_LINES:
        return re.sub(r'^static\s+', '', text)
    return text


def expected_body(f):
    body = []
    fwd = []
    for name, a, b, cond in file_funcs.get(f, []):
        if not is_exported(name) and needs_forward_decl(f, name, a):
            fwd.append((name, a))
    if fwd:
        body.append('/* 文件内前向声明 */')
        for name, a in sorted(fwd, key=lambda x: x[1]):
            body.append('static %s;' % extract_sig(a))
        body.append('')
    for ln in range(1, len(lines) + 1):
        if line_file.get(ln) == f:
            body.append(strip_static_for_export(lines[ln - 1], f, ln))
    while body and body[-1] == '':
        body.pop()
    return body


def actual_body(path):
    t = open(path, encoding='utf-8').read().split('\n')
    while t and t[-1] == '':
        t.pop()
    # 跳过头部注释块 + include 行 + 空行
    i = 0
    if t and t[0].startswith('/*'):
        while i < len(t) and '*/' not in t[i]:
            i += 1
        i += 1
    while i < len(t) and (t[i].startswith('#include') or t[i] == ''):
        i += 1
    return t[i:]


files_map = {'bms_dash': 'ui_bms_dashboard.c', 'quick': 'ui_quick_panel.c',
             'settings': 'ui_settings.c', 'settings_pickers': 'ui_settings_pickers.c',
             'settings_system': 'ui_settings_system.c', 'pages_common': 'ui_dashboard_pages.c',
             'controller_dash': 'ui_dashboard_controller.c', 'fireblade': 'ui_dashboard_fireblade.c',
             'speed': 'ui_dashboard_speed.c', 'screen': 'ui_screen.c',
             'boot_ota': 'ui_boot_ota.c', 'simulator': 'ui_simulator.c',
             'core': 'esp_bms_lvgl_ui.c'}
ok = True
for f, fname in files_map.items():
    exp = expected_body(f)
    act = actual_body('.trellis/ui_out/' + fname)
    if exp != act:
        ok = False
        print(f"!! {fname}: 预期 {len(exp)} 行, 实际 {len(act)} 行")
        for i, (e, a) in enumerate(zip(exp, act)):
            if e != a:
                print(f"  差异@{i}: 预期={e[:70]!r}")
                print(f"         实际={a[:70]!r}")
                break
        m = min(len(exp), len(act))
        if len(exp) > len(act):
            print("  预期多出:", [x[:60] for x in exp[m:m + 4]])
        elif len(act) > len(exp):
            print("  实际多出:", [x[:60] for x in act[m:m + 4]])
    else:
        print(f"  OK {fname} ({len(exp)} 行)")
print("=== 全部一致 ===" if ok else "=== 存在差异 ===")
sys.exit(0 if ok else 1)
