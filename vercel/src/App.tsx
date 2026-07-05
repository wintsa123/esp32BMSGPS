import { FormEvent, useEffect, useMemo, useRef, useState } from "react";

const CONTROL_SERVICE_UUID = "7b47f060-0f68-4ad2-9a25-7f3d36a6c7a1";
const CONTROL_TX_UUID = "7b47f061-0f68-4ad2-9a25-7f3d36a6c7a1";
const CONTROL_RX_UUID = "7b47f062-0f68-4ad2-9a25-7f3d36a6c7a1";

type Language = "zh" | "en";
type Transport = "http" | "ble";
type Tab = "overview" | "network" | "device" | "update";
type JsonRecord = Record<string, unknown>;

type Status = {
  version?: string;
  speed?: string;
  speed_unit?: string;
  bms?: string;
  wifi?: string;
  ota?: string;
  local_battery_mv?: number | null;
  pack_voltage_mv?: number | null;
};

type Config = {
  brightness?: number;
  display_rotation?: string;
  speed_unit?: string;
  language?: Language;
  external_ssid?: string;
  bms_mac?: string | null;
};

type BmsCandidate = {
  mac?: string;
  name?: string | null;
  rssi?: number;
};

type BleState = {
  tx?: BluetoothRemoteGATTCharacteristic;
  rx?: BluetoothRemoteGATTCharacteristic;
  deviceName?: string;
};

type PendingBle = {
  resolve: (value: unknown) => void;
  reject: (error: Error) => void;
  timer: number;
};

const I18N = {
  zh: {
    apPassword: "设置热点密码",
    battery: "电池",
    bindBms: "绑定 BMS",
    bluetoothUnsupported: "当前浏览器不支持 Web Bluetooth",
    bms: "BMS",
    brightness: "亮度",
    checkUpdate: "检查更新",
    clearPassword: "清除密码",
    connectBle: "蓝牙",
    connectHttp: "热点 API",
    connected: "已连接",
    connecting: "连接中",
    device: "设备",
    deviceAddress: "设备地址",
    disabled: "关闭",
    disconnected: "已断开",
    downloading: "下载中",
    failed: "失败",
    firmware: "固件",
    httpConnected: "热点 API 已连接",
    idle: "空闲",
    language: "语言",
    network: "网络",
    noCandidates: "暂无候选设备",
    offline: "离线",
    online: "在线",
    overview: "概览",
    passwordCanceled: "已取消密码输入",
    passwordCleared: "密码已清除",
    passwordPrompt: "输入 ESP32 设置密码",
    passwordWrong: "密码错误",
    refresh: "刷新",
    readyToReboot: "待重启",
    saved: "已保存",
    saveDevice: "保存设备",
    saveWifi: "保存 Wi-Fi",
    scanBms: "扫描 BMS",
    screenRotation: "屏幕方向",
    setupAp: "设置热点",
    speed: "速度",
    speedUnit: "速度单位",
    startUpdate: "开始更新",
    update: "更新",
    updateAvailable: "有更新",
    verifying: "校验中",
    wifi: "Wi-Fi",
    wifiPassword: "外部 Wi-Fi 密码",
    wifiSsid: "外部 Wi-Fi SSID",
  },
  en: {
    apPassword: "Setup AP password",
    battery: "Battery",
    bindBms: "Bind BMS",
    bluetoothUnsupported: "Web Bluetooth is not supported",
    bms: "BMS",
    brightness: "Brightness",
    checkUpdate: "Check update",
    clearPassword: "Clear password",
    connectBle: "Bluetooth",
    connectHttp: "Hotspot API",
    connected: "connected",
    connecting: "connecting",
    device: "Device",
    deviceAddress: "Device address",
    disabled: "disabled",
    disconnected: "disconnected",
    downloading: "downloading",
    failed: "failed",
    firmware: "Firmware",
    httpConnected: "Hotspot API connected",
    idle: "idle",
    language: "Language",
    network: "Network",
    noCandidates: "No candidates",
    offline: "offline",
    online: "online",
    overview: "Overview",
    passwordCanceled: "Password entry canceled",
    passwordCleared: "Password cleared",
    passwordPrompt: "Enter ESP32 setup password",
    passwordWrong: "Wrong password",
    refresh: "Refresh",
    readyToReboot: "ready to reboot",
    saved: "Saved",
    saveDevice: "Save device",
    saveWifi: "Save Wi-Fi",
    scanBms: "Scan BMS",
    screenRotation: "Screen rotation",
    setupAp: "Setup AP",
    speed: "Speed",
    speedUnit: "Speed unit",
    startUpdate: "Start update",
    update: "Update",
    updateAvailable: "update available",
    verifying: "verifying",
    wifi: "Wi-Fi",
    wifiPassword: "External Wi-Fi password",
    wifiSsid: "External Wi-Fi SSID",
  },
} satisfies Record<Language, Record<string, string>>;

const STATE_LABELS: Record<string, keyof typeof I18N.zh> = {
  connected: "connected",
  connecting: "connecting",
  disabled: "disabled",
  downloading: "downloading",
  failed: "failed",
  idle: "idle",
  offline: "offline",
  online: "online",
  ready_to_reboot: "readyToReboot",
  setup_ap: "setupAp",
  setup_ap_only: "setupAp",
  station_connected: "connected",
  station_connecting: "connecting",
  update_available: "updateAvailable",
  verifying: "verifying",
};

const encoder = new TextEncoder();
const decoder = new TextDecoder();

export default function App() {
  const [language, setLanguage] = useState<Language>("zh");
  const [transport, setTransport] = useState<Transport>("http");
  const [tab, setTab] = useState<Tab>("overview");
  const [baseUrl, setBaseUrl] = useState("http://192.168.4.1");
  const [setupPassword, setSetupPassword] = useState(() => sessionStorage.getItem("setupPassword") || "");
  const [status, setStatus] = useState<Status>({});
  const [config, setConfig] = useState<Config>({ language: "zh", display_rotation: "landscape", speed_unit: "km/h" });
  const [candidates, setCandidates] = useState<BmsCandidate[]>([]);
  const [message, setMessage] = useState("未连接");
  const [busy, setBusy] = useState(false);
  const [ble, setBle] = useState<BleState>({});
  const transportRef = useRef<Transport>("http");
  const bleRef = useRef<BleState>({});
  const pendingBle = useRef(new Map<string, PendingBle>());
  const bleBuffer = useRef("");

  const t = useMemo(() => {
    return (key: keyof typeof I18N.zh) => I18N[language][key] || I18N.zh[key] || key;
  }, [language]);

  useEffect(() => {
    document.documentElement.lang = language === "zh" ? "zh-CN" : "en";
  }, [language]);

  function rememberPassword(password: string) {
    setSetupPassword(password);
    sessionStorage.setItem("setupPassword", password);
  }

  function setCurrentTransport(next: Transport) {
    transportRef.current = next;
    setTransport(next);
  }

  function setCurrentBle(next: BleState) {
    bleRef.current = next;
    setBle(next);
  }

  function requirePassword(force = false) {
    let password = setupPassword;
    if (force || !password) {
      password = window.prompt(t("passwordPrompt"), password) || "";
    }
    if (!password) throw new Error(t("passwordCanceled"));
    rememberPassword(password);
    return password;
  }

  function clearPassword() {
    sessionStorage.removeItem("setupPassword");
    setSetupPassword("");
    setMessage(t("passwordCleared"));
  }

  async function run(label: string, work: () => Promise<void>) {
    setBusy(true);
    try {
      await work();
      setMessage(label);
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error));
    } finally {
      setBusy(false);
    }
  }

  async function api(path: string, options: RequestInit = {}): Promise<unknown> {
    const currentBle = bleRef.current;
    if (transportRef.current === "ble" && currentBle.tx && currentBle.rx) {
      return bleApi(path, options);
    }
    return httpApi(path, options);
  }

  async function httpApi(path: string, options: RequestInit = {}, retryAuth = true): Promise<unknown> {
    const headers = new Headers(options.headers || {});
    const password = requirePassword();
    headers.set("X-Setup-Password", password);
    headers.set("Authorization", `Basic ${btoa(`esp32:${password}`)}`);
    if (options.body && !headers.has("content-type")) headers.set("content-type", "application/json");

    const response = await fetchLocalDevice(normalizeBaseUrl(baseUrl) + path, {
      ...options,
      headers,
      mode: "cors",
      cache: "no-store",
    });
    if (response.status === 401) {
      sessionStorage.removeItem("setupPassword");
      setSetupPassword("");
      if (retryAuth) {
        requirePassword(true);
        return httpApi(path, options, false);
      }
      throw new Error(t("passwordWrong"));
    }
    if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
    return response.headers.get("content-type")?.includes("application/json") ? response.json() : response.text();
  }

  async function fetchLocalDevice(url: string, options: RequestInit) {
    const attempts = [{ targetAddressSpace: "local" }, { targetAddressSpace: "private" }, {}] as RequestInit[];
    let lastError: unknown;
    for (const extra of attempts) {
      try {
        return await fetch(url, { ...options, ...extra });
      } catch (error) {
        lastError = error;
      }
    }
    throw lastError;
  }

  async function bleApi(path: string, options: RequestInit = {}) {
    const currentBle = bleRef.current;
    if (!currentBle.tx) throw new Error("BLE not connected");
    const id = `${Date.now()}-${Math.random().toString(16).slice(2)}`;
    const request = {
      id,
      method: options.method || "GET",
      path,
      setup_password: requirePassword(),
      body: options.body ? String(options.body) : "",
    };
    const promise = new Promise((resolve, reject) => {
      const timer = window.setTimeout(() => {
        pendingBle.current.delete(id);
        reject(new Error("蓝牙请求超时"));
      }, 8000);
      pendingBle.current.set(id, { resolve, reject, timer });
    });
    try {
      await writeBleText(currentBle.tx, JSON.stringify(request) + "\n");
    } catch (error) {
      const pending = pendingBle.current.get(id);
      if (pending) {
        window.clearTimeout(pending.timer);
        pendingBle.current.delete(id);
        pending.reject(error instanceof Error ? error : new Error(String(error)));
      }
    }
    return promise;
  }

  async function writeBleText(tx: BluetoothRemoteGATTCharacteristic, text: string) {
    const bytes = encoder.encode(text);
    for (let offset = 0; offset < bytes.length; offset += 180) {
      const chunk = bytes.slice(offset, offset + 180);
      if (tx.writeValueWithoutResponse) {
        await tx.writeValueWithoutResponse(chunk);
      } else if (tx.writeValue) {
        await tx.writeValue(chunk);
      } else {
        throw new Error("BLE write is not supported");
      }
    }
  }

  function onBleValue(event: Event) {
    const target = event.target as unknown as BluetoothRemoteGATTCharacteristic;
    if (!target.value) return;
    bleBuffer.current += decoder.decode(target.value);
    let newline = bleBuffer.current.indexOf("\n");
    while (newline >= 0) {
      const line = bleBuffer.current.slice(0, newline).trim();
      bleBuffer.current = bleBuffer.current.slice(newline + 1);
      if (line) resolveBleLine(line);
      newline = bleBuffer.current.indexOf("\n");
    }
  }

  function resolveBleLine(line: string) {
    const message = JSON.parse(line) as { id?: string; status?: number; error?: string; body?: string };
    if (!message.id) return;
    const pending = pendingBle.current.get(message.id);
    if (!pending) return;
    window.clearTimeout(pending.timer);
    pendingBle.current.delete(message.id);
    if ((message.status || 200) >= 400) {
      pending.reject(new Error(message.error || `BLE ${message.status}`));
      return;
    }
    pending.resolve(parseMaybeJson(message.body || ""));
  }

  async function connectHttp() {
    await run(t("httpConnected"), async () => {
      setCurrentTransport("http");
      requirePassword();
      await loadConfig();
      await refresh();
    });
  }

  async function connectBle() {
    await run("BLE", async () => {
      if (!navigator.bluetooth) throw new Error(t("bluetoothUnsupported"));
      const device = await navigator.bluetooth.requestDevice({
        acceptAllDevices: true,
        optionalServices: [CONTROL_SERVICE_UUID],
      });
      const server = await device.gatt?.connect();
      if (!server) throw new Error(t("disconnected"));
      const service = await server.getPrimaryService(CONTROL_SERVICE_UUID);
      const tx = await service.getCharacteristic(CONTROL_TX_UUID);
      const rx = await service.getCharacteristic(CONTROL_RX_UUID);
      await rx.startNotifications();
      rx.addEventListener("characteristicvaluechanged", onBleValue);
      device.addEventListener("gattserverdisconnected", () => {
        setCurrentBle({});
        setCurrentTransport("http");
        setMessage(t("disconnected"));
      });
      setCurrentBle({ tx, rx, deviceName: device.name || device.id });
      setCurrentTransport("ble");
      await loadConfig();
      await refresh();
    });
  }

  async function refresh() {
    const data = (await api("/api/status")) as Status;
    setStatus(data);
  }

  async function loadConfig() {
    const data = (await api("/api/config")) as Config;
    setConfig({
      brightness: data.brightness ?? 80,
      display_rotation: data.display_rotation ?? "landscape",
      speed_unit: data.speed_unit ?? "km/h",
      language: data.language ?? "zh",
      external_ssid: data.external_ssid ?? "",
      bms_mac: data.bms_mac ?? "",
    });
    setLanguage(data.language === "en" ? "en" : "zh");
  }

  async function loadBmsCandidates() {
    const data = (await api("/api/bms/candidates")) as { candidates?: BmsCandidate[] };
    setCandidates(Array.isArray(data.candidates) ? data.candidates : []);
  }

  async function post(path: string, body?: JsonRecord) {
    await api(path, { method: "POST", body: body ? JSON.stringify(body) : "" });
  }

  const submitWifi = (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    const body = Object.fromEntries(new FormData(event.currentTarget)) as JsonRecord;
    void run(t("saved"), async () => {
      await post("/api/wifi", body);
      await refresh();
    });
  };

  const submitApPassword = (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    const body = Object.fromEntries(new FormData(event.currentTarget)) as { password?: string };
    void run(t("saved"), async () => {
      await post("/api/ap-password", body);
      if (body.password) rememberPassword(body.password);
    });
  };

  const submitDevice = (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    const body = Object.fromEntries(new FormData(event.currentTarget)) as JsonRecord;
    void run(t("saved"), async () => {
      await post("/api/config", body);
      await loadConfig();
      await refresh();
    });
  };

  const battery = formatMillivolts(status.local_battery_mv ?? status.pack_voltage_mv);
  const connection = transport === "ble" ? `BLE ${ble.deviceName || ""}`.trim() : normalizeBaseUrl(baseUrl);

  return (
    <main className="shell">
      <header className="topbar">
        <div>
          <p className="eyebrow">{connection}</p>
          <h1>ESP32 BMS GPS</h1>
        </div>
        <button className="icon-button" type="button" disabled={busy} onClick={() => void run(t("refresh"), refresh)}>
          ↻
        </button>
      </header>

      <section className="hero">
        <div>
          <span>{t("firmware")}</span>
          <strong>{status.version || "--"}</strong>
        </div>
        <div>
          <span>{t("wifi")}</span>
          <strong>{formatState(status.wifi, t)}</strong>
        </div>
      </section>

      <section className={tab === "overview" ? "panel active" : "panel"}>
        <Metric label={t("speed")} value={`${status.speed || "--"} ${status.speed_unit || ""}`.trim()} />
        <Metric label={t("battery")} value={battery} />
        <Metric label={t("bms")} value={formatState(status.bms, t)} />
        <Metric label="OTA" value={formatState(status.ota, t)} />
      </section>

      <section className={tab === "network" ? "panel active" : "panel"}>
        <div className="stack">
          <label>
            <span>{t("deviceAddress")}</span>
            <input value={baseUrl} inputMode="url" onChange={(event) => setBaseUrl(event.target.value)} />
          </label>
          <div className="button-row">
            <button type="button" disabled={busy} onClick={() => void connectHttp()}>
              {t("connectHttp")}
            </button>
            <button className="secondary" type="button" disabled={busy} onClick={() => void connectBle()}>
              {t("connectBle")}
            </button>
          </div>
          <button className="quiet" type="button" onClick={clearPassword}>
            {t("clearPassword")}
          </button>
        </div>

        <form className="stack" onSubmit={submitWifi}>
          <label>
            <span>{t("wifiSsid")}</span>
            <input name="ssid" maxLength={32} defaultValue={config.external_ssid || ""} autoComplete="off" />
          </label>
          <label>
            <span>{t("wifiPassword")}</span>
            <input name="password" type="password" maxLength={64} autoComplete="new-password" />
          </label>
          <button type="submit" disabled={busy}>
            {t("saveWifi")}
          </button>
        </form>

        <form className="stack" onSubmit={submitApPassword}>
          <label>
            <span>{t("apPassword")}</span>
            <input name="password" type="password" minLength={8} maxLength={64} autoComplete="new-password" />
          </label>
          <button type="submit" disabled={busy}>
            {t("setupAp")}
          </button>
        </form>
      </section>

      <section className={tab === "device" ? "panel active" : "panel"}>
        <form className="stack" onSubmit={submitDevice}>
          <label>
            <span>{t("brightness")}: {config.brightness ?? 80}%</span>
            <input
              name="brightness"
              type="range"
              min={10}
              max={100}
              value={config.brightness ?? 80}
              onChange={(event) => setConfig({ ...config, brightness: Number(event.target.value) })}
            />
          </label>
          <label>
            <span>{t("screenRotation")}</span>
            <select name="display_rotation" value={config.display_rotation || "landscape"} onChange={(event) => setConfig({ ...config, display_rotation: event.target.value })}>
              <option value="portrait">竖屏</option>
              <option value="landscape">横屏</option>
              <option value="inverted_portrait">反向竖屏</option>
              <option value="inverted_landscape">反向横屏</option>
            </select>
          </label>
          <label>
            <span>{t("speedUnit")}</span>
            <select name="speed_unit" value={config.speed_unit || "km/h"} onChange={(event) => setConfig({ ...config, speed_unit: event.target.value })}>
              <option>km/h</option>
              <option>mph</option>
            </select>
          </label>
          <label>
            <span>{t("language")}</span>
            <select
              name="language"
              value={language}
              onChange={(event) => {
                const value = event.target.value === "en" ? "en" : "zh";
                setLanguage(value);
                setConfig({ ...config, language: value });
              }}
            >
              <option value="zh">中文</option>
              <option value="en">English</option>
            </select>
          </label>
          <label>
            <span>BMS MAC</span>
            <input name="bms_mac" value={config.bms_mac || ""} placeholder="AA:BB:CC:DD:EE:FF" onChange={(event) => setConfig({ ...config, bms_mac: event.target.value })} />
          </label>
          <div className="button-row">
            <button type="button" disabled={busy} onClick={() => void run(t("scanBms"), async () => { await post("/api/bms/scan"); await loadBmsCandidates(); })}>
              {t("scanBms")}
            </button>
            <button className="secondary" type="button" disabled={busy || !config.bms_mac} onClick={() => void run(t("bindBms"), async () => { await post("/api/bms/bind", { mac: config.bms_mac || "" }); await loadBmsCandidates(); })}>
              {t("bindBms")}
            </button>
          </div>
          <CandidateList candidates={candidates} emptyLabel={t("noCandidates")} onPick={(mac) => setConfig({ ...config, bms_mac: mac })} />
          <button type="submit" disabled={busy}>
            {t("saveDevice")}
          </button>
        </form>
      </section>

      <section className={tab === "update" ? "panel active" : "panel"}>
        <div className="stack">
          <Metric label="OTA" value={formatState(status.ota, t)} />
          <button type="button" disabled={busy} onClick={() => void run(t("checkUpdate"), async () => { await post("/api/ota/check"); await refresh(); })}>
            {t("checkUpdate")}
          </button>
          <button className="secondary" type="button" disabled={busy} onClick={() => void run(t("startUpdate"), async () => { await post("/api/ota/start"); await refresh(); })}>
            {t("startUpdate")}
          </button>
        </div>
      </section>

      <p className="message">{busy ? "..." : message}</p>

      <nav className="tabs" aria-label="main">
        <TabButton active={tab === "overview"} label={t("overview")} icon="⌁" onClick={() => setTab("overview")} />
        <TabButton active={tab === "network"} label={t("network")} icon="⇄" onClick={() => setTab("network")} />
        <TabButton active={tab === "device"} label={t("device")} icon="⚙" onClick={() => setTab("device")} />
        <TabButton active={tab === "update"} label={t("update")} icon="⇧" onClick={() => setTab("update")} />
      </nav>
    </main>
  );
}

function Metric({ label, value }: { label: string; value: string }) {
  return (
    <article className="metric">
      <span>{label}</span>
      <strong>{value || "--"}</strong>
    </article>
  );
}

function CandidateList({ candidates, emptyLabel, onPick }: { candidates: BmsCandidate[]; emptyLabel: string; onPick: (mac: string) => void }) {
  if (candidates.length === 0) return <p className="empty">{emptyLabel}</p>;
  return (
    <div className="candidate-list">
      {candidates.map((candidate) => (
        <button key={candidate.mac} className="candidate" type="button" onClick={() => candidate.mac && onPick(candidate.mac)}>
          <span>
            <strong>{candidate.name || "Ant BMS"}</strong>
            <small>{candidate.mac || "--"}</small>
          </span>
          <small>{Number.isFinite(candidate.rssi) ? `${candidate.rssi} dBm` : ""}</small>
        </button>
      ))}
    </div>
  );
}

function TabButton({ active, icon, label, onClick }: { active: boolean; icon: string; label: string; onClick: () => void }) {
  return (
    <button className={active ? "tab active" : "tab"} type="button" onClick={onClick}>
      <span>{icon}</span>
      {label}
    </button>
  );
}

function normalizeBaseUrl(value: string) {
  return (value.trim() || "http://192.168.4.1").replace(/\/$/, "");
}

function formatMillivolts(value: number | null | undefined) {
  return Number.isFinite(value) ? `${((value as number) / 1000).toFixed(2)} V` : "--";
}

function formatState(value: unknown, t: (key: keyof typeof I18N.zh) => string) {
  const raw = String(value || "");
  const key = STATE_LABELS[raw];
  return key ? t(key) : raw || "--";
}

function parseMaybeJson(value: string) {
  const trimmed = value.trim();
  if (!trimmed.startsWith("{")) return value;
  return JSON.parse(trimmed);
}
