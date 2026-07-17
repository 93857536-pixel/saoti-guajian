#include "config.h"
#include "pins.h"
#include "modules/display.h"

#include <cstring>

#if !USE_MOCK_DISPLAY
#include <TFT_eSPI.h>
#endif

namespace {

#if !USE_MOCK_DISPLAY
TFT_eSPI gTft;

// 深青绿底 + 薄荷绿点缀（避免常见紫/奶油风）
constexpr uint16_t COL_BG = 0x0863;      // #081C20
constexpr uint16_t COL_BAR = 0x10A4;     // 顶栏略亮
constexpr uint16_t COL_LINE = 0x2A69;    // 分隔线
constexpr uint16_t COL_ACCENT = 0x07F5;  // mint
constexpr uint16_t COL_WARM = 0xFCA0;    // soft amber
constexpr uint16_t COL_TEXT = 0xEF7D;    // near-white
constexpr uint16_t COL_MUTED = 0x7BF0;   // cool grey
constexpr uint16_t COL_OK = 0x2FE4;      // green
constexpr uint16_t COL_WARN = 0xFCA0;
constexpr uint16_t COL_ERR = 0xF986;     // soft red
constexpr uint16_t COL_WIFI = 0x5E7C;    // sky
constexpr uint16_t COL_SIM = 0xFBA5;     // coral

const char* screenTitle(UiScreen screen) {
  switch (screen) {
    case UiScreen::Boot:
      return "SAOTI";
    case UiScreen::Idle:
      return "SAOTI";
    case UiScreen::Capturing:
      return "CAPTURE";
    case UiScreen::Uploading:
      return "WORKING";
    case UiScreen::Result:
      return "DONE";
    case UiScreen::Error:
      return "OOPS";
  }
  return "?";
}

const char* screenSubtitle(UiScreen screen) {
  switch (screen) {
    case UiScreen::Boot:
      return "starting up";
    case UiScreen::Idle:
      return "scan pendant";
    case UiScreen::Capturing:
      return "hold steady";
    case UiScreen::Uploading:
      return "please wait";
    case UiScreen::Result:
      return "all set";
    case UiScreen::Error:
      return "try again";
  }
  return "";
}

uint16_t accentFor(UiScreen screen) {
  switch (screen) {
    case UiScreen::Result:
      return COL_OK;
    case UiScreen::Error:
      return COL_ERR;
    case UiScreen::Capturing:
    case UiScreen::Uploading:
      return COL_WARM;
    case UiScreen::Boot:
      return COL_ACCENT;
    default:
      return COL_ACCENT;
  }
}

void fillBackground() { gTft.fillScreen(COL_BG); }

void drawStatusBar(const char* batt, const char* net) {
  gTft.fillRect(0, 0, 240, 30, COL_BAR);
  gTft.drawFastHLine(0, 30, 240, COL_LINE);

  // 电池胶囊
  if (batt && batt[0]) {
    uint16_t bc = COL_OK;
    if (batt[0] == '-' || strstr(batt, "--")) {
      bc = COL_MUTED;
    } else {
      const int pct = atoi(batt);
      if (pct <= 15) {
        bc = COL_ERR;
      } else if (pct <= 30) {
        bc = COL_WARN;
      }
    }
    // 电池外框
    gTft.drawRoundRect(8, 8, 34, 14, 2, bc);
    gTft.fillRect(42, 12, 3, 6, bc);
    // 电量填充（粗略）
    int fill = 0;
    if (batt[0] != '-' && !strstr(batt, "--")) {
      fill = (28 * atoi(batt)) / 100;
      if (fill < 0) {
        fill = 0;
      }
      if (fill > 28) {
        fill = 28;
      }
    }
    if (fill > 0) {
      gTft.fillRoundRect(10, 10, fill, 10, 1, bc);
    }
    gTft.setTextDatum(TL_DATUM);
    gTft.setTextColor(COL_TEXT, COL_BAR);
    gTft.drawString(batt, 50, 8, 2);
  }

  // 网络胶囊（右上）
  if (net && net[0]) {
    uint16_t nc = COL_WARM;
    if (strstr(net, "WiFi") && strstr(net, "SIM")) {
      nc = COL_WARM;
    } else if (strstr(net, "WiFi")) {
      nc = COL_WIFI;
    } else if (strstr(net, "SIM")) {
      nc = COL_SIM;
    } else if (strstr(net, "NoNet")) {
      nc = COL_MUTED;
    }
    const int tw = gTft.textWidth(net, 2);
    const int chipW = tw + 14;
    const int chipX = 232 - chipW;
    gTft.fillRoundRect(chipX, 6, chipW, 18, 9, nc);
    gTft.setTextDatum(MC_DATUM);
    gTft.setTextColor(COL_BG, nc);
    gTft.drawString(net, chipX + chipW / 2, 15, 2);
  }
}

void drawBrandBlock(UiScreen screen, const char* message) {
  const uint16_t accent = accentFor(screen);

  // 品牌主标题
  gTft.setTextDatum(MC_DATUM);
  gTft.setTextColor(COL_TEXT, COL_BG);
  gTft.drawString(screenTitle(screen), 120, 88, 4);

  // 强调短线
  gTft.fillRoundRect(96, 108, 48, 3, 1, accent);

  // 副标题
  gTft.setTextColor(COL_MUTED, COL_BG);
  gTft.drawString(screenSubtitle(screen), 120, 128, 2);

  // 用户消息 / 操作提示
  if (message && message[0] != '\0') {
    gTft.setTextColor(COL_TEXT, COL_BG);
    gTft.drawString(message, 120, 168, 2);
  }

  // 底部分隔
  gTft.drawFastHLine(24, 210, 192, COL_LINE);
}
#endif

}  // namespace

bool Display::begin() {
#if USE_MOCK_DISPLAY
#if !USB_STREAM_ENABLE
  Serial.println("[LCD] mock mode — UI via serial");
#endif
  show(UiScreen::Boot, "mock display");
  bl_on_ = true;
  ready_ = true;
  return true;
#else
  Serial.println("[LCD] ST7789 init...");
  pinMode(pins::LCD_BL, OUTPUT);
  digitalWrite(pins::LCD_BL, HIGH);
  bl_on_ = true;
  gTft.init();
  gTft.setRotation(0);
  fillBackground();
  gTft.setTextDatum(MC_DATUM);
  ready_ = true;
  show(UiScreen::Boot, "starting...");
  Serial.println("[LCD] ST7789 ready");
  return true;
#endif
}

void Display::setBacklight(bool on) {
  if (bl_on_ == on) {
    return;
  }
  bl_on_ = on;
#if !USE_MOCK_DISPLAY
  if (ready_) {
    digitalWrite(pins::LCD_BL, on ? HIGH : LOW);
  }
#endif
}

void Display::setNetBadge(const char* label) {
  if (!label) {
    netBadge_[0] = '\0';
    return;
  }
  strncpy(netBadge_, label, sizeof(netBadge_) - 1);
  netBadge_[sizeof(netBadge_) - 1] = '\0';
}

void Display::setBattBadge(const char* label) {
  if (!label) {
    battBadge_[0] = '\0';
    return;
  }
  strncpy(battBadge_, label, sizeof(battBadge_) - 1);
  battBadge_[sizeof(battBadge_) - 1] = '\0';
}

void Display::show(UiScreen screen, const char* message) {
  logScreen(screen, message);
#if !USE_MOCK_DISPLAY
  if (ready_) {
    drawHardware(screen, message);
  }
#endif
}

void Display::showProgress(int percent) {
#if !USB_STREAM_ENABLE
  Serial.printf("[LCD] progress %d%%\n", percent);
#endif
#if !USE_MOCK_DISPLAY
  if (!ready_) {
    return;
  }
  if (percent < 0) {
    percent = 0;
  }
  if (percent > 100) {
    percent = 100;
  }
  constexpr int x = 28;
  constexpr int y = 218;
  constexpr int w = 184;
  constexpr int h = 10;
  gTft.fillRoundRect(x, y, w, h, 5, COL_LINE);
  const int fw = (w * percent) / 100;
  if (fw > 0) {
    gTft.fillRoundRect(x, y, fw, h, 5, COL_ACCENT);
  }
#endif
}

void Display::showAnswer(const char* answer) {
  logScreen(UiScreen::Result, answer ? answer : "");
#if !USE_MOCK_DISPLAY
  if (!ready_) {
    return;
  }
  fillBackground();
  drawStatusBar(battBadge_, netBadge_);

  gTft.setTextDatum(MC_DATUM);
  gTft.setTextColor(COL_OK, COL_BG);
  gTft.drawString("ANSWER", 120, 48, 2);
  gTft.fillRoundRect(96, 62, 48, 3, 1, COL_OK);

  gTft.setTextColor(COL_TEXT, COL_BG);
  gTft.setTextDatum(TL_DATUM);
  gTft.setTextFont(2);
  String line;
  int y = 78;
  auto flushLine = [&]() {
    if (line.length() == 0) {
      return;
    }
    gTft.drawString(line, 16, y, 2);
    y += 18;
    line = "";
  };
  if (answer) {
    for (size_t i = 0; answer[i] && y < 208; ++i) {
      const unsigned char c = static_cast<unsigned char>(answer[i]);
      if (c == '\n') {
        flushLine();
        continue;
      }
      if (c < 32 || c > 126) {
        if (line.length() == 0 || line[line.length() - 1] != '.') {
          line += '.';
        }
      } else {
        line += static_cast<char>(c);
      }
      if (line.length() >= 20) {
        flushLine();
      }
    }
    flushLine();
  }

  gTft.drawFastHLine(24, 214, 192, COL_LINE);
  gTft.setTextDatum(MC_DATUM);
  gTft.setTextColor(COL_MUTED, COL_BG);
  gTft.drawString("full text: 192.168.4.1/answer", 120, 226, 1);
#endif
}

#if !USE_MOCK_DISPLAY
void Display::drawBattBadge() {
  // 由 drawStatusBar 统一绘制
}

void Display::drawNetBadge() {
  // 由 drawStatusBar 统一绘制
}

void Display::drawHardware(UiScreen screen, const char* message) {
  fillBackground();
  drawStatusBar(battBadge_, netBadge_);

  if (screen == UiScreen::Idle) {
    drawBrandBlock(screen, nullptr);
    gTft.setTextDatum(MC_DATUM);
    gTft.setTextColor(COL_ACCENT, COL_BG);
    gTft.drawString(message && message[0] ? message : "Press BOOT", 120, 168,
                    2);
    gTft.setTextColor(COL_MUTED, COL_BG);
    gTft.drawString("long press = AI test", 120, 192, 1);
  } else {
    drawBrandBlock(screen, message);
  }
}
#endif

void Display::logScreen(UiScreen screen, const char* message) {
#if USB_STREAM_ENABLE
  (void)screen;
  (void)message;
  return;
#endif
  const char* name = "?";
  switch (screen) {
    case UiScreen::Boot:
      name = "BOOT";
      break;
    case UiScreen::Idle:
      name = "IDLE";
      break;
    case UiScreen::Capturing:
      name = "CAPTURE";
      break;
    case UiScreen::Uploading:
      name = "UPLOAD";
      break;
    case UiScreen::Result:
      name = "RESULT";
      break;
    case UiScreen::Error:
      name = "ERROR";
      break;
  }
  if (message) {
    Serial.printf("[LCD] %s | %s\n", name, message);
  } else {
    Serial.printf("[LCD] %s\n", name);
  }
}
