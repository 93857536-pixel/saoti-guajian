# 扫题挂件（Saoti GuaJian）

ESP32-S3 扫题挂件完整工程：固件、macOS 客户端、装配与 3D 外壳。

## 仓库结构

| 目录 | 说明 |
|------|------|
| [`firmware/`](firmware/) | ESP32-S3 PlatformIO 固件（OV5640 / ST7789 / A7670G / USB 串口协议） |
| [`macos-app/`](macos-app/) | macOS 原生 App「扫题挂件 / SaotiCam」 |
| [`hardware/`](hardware/) | 装配说明、接线、3D 外壳 STL/STEP |

架构说明见 [`firmware/ARCHITECTURE.md`](firmware/ARCHITECTURE.md)。

## 如何打开 macOS 软件

### 方式 A：直接打开已编译 App（本机已构建时）

```bash
open ~/Documents/saoti-guajian-ios/build/Build/Products/Debug/SaotiCam.app
```

或在 Finder 中进入上述路径，双击 `SaotiCam.app`。

### 方式 B：从本仓库重新编译

```bash
cd macos-app
brew install xcodegen   # 若未安装
xcodegen generate
xcodebuild -scheme SaotiCam -configuration Debug -derivedDataPath build build
open build/Build/Products/Debug/SaotiCam.app
```

连接设备：插入 CH343 USB 串口 → App 右上角选端口 → 波特率 **115200**（4G 联调）或 **921600**（USB 推流固件）→ 点「连接」。

## 固件快速烧录

```bash
cd firmware
pio run -e esp32s3-devkitc-1 -t upload          # 真屏 + 全功能
# pio run -e esp32s3-devkitc-1-nolcd -t upload   # USB 推流联调（mock 屏）
# pio run -e esp32s3-devkitc-1-4gtest -t upload  # 4G 联调（115200）
```

串口命令：`?` 状态 JSON，`s` 抓拍，`DIAG` 4G 诊断，`V`/`v` 开/关 USB 推流。

## 硬件要点

- 主控：ESP32-S3 N16R8（如 YD-ESP32-23）
- 摄像头：OV5640
- 屏幕：ST7789 1.3" 240×240（**带 PCB 排针/PH2.0**，接 GPIO 35–40）
- 4G：飞思创 A7670G（UART 默认 ESP TX=21 / RX=47）

详细接线见 [`hardware/ASSEMBLY.md`](hardware/ASSEMBLY.md)。

## License

个人/学习项目；按需自行补充许可证。
