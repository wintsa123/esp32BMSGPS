# APK 自更新与在线固件查询

## Goal

1. APK（两轮智控 / android-cast）增加应用内更新功能：在线检查新版本，下载新 APK 并安装。
2. APK 与 Vercel 网页版的固件更新支持"在线查询"：从服务器拉取可用固件清单，选择后自动下载并走现有设备热点 OTA 上传流程。

## Background

- 现有 OTA 流程（APK 与网页版一致）：用户在设备热点 HTTP（`192.168.4.1`）上把 `.bin` 固件 POST 给设备，并附带四位验证码（CRC-32 对 app bin 取模 10000，设备端独立校验）。
- APK 目前只能从本地文件选择固件（`ACTION_OPEN_DOCUMENT`），无应用内更新能力。
- 项目已有 Vercel 部署（`esp-bms-setting.vercel.app`），`vercel/` 是 Vite + React 静态站点，`vercel/dist` 为构建输出。
- 固件构建产物在 `firmware-builds/<profile>/`（含 `firmware.env`：PROFILE/FIRMWARE_VERSION/MCU 等）。
- `scripts/publish-flash-artifacts.py` 已有 CRC-32 验证码算法（`firmware_code()`），可复用。

## Decisions（已与用户确认）

- **更新源**：现有 Vercel 站点静态托管（`https://esp-bms-setting.vercel.app`），不引入新服务器。
- **范围**：APK 与 Vercel 网页版都支持在线查询固件；APK 支持自更新。
- **固件传输**：APK 从服务器下载 `.bin` 后，复用现有设备热点上传 OTA 流程（`DeviceApi.uploadFirmware`）。

## Server 设计

`vercel/public/` 下静态资源（vite build 自动复制进 dist）：

- `apk/latest.json`：
  ```json
  {
    "versionCode": 2,
    "versionName": "0.2.0",
    "url": "/apk/两轮智控.apk",
    "size": 3934388,
    "note": "更新说明（中文）",
    "publishedAt": "2026-08-22"
  }
  ```
- `apk/两轮智控.apk`：最新 APK 二进制。
- `firmware/firmware.json`：
  ```json
  {
    "updatedAt": "2026-08-22T00:00:00+08:00",
    "firmwares": [
      {
        "profile": "esp32-full",
        "name": "ESP32 完整版",
        "chip": "esp32",
        "version": "dev",
        "url": "/firmware/esp32-full.bin",
        "size": 123456,
        "code": "1234",
        "note": ""
      }
    ]
  }
  ```
- `firmware/<profile>.bin`：固件二进制（从 firmware-builds 复制）。

发布脚本 `scripts/publish-update-server.py`：

- `--apk <path>`：复制 APK 到 `vercel/public/apk/`，从 `android-cast/app/build.gradle.kts` 读 versionCode/versionName 生成 `latest.json`。
- `--firmware-dir firmware-builds/`：遍历各 profile，读取 `firmware.env`，用 `firmware_code()`（CRC-32 % 10000）计算验证码，复制 `<profile>.bin` 到 `vercel/public/firmware/`，生成 `firmware.json`。
- 幂等、可重复执行；输出人类可读的发布报告。

## APK 设计

### UpdateApi.kt（新文件）

- `data class ApkRelease(versionCode, versionName, url, size, note, publishedAt)`
- `data class FirmwareRelease(profile, name, chip, version, url, size, code, note)`
- `checkApkUpdate(): ApkRelease?` — GET latest.json（解析失败视为无更新源）。
- `fetchFirmwareList(): List<FirmwareRelease>` — GET firmware.json。
- `downloadFile(url: String, dest: File, onProgress: (Int) -> Unit)` — HttpURLConnection 流式下载（与项目零第三方依赖风格一致，超时/断网给出明确错误）。
- 基础 URL 常量 `https://esp-bms-setting.vercel.app`。

### MainActivity

- 设置页新增"应用更新"卡片：
  - 显示当前版本（`BuildConfig.VERSION_NAME`）。
  - "检查更新"按钮 → 后台线程检查 → 有新版本弹确认对话框（版本号 + note）→ 下载（进度条/对话框进度）→ `FileProvider` 提供 `content://` URI → `ACTION_VIEW` + `application/vnd.android.package-archive` 触发系统安装。
- 固件更新卡片改造：
  - 新增"在线查询固件"按钮 → 后台拉取清单 → `AlertDialog` 单选列表（名称 + 版本 + 大小 + 验证码）→ 选中后下载 `.bin` 到 cache 并自动填入验证码、更新 `otaFile`（复用 `readFirmware` 的 ByteArray 流程）。
  - "上传并更新"按钮逻辑复用现有 `DeviceApi.uploadFirmware`。
- 下载与安装：
  - 需要 `android.permission.REQUEST_INSTALL_PACKAGES` + `FileProvider`（`cache-path` 指向 `cacheDir/update/`）。
  - Android 8+ 未知来源安装由系统安装器处理；未授权时给出"请在系统设置中允许安装未知应用"提示。

### 版本

- `build.gradle.kts` versionCode 1 → 2、versionName "0.1.0" → "0.2.0"（体现新版本；发布脚本读取该值生成 latest.json）。

## Web 设计（vercel/src/App.tsx）

- OTA 表单区新增"在线固件"选择器：
  - 按钮/下拉拉取 `https://esp-bms-setting.vercel.app/firmware/firmware.json`（CORS：Vercel 默认 `Access-Control-Allow-Origin: *`，静态 GET 可用）。
  - 选择后 fetch 下载 `.bin` 为 Blob → 放入现有 `firmwareFile` state，自动填入验证码。
- 新增"应用下载"链接 → `/apk/两轮智控.apk`（提供 APK 更新入口）。
- 文案中英双语（现有 `t()` 结构）。

## Tests

- APK：`./gradlew -p android-cast :app:testDebugUnitTest` + `:app:assembleDebug`；UpdateApi 解析逻辑单测。
- Web：`npm run typecheck --prefix vercel` + `npm run build --prefix vercel`。
- 发布脚本：dry-run 执行，校验 latest.json/firmware.json 结构与验证码算法（与 `publish-flash-artifacts.py` 一致）。
- GitNexus `detect-changes` 前后对比。

## Out of scope

- 设备固件端无改动（固件 OTA 协议不变）。
- 不引入 APK 签名/升级通道（沿用现有 debug 签名发布流程）。
