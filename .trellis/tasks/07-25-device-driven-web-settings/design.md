# 设备驱动的 Web 设置设计

## 边界

设备是唯一的运行时能力来源。Vercel 是静态前端，浏览器通过用户输入的局域网 HTTP 地址直连设备；它没有、也不应增加云端中继。局域网页面同样直接连设备。

本任务只动态化 Web 设置：普通值、BMS 扫描/绑定和 OTA。热点密码、Vercel 的 BLE 连接/传输不进入清单，也不改动其现有实现。

固件继续拥有所有状态校验、异步主循环交接和 NVS 持久化。浏览器不保存设置副本，既有 `/api/config`、`/api/bms/scan`、`/api/bms/bind` 和 `/api/ota` 仍是实际执行入口。

## 数据流

```
浏览器 HTTP 连接
  -> GET /api/settings/manifest
  -> 固件按编译期 feature 和运行时可编辑性生成清单
  -> Web 以受限控件类型渲染
  -> 既有 POST 路由
  -> 运行时主循环 / NVS
  -> GET /api/settings/manifest 重新读取并渲染当前值
```

Vercel 与嵌入式页面都必须在每次 HTTP 连接建立时清空上一次清单、重新读取。请求失败、协议版本不支持或清单校验失败时，不渲染可编辑控件，只复用各自已有消息组件显示“固件不支持动态设置，请升级”或具体请求错误。

## Manifest V1

新增 `GET /api/settings/manifest`。响应是版本化 JSON，包含设备固件已支持的 section 和 item；设备不支持的功能不会出现在响应中。

```json
{
  "protocol_version": 1,
  "sections": [
    {
      "id": "device",
      "label": { "zh": "设备", "en": "Device" },
      "submit": { "endpoint": "/api/config", "method": "POST" },
      "items": [
        {
          "id": "brightness",
          "kind": "range",
          "label": { "zh": "亮度", "en": "Brightness" },
          "value": 80,
          "min": 10,
          "max": 100,
          "step": 1
        }
      ]
    }
  ]
}
```

允许的 `kind` 固定为 `range`、`select`、`action`、`choice` 和 `upload`。前端拒绝未知 `kind`、缺失 `id`/双语标签、非法数值范围、非相对 `/api/` 地址和重复 item id；不得把设备字符串当 HTML、组件名或 JavaScript 执行。

- `range`：数值、最小值、最大值、步长。
- `select`：当前值和设备声明的 `{value,label}` 选项。
- `action`：无输入 POST，例如 BMS 扫描。
- `choice`：可由动作刷新候选项的单选值；BMS 绑定使用该类型和既有候选接口。
- `upload`：OTA 的二进制文件与四位验证码输入，仍提交到既有 OTA 路由。

section 的 `submit` 让同一组普通字段合并提交给既有 `/api/config`，避免快速逐项提交覆盖运行时的单槽 pending config。BMS 和 OTA 使用清单声明的 action/upload 端点；前端只允许本设备同源的相对 `/api/` 地址。

## 固件能力映射

运行时按实际 feature 宏和已有校验规则生成清单：

- 始终可编辑的显示设置仅在对应运行时能力真实存在时加入。
- 音量受 audio feature 控制。
- 速度单位和速度来源的选项按 GPS/controller 组合裁剪。
- BMS 类型、扫描、候选/绑定仅在 BMS feature 启用时加入。
- OTA 上传仅在 OTA feature 启用时加入。
- 热点密码和 BLE 连接不加入。

构建期禁用模块时，HTTP action 路由本身仍沿用现有 `501 Not Implemented` 保护；manifest 不暴露这些 action。这样清单是用户可见能力的来源，后端仍是最终授权和验证点。

## 前端渲染

`main/web/index.html` 与 `vercel/src/App.tsx` 都移除固定设置字段和固定 BMS/OTA 显示条件，替换为同一份 V1 schema 语义的渲染器。两者可分别实现，因为一个是原生 DOM、另一个是 React；不新增共享构建包。

页面默认选择 `label.zh`。语言 item 成功保存并重新读取清单后，使用 `label.en` 切换为英文；入口仍位于设备设置 section。设备返回的 value 是唯一的控件初始值。

局域网页面必须使用消息区域，Vercel 使用已有 `message` state，处理旧固件的 404/501 或 `protocol_version != 1`。切换设备地址、断连或任一 manifest 加载失败时清空内存中的 manifest 和待提交值，防止旧设备值写入新设备。

## 兼容与回退

旧固件没有新端点时，页面提示升级且不显示旧的固定表单。无需迁移 NVS，也不改变旧的 `/api/config` 请求格式。回退只需撤销 manifest route 和两个前端的动态渲染；既有设置 API 保持原样。

## 风险控制

- 设备没有可信 JSON 库时，手工 JSON 输出必须逐个检查缓冲区长度；标签和选项均为固件常量，禁止拼接外部输入。
- Vercel 从 HTTPS 页面访问 HTTP 私网设备受浏览器 Private Network Access 限制；现有 CORS/PNA 头继续适用于新端点。
- OTA 仍仅允许 HTTP 传输；BLE 连接不作为动态设置 transport。
- HTTP handler 不直接写 NVS 或执行长操作，沿用现有 pending queue / OTA handler 行为。
