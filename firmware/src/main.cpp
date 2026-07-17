#include "config.h"
#include "pins.h"
#include "app_control.h"
#include "modules/battery.h"
#include "modules/button.h"
#include "modules/camera.h"
#include "modules/display.h"
#include "modules/modem.h"
#include "modules/stream_server.h"
#include "modules/usb_stream.h"
#include "modules/solver.h"
#include "serial_lock.h"
#include "test_question_jpeg.h"

#if !USE_MOCK_CAMERA
#include <Wire.h>
#endif
#include <WiFi.h>

namespace {

enum class AppState {
  Idle,
  Capturing,
  Uploading,
  ShowResult,
};

AppState state = AppState::Idle;
Display display;
Camera camera;
Modem modem;
Button button;
StreamServer streamServer;
UsbStream usbStream;
Solver solver;

uint32_t result_until = 0;
uint32_t error_until = 0;
uint32_t last_ui_activity = 0;
bool system_ready = false;
bool colorbarOn = false;

volatile bool gPendingFixedAi = false;
volatile bool gPendingCapture = false;

void setState(AppState next) { state = next; }

void noteUiActivity() {
  last_ui_activity = millis();
  display.setBacklight(true);
}

void showErrorHold(const char* message) {
  noteUiActivity();
  display.show(UiScreen::Error, message);
  error_until = millis() + ERROR_HOLD_MS;
  setState(AppState::Idle);
}

const char* idleHint() {
  return camera.isReady() ? "Press BOOT to scan" : "Press BOOT = AI test";
}

// 顶部网络条：空闲显示可用链路；扫题时显示实际使用的 Via WiFi / Via SIM
void refreshNetBadge(const char* forceVia = nullptr) {
  if (forceVia && forceVia[0]) {
    char buf[24];
    snprintf(buf, sizeof(buf), "Via %s", forceVia);
    display.setNetBadge(buf);
    return;
  }
  const bool w = modem.isWifiReady();
  const bool c = modem.isCellReady();
  if (w && c) {
    display.setNetBadge("WiFi+SIM");
  } else if (w) {
    display.setNetBadge("WiFi");
  } else if (c) {
    display.setNetBadge("SIM");
  } else {
    display.setNetBadge("NoNet");
  }
}

void refreshBattBadge() { display.setBattBadge(battery::label()); }

void showIdle() {
  noteUiActivity();
  error_until = 0;
  refreshBattBadge();
  refreshNetBadge();
  display.show(UiScreen::Idle, idleHint());
}

void preparePeripheralsForCamera() {
  prepareModemPins();
#if !USE_MOCK_CAMERA
  Wire.end();
#endif
}

#if !USE_MOCK_CAMERA
void i2cScan(int sda, int scl, const char* label) {
  Wire.end();
  delay(10);
  Wire.begin(sda, scl);
  Wire.setClock(100000);
  Serial.printf("[DIAG] I2C scan %s (SDA=GPIO%d SCL=GPIO%d):\n", label, sda, scl);
  int count = 0;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[DIAG]   0x%02X", addr);
      if (addr == 0x3C) {
        Serial.print(" <- OV5640 SCCB");
      }
      Serial.println();
      ++count;
    }
  }
  if (count == 0) {
    Serial.println("[DIAG]   (no devices — check 3.3V/GND/SDA/SCL/排线)");
  }
}

void runHardwareDiag() {
  Serial.println("[DIAG] ---- hardware probe ----");
  Serial.printf("[DIAG] camera: SDA=%d SCL=%d XCLK=%d D0-7=%d-%d\n",
                pins::CAM_SIOD, pins::CAM_SIOC, pins::CAM_XCLK, pins::CAM_D0,
                pins::CAM_D7);
  Serial.println("[DIAG] Waveshare C silk: D2..D9 -> ESP CAM_D0..D7 (GPIO6..13)");
  Serial.printf("[DIAG] 4G: ESP-TX=%d -> 模RX | ESP-RX=%d <- 模TX\n",
                pins::MODEM_TX, pins::MODEM_RX);
  Serial.println("[DIAG] 4G: VIN独立5V≥2A+共地 | PEN→48常高 | PWK→47脉冲 | 蓝灯=开机");
  i2cScan(pins::CAM_SIOD, pins::CAM_SIOC, "normal");
  i2cScan(pins::CAM_SIOC, pins::CAM_SIOD, "swapped");
  Serial.println("[DIAG] ---- end probe ----");
}
#endif

void runFixedImageSolvePipeline() {
#if !USE_OPENAI_SOLVER
  showErrorHold("AI disabled");
  return;
#else
#if USB_STREAM_ENABLE
  const bool resumeUsb = usbStream.isActive();
  usbStream.setActive(false);
  delay(50);
#endif

  setState(AppState::Uploading);
  refreshNetBadge("WiFi");  // 百炼 AI 走 WiFi
  display.show(UiScreen::Uploading, "test AI...");
  display.showProgress(20);
  Serial.printf("[AI] fixed-image test (%u bytes jpeg)\n",
                static_cast<unsigned>(kTestQuestionJpegLen));

  const SolveResult solved =
      solver.solveJpeg(kTestQuestionJpeg, kTestQuestionJpegLen);
  display.showProgress(100);
  if (!solved.ok) {
    Serial.printf("[AI] fixed-image FAIL: %s\n",
                  solved.error ? solved.error : "?");
    refreshNetBadge();
    showErrorHold(solved.error ? solved.error : "AI failed");
#if USB_STREAM_ENABLE
    if (resumeUsb) {
      usbStream.setActive(true);
    }
#endif
    return;
  }

  noteUiActivity();
  display.showAnswer(solved.answer.c_str());
  result_until = millis() + 15000;
  setState(AppState::ShowResult);
#if USB_STREAM_ENABLE
  if (resumeUsb) {
    usbStream.setActive(true);
  }
#endif
#endif
}

void runCapturePipeline() {
  if (!camera.isReady()) {
    Serial.println("[APP] capture skipped: camera offline");
    showErrorHold("camera offline");
    return;
  }

#if USB_STREAM_ENABLE
  const bool resumeUsb = usbStream.isActive();
  usbStream.setActive(false);
#endif
#if !USE_MOCK_CAMERA
  // SoftAP /stream 可能正持有 fb；暂停预览后再拍照。
  camera.setStreamingPaused(true);
#endif
  delay(250);

  noteUiActivity();
  setState(AppState::Capturing);
  refreshNetBadge();
  display.show(UiScreen::Capturing, "shooting...");
  const CaptureResult cap = camera.capture();
#if !USE_MOCK_CAMERA
  camera.setStreamingPaused(false);
#endif
  if (!cap.ok) {
    showErrorHold(cap.error ? cap.error : "capture failed");
#if USB_STREAM_ENABLE
    if (resumeUsb) {
      usbStream.setActive(true);
    }
#endif
    return;
  }
  if (cap.jpeg.size() < 800) {
    Serial.printf("[CAM] jpeg too small (%u)\n",
                  static_cast<unsigned>(cap.jpeg.size()));
    showErrorHold("bad image");
#if USB_STREAM_ENABLE
    if (resumeUsb) {
      usbStream.setActive(true);
    }
#endif
    return;
  }

  noteUiActivity();
  setState(AppState::Uploading);
  refreshNetBadge("WiFi");  // AI 默认走 WiFi
  display.show(UiScreen::Uploading, "AI solving...");
  display.showProgress(20);

#if USE_OPENAI_SOLVER
  const SolveResult solved = solver.solveJpeg(cap.jpeg.data(), cap.jpeg.size());
  display.showProgress(100);
  if (!solved.ok) {
    Serial.printf("[AI] solve failed: %s\n", solved.error ? solved.error : "?");
    display.show(UiScreen::Uploading, "fallback up...");
    const UploadResult up = modem.upload(cap.jpeg.data(), cap.jpeg.size());
    refreshNetBadge(up.via ? up.via : (up.ok ? "?" : nullptr));
    if (!up.ok) {
      refreshNetBadge();
      showErrorHold(solved.error ? solved.error : "AI failed");
#if USB_STREAM_ENABLE
      if (resumeUsb) {
        usbStream.setActive(true);
      }
#endif
      return;
    }
    noteUiActivity();
    display.show(UiScreen::Result, "uploaded");
  } else {
    refreshNetBadge("WiFi");
    noteUiActivity();
    display.showAnswer(solved.answer.c_str());
  }
#else
  const UploadResult up = modem.upload(cap.jpeg.data(), cap.jpeg.size());
  display.showProgress(100);
  refreshNetBadge(up.via ? up.via : nullptr);
  if (!up.ok) {
    refreshNetBadge();
    showErrorHold(up.error ? up.error : "upload failed");
#if USB_STREAM_ENABLE
    if (resumeUsb) {
      usbStream.setActive(true);
    }
#endif
    return;
  }
  display.show(UiScreen::Result, "scan OK");
#endif

  result_until = millis() + 12000;
  setState(AppState::ShowResult);
#if USB_STREAM_ENABLE
  if (resumeUsb) {
    usbStream.setActive(true);
  }
#endif
}

// BOOT / 's'：有摄像头就拍照解题；无摄像头则自动用内置固定题图测 AI。
void triggerScanOrTest() {
  if (camera.isReady()) {
    runCapturePipeline();
    return;
  }
  Serial.println("[APP] camera offline — BOOT falls back to fixed-image AI test");
  refreshNetBadge();
  display.show(UiScreen::Idle, "no cam,testAI");
  delay(200);
  runFixedImageSolvePipeline();
}

void printBootSummary() {
  Serial.println("[APP] ---- boot summary ----");
  Serial.printf("[APP] display=%s camera=%s\n",
                display.isReady() ? "OK" : "FAIL",
                camera.isReady() ? "OK" : "FAIL");
  Serial.printf("[APP] network: WiFi=%s 4G=%s SoftAP=%s\n",
                modem.isWifiReady() ? "OK" : "FAIL",
                modem.isCellReady() ? "OK" : "FAIL",
                modem.streamingIp().c_str());
  system_ready = display.isReady() && camera.isReady();
  Serial.println("[APP] cmds: BOOT/s=scan  longBOOT/t=AI test  ?=status  NET=4G");
  if (system_ready) {
    Serial.println("[APP] ready — short BOOT to scan, long BOOT to test AI");
  } else {
    Serial.println("[APP] degraded — short/long BOOT use fixed-image AI test");
  }
}

void publishModuleStatus() { Serial.println(appStatusJson()); }

void handleSerialLine(const String& line) {
  const String cmd = line;
  if (cmd == "?" || cmd == "STATUS" || cmd == "status") {
    publishModuleStatus();
    return;
  }
  if (cmd == "t" || cmd == "T" || cmd == "TEST" || cmd == "test") {
    if (state == AppState::Idle) {
      runFixedImageSolvePipeline();
    }
    return;
  }
  if (cmd.startsWith("APN=") || cmd.startsWith("apn=")) {
    String apn = cmd.substring(4);
    apn.trim();
    if (modem.setApn(apn.c_str())) {
      Serial.printf("{\"type\":\"apn\",\"ok\":true,\"apn\":\"%s\"}\n",
                    modem.activeApn().c_str());
    } else {
      Serial.println("{\"type\":\"apn\",\"ok\":false}");
    }
    return;
  }
  if (cmd == "DIAG" || cmd == "diag" || cmd == "m" || cmd == "M") {
    if (!usbStream.isActive()) {
      modem.runDiagnostics();
      publishModuleStatus();
    }
    return;
  }
  if (cmd == "SCAN" || cmd == "scan" || cmd == "u" || cmd == "U") {
    if (!usbStream.isActive()) {
      modem.scanUartPins();
      publishModuleStatus();
    }
    return;
  }
  if (cmd == "NET" || cmd == "net") {
    if (!usbStream.isActive()) {
      modem.ensureInitialized();
      const bool ok = modem.runDiagnostics();
      Serial.printf("{\"type\":\"net\",\"ok\":%s,\"apn\":\"%s\",\"csq\":%d}\n",
                    ok ? "true" : "false", modem.activeApn().c_str(),
                    modem.lastCsq());
    }
    return;
  }
}

}  // namespace

void appRequestFixedAiTest() { gPendingFixedAi = true; }

bool appConsumeFixedAiTestRequest() {
  if (!gPendingFixedAi) {
    return false;
  }
  gPendingFixedAi = false;
  return true;
}

void appRequestCapture() { gPendingCapture = true; }

bool appConsumeCaptureRequest() {
  if (!gPendingCapture) {
    return false;
  }
  gPendingCapture = false;
  return true;
}

String appStatusJson() {
  String j;
  j.reserve(320);
  j += F("{\"type\":\"status\",\"cam\":");
  j += camera.isReady() ? F("true") : F("false");
  j += F(",\"lcd\":");
  j += display.isReady() ? F("true") : F("false");
  j += F(",\"cell\":");
  j += modem.isCellReady() ? F("true") : F("false");
  j += F(",\"wifi\":");
  j += modem.isWifiReady() ? F("true") : F("false");
  j += F(",\"sta\":\"");
  j += (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("");
  j += F("\",\"ap\":\"");
  j += modem.streamingIp();
  j += F("\",\"apn\":\"");
  j += modem.activeApn();
  j += F("\",\"csq\":");
  j += String(modem.lastCsq());
  j += F(",\"usb\":");
  j += usbStream.isActive() ? F("true") : F("false");
  j += F(",\"has_answer\":");
  j += solverHasAnswer() ? F("true") : F("false");
  j += F(",\"answer_age_ms\":");
  j += String(solverAnswerAgeMs());
  j += F(",\"last_error\":\"");
  {
    const String err = solverLastError();
    for (size_t i = 0; i < err.length(); ++i) {
      const char c = err[i];
      if (c == '"' || c == '\\') {
        j += '\\';
      }
      if (c != '\n' && c != '\r') {
        j += c;
      }
    }
  }
  j += F("\",\"mock_cam\":");
  j += String(USE_MOCK_CAMERA);
  j += F(",\"mock_lcd\":");
  j += String(USE_MOCK_DISPLAY);
  j += F(",\"mock_net\":");
  j += String(USE_MOCK_MODEM);
  j += F("}");
  return j;
}

void setup() {
#if USB_STREAM_ENABLE
  Serial.begin(USB_STREAM_BAUD);
#else
  Serial.begin(115200);
#endif
  serialLockInit();
  delay(500);
  Serial.println();
  Serial.println("=== saoti-guajian firmware ===");
  Serial.printf("build mock lcd=%d cam=%d net=%d skip_modem=%d\n", USE_MOCK_DISPLAY,
                USE_MOCK_CAMERA, USE_MOCK_MODEM, USB_STREAM_SKIP_MODEM_ON_BOOT);

  button.begin(pins::BUTTON);
  battery::begin();
  display.begin();
  refreshBattBadge();
  display.show(UiScreen::Boot, "init...");

  preparePeripheralsForCamera();

#if !USE_MOCK_CAMERA && !USB_STREAM_ENABLE
  runHardwareDiag();
  Wire.end();
#endif
  camera.begin();

#if USB_STREAM_ENABLE
  if (camera.isReady()) {
    usbStream.begin(camera);
  }
#endif

#if STREAM_ENABLE
  // SoftAP 控制台不依赖摄像头
  streamServer.begin(camera, modem);
#endif

#if !USB_STREAM_SKIP_MODEM_ON_BOOT
  // SoftAP 之后：连 STA WiFi + 探测 4G（AI 优先 WiFi）
  display.show(UiScreen::Boot, "net init...");
  modem.begin();
#else
  Serial.println("[NET] modem init skipped (USB stream debug mode)");
#endif

  printBootSummary();
  publishModuleStatus();
  showIdle();
}

void loop() {
  button.update();

  if (state == AppState::Idle) {
    if (button.longPressEdge()) {
      noteUiActivity();
      Serial.println("[APP] long BOOT -> fixed-image AI test");
      runFixedImageSolvePipeline();
    } else if (button.shortPressEdge()) {
      noteUiActivity();
      triggerScanOrTest();
    } else if (appConsumeFixedAiTestRequest()) {
      noteUiActivity();
      runFixedImageSolvePipeline();
    } else if (appConsumeCaptureRequest()) {
      noteUiActivity();
      triggerScanOrTest();
    }
  }

  static String serialLine;
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      serialLine.trim();
      if (serialLine.length() > 0) {
        if (serialLine.length() == 1) {
          const char ch = serialLine.charAt(0);
          if ((ch == 's' || ch == 'S') && state == AppState::Idle) {
            triggerScanOrTest();
          } else if ((ch == 't' || ch == 'T') && state == AppState::Idle) {
            runFixedImageSolvePipeline();
          } else if (ch == 'V') {
            usbStream.setActive(true);
          } else if (ch == 'v') {
            usbStream.setActive(false);
          } else if (ch == 'C') {
#if !USE_MOCK_CAMERA
            colorbarOn = !colorbarOn;
            camera.setColorbar(colorbarOn);
            if (!colorbarOn) {
              applyOv5640StreamTuning();
            }
#endif
          } else {
            handleSerialLine(serialLine);
          }
        } else {
          handleSerialLine(serialLine);
        }
      }
      serialLine = "";
      continue;
    }
    if (serialLine.length() == 0 &&
        (c == 'V' || c == 'v' || c == 's' || c == 'S' || c == 't' || c == 'T' ||
         c == 'C' || c == 'm' || c == 'M' || c == 'u' || c == 'U' || c == '?')) {
      if ((c == 's' || c == 'S') && state == AppState::Idle) {
        triggerScanOrTest();
      } else if ((c == 't' || c == 'T') && state == AppState::Idle) {
        runFixedImageSolvePipeline();
      } else if (c == 'V') {
        usbStream.setActive(true);
      } else if (c == 'v') {
        usbStream.setActive(false);
      } else if (c == 'C') {
#if !USE_MOCK_CAMERA
        colorbarOn = !colorbarOn;
        camera.setColorbar(colorbarOn);
        if (!colorbarOn) {
          applyOv5640StreamTuning();
        }
#endif
      } else {
        String one;
        one += c;
        handleSerialLine(one);
      }
      continue;
    }
    if (serialLine.length() < 64) {
      serialLine += c;
    }
  }

  if (state == AppState::ShowResult && millis() >= result_until) {
    showIdle();
    setState(AppState::Idle);
  }

  // Error 屏停留后自动回 Idle 提示（此前状态已是 Idle，但 UI 卡在 OOPS）
  if (state == AppState::Idle && error_until != 0 && millis() >= error_until) {
    showIdle();
  }

  // 待机时电量变化才重绘，避免整屏闪烁
  static uint32_t lastBattUi = 0;
  static int lastBattPct = -999;
  if (state == AppState::Idle && error_until == 0 &&
      millis() - lastBattUi > 5000) {
    lastBattUi = millis();
    const int pct = battery::percent();
    if (pct != lastBattPct) {
      lastBattPct = pct;
      showIdle();
    }
  }

  // 空闲超时关背光，省电；任意按键/操作会 noteUiActivity 再点亮
  if (state == AppState::Idle && error_until == 0 && display.backlightOn() &&
      IDLE_BACKLIGHT_MS > 0 &&
      (millis() - last_ui_activity) >= IDLE_BACKLIGHT_MS) {
    display.setBacklight(false);
  }

  delay(10);
}
