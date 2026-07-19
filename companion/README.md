# 扫题挂件 Companion（iOS / watchOS / macOS）

通过 **蓝牙** 连接 ESP32 扫题挂件：状态、远程扫题、答案全文、取景缩略图、补光。

固件协议见 [`../firmware/docs/BLE_COMPANION.md`](../firmware/docs/BLE_COMPANION.md)。

## 打开工程

```bash
cd companion
xcodegen generate
open SaotiCompanion.xcodeproj
```

| Scheme | 平台 |
|--------|------|
| `SaotiCompanion` | iPhone（含 Watch App） |
| `SaotiCompanionMac` | Mac |

在 Xcode 里填入你的 **Team**（Signing），真机调试蓝牙（模拟器无 BLE）。

## 功能

- **控制**：电量 / 4G / 相位；一键扫题、唤醒、补光  
- **取景**：按需拉一帧缩略图（非实时流）  
- **答案**：当前全文 + 本地历史  
- **设备**：扫描 `Saoti-*`、连接、日志  
- **Watch**：扫题 / 唤醒 / 进度与答案摘要（依赖 iPhone 已连挂件）

## 重要

iPhone「设置 → 蓝牙」**不会**列出本挂件（自定义 GATT）。请在本 App「设备」页扫描；对照挂件屏幕上的 `Saoti-XXXX`。

## 权限

- `NSBluetoothAlwaysUsageDescription`  
- Background：`bluetooth-central`（扫题过程中保持连接）
