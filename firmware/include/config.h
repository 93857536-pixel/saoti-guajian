#pragma once

// ── 运行模式（默认全部真实硬件；仅开发板调试时用 mock 环境编译） ──
#ifndef USE_MOCK_DISPLAY
#define USE_MOCK_DISPLAY 0
#endif
#ifndef USE_MOCK_CAMERA
#define USE_MOCK_CAMERA 0
#endif
#ifndef USE_MOCK_MODEM
#define USE_MOCK_MODEM 0
#endif

#ifndef MODEM_PC_PASSTHROUGH
#define MODEM_PC_PASSTHROUGH 0
#endif

// ── 蜂窝网络（蜗牛移动多为联通转售，默认 3gnet） ─────────
#ifndef CELL_APN
#define CELL_APN "3gnet"
#endif
// 备用 APN（诊断/联网失败时依次尝试）
#ifndef CELL_APN_FALLBACKS
#define CELL_APN_FALLBACKS "cmnet", "cmiot", "uninet", "wonet"
#endif

// ── 双路径网络（WiFi + 4G 自动切换） ─────────────────────
#ifndef NET_AUTO_SWITCH
#define NET_AUTO_SWITCH 1
#endif
#ifndef NET_PREFER_WIFI
#define NET_PREFER_WIFI 1  // 1=WiFi 优先，0=4G 优先
#endif
#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 10000
#endif
#ifndef USE_WIFI_FALLBACK
#define USE_WIFI_FALLBACK NET_AUTO_SWITCH
#endif

// 填写 WIFI_SSID / WIFI_PASS；SSID 为 "YOUR_SSID" 时跳过 WiFi
#define WIFI_SSID "YOUR_SSID"
#define WIFI_PASS "YOUR_PASSWORD"

// 上传接口（后续替换为真实扫题服务器）
#define UPLOAD_HOST "httpbin.org"
#define UPLOAD_PATH "/post"

// ── 业务参数 ──────────────────────────────────────────────
#define BUTTON_DEBOUNCE_MS 50
#define BUTTON_LONG_PRESS_MS 3000
#define CAPTURE_JPEG_QUALITY 12
#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480

// ── MJPEG 实时图传（WiFi / SoftAP） ───────────────────────
#ifndef STREAM_ENABLE
#define STREAM_ENABLE 1
#endif
#ifndef STREAM_PORT
#define STREAM_PORT 80
#endif
#define STREAM_AP_SSID "SaotiCam"
#define STREAM_AP_PASS "saoti1234"

// ── USB 串口图传（Mac 直连，无需 WiFi） ─────────────────
#ifndef USB_STREAM_ENABLE
#define USB_STREAM_ENABLE 1
#endif
#ifndef USB_STREAM_BAUD
#define USB_STREAM_BAUD 921600
#endif
#ifndef USB_STREAM_FPS
#define USB_STREAM_FPS 2
#endif
#ifndef USB_STREAM_JPEG_QUALITY
#define USB_STREAM_JPEG_QUALITY 12
#endif
#ifndef USB_STREAM_WARMUP_FRAMES
#define USB_STREAM_WARMUP_FRAMES 1
#endif
#ifndef USB_STREAM_COLORBAR_TEST
#define USB_STREAM_COLORBAR_TEST 0
#endif
#ifndef USB_STREAM_AEC_VALUE
#define USB_STREAM_AEC_VALUE 1500
#endif
#ifndef USB_STREAM_SKIP_MODEM_ON_BOOT
#define USB_STREAM_SKIP_MODEM_ON_BOOT 1
#endif

#define LOG_TAG "SAOTI"
