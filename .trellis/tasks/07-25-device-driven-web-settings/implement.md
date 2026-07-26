# 设备驱动的 Web 设置实施计划

## 实施顺序

1. 用 GitNexus 对 `esp_bms_idf_runtime_http_api_handler`、`runtime_http_config_handler`、`runtime_http_post_config_handler` 和网络 HTTP server 注册函数执行 upstream impact；若风险为 HIGH/CRITICAL，先告知用户再编辑。
2. 读取固件相关 Trellis 规范与 `trellis-before-dev` 指引，确认 feature 宏、HTTP 缓冲区和可用测试命令。
3. 在运行时实现 `GET /api/settings/manifest` V1：按 feature 宏和现有配置值生成受限 JSON，并将路由接入既有 API dispatcher；不修改已有配置写入语义。
4. 为 V1 manifest 增加最小主机侧或可运行的协议校验，覆盖全功能与裁剪功能、未知 kind/版本、以及旧固件端点缺失的前端降级。
5. 重写 `main/web/index.html` 的设置区为 manifest 渲染，保留热点密码和非设置页面；加载失败清空 UI 并在既有消息区域提示升级。
6. 重写 `vercel/src/App.tsx` 的设备与更新区为 manifest 渲染；HTTP 连接成功后加载清单，BLE 连接不加载动态设置；复用现有 `message` state 处理旧固件提示。
7. 按需要更新 `vercel/src/styles.css`，只补动态表单已有布局所需的最小样式。
8. 更新或新增 `preview/` 下的 Web UI 预览图，仅在视觉输出实际变化时创建。
9. 运行格式化、固件相关构建/测试、Vercel `typecheck` 和 `build`；验证完整、无 BMS、无 OTA 三组 manifest 的可见项和提交路由。
10. 运行 `git diff --check`、GitNexus `detect_changes()`，复核变更只覆盖运行时 HTTP 和两个 Web 客户端的预期执行流。

## 验证门槛

- 完整功能、BMS 裁剪和 OTA 裁剪时，manifest 不宣称不存在的功能。
- 两个客户端均只渲染 manifest item，清单加载失败时不残留上一个设备的控件。
- 普通值仍经 `/api/config` 的既有 pending queue 和 NVS 保存；BMS action 与 OTA 仍经原有 handler。
- Vercel `npm run typecheck --prefix vercel` 与 `npm run build --prefix vercel` 通过。
- 适用的 ESP-IDF build/host test、`git diff --check` 和 GitNexus `detect_changes()` 通过。

## 回退点

- 固件：移除 manifest route 和生成函数，不触及既有 `/api/config`、BMS、OTA 写入代码。
- 前端：恢复固定页面前必须同时恢复旧固件兼容策略；本任务不采用该回退，因为已决定旧固件显示升级提示。
