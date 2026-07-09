#include "config.h"
#include "pins.h"
#include "modules/display.h"

#if !USE_MOCK_DISPLAY
#include <TFT_eSPI.h>
#endif

namespace {

#if !USE_MOCK_DISPLAY
TFT_eSPI gTft;
#endif

const char* screenTitle(UiScreen screen) {
  switch (screen) {
    case UiScreen::Boot:
      return "SAOTI";
    case UiScreen::Idle:
      return "Ready";
    case UiScreen::Capturing:
      return "Capture";
    case UiScreen::Uploading:
      return "Upload";
    case UiScreen::Result:
      return "OK";
    case UiScreen::Error:
      return "Error";
  }
  return "?";
}

#if !USE_MOCK_DISPLAY
uint16_t screenColor(UiScreen screen) {
  switch (screen) {
    case UiScreen::Result:
      return TFT_GREEN;
    case UiScreen::Error:
      return TFT_RED;
    case UiScreen::Capturing:
    case UiScreen::Uploading:
      return TFT_CYAN;
    default:
      return TFT_WHITE;
  }
}
#endif

}  // namespace

bool Display::begin() {
#if USE_MOCK_DISPLAY
#if !USB_STREAM_ENABLE
  Serial.println("[LCD] mock mode — UI via serial");
#endif
  show(UiScreen::Boot, "mock display");
  ready_ = true;
  return true;
#else
  Serial.println("[LCD] ST7789 init...");
  pinMode(pins::LCD_BL, OUTPUT);
  digitalWrite(pins::LCD_BL, HIGH);
  gTft.init();
  gTft.setRotation(0);
  gTft.fillScreen(TFT_BLACK);
  gTft.setTextDatum(MC_DATUM);
  ready_ = true;
  show(UiScreen::Boot, "starting...");
  Serial.println("[LCD] ST7789 ready");
  return true;
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
  if (!ready_) return;
  gTft.fillRect(20, 200, 200, 16, TFT_BLACK);
  gTft.drawRect(20, 200, 200, 16, TFT_DARKGREY);
  const int w = (200 * percent) / 100;
  if (w > 0) {
    gTft.fillRect(20, 200, w, 16, TFT_BLUE);
  }
#endif
}

#if !USE_MOCK_DISPLAY
void Display::drawHardware(UiScreen screen, const char* message) {
  gTft.fillScreen(TFT_BLACK);
  gTft.setTextColor(screenColor(screen), TFT_BLACK);
  gTft.drawString(screenTitle(screen), 120, 70, 4);
  if (message && message[0] != '\0') {
    gTft.setTextColor(TFT_WHITE, TFT_BLACK);
    gTft.drawString(message, 120, 130, 2);
  }
}
#endif

void Display::logScreen(UiScreen screen, const char* message) {
#if USB_STREAM_ENABLE
  // USB 二进制推流与 Serial 共用 CH343，禁止 LCD 文本刷屏破坏帧同步。
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
