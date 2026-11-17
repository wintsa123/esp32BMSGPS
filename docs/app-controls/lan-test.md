# 局域网网页连接测试

2026-11-18：设置页增加面板蓝牙连接、Wi-Fi 设备地址与连接按钮。蓝牙使用访问网页的电脑或手机的适配器；HTTP 使用设备地址访问 ESP32。浏览器不能切换系统 Wi-Fi，请先通过系统连接面板热点或同一局域网。

正式模式构建才能测试真实设备。`npm run preview:fnconnect` 使用演示控制项，不能作为硬件连接通过的证据。

```sh
cd vercel
npm run build
LAN_TLS_CERT=/path/to/server.crt LAN_TLS_KEY=/path/to/server.key \
  npm run preview:lan -- --port 8789
```

本次临时服务：`https://192.168.2.108:8789/`（正式模式）；HTTP 入口 `http://192.168.2.108:8787/` 也提供正式构建。证书、私钥保存在仓库外的 `/vol1/1000/tmp/esp-bms-lan-tls/`，未提交。临时证书有效期 14 天，包含当前 IP。临时 CA 公钥可从 HTTP 入口的 `/lan-test-ca.crt` 下载；仅本次测试使用，需在访问者电脑信任该 CA 后正常使用 HTTPS。服务不是开机自启，重新构建会清理 `dist` 中临时复制的 CA 公钥。

使用支持 Web Bluetooth 的 Chrome/Edge，先打开系统蓝牙，再在设置页点击“连接面板蓝牙”，由浏览器选择 ESP32 并授权。Linux Chromium 可能还需要启用实验性 Web 平台功能。普通局域网 HTTP 页面不能使用 Web Bluetooth；页面会提示改用 HTTPS 或 localhost。也可以通过 SSH 将服务转发到访问者电脑的 localhost。

Wi-Fi 设备地址通常是面板热点的 `http://192.168.4.1`，或设备加入局域网后实际获得的 IP。浏览器提示本地网络访问权限时需要允许；网络必须能路由到设备，设备的 HTTP 服务也需要允许跨域。设置页连接成功后，上方 Wi-Fi 状态才代表 HTTP 链路已通。

验证：

```sh
node tests/dashboard_lan_connection_check.cjs
```

该测试针对正式 HTTPS 页面，启动独立 HTTP 设备替身，通过实际局域网 TCP/CORS 请求验证状态读取，再用蓝牙协议替身验证 GATT 服务 UUID、请求分帧、响应和断开事件，以及普通 HTTP 下的安全上下文提示。测试显式允许本地网络权限并接受本地测试证书，不表示访问者电脑已经授权或信任证书。截图在 `preview/lan-device-connections.png`。

本次主机没有可用蓝牙适配器或无线网卡，未连接实体 ESP32；测试替身通过不能替代用户电脑的系统授权、蓝牙射频连接、Wi-Fi 入网或设备数据实测。高德在线骑行接口未重复调用。
