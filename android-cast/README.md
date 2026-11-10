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
- `BmsNative.connectBle()` 按需请求蓝牙权限并连接设备，`BmsNative.bleConnected()` 查询连接状态。
- `BmsNative.openNative(route)` 接受 `CAST`、`BMS`、`RECORDS`、`SETTINGS`。进入原生页前释放首页 BLE，返回时恢复连接，投屏服务继续沿用原有生命周期。

底座参考用户指定的 `lswlc33/dashboard` commit `35aa033eecb95f214dce059850aa629557abfd98` 中 `app-shell/webnext/.../MainActivity.java` 的可信 WebView、JavascriptInterface、资源拦截、系统边距及外部导航模式，按本项目 Kotlin、DeviceApi 和 DeviceBleSession 实现。未引入其云端账号、车控和车辆位置页面；不是完整复制原项目壳。

尚需真机验证：Android 系统 WebView 的 BLE 权限、热点路由、文件选择、OTA 和投屏往返。离线构建和单元测试不能代替这些检查。
