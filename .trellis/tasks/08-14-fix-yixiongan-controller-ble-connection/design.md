# Technical Design

## Boundary

改动限定在 `esp_bms_controller_ble` 的已知 FFE0 profile 特征匹配，以及一条最小
source contract 检查。现有 GAP、service discovery、CCCD、subscription、首帧超时和
在线投影不变。

## Data Flow

```text
NUS service absent
  -> FFE0 service found
  -> characteristic discovery
     -> FFEC notify + FFEF write: existing split-handle path
     -> FFE1 notify + write: same handle assigned to notify and write
  -> discover CCCD on notify handle
  -> subscribe
  -> existing open/keepalive commands
  -> first valid FarDriver telemetry frame -> ONLINE
```

## Key Decision

FFE1 是从目标真机日志和 FFE0 服务结构得到的明确兼容合同。只在 UUID 为 FFE1 且
properties 同时包含 notify 和一种 write 能力时复用句柄；不退化成“第一个可通知/可写
特征”，以免误连同服务中的无关特征。

## Rollback

该兼容分支可单独撤销，不改变 runtime ABI、NVS、协议帧或 UI。
