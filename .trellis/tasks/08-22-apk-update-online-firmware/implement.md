# Implement: APK 自更新与在线固件查询

## 变更总览

| 端 | 文件 | 说明 |
|----|------|------|
| 服务端 | `scripts/publish-update-server.py`（新增） | 发布 APK + 固件到 `vercel/public/`，生成 `latest.json` / `firmware.json` |
| 服务端 | `vercel/public/{apk,firmware}/`（生成产物） | 静态更新源，vite build 自动复制进 dist |
| APK | `UpdateApi.kt`（新增） | 拉取清单 + 下载（零依赖 HttpURLConnection） |
| APK | `UpdateFileProvider.kt`（新增） | 最小 ContentProvider，暴露 cache/update 供系统安装器 |
| APK | `MainActivity.kt` | 应用更新卡片 + 在线固件查询/下载，上传复用现有 DeviceApi |
| APK | `AndroidManifest.xml` | `REQUEST_INSTALL_PACKAGES` + provider 声明 |
| APK | `build.gradle.kts` | versionCode 2 / versionName 0.2.0 |
| APK | `UpdateApiTest.kt`（新增） | 清单解析单测 5 例 |
| Web | `vercel/src/App.tsx` / `styles.css` | update 页在线固件选择 + APK 下载链接（含 /cast 落地页） |

## 关键设计

### 更新源 URL 约定（vercel 部署后）

```
https://esp-bms-setting.vercel.app/apk/latest.json
https://esp-bms-setting.vercel.app/apk/两轮智控.apk
https://esp-bms-setting.vercel.app/firmware/firmware.json
https://esp-bms-setting.vercel.app/firmware/<profile>.bin
```

`latest.json` 的 versionCode/versionName 从 `android-cast/app/build.gradle.kts` 自动读取；`firmware.json` 的验证码由发布脚本按固件端算法（CRC-32 % 10000）独立计算，与 `scripts/build-firmware.py` 输出一致（已用 output/esp32/esp32.bin 验证：code=1397 两边一致）。

### APK 自更新流程

设置页新增"应用更新"卡片 → 检查更新（versionCode 比较）→ 确认对话框 → 下载到 `cacheDir/update/` → `UpdateFileProvider` content URI → `ACTION_VIEW` + `application/vnd.android.package-archive`。未授权安装时 catch 并提示"请在系统设置中允许安装未知应用"。

### 在线固件流程

设置页固件更新卡片新增"在线查询固件" → 拉取 firmware.json → AlertDialog 单选 → 下载 .bin（校验 ≤1.5MB）→ 自动填入四位验证码 → 复用 `DeviceApi.uploadFirmware` 经设备热点 OTA。网页版同构：update 页"查询可用固件" → select 选择 → fetch 下载为 Blob → 自动填码 → 原提交表单上传。

### 构建/验证命令

```bash
# APK（注意：~/.gradle 挂载只读，需指定可写 GRADLE_USER_HOME）
GRADLE_USER_HOME=$PWD/.tools/gradle-home RUN_TESTS=1 ./scripts/build-android-cast.sh

# Web
cd vercel && npm run typecheck && npm run build

# 发布（生成 vercel/public/，提交 vercel 仓库即部署）
python3 scripts/publish-update-server.py --apk 两轮智控.apk --firmware esp32=output/esp32/esp32.bin
```

## 验证结果

- `:app:testDebugUnitTest` BUILD SUCCESSFUL，UpdateApiTest 5/5 通过
- `:app:assembleDebug` 成功，`两轮智控.apk` 重新生成（0.2.0）
- vercel typecheck + build 通过，dist 包含 apk/ firmware/ 静态资源
- 发布脚本 dry-run 与真实运行均通过，验证码算法与 build-firmware.py 一致

## 待办（需用户操作）

- vercel 仓库（wintsa123/espBmsSetting）commit + push 触发线上部署
- 主仓库 commit（android-cast、scripts、任务文档）
- 后续发布新固件：重新运行 publish-update-server.py 并在 vercel 仓库提交

## 环境坑记录

- `~/.gradle` 符号链接指向 `/vol2/wintsa/dev-relocated/.gradle`，该盘只读，Gradle native services 初始化失败。解决：`GRADLE_USER_HOME` 指向 workspace 内 `.tools/gradle-home`（已在主仓库 .gitignore 之外，`.tools/` 未入库）。
