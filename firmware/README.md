# 扫题挂件固件

ESP32-S3 扫题挂件 PlatformIO 项目。默认 **mock 模式**，开发板到货即可编译烧录，串口模拟屏幕/摄像头/4G 完整流程。

## 环境

- [PlatformIO](https://platformio.org/) — **已安装**（Core 6.1.19 + Cursor 插件）
- 板子：**ESP32-S3-DevKitC-1 N16R8**
- 连接：Type-C 数据线

### 已安装组件

| 组件 | 状态 |
|------|------|
| PlatformIO Core (`pio`) | `pip3 install`，路径 `~/Library/Python/3.9/bin` |
| Cursor 插件 `platformio.platformio-ide` | 已从 VSIX 安装 |
| Cursor 插件 `davidgomes.platformio-ide-cursor` | OpenVSX 兼容版 |

终端若找不到 `pio`，先执行：

```bash
source ~/Documents/saoti-guajian-fw/scripts/pio-env.sh
```

或在 Cursor 里 **重新加载窗口**（Cmd+Shift+P → `Reload Window`），左侧应出现 PlatformIO 图标（蚂蚁头）。

## 快速开始

```bash
cd ~/Documents/saoti-guajian-fw
pio run -t upload
pio device monitor
```

1. 打开串口监视器 **115200**
2. 按开发板 **BOOT 键（GPIO0）** 或串口输入 **`s`** 触发一次扫题流程
3. 串口会打印：`CAPTURE → UPLOAD → RESULT`

## 目录结构

```
saoti-guajian-fw/
├── platformio.ini      # 板级配置
├── include/
│   ├── config.h        # mock 开关、WiFi、业务参数
│   ├── pins.h          # 引脚（与 ASSEMBLY.md 一致）
│   └── modules/        # 模块头文件
└── src/
    ├── main.cpp        # 主状态机
    └── modules/        # 各模块实现
```

## Mock → 真实硬件

| 模块到货 | 操作 |
|----------|------|
| ST7789 屏 | `config.h` 设 `USE_MOCK_DISPLAY 0`，配置 TFT_eSPI，实现 `display.cpp` 硬件分支 |
| OV5640 | `USE_MOCK_CAMERA 0`，添加 esp32-camera，实现 `camera.cpp` |
| A7670G | `USE_MOCK_MODEM 0`，实现 UART AT 驱动 |
| 全部硬件 | `pio run -e esp32s3-devkitc-1-hw -t upload` |

或使用环境 `esp32s3-devkitc-1-hw`（已预置 lib_deps）。

## WiFi 调试上传

mock 模式下可用 WiFi 代替 4G 测 HTTP 上传。编辑 `include/config.h`：

```cpp
#define WIFI_SSID "你的WiFi"
#define WIFI_PASS "你的密码"
```

## 引脚

见 `include/pins.h`，与 `~/Documents/saoti-guajian-3d/ASSEMBLY.md` 第 6 章一致。

## 主流程

```
空闲 → 按键/串口触发 → 拍照 → 上传 → 显示结果 → 回到空闲
```

## 相关文档

- 组装指南：`~/Documents/saoti-guajian-3d/ASSEMBLY.md`
- 纯文本版：`~/Documents/saoti-guajian-3d/ASSEMBLY.txt`
