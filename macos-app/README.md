# SaotiCam macOS 客户端

在 Mac 上通过 WiFi 实时查看 ESP32-S3 + OV5640 摄像头的 MJPEG 视频流。

## 前置条件

1. 已将 [saoti-guajian-fw](../saoti-guajian-fw/) 固件烧录到 ESP32-S3 开发板
2. 摄像头模块接线正确，串口可看到 `[STREAM] MJPEG http://...` 日志
3. macOS 已安装 Xcode 与 xcodegen（`brew install xcodegen`）

## 快速开始

### 1. 烧录固件

```bash
cd ../saoti-guajian-fw
python3 -m platformio run -e esp32s3-devkitc-1-nolcd -t upload
```

若 `config.h` 中 `WIFI_SSID` 仍为 `"YOUR_SSID"`，设备会自动开启 SoftAP，无需家庭 WiFi。

### 2. 连接网络

**方式 A — Mac 直连热点（默认）**

| 项目 | 值 |
|------|-----|
| WiFi 名称 | `SaotiCam` |
| 密码 | `saoti1234` |
| 设备 IP | `192.168.4.1` |

在 Mac 系统设置 → WiFi 中连接 `SaotiCam` 热点。

**方式 B — 同一局域网**

在固件 `config.h` 填写家庭 WiFi 的 `WIFI_SSID` / `WIFI_PASS`，烧录后查看串口日志中的 STA IP（如 `192.168.1.xxx`），Mac 连同一 WiFi 后在 App 输入该 IP。

### 3. 编译运行 macOS App

```bash
cd /Users/linminhao/Documents/saoti-guajian-ios
xcodegen generate
open SaotiCam.xcodeproj
```

在 Xcode 中选择 **My Mac** 运行，或命令行构建：

```bash
xcodebuild -scheme SaotiCam -destination 'platform=macOS' build
```

构建完成后可在 `DerivedData` 或 Xcode 中直接 Run 启动 App。

### 4. 使用 App

**USB 串口（推荐，无需 WiFi）**

1. ESP32 用 USB 线连 Mac（烧录口即可）
2. 打开 SaotiCam，连接方式选 **USB 串口**
3. 串口选 `cu.usbserial-...`，点击 **连接**

**WiFi**

1. Mac 连接 `SaotiCam` 热点或同一局域网
2. 连接方式选 **WiFi**，IP 填 `192.168.4.1`（或 STA IP）
3. 点击 **连接**

点击画面可隐藏/显示控制栏；点击 **断开** 停止拉流。

> USB 模式固件默认跳过 4G 初始化，启动更快、串口不被 AT 日志干扰。

## HTTP 端点（固件提供）

| 路径 | 说明 |
|------|------|
| `GET /stream` | MJPEG 实时流（multipart/x-mixed-replace） |
| `GET /capture` | 单张 JPEG 快照 |
| `GET /` | 状态页（含流预览链接） |

## 故障排查

- **连接失败**：确认 Mac 已连上 `SaotiCam` 热点或同一局域网；浏览器访问 `http://192.168.4.1/` 验证
- **无画面**：查看串口是否有 `[CAM]` / `[STREAM]` 日志；确认 OV5640 初始化成功
- **ATS 报错**：App 已配置本地 HTTP 例外；若 IP 不在 192.168.x.x 段，需在 `Info.plist` 补充对应域
