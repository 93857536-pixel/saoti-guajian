#include "config.h"
#include "pins.h"
#include "modules/display.h"

#include <cstring>

#if !USE_MOCK_DISPLAY
#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>
#endif

namespace {

#if !USE_MOCK_DISPLAY
TFT_eSPI gTft;
U8g2_for_TFT_eSPI gU8f;

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
      return "扫题挂件";
    case UiScreen::Idle:
      return "扫题挂件";
    case UiScreen::Capturing:
      return "拍照中";
    case UiScreen::Uploading:
      return "解题中";
    case UiScreen::Result:
      return "完成";
    case UiScreen::Error:
      return "出错了";
  }
  return "?";
}

const char* screenSubtitle(UiScreen screen) {
  switch (screen) {
    case UiScreen::Boot:
      return "正在启动";
    case UiScreen::Idle:
      return "准备扫题";
    case UiScreen::Capturing:
      return "请保持稳定";
    case UiScreen::Uploading:
      return "请稍候";
    case UiScreen::Result:
      return "解答完成";
    case UiScreen::Error:
      return "请重试";
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

void u8Begin() {
  gU8f.setFontMode(1);
  gU8f.setFontDirection(0);
  gU8f.setBackgroundColor(COL_BG);
}

// 标题用 16px、正文 14px：比 12px 点阵更清晰好看
const uint8_t* fontTitle() { return u8g2_font_wqy16_t_gb2312a; }
const uint8_t* fontBody() { return u8g2_font_wqy14_t_gb2312a; }
const uint8_t* fontSmall() { return u8g2_font_wqy12_t_gb2312a; }

void u8PrintAt(int x, int y, const char* text, uint16_t color,
               const uint8_t* font = nullptr) {
  if (!font) {
    font = fontBody();
  }
  gU8f.setFont(font);
  gU8f.setForegroundColor(color);
  gU8f.setCursor(x, y);
  gU8f.print(text ? text : "");
}

void u8PrintCenter(int cx, int y, const char* text, uint16_t color,
                   const uint8_t* font = nullptr) {
  if (!font) {
    font = fontTitle();
  }
  gU8f.setFont(font);
  gU8f.setForegroundColor(color);
  const int w = gU8f.getUTF8Width(text ? text : "");
  gU8f.setCursor(cx - w / 2, y);
  gU8f.print(text ? text : "");
}

void drawBolt(int x, int y, uint16_t c) {
  gTft.fillTriangle(x + 5, y, x, y + 7, x + 4, y + 7, c);
  gTft.fillTriangle(x + 2, y + 6, x + 7, y + 6, x + 1, y + 14, c);
}

void drawStatusBar(const char* batt, const char* net, bool charging) {
  gTft.fillRect(0, 0, 240, 30, COL_BAR);
  gTft.drawFastHLine(0, 30, 240, COL_LINE);

  if (batt && batt[0]) {
    uint16_t bc = COL_OK;
    if (charging) {
      bc = COL_ACCENT;
    } else if (batt[0] == '-' || strstr(batt, "--")) {
      bc = COL_MUTED;
    } else {
      const int pct = atoi(batt);
      if (pct <= 15) {
        bc = COL_ERR;
      } else if (pct <= 30) {
        bc = COL_WARN;
      }
    }
    gTft.drawRoundRect(8, 8, 34, 14, 2, bc);
    gTft.fillRect(42, 12, 3, 6, bc);
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
    if (charging) {
      const int pulse = static_cast<int>((millis() / 400) % 5);
      fill = 8 + pulse * 5;
      if (fill > 28) {
        fill = 28;
      }
    }
    if (fill > 0) {
      gTft.fillRoundRect(10, 10, fill, 10, 1, bc);
    }
    gTft.setTextDatum(TL_DATUM);
    gTft.setTextColor(COL_TEXT, COL_BAR);
    if (charging) {
      drawBolt(48, 7, COL_ACCENT);
      gTft.drawString(batt, 58, 8, 2);
      u8Begin();
      gU8f.setBackgroundColor(COL_BAR);
      u8PrintAt(98, 21, "充电", COL_ACCENT, fontSmall());
    } else {
      gTft.drawString(batt, 50, 8, 2);
    }
  }

  if (net && net[0]) {
    uint16_t nc = COL_WARM;
    if (strstr(net, "WiFi") && (strstr(net, "4G") || strstr(net, "SIM"))) {
      nc = COL_WARM;
    } else if (strstr(net, "WiFi")) {
      nc = COL_WIFI;
    } else if (strstr(net, "4G") || strstr(net, "SIM")) {
      nc = COL_SIM;
    } else if (strstr(net, "无网") || strstr(net, "NoNet")) {
      nc = COL_MUTED;
    }
    u8Begin();
    gU8f.setFont(fontBody());
    gU8f.setBackgroundColor(nc);
    const int tw = gU8f.getUTF8Width(net);
    const int chipW = tw + 16;
    const int chipX = 232 - chipW;
    gTft.fillRoundRect(chipX, 5, chipW, 20, 9, nc);
    gU8f.setForegroundColor(COL_BG);
    gU8f.setCursor(chipX + 8, 21);
    gU8f.print(net);
  }
}

void drawBrandBlock(UiScreen screen, const char* message) {
  const uint16_t accent = accentFor(screen);
  u8Begin();
  u8PrintCenter(120, 96, screenTitle(screen), COL_TEXT, fontTitle());
  gTft.fillRoundRect(88, 112, 64, 3, 1, accent);
  u8PrintCenter(120, 138, screenSubtitle(screen), COL_MUTED, fontBody());
  if (message && message[0] != '\0') {
    u8PrintCenter(120, 172, message, COL_TEXT, fontBody());
  }
  gTft.drawFastHLine(24, 210, 192, COL_LINE);
}

void drawProgressBar(int percent) {
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
}
#endif

}  // namespace

bool Display::begin() {
#if USE_MOCK_DISPLAY
#if !USB_STREAM_ENABLE
  Serial.println("[LCD] mock mode — UI via serial");
#endif
  show(UiScreen::Boot, "模拟显示");
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
  gU8f.begin(gTft);
  fillBackground();
  gTft.setTextDatum(MC_DATUM);
  ready_ = true;
  show(UiScreen::Boot, "正在启动...");
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

void Display::setCharging(bool on) { charging_ = on; }

void Display::setAnswerApHint(const char* hint) {
  if (!hint) {
    answerApHint_[0] = '\0';
    return;
  }
  strncpy(answerApHint_, hint, sizeof(answerApHint_) - 1);
  answerApHint_[sizeof(answerApHint_) - 1] = '\0';
}

void Display::tickStatusBar() {
#if !USE_MOCK_DISPLAY
  if (!ready_) {
    return;
  }
  drawStatusBar(battBadge_, netBadge_, charging_);
#endif
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
  drawProgressBar(percent);
#endif
}

void Display::showSteps(int step, int total, const char* const* labels,
                        const char* tip) {
  if (step < 1) {
    step = 1;
  }
  if (total < 1) {
    total = 1;
  }
  if (step > total) {
    step = total;
  }
#if !USB_STREAM_ENABLE
  Serial.printf("[LCD] step %d/%d %s\n", step, total,
                (labels && step >= 1) ? labels[step - 1] : "");
#endif
#if USE_MOCK_DISPLAY
  (void)labels;
  (void)tip;
  return;
#else
  if (!ready_ || !labels) {
    return;
  }
  fillBackground();
  drawStatusBar(battBadge_, netBadge_, charging_);
  u8Begin();
  u8PrintCenter(120, 50, "扫题进度", COL_TEXT, fontTitle());
  gTft.fillRoundRect(88, 58, 64, 3, 1, COL_WARM);

  const int maxShow = total > 6 ? 6 : total;
  const int start = (step > maxShow) ? (step - maxShow) : 1;
  int y = 82;
  for (int i = start; i <= total && y <= 196; ++i) {
    const bool done = i < step;
    const bool cur = i == step;
    uint16_t col = done ? COL_OK : (cur ? COL_ACCENT : COL_MUTED);
    char mark[8];
    if (done) {
      snprintf(mark, sizeof(mark), "%d.", i);
    } else if (cur) {
      snprintf(mark, sizeof(mark), ">%d", i);
    } else {
      snprintf(mark, sizeof(mark), " %d", i);
    }
    u8PrintAt(16, y, mark, col, fontBody());
    u8PrintAt(46, y, labels[i - 1] ? labels[i - 1] : "", col, fontBody());
    if (cur) {
      gTft.fillRoundRect(8, y - 14, 4, 16, 1, COL_ACCENT);
    }
    y += 22;
  }

  if (tip && tip[0]) {
    u8PrintCenter(120, 208, tip, COL_MUTED, fontSmall());
  }
  const int pct = (step * 100) / total;
  drawProgressBar(pct);
#endif
}

void Display::showAnswer(const char* answer) {
  logScreen(UiScreen::Result, answer ? answer : "");
#if !USE_MOCK_DISPLAY
  if (!ready_) {
    return;
  }
  fillBackground();
  drawStatusBar(battBadge_, netBadge_, charging_);

  u8Begin();
  u8PrintCenter(120, 52, "答案", COL_OK, fontTitle());
  gTft.fillRoundRect(88, 58, 64, 3, 1, COL_OK);

  gU8f.setForegroundColor(COL_TEXT);
  gU8f.setFont(fontBody());
  const int maxW = 208;
  const int lineH = 18;
  int y = 80;
  String line;
  auto flushLine = [&]() {
    if (line.length() == 0) {
      return;
    }
    gU8f.setCursor(16, y);
    gU8f.print(line);
    y += lineH;
    line = "";
  };
  auto utf8Len = [](unsigned char c) -> size_t {
    if (c < 0x80) {
      return 1;
    }
    if ((c & 0xE0) == 0xC0) {
      return 2;
    }
    if ((c & 0xF0) == 0xE0) {
      return 3;
    }
    if ((c & 0xF8) == 0xF0) {
      return 4;
    }
    return 1;
  };
  if (answer) {
    for (size_t i = 0; answer[i] && y <= 200;) {
      const unsigned char c = static_cast<unsigned char>(answer[i]);
      if (c == '\r') {
        ++i;
        continue;
      }
      if (c == '\n') {
        flushLine();
        ++i;
        continue;
      }
      const size_t cl = utf8Len(c);
      String ch;
      for (size_t k = 0; k < cl && answer[i + k]; ++k) {
        ch += answer[i + k];
      }
      i += cl;
      String test = line + ch;
      if (gU8f.getUTF8Width(test.c_str()) > maxW) {
        flushLine();
        if (y > 200) {
          break;
        }
      }
      line += ch;
    }
    flushLine();
  }

  gTft.drawFastHLine(24, 210, 192, COL_LINE);
  gU8f.setForegroundColor(COL_MUTED);
  gU8f.setFont(fontSmall());
  const char* hint =
      answerApHint_[0] ? answerApHint_ : "连热点看全文 /answer";
  gU8f.setCursor(12, 228);
  gU8f.print(hint);
#endif
}

#if !USE_MOCK_DISPLAY
void Display::drawBattBadge() {}
void Display::drawNetBadge() {}

void Display::drawHardware(UiScreen screen, const char* message) {
  fillBackground();
  drawStatusBar(battBadge_, netBadge_, charging_);

  if (screen == UiScreen::Idle) {
    drawBrandBlock(screen, nullptr);
    u8Begin();
    if (charging_) {
      u8PrintCenter(120, 150, "充电中", COL_ACCENT, fontTitle());
      u8PrintCenter(120, 174,
                    message && message[0] ? message : "短按开始扫题", COL_TEXT,
                    fontBody());
      u8PrintCenter(120, 196, "请用扫题挂件App连接", COL_MUTED, fontSmall());
      u8PrintCenter(120, 214, "系统蓝牙设置搜不到", COL_MUTED, fontSmall());
    } else {
      // 主行：蓝牙名 Saoti-XXXX（对照 App 设备页）
      u8PrintCenter(120, 160,
                    message && message[0] ? message : "短按开始扫题", COL_ACCENT,
                    fontBody());
      u8PrintCenter(120, 186, "请用扫题挂件App连接", COL_TEXT, fontSmall());
      u8PrintCenter(120, 206, "系统设置里搜不到属正常", COL_MUTED, fontSmall());
      u8PrintCenter(120, 224, "短按扫题 · 长按测AI", COL_MUTED, fontSmall());
    }
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
  Serial.printf("[LCD] %s %s\n", name, message ? message : "");
}
