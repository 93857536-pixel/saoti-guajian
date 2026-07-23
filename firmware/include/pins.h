#pragma once

// 引脚：YD-ESP32-S3 N16R8 实测可用映射（避开 OPI 35–37、USB 19/20、UART0 43/44）

namespace pins {

// ── ST7789 SPI（微雪 1.3" 模块）──────────────────────────
constexpr int LCD_MOSI = 1;   // DIN → 丝印 1
constexpr int LCD_SCLK = 3;   // CLK → 丝印 3
constexpr int LCD_CS = 38;
constexpr int LCD_DC = 39;
constexpr int LCD_RST = 40;
constexpr int LCD_BL = 41;

// ── OV5640 DVP / SCCB ─────────────────────────────────────
// ESP 侧命名 CAM_D0..D7（esp32-camera 8bit）。
// 微雪 OV5640 Camera Board (C) 丝印是 D2..D9（取 10bit 高 8 位）：
//   板 D2→GPIO6(=CAM_D0) … 板 D9→GPIO13(=CAM_D7)
// 正点原子等常见模块丝印 D0..D7 则直接对 CAM_D0..D7。
constexpr int CAM_XCLK = 15;
constexpr int CAM_SIOD = 4;   // 板 SIOD / SDA
constexpr int CAM_SIOC = 5;   // 板 SIOC / SCL
constexpr int CAM_D0 = 6;     // 微雪丝印 D2
constexpr int CAM_D1 = 7;     // 微雪丝印 D3
constexpr int CAM_D2 = 8;     // 微雪丝印 D4
constexpr int CAM_D3 = 9;     // 微雪丝印 D5
constexpr int CAM_D4 = 10;    // 微雪丝印 D6
constexpr int CAM_D5 = 11;    // 微雪丝印 D7
constexpr int CAM_D6 = 12;    // 微雪丝印 D8
constexpr int CAM_D7 = 13;    // 微雪丝印 D9
constexpr int CAM_VSYNC = 16;
constexpr int CAM_HREF = 17;
constexpr int CAM_PCLK = 18;
constexpr int CAM_PWDN = -1;  // 微雪可悬空
constexpr int CAM_RESET = -1; // 微雪可悬空

// ── FS-MCore-A7670G（全球版 AT）UART ─────────────────────
// VIN 独立 5V≥2A；PEN→3V3/GPIO48 常高；PWK→GPIO47 开机脉冲；NET 悬空
// 勿把模 TX 接到旧说明的 GPIO47（那是 PWK）；勿占 LCD 背光 GPIO41
constexpr int MODEM_TX = 21;  // → 模 RX
constexpr int MODEM_RX = 2;   // ← 模 TX
// ALT 探测脚已停用：42=KEY2，45=TTS TX，避免与按键/语音冲突
constexpr int MODEM_TX_ALT = -1;
constexpr int MODEM_RX_ALT = -1;
constexpr int MODEM_PWRKEY = 47;  // → 模 PWK / PWRKEY
constexpr int MODEM_PEN = 48;     // → 模 PEN（使能，常高）
constexpr int MODEM_RESET = -1;
constexpr int MODEM_NET_STATUS = -1;

// ── 按键（2 位独立按键模块）──────────────────────────────
// 模块常见丝印：GND + KEY1 + KEY2（按下拉低；ESP 内部上拉）
// KEY1→GPIO0 扫题；KEY2→GPIO42 唤醒/休眠切换；板载 BOOT 可并联 KEY1
constexpr int BUTTON = 0;
constexpr int BUTTON2 = 42;

// ── UART TTS（XFS5152 / SYN6288 同类串口语音合成）────────
// ESP TX(GPIO45) → 模块 RX；可选 BUSY(GPIO46)←模块 BUSY；喇叭接模块功放
// 与 A7670 的 Serial2 分开，使用 Serial1
constexpr int TTS_TX = 45;
constexpr int TTS_RX = -1;   // 多数模块只收不发，可不接
constexpr int TTS_BUSY = 46; // -1 = 不用忙线，按字数延时

// ── 电池电压 ADC（需分压，见 WIRING.md）──────────────────
// 电池+ → 100k → GPIO14 → 100k → GND（分压比 2）
// ADC1(GPIO1–10) 已被屏/摄像占满，只能用 ADC2 的 GPIO14。
// SoftAP 期间固件冻结采样（见 battery::setAdcFrozen）。
constexpr int BAT_ADC = 14;

}  // namespace pins
