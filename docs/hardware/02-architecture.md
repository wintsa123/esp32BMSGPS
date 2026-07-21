# esp32BmsGps 车机硬件方案

- 日期：2026-07-18
- 阶段：2 - 架构选择

## 候选比较

| 候选 | 主控/无线 | 电源与接口 | 优点 | 排除/选择原因 |
|---|---|---|---|---|
| 国产/国内供应链优先 | ESP32-S3 模组 | SGM2536、SY8113I、HT517、ATGM336H | 成本低、集成度高、小批量落地快 | 作为 R1 主体 |
| 海外主流生态 | STM32H7 + 独立 Wi-Fi/BLE | TI/ADI/NXP + u-blox | 资料与工业生态成熟 | BOM、板面积、软件复杂度超出约 100 元边界 |
| 混合折中 | ESP32-S3 + 国内电源/GNSS + 少量海外保护料 | BAV199 等关键小料保留高质量选择 | 成本、资料完整度和风险较平衡 | 最终采用；eFuse 从资料不足的 PW1558 回退到 SGM2536 |

## 系统框图

```text
USB-C 5V / 专用12V
        │
   3A Fuse + SMBJ17A
        │
   SGM2536FR eFuse
        │ VIN_PROT
        ├── SY8113I ── 3V3 ── ESP32-S3 / LCD / CTP
        │                       └─ 磁珠 ─ 3V3_GNSS ─ GNSS
        └── SY8113I ── 5V_AUDIO ─ HT517 ─ 4Ω Speaker

ESP32-S3 ── 8-bit I80 ── ILI9488 LCD
         ├─ I²C/INT/RST ─ FT6336U
         ├─ UART/1PPS ─── ATGM336H-F8N-76 ─ U.FL ─ SMA ─ 有源天线
         ├─ I²S/EN ────── HT517
         └─ Native USB ── USB-C D+/D-
```

## 选择结论

R1 采用以国内供应链为主体的混合架构。它满足显示、Wi-Fi/BLE、GNSS 和音频需求，同时将 PCB 控制在单主板、4 层、小批量可装配范围。
