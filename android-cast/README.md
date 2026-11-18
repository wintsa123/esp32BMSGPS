# 两轮智控 Android App

默认启动离线内置的 Dashboard 首页；首页、设备连接、最近骑行和设备设置由 `vercel/` 前端提供。原生投屏、仪表详情、骑行历史及固件更新通过页面内的原生入口打开，继续使用现有设备协议。

前端 `vercel/` 是独立仓库 `wintsa123/espBmsSetting`；本次前端提交为 `8feca5d`。通过 fnconnect 预览执行 `npm --prefix vercel run preview:fnconnect`，端口固定为 `8787`；使用构建产物和相对资源路径，避免请求站点根目录的开发热更新模块。

## 构建

```sh
RUN_TESTS=1 scripts/build-android-cast.sh
```

脚本先构建前端，Gradle 将 `vercel/dist` 打包进 APK；不把下载用的 APK 和固件二进制重复打包。输出为仓库根目录 `两轮智控.apk`（调试签名）。直接运行 Gradle 前，先执行 `npm --prefix vercel run build`。

## 前端桥

仅内置 `https://bms.local` 文档可留在 WebView，外部页面交给系统浏览器。脚本、子页面由 CSP 限制为内置资源，原生请求仅允许本地 IPv4 设备和 `/api/` 路径。

- `BmsNative.request(id, method, path, body, host)` 异步执行请求。`host` 为本地 IPv4 地址（可带端口），或 `ble`。响应调用 `window.__bmsNativeReply(id, json)`，`json` 为 `{ok:true,body:...}` 或 `{ok:false,error:...}` 的 JSON 字符串。
- `BmsNative.connectBle()` 按需请求蓝牙权限并连接设备，`BmsNative.bleConnected()` 和 `getBleState()` 查询连接结果。蓝牙失败或拒绝权限会结束网页等待。
- 投屏入口统一打开网页 `WebCast`，通过 `startCast(host)`、`stopCast()`、`getCastState()` 调用原有 `CastService`。每次录屏重新取得系统授权；取消授权或关闭等待页面会作废本次请求。
- 已删除旧投屏、仪表、记录、设置页面及其专用原生图表。旧 `.MainActivity` 组件仅保留 manifest 别名，投屏二维码和旧入口均转至 `DashboardActivity` 网页。
- 系统连接设置只在实际离开并返回 App 后回调一次；不会重载 WebView。APK 安装通过 `installAppUpdateFrom(source)` 下载并交给系统安装器，网页读取 `getAppUpdateState()`，不再跳旧设置页。

底座参考用户指定的 `lswlc33/dashboard` commit `35aa033eecb95f214dce059850aa629557abfd98` 中 `app-shell/webnext/.../MainActivity.java` 的可信 WebView、JavascriptInterface、资源拦截、系统边距及外部导航模式，按本项目 Kotlin、DeviceApi 和 DeviceBleSession 实现。未引入其云端账号、车控和车辆位置页面；不是完整复制原项目壳。

尚需真机验证：Android 系统 WebView 的 BLE 权限、热点路由、文件选择、OTA 和投屏往返。离线构建和单元测试不能代替这些检查。
