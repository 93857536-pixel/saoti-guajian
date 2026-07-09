#pragma once

// 引脚定义与 ASSEMBLY.md 第 6 章一致
// ST7789 使用 35–40，避免与 OV5640 DVP 冲突

namespace pins {

// ── ST7789 SPI ────────────────────────────────────────────
constexpr int LCD_MOSI = 35;
constexpr int LCD_SCLK = 36;
constexpr int LCD_CS = 37;
constexpr int LCD_DC = 38;
constexpr int LCD_RST = 39;
constexpr int LCD_BL = 40;

// ── OV5640 DVP / SCCB ─────────────────────────────────────
constexpr int CAM_XCLK = 15;
constexpr int CAM_SIOD = 4;
constexpr int CAM_SIOC = 5;
constexpr int CAM_D0 = 6;
constexpr int CAM_D1 = 7;
constexpr int CAM_D2 = 8;
constexpr int CAM_D3 = 9;
constexpr int CAM_D4 = 10;
constexpr int CAM_D5 = 11;
constexpr int CAM_D6 = 12;
constexpr int CAM_D7 = 13;
constexpr int CAM_VSYNC = 16;
constexpr int CAM_HREF = 17;
constexpr int CAM_PCLK = 18;
constexpr int CAM_PWDN = -1;
constexpr int CAM_RESET = -1;

// ── A7670G UART ──────────────────────────────────────────
// 电脑 CH343 占用 UART0(GPIO43/44)，4G 默认走 21/47。
// 若你按 ASSEMBLY 旧表接了 43/44，探测时会自动尝试。
constexpr int MODEM_TX = 21;
constexpr int MODEM_RX = 47;
constexpr int MODEM_TX_ALT = 43;
constexpr int MODEM_RX_ALT = 44;
constexpr int MODEM_PWRKEY = 41;
constexpr int MODEM_RESET = 42;
constexpr int MODEM_NET_STATUS = 2;

// ── 按键（拍照） ──────────────────────────────────────────
constexpr int BUTTON = 0;  // INPUT_PULLUP，按下 LOW

}  // namespace pins
