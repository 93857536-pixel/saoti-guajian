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

// ── AI 扫题解题（默认：智谱 GLM-V，OpenAI 兼容接口） ──
#ifndef USE_OPENAI_SOLVER
#define USE_OPENAI_SOLVER 1
#endif
#ifndef OPENAI_BASE_URL
#define OPENAI_BASE_URL \
  "https://open.bigmodel.cn/api/paas/v4/chat/completions"
#endif
// 首选视觉模型；额度不足时固件会自动切换到其它 VL 模型（见 solver 模型池）
#ifndef OPENAI_MODEL
#define OPENAI_MODEL "glm-4v-flash"
#endif
#ifndef OPENAI_TIMEOUT_MS
#define OPENAI_TIMEOUT_MS 60000
#endif
// 单次扫题最多尝试几个视觉模型（遇额度耗尽自动换）
#ifndef OPENAI_MODEL_MAX_TRIES
#define OPENAI_MODEL_MAX_TRIES 6
#endif

// 蜗牛移动：品牌≠制式。本机卡实测 IMSI 46001…/ICCID 898601… = 中国联通
// 固件会按 IMSI 自动选 APN；默认 wonet（联通），备用 cmiot/cmnet 等
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
// 无独立 CHG 脚：进入「充电中」须看到充电/5V 轨；电压上升只用于维持状态。
// 勿用「满电电压≥4.15」或短暂回升单独进入充电——扫题后负载卸掉会误报。
#ifndef BAT_V_CHARGE_RISE
#define BAT_V_CHARGE_RISE 0.050f
#endif
#ifndef BAT_CHARGE_RISE_COUNT
#define BAT_CHARGE_RISE_COUNT 4
#endif
// 仅当分压还原电压明显高于锂电满电+噪声时才判充电轨。
// 7.0 太低：ADC 顶格≈3.3V×2=6.6～7.0 会误报；真接 5V 分压通常 ≥8V。
#ifndef BAT_RATIOED_CHARGE_RAIL
#define BAT_RATIOED_CHARGE_RAIL 8.0f
#endif

#ifndef CELL_APN
#define CELL_APN "wonet"
#endif
#ifndef CELL_APN_FALLBACKS
#define CELL_APN_FALLBACKS "cmiot", "cmmtm", "cmnet", "3gnet", "scuiot", "ctnet"
#endif

// 组网策略：默认只用 4G（不再连家用 WiFi / SoftAP）
#ifndef NET_CELL_ONLY
#define NET_CELL_ONLY 1
#endif
#ifndef NET_AUTO_SWITCH
#define NET_AUTO_SWITCH 0
#endif
#ifndef NET_PREFER_WIFI
#define NET_PREFER_WIFI 0
#endif
#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 15000
#endif
#if NET_CELL_ONLY
#undef USE_WIFI_FALLBACK
#define USE_WIFI_FALLBACK 0
#else
#ifndef USE_WIFI_FALLBACK
#define USE_WIFI_FALLBACK NET_AUTO_SWITCH
#endif
#endif

// WiFi 来自 secrets.h（CELL_ONLY 时忽略）
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
// 空闲后关闭摄像头 + 4G 射频（AT+CFUN=0），按键/扫题时再唤醒
#ifndef IDLE_PERIPH_SLEEP_MS
#define IDLE_PERIPH_SLEEP_MS 90000
#endif
// 答案页停留（含 SoftAP 看全文）
#ifndef RESULT_HOLD_MS
#define RESULT_HOLD_MS 45000
#endif
// JPEG 质量：esp32-camera/jpge 数值越小画质越好（文件越大）
// 现走 ESP-TLS，可比较旧 HTTPDATA 路径放宽体积
#if NET_CELL_ONLY
#ifndef CAPTURE_JPEG_QUALITY
#define CAPTURE_JPEG_QUALITY 8
#endif
#else
#ifndef CAPTURE_JPEG_QUALITY
#define CAPTURE_JPEG_QUALITY 8
#endif
#endif
#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480
// ESP-TLS 分片发送；放宽以便 cloud-safe 高质量软编码
#ifndef CELL_AI_MAX_JPEG
#define CELL_AI_MAX_JPEG 48000
#endif
// frame2jpg 质量 1–100，越高越清晰（与 OV5640 硬件 jpeg_quality 刻度相反）
#ifndef CLOUD_SAFE_JPG_QUALITY
#define CLOUD_SAFE_JPG_QUALITY 92
#endif
// 近黑/空图拒绝：体积小且熵低才丢；高熵小图（文字多、压得好）仍放行
#ifndef CLOUD_SAFE_MIN_JPEG
#define CLOUD_SAFE_MIN_JPEG 4000
#endif
#ifndef CLOUD_SAFE_MIN_DIVERSITY
#define CLOUD_SAFE_MIN_DIVERSITY 48
#endif
// 熵很高时允许略小体积（避免误拒清晰但压缩得好的帧）
#ifndef CLOUD_SAFE_RICH_DIVERSITY
#define CLOUD_SAFE_RICH_DIVERSITY 100
#endif
#ifndef CLOUD_SAFE_RICH_MIN_JPEG
#define CLOUD_SAFE_RICH_MIN_JPEG 3200
#endif

// 扫题捕获分辨率：1=优先 HVGA(480x320)，失败回退 QVGA（VGA 在本板易卡死）
// 智谱对 OV5640 硬件 JPEG 兼容性差 → cloud-safe 软编码；HVGA 提升字迹
#ifndef CAPTURE_USE_HVGA
#define CAPTURE_USE_HVGA 1
#endif
#ifndef CLOUD_SAFE_USE_HVGA
#define CLOUD_SAFE_USE_HVGA 1
#endif
// 装机朝向：镜头朝外壳正面外；微雪 C 型在 ESP32-S3 上常见需垂直翻转才「正前方朝上」
// 若画面仍反了，串口发 CAMFLIP / CAMMIRROR 切换，或改下面默认值重编
#ifndef CAM_VFLIP
#define CAM_VFLIP 1
#endif
#ifndef CAM_HMIRROR
#define CAM_HMIRROR 0
#endif

// 拍照前 OV5640 自动对焦（需模组 AF-VCC 接 3.3V，微雪 C 型通常已接）
#ifndef CAM_AUTO_FOCUS
#define CAM_AUTO_FOCUS 1
#endif

// 拍照自动补光（微雪 C 型镜头两侧灯，经 OV5640 STROBE）
#ifndef CAM_AUTO_FLASH
#define CAM_AUTO_FLASH 1
#endif
// 场景平均亮度 0x56A1 低于此值 → 开灯（AEC 未跟上的偏暗）
#ifndef CAM_FLASH_AVG_THRESHOLD
#define CAM_FLASH_AVG_THRESHOLD 40
#endif
// AGC 增益低字节 0x350B 高于此值 → 开灯（AEC 已拉满，环境仍偏暗）
#ifndef CAM_FLASH_GAIN_THRESHOLD
#define CAM_FLASH_GAIN_THRESHOLD 0x28
#endif

// SoftAP/STA 预览：纯 4G 模式默认关闭（可用 USB 串口推流）
#ifndef STREAM_ENABLE
#if NET_CELL_ONLY
#define STREAM_ENABLE 0
#else
#define STREAM_ENABLE 1
#endif
#endif
// BLE Companion（须在 ANSWER_SOFTAP 之前定义）
#ifndef BLE_GATT_ENABLE
#define BLE_GATT_ENABLE 1
#endif
// 解题成功后临时开 SoftAP；启用 BLE 时默认关（WiFi AP 会冲掉 BLE 广播）
#ifndef ANSWER_SOFTAP_ENABLE
#if BLE_GATT_ENABLE
#define ANSWER_SOFTAP_ENABLE 0
#else
#define ANSWER_SOFTAP_ENABLE 1
#endif
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
#define USB_STREAM_AEC_VALUE 1800
#endif
#ifndef USB_STREAM_SKIP_MODEM_ON_BOOT
#define USB_STREAM_SKIP_MODEM_ON_BOOT 0
#endif

#define LOG_TAG "SAOTI"

#ifndef FW_VERSION
#define FW_VERSION "1.1.0-ble"
#endif
