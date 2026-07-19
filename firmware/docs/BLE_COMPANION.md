# 扫题挂件 BLE Companion 协议

ESP32-S3 **硬件自带 BLE**；固件启用 GATT 后，广播名形如 `Saoti-XXXX`（MAC 后四位）。

Apple 工程：[`../../companion`](../../companion)（iOS / macOS / watchOS）。

## 服务 UUID

| 角色 | UUID |
|------|------|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| STATUS（Read+Notify） | `6E400002-…` |
| COMMAND（Write） | `6E400003-…` |
| ANSWER（Read+Notify） | `6E400004-…` |
| THUMB（Notify） | `6E400005-…` |
| EVENT（Notify） | `6E400006-…` |

协议版本：`fw_proto=1`（见 STATUS JSON）。

## COMMAND 文本

| 命令 | 作用 |
|------|------|
| `ping` | EVENT 回 `pong` |
| `scan` / `s` | 触发扫题（等同 BOOT） |
| `wake` | 唤醒摄像头/4G |
| `thumb` / `preview` | 抓一帧 QQVGA 缩略图经 THUMB 下发 |
| `flash=1` / `flash=0` | 补光开关 |
| `status` | 立即推送 STATUS |
| `answer` | 推送当前答案 |

## STATUS JSON（节选）

`cam` `lcd` `cell` `bat_pct` `charging` `csq` `sleeping` `busy` `phase` `fw` `fw_proto` `ble` `has_answer` `last_error` …

`phase`：`idle` | `capturing` | `uploading` | `result`

## THUMB 分片

1. 首包 8 字节：`THMB` + `uint32` little-endian 长度  
2. 后续 Notify：JPEG 原始字节（约 180B/包）

## 烧录与验证

```bash
cd firmware
pio run -e esp32s3-devkitc-1 -t upload
# 串口应出现：[BLE] advertising as Saoti-XXXX
```

手机/Mac 打开 **扫题挂件** App → 设备页扫描 → 连接 → 控制页扫题。

## 重要：为什么「手机设置 → 蓝牙」搜不到？

| 平台 | 系统蓝牙设置 |
|------|----------------|
| **iPhone / iPad** | **永远不会**列出本挂件（自定义 GATT，非耳机/键盘类） |
| **Android** | 部分机型在「配对新设备」里能看到 `Saoti-XXXX`，但仍需 App 才能扫题 |
| **正确做法** | 打开 **扫题挂件 Companion App** →「设备」→ 扫描 → 点 `Saoti-XXXX` |

挂件屏幕空闲时会显示蓝牙名（如 `Saoti-F79D`）和「请用扫题挂件App连接」。

## 说明

- V1 **不做** BLE 实时视频；实时预览仍用 SoftAP/USB（Mac SaotiCam）。  
- Watch 经 iPhone WatchConnectivity 转发，不直接连挂件。  
- 关闭 BLE：编译加 `-DBLE_GATT_ENABLE=0`。
