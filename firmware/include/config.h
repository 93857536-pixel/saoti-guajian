#pragma once

// 本地密钥与 WiFi（无 secrets.h 时用 example 占位）
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

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

// FS-MCore-A7670G：PEN 使能 + PWK 拉低约 1.2s 开机（店家要求 PEN 常接 3.3V）
#ifndef MODEM_PULSE_PWRKEY
#define MODEM_PULSE_PWRKEY 1
#endif

#ifndef MODEM_BOOT_WAIT_MS
#define MODEM_BOOT_WAIT_MS 10000
#endif

#ifndef MODEM_FAST_PROBE
#define MODEM_FAST_PROBE 0
#endif

// ── AI 扫题解题（默认：阿里云百炼通义 VL，OpenAI 兼容接口） ──
#ifndef USE_OPENAI_SOLVER
#define USE_OPENAI_SOLVER 1
#endif
#ifndef OPENAI_BASE_URL
#define OPENAI_BASE_URL \
  "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"
#endif
#ifndef OPENAI_MODEL
#define OPENAI_MODEL "qwen-vl-plus"
#endif
#ifndef OPENAI_TIMEOUT_MS
#define OPENAI_TIMEOUT_MS 60000
#endif

// 蜗牛移动：品牌≠制式。本机卡实测 IMSI 46000…/ICCID 898600… = 中国移动物联
// 固件会按 IMSI 自动选 APN；默认 cmiot（移动物联），备用 cmmtm/cmnet
// 电池电量：分压比 / 空满电压（磷酸铁锂请改阈值）
#ifndef BAT_DIVIDER_RATIO
#define BAT_DIVIDER_RATIO 2.0f  // Vbat = Vadc * ratio（100k+100k）
#endif
#ifndef BAT_V_EMPTY
#define BAT_V_EMPTY 3.30f
#endif
#ifndef BAT_V_FULL
#define BAT_V_FULL 4.20f
#endif

#ifndef CELL_APN
#define CELL_APN "cmiot"
#endif
#ifndef CELL_APN_FALLBACKS
#define CELL_APN_FALLBACKS "cmmtm", "cmnet", "wonet", "3gnet", "scuiot", "ctnet"
#endif

#ifndef NET_AUTO_SWITCH
#define NET_AUTO_SWITCH 1
#endif
#ifndef NET_PREFER_WIFI
#define NET_PREFER_WIFI 1
#endif
#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 15000
#endif
#ifndef USE_WIFI_FALLBACK
#define USE_WIFI_FALLBACK NET_AUTO_SWITCH
#endif

// WiFi 来自 secrets.h；空字符串表示未配置
#ifndef WIFI_SSID
#define WIFI_SSID SECRETS_WIFI_SSID
#endif
#ifndef WIFI_PASS
#define WIFI_PASS SECRETS_WIFI_PASS
#endif

#define UPLOAD_HOST "httpbin.org"
#define UPLOAD_PATH "/post"

#define BUTTON_DEBOUNCE_MS 50
#define BUTTON_LONG_PRESS_MS 3000
#ifndef ERROR_HOLD_MS
#define ERROR_HOLD_MS 4000
#endif
#ifndef IDLE_BACKLIGHT_MS
#define IDLE_BACKLIGHT_MS 60000
#endif
#define CAPTURE_JPEG_QUALITY 12
#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480

#ifndef STREAM_ENABLE
#define STREAM_ENABLE 1
#endif
#ifndef STREAM_PORT
#define STREAM_PORT 80
#endif
#define STREAM_AP_SSID "SaotiCam"
#define STREAM_AP_PASS "saoti1234"

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
#define USB_STREAM_SKIP_MODEM_ON_BOOT 0
#endif

#define LOG_TAG "SAOTI"
