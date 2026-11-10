"""Exercise the shipped React UI with its installed TypeScript/React dependencies."""

import json
import re
import shutil
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class DashboardUiContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which("node") or not (ROOT / "vercel/node_modules/typescript").exists():
            raise unittest.SkipTest("Run npm ci in vercel and install Node.js first")
        result = subprocess.run(
            ["node", "-e", r'''
const fs = require("node:fs");
const vm = require("node:vm");
const { createRequire } = require("node:module");
const path = require("node:path");
const localRequire = createRequire(path.resolve("vercel/package.json"));
const ts = localRequire("typescript");
const React = localRequire("react");
const { renderToStaticMarkup } = localRequire("react-dom/server");
const source = fs.readFileSync("vercel/src/App.tsx", "utf8").replace(
  "  function setCurrentTransport", "  globalThis.bleTest = { bleApi, writeBleText, onBleValue, bleRef, pendingBle };\n  function setCurrentTransport"
) + `
module.exports.uiTest = { validSection, ManifestControl, RideRecordList, SessionList,
  formatMillivolts, formatDeciAmps, formatBmsCapacityEstimate, I18N,
  CONTROL_SERVICE_UUID, CONTROL_TX_UUID, CONTROL_RX_UUID };`;
const context = vm.createContext({
  window: { location: { pathname: "/", search: "" }, setTimeout, clearTimeout },
  document: { baseURI: "http://localhost/" }, __WEB_VERSION__: "test-web-version",
  console, URL, URLSearchParams, TextEncoder, TextDecoder, setTimeout, clearTimeout });
const modules = new Map();
function loadTs(filename, contents) {
  filename = path.resolve(filename);
  if (modules.has(filename)) return modules.get(filename);
  const loaded = { exports: {} };
  modules.set(filename, loaded);
  const requireSource = name => {
    if (!name.startsWith('.')) return localRequire(name);
    const base = path.resolve(path.dirname(filename), name);
    if (base.endsWith('.css')) return {};
    const target = [base, base + '.ts', base + '.tsx'].find(file => fs.existsSync(file));
    if (!target) throw new Error('Missing module ' + base);
    return loadTs(target).exports;
  };
  // Vite replaces import.meta.env during the production bundle; mirror its empty update-base default.
  const input = (contents ?? fs.readFileSync(filename, 'utf8')).replaceAll('import.meta.env.VITE_UPDATE_BASE_URL', JSON.stringify(''));
  const compiled = ts.transpileModule(input, { compilerOptions: {
    module: ts.ModuleKind.CommonJS, jsx: ts.JsxEmit.ReactJSX, target: ts.ScriptTarget.ES2022,
  }}).outputText;
  vm.runInContext('(function(module,exports,require){' + compiled + '\n})', context)(loaded, loaded.exports, requireSource);
  return loaded;
}
const appModule = loadTs('vercel/src/App.tsx', source);
const ui = appModule.exports.uiTest;
const t = key => ui.I18N.zh[key] || key;
const labels = { zh: "GPS 轨迹", en: "GPS track" };
const mapSection = { id: "records", label: labels,
  items: [{ id: "gps_track", kind: "map", label: labels, endpoint: "/api/gps/track" }] };
const render = (component, props) => renderToStaticMarkup(React.createElement(component, props));
const page = render(appModule.exports.default, {});
(async () => {
  const ble = context.bleTest;
  const chunks = [];
  ble.bleRef.current = { tx: { writeValueWithResponse: async chunk => chunks.push(Array.from(chunk)) } };
  const request = ble.bleApi("/api/config", { method: "POST", body: JSON.stringify({ language: "zh" }) });
  await new Promise(resolve => setImmediate(resolve));
  const envelope = JSON.parse(Buffer.from(chunks.flatMap(chunk => chunk.slice(1))).toString("utf8"));
  const response = Buffer.from(JSON.stringify({ id: envelope.id, status: 200, body: { value: "中文" } }));
  const notify = bytes => ble.onBleValue({ target: { value: new DataView(Uint8Array.from(bytes).buffer) } });
  const split = response.indexOf(Buffer.from("中文")) + 1;
  notify([1, ...response.subarray(0, split)]);
  notify([2, ...response.subarray(split)]);
  const result = await request;
  const oversized = await ble.bleApi("/api/config", { method: "POST", body: JSON.stringify({ value: "x".repeat(600) }) }).then(() => false, () => true);
  const malformed = ble.bleApi("/api/status").then(() => false, () => true);
  await new Promise(resolve => setImmediate(resolve));
  notify([2, 123, 125]);
console.log(JSON.stringify({
  page,
  ble: { envelope, result, oversized, malformed: await malformed,
    uuids: [ui.CONTROL_SERVICE_UUID, ui.CONTROL_TX_UUID, ui.CONTROL_RX_UUID],
    firstFlag: chunks[0][0], lastFlag: chunks[chunks.length - 1][0],
    maxChunk: Math.max(...chunks.map(chunk => chunk.length)), pending: ble.pendingBle.current.size },
  mapAccepted: ui.validSection(mapSection),
  invalidAccepted: ui.validSection({ ...mapSection, items: [{ id: "bad", kind: "unknown", label: labels }] }),
  millivolts: [52400, 0, null, undefined].map(ui.formatMillivolts),
  deciAmps: [-126, 0, null, undefined].map(ui.formatDeciAmps),
  capacity: ui.formatBmsCapacityEstimate({ bms_capacity_estimate_state: "ready", bms_capacity_estimate_mah: 18600 }, t),
  peaks: render(ui.RideRecordList, { t, records: [{ current: true,
    max_current: { pack_voltage_mv: 52400, current_deci_amps: -126, delta_cell_voltage_mv: 18, soc_percent: 78, temperatures_c: [28, null, 31] },
    max_delta: { pack_voltage_mv: 51900, current_deci_amps: -40, delta_cell_voltage_mv: 32, soc_percent: 76, temperatures_c: [30] },
  }] }),
  language: render(ui.ManifestControl, { item: { id: "language", kind: "select", label: { zh: "语言", en: "Language" },
    options: [{ value: "zh", label: { zh: "中文", en: "Chinese" } }, { value: "en", label: { zh: "英文", en: "English" } }] },
    language: "zh", value: "zh", busy: false, candidates: [],
    onValue() {}, onAction() {}, onLoadChoices() {}, onSubmitChoice() {} }),
  sessions: render(ui.SessionList, { language: "zh", t, available: true,
    sessions: [{ id: 7, start_s: 1000, elapsed_s: 125, samples: 63, calibrated: false, truncated: false }] }),
  noSessions: render(ui.SessionList, { language: "zh", t, available: true, sessions: [] }),
  unavailableSessions: render(ui.SessionList, { language: "zh", t, available: false, sessions: [] }),
}));
})().catch(error => { console.error(error); process.exitCode = 1; });
'''],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        if result.returncode:
            raise AssertionError(result.stderr)
        cls.output = json.loads(result.stdout)

    def test_initial_page_is_chinese_and_has_no_zeeho_vehicle_controls(self):
        page = self.output["page"]
        self.assertIn("最近骑行", page)
        self.assertIn("蓝牙连接", page)
        self.assertIn("Wi-Fi 连接", page)
        self.assertIn("系统信息", page)
        self.assertNotIn("device-subnav", page)
        self.assertNotIn("my-list-row", page)
        self.assertNotIn(">我的</span>", page)
        self.assertIn('>设置</span>', page)
        for card in ("bluetooth", "wifi", "system", "device"):
            self.assertIn(f'aria-labelledby="settings-{card}-title"', page)
        self.assertEqual(1, page.count('aria-labelledby="settings-update-title"'))
        self.assertEqual(3, page.count('class="update-row"'))
        for title in ("App 更新", "Web 前端更新", "固件更新"):
            self.assertIn(f'aria-label="{title}"', page)
            self.assertIn(f'aria-label="{title} · 查看更新"', page)
        self.assertNotIn('role="dialog"', page)
        self.assertNotIn("再次检查", page)
        self.assertIn('name="password"', page)
        self.assertIn('面板版本', page)
        self.assertIn('App 版本', page)
        self.assertIn('aria-label="功能"></section>', page)
        self.assertNotIn('class="chip">AE5</span>', page)
        for removed in ("ZEEHO", "zeeho", "车辆位置", "车辆设置", "解锁车辆", "鸣笛", "寻车"):
            self.assertNotIn(removed, page)

    def test_web_uses_original_dashboard_shell_and_rendered_source_parts(self):
        page = self.output["page"]
        for marker in ('class="stage web-preview"', 'id="phone"', 'class="preview-statusbar"',
                       'id="topbar"', 'id="scroll"', 'id="tabbar"', 'class="tab-ind"',
                       'data-num=', 'class="bar grow"', 'hero-in'):
            self.assertIn(marker, page)
        self.assertEqual(3, len(re.findall(r'data-tab="', page)))
        self.assertIn('ae5', page.lower())
        self.assertIn('>功能</span>', page)
        self.assertIn('<img', page)
        title = re.search(r'class="app-title"[^>]*>(.*?)</', page).group(1)
        self.assertIn("两轮智控", title)
        self.assertNotIn("button", title)
        self.assertNotIn("192.168", title)

    def test_device_units_preserve_zero_and_missing_values(self):
        self.assertEqual(["52.40 V", "0.00 V", "--", "--"], self.output["millivolts"])
        self.assertEqual(["-12.6 A", "0.0 A", "--", "--"], self.output["deciAmps"])
        self.assertEqual("18.6 Ah", self.output["capacity"])

    def test_gps_manifest_does_not_hide_all_device_settings(self):
        self.assertTrue(self.output["mapAccepted"])
        self.assertFalse(self.output["invalidAccepted"])

    def test_peak_records_render_real_device_measurements(self):
        peaks = self.output["peaks"]
        for measurement in ("52.40 V", "-12.6 A", "32", "78"):
            self.assertIn(measurement, peaks)
        self.assertNotIn("null", peaks)
        self.assertIn('name="language"', self.output["language"])
        self.assertIn("中文", self.output["language"])

    def test_recent_sessions_use_device_duration_and_uncalibrated_label(self):
        sessions = self.output["sessions"]
        self.assertIn("#7", sessions)
        self.assertIn("2m 5s", re.sub(r"<[^>]*>", "", sessions))
        self.assertIn("63", sessions)
        self.assertNotIn("1970", sessions)
        self.assertNotIn("km", sessions)
        self.assertNotEqual(self.output["noSessions"], self.output["unavailableSessions"])

    def test_browser_ble_matches_firmware_framing_and_json_envelope(self):
        ble = self.output["ble"]
        self.assertEqual({"v": 1, "id": 1, "method": "POST", "path": "/api/config", "body": {"language": "zh"}}, ble["envelope"])
        self.assertEqual({"value": "中文"}, ble["result"])
        self.assertEqual(1, ble["firstFlag"])
        self.assertEqual(2, ble["lastFlag"])
        self.assertLessEqual(ble["maxChunk"], 20)
        self.assertTrue(ble["oversized"])
        self.assertTrue(ble["malformed"])
        self.assertEqual(0, ble["pending"])
        self.assertEqual([
            "4f91e100-23b2-4ee7-9f9a-86d1093f0a01",
            "4f91e100-23b2-4ee7-9f9a-86d1093f0a02",
            "4f91e100-23b2-4ee7-9f9a-86d1093f0a03",
        ], ble["uuids"])


if __name__ == "__main__":
    unittest.main()
