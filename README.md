# 扫题挂件（Saoti GuaJian）

ESP32-S3 扫题挂件完整工程：固件、BLE Companion（iOS / watchOS / macOS）、USB 联调 App、装配与 3D 外壳。

## 仓库结构

| 目录 | 说明 |
|------|------|
| [`firmware/`](firmware/) | ESP32-S3 PlatformIO 固件（OV5640 / ST7789 / A7670G / BLE GATT / USB 串口） |
| [`companion/`](companion/) | **扫题挂件 App**（蓝牙）：iPhone + Apple Watch + Mac |
| [`macos-app/`](macos-app/) | macOS「SaotiCam」：USB 串口预览 / 联调 |
| [`hardware/`](hardware/) | 装配说明、接线、3D 外壳 STL/STEP |

架构：[`firmware/ARCHITECTURE.md`](firmware/ARCHITECTURE.md)  
BLE 协议：[`firmware/docs/BLE_COMPANION.md`](firmware/docs/BLE_COMPANION.md)

## 系统怎么用

1. 烧录 `firmware`（默认整机 env）
2. 屏上显示蓝牙名（如 `Saoti-F79D`）
3. 打开 **Companion App** →「设备」扫描连接（**不要**用 iPhone「设置 → 蓝牙」搜）
4. 短按 BOOT 或 App「扫题」→ 4G 调智谱 GLM-V → 答案经 BLE / 屏显示

AI 默认走 **4G only**（`NET_CELL_ONLY=1`）。USB 推流仍可用 Mac SaotiCam。

## Companion App（推荐日常使用）

```bash
cd companion
brew install xcodegen   # 若未安装
xcodegen generate
open SaotiCompanion.xcodeproj
```

| Scheme | 平台 |
|--------|------|
| `SaotiCompanion` | iPhone（含 Watch App） |
| `SaotiCompanionMac` | Mac |

在 Xcode 填入 Team（Signing），真机调试蓝牙（模拟器无 BLE）。

功能：状态 / 远程扫题 / 答案全文 / 取景缩略图 / 补光；Watch 经 iPhone 转发命令。

## macOS USB 联调 App（SaotiCam）

```bash
cd macos-app
xcodegen generate
xcodebuild -scheme SaotiCam -configuration Debug -derivedDataPath build build
open build/Build/Products/Debug/SaotiCam.app
```

插入 CH343 / 原生 USB → 选端口 → 波特率 **115200**（4G）或 **921600**（USB 推流）→「连接」。

## 固件快速烧录

```bash
cd firmware
cp include/secrets.example.h include/secrets.h   # 首次，填入智谱 Key
pio run -e esp32s3-devkitc-1 -t upload          # 真屏 + 全功能（含 BLE）
# pio run -e esp32s3-devkitc-1-nolcd -t upload   # USB 推流联调（mock 屏）
# pio run -e esp32s3-devkitc-1-4gtest -t upload  # 4G 联调
```

常用串口命令：`?` 状态，`s` 扫题，`t` AI 测图，`DIAG` 4G，`V`/`v` USB 推流，`BLE` 自检广播。

关闭 BLE：编译加 `-DBLE_GATT_ENABLE=0`。

## 硬件要点

- 主控：YD-ESP32-S3 **N16R8**
- 摄像头：微雪 **OV5640 Camera Board (C)**（DVP；丝印 D2–D9→GPIO6–13）
- 屏幕：微雪 **1.3" ST7789**（`1/3/38/39/40/41`）
- 4G：FS-MCore **A7670**（ESP TX=21→模 RX，模 TX→ESP 2；PWK=47，PEN=48；VIN 独立 5V）
- 电池 ADC：GPIO14 分压
- 外壳 v7.36（76×110×58）：见 [`hardware/case/`](hardware/case/)，打印交付 `stl/saoti_front.stl` + `saoti_back.stl`

引脚以 [`firmware/include/pins.h`](firmware/include/pins.h) 为准。  
接线：[`hardware/WIRING.md`](hardware/WIRING.md)。

## License

个人/学习项目；按需自行补充许可证。
