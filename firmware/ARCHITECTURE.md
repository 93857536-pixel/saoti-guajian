# 扫题挂件 — 项目总体架构

## 1. 系统目标

ESP32-S3「扫题挂件」：按键/串口触发 OV5640 拍照 → WiFi/4G 上传扫题服务 → ST7789 显示状态；同时支持 Mac 端 **SaotiCam** 通过 USB 串口或 WiFi MJPEG 实时预览。

| 仓库 | 路径 | 职责 |
|------|------|------|
| 固件 | `saoti-guajian-fw/` | 摄像头、显示、4G、USB/WiFi 推流、拍照上传 |
| Mac App | `saoti-guajian-ios/` | USB/WiFi 实时预览 |
| 装配文档 | `saoti-guajian-3d/ASSEMBLY.md` | 硬件接线、供电、联调步骤 |

## 2. 硬件组成

```
┌─────────────────────────────────────────────────────────┐
│  YD-ESP32-23 (ESP32-S3-WROOM-1-N16R8, 16MB+8MB PSRAM)  │
│                                                         │
│  OV5640 DVP ── GPIO4/5 SCCB, 6-13 data, 15 XCLK ...     │
│  ST7789 SPI ── GPIO35-40                                │
│  A7670G UART ── TX21 / RX47 / PWRKEY41 / RST42          │
│  按键 ── GPIO0                                          │
│  CH343 USB-UART ── 电脑串口 @ 921600（USB 推流）         │
└─────────────────────────────────────────────────────────┘
```

网络模块说明（飞思创 FS-MCore-A7670C）主要管 **4G 供电/UART/SIM**，与摄像头黑屏无直接关系；摄像头黑屏优先查 OV5640 排线、3.3V、镜头保护膜、DVP 数据线。

## 3. 固件模块

```
main.cpp
  ├─ Camera      OV5640 初始化 / 抓拍 / 曝光调优
  ├─ Display     ST7789 或 mock 串口 UI
  ├─ Modem       A7670G AT + WiFi 双路径上传
  ├─ Button      GPIO0 短按拍照
  ├─ UsbStream   串口二进制帧推流（SC 协议）
  └─ StreamServer WiFi SoftAP MJPEG（/stream /capture）
```

### 状态机

```
Idle ──按键/s──► Capturing ──► Uploading ──► ShowResult ──2.5s──► Idle
                  │
                  └── USB 推流中会先暂停，结束后自动恢复
```

### USB 推流协议

```
[53 43 01 FE] [len u32 LE] [JPEG bytes]     # 0xFE JPEG
[53 43 01 FC] [len u32 LE] [w u16][h u16][RGB565...]  # 0xFC
```

串口命令：`V` 开流 / `v` 停流 / `s` 拍照 / `C` 切换彩条测试 / `m` 4G 诊断。

### 编译环境

| env | 用途 |
|-----|------|
| `esp32s3-devkitc-1` | 全硬件（屏+摄+4G） |
| `esp32s3-devkitc-1-nolcd` | 无屏 + USB 推流联调（当前 Mac 调试用） |
| `esp32s3-devkitc-1-mock` | 全 mock，无外设 |

## 4. Mac App 数据流

```
ESP32 USB ──► SerialStreamClient ──► PlatformImage(JPEG/RGB565)
                                      │
                                      ▼
                               FrameDisplayView (CGImage)
WiFi /stream ──► MjpegStreamClient ──► NSImage.cgImage ──┘
```

## 5. 已知问题与对策

| 现象 | 原因 | 对策 |
|------|------|------|
| 一直「解码中」 | 无有效帧 / 解码失败 | 已修推流卡死；确认 `[USB] ready` |
| 「直播中」但黑屏 | OV5640 硬件 JPEG 内容全黑/损坏 | 撕镜头膜、补光；串口 `C` 彩条诊断；查 DVP 数据线 |
| RGB565/YUV422 整板 init | 本模块不稳定，会重启或卡死 | 保持 JPEG QVGA 启动 |
| 拍照后无画面 | 推流被关未恢复 | 已自动 resume |
| WiFi 模式黑屏 | App 未把 WiFi 帧传给视图 | 已修复 |
| 4G 上传失败 | APN/SIM/供电 | 查 ASSEMBLY.md 第 9 章；`m` 诊断 |

### 黑屏诊断（重要）

1. 串口发 `V` 开流后，再发 `C` 打开彩条。
2. **若出现彩条** → DVP/USB/App 链路正常，问题在镜头/曝光/环境光。
3. **若仍全黑** → 查 OV5640 排线（D0–D7/PCLK/VSYNC）、3.3V 供电、共地。
4. 飞思创 A7670 文档只管 4G，与摄像头黑屏无关。

## 6. 联调顺序（推荐）

1. `nolcd` 烧录 → SaotiCam USB 看图
2. 串口 `C` 彩条：区分软件链路 vs 光学/硬件
3. 接 ST7789，切默认 env 验证 UI
4. 接 A7670G，配置 APN/WiFi，验证拍照上传
5. 装壳前再跑一遍完整流程
