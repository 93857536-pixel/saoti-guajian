#include "config.h"
#include "pins.h"
#include "app_control.h"
#include "modules/answer_ap.h"
#include "modules/battery.h"
#include "modules/button.h"
#include "modules/camera.h"
#include "modules/display.h"
#include "modules/modem.h"
#include "modules/stream_server.h"
#include "modules/usb_stream.h"
#include "modules/solver.h"
#include "modules/ble_gatt.h"
#include "serial_lock.h"
#include "test_question_jpeg.h"

#if !USE_MOCK_CAMERA
#include <Wire.h>
#endif
#if USE_WIFI_FALLBACK || STREAM_ENABLE
#include <WiFi.h>
#endif

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
volatile bool gPendingWake = false;
volatile bool gPendingThumb = false;
volatile bool gPendingFlash = false;
volatile bool gPendingFlashOn = false;
volatile bool gPipelineBusyFlag = false;

void setState(AppState next) {
  state = next;
  const char* phase = "idle";
  switch (next) {
    case AppState::Capturing:
      phase = "capturing";
      break;
    case AppState::Uploading:
      phase = "uploading";
      break;
    case AppState::ShowResult:
      phase = "result";
      break;
    case AppState::Idle:
    default:
      phase = "idle";
      break;
  }
  ble_gatt::setPhase(phase);
}

const char* uiError(const char* err) {
  if (!err || !err[0]) {
    return "出错了，请重试";
  }
  if (strstr(err, "wake")) {
    return "唤醒失败";
  }
  if (strstr(err, "camera offline") || strstr(err, "not ready")) {
    return "摄像头未就绪";
  }
  if (strstr(err, "capture") || strstr(err, "fb_get")) {
    return "拍照失败";
  }
  if (strstr(err, "bad image") || strstr(err, "too small") ||
      strstr(err, "1210")) {
    return "图片无效，请重拍";
  }
  if (strstr(err, "retake") || strstr(err, "too large") ||
      strstr(err, "jpeg too large")) {
    return "图片太大，请靠近重拍";
  }
  if (strstr(err, "4G") || strstr(err, "network") || strstr(err, "modem")) {
    return "网络未就绪";
  }
  if (strstr(err, "busy") || strstr(err, "429")) {
    return "模型繁忙，请稍后再试";
  }
  if (strstr(err, "free tier") || strstr(err, "exhausted")) {
    return "免费额度用尽";
  }
  if (strstr(err, "http") || strstr(err, "TLS") || strstr(err, "ai ")) {
    return "AI 请求失败";
  }
  if (strstr(err, "parse")) {
    return "答案解析失败";
  }
  if (strstr(err, "API key") || strstr(err, "Zhipu") || strstr(err, "set API")) {
    return "请配置智谱 API Key";
  }
  // 已是中文则原样显示
  for (const char* p = err; *p; ++p) {
    if (static_cast<unsigned char>(*p) >= 0x80) {
      return err;
    }
  }
  return "出错了，请重试";
}

void noteUiActivity() {
  last_ui_activity = millis();
  display.setBacklight(true);
}

bool periphSleeping() {
  return camera.isSleeping() || modem.radioSleeping();
}

void enterPeriphSleep() {
  if (answer_ap::active()) {
    return;
  }
#if USB_STREAM_ENABLE
  // USB 推流中 deinit 相机会与 usb_stream 任务抢 fb → 挂死/黑屏
  if (usbStream.isActive()) {
    return;
  }
#endif
  if (periphSleeping()) {
    return;
  }
  Serial.println("[PWR] peripheral sleep (cam + modem CFUN=0)");
#if USB_STREAM_ENABLE
  usbStream.setActive(false);
#endif
#if !USE_MOCK_CAMERA
  camera.setStreamingPaused(true);
#endif
  delay(50);
  camera.sleep();
#if !USE_MOCK_CAMERA
  camera.setStreamingPaused(false);
#endif
  modem.sleepRadio();
  display.setBacklight(false);
}

// 前向声明
void showScanStep(int step, const char* tip = nullptr);
void showBootSelfCheck(int step, const char* tip);
void showWakeSelfCheck(int step, const char* tip);
void refreshBattBadge();
void refreshNetBadge(const char* forceVia = nullptr);

// 开机 / 休眠唤醒 分步自检（屏上可见）
constexpr int kBootCheckCount = 5;
const char* const kBootCheckSteps[kBootCheckCount] = {
    "摄像头", "蓝牙广播", "4G 模组", "蜂窝网络", "电池",
};
constexpr int kWakeCheckCount = 6;
const char* const kWakeCheckSteps[kWakeCheckCount] = {
    "唤醒摄像头", "检测摄像头", "检测蓝牙", "唤醒 4G", "检测网络", "检测电池",
};

void showBootSelfCheck(int step, const char* tip) {
  noteUiActivity();
  refreshBattBadge();
  refreshNetBadge();
  display.setBacklight(true);
  display.showSteps(step, kBootCheckCount, kBootCheckSteps, tip);
}

void showWakeSelfCheck(int step, const char* tip) {
  noteUiActivity();
  refreshBattBadge();
  refreshNetBadge();
  display.setBacklight(true);
  display.showSteps(step, kWakeCheckCount, kWakeCheckSteps, tip);
}

// 蓝牙自检：重新广播，并在 tip 里带上可搜索的名字
bool runBleSelfCheck(void (*showStep)(int, const char*), int stepIndex) {
#if !BLE_GATT_ENABLE
  showStep(stepIndex, "蓝牙未编译");
  delay(200);
  return false;
#else
  showStep(stepIndex, "正在检查蓝牙广播…");
  const bool ok = ble_gatt::selfCheck();
  if (!ok) {
    showStep(stepIndex, "蓝牙启动失败");
    delay(700);
    return false;
  }
  char tip[48];
  snprintf(tip, sizeof(tip), "可搜 %s", ble_gatt::advName());
  showStep(stepIndex, tip);
  Serial.printf("[BLE] check ok — search App for %s (not in 系统蓝牙列表)\n",
                ble_gatt::advName());
  delay(500);
  return true;
#endif
}

bool wakePeripherals(const char* reason) {
  noteUiActivity();
  const bool needCam = camera.isSleeping();
  const bool needModem = modem.radioSleeping();
  // 外设已醒时仍做蓝牙自检（保证可被 App 搜到）
  if (!needCam && !needModem) {
    display.setBacklight(true);
    Serial.printf("[PWR] light selfcheck ble (%s)\n", reason ? reason : "?");
    return runBleSelfCheck(showWakeSelfCheck, 3);
  }
  Serial.printf("[PWR] wake+selfcheck (%s)...\n", reason ? reason : "?");
  display.setBacklight(true);
  bool ok = true;

  // 1) 摄像头
  showWakeSelfCheck(1, needCam ? "正在重新启动摄像头…" : "摄像头已就绪");
  if (needCam) {
    if (!camera.wake()) {
      ok = false;
      showWakeSelfCheck(1, "摄像头唤醒失败");
      delay(600);
    }
  }

  // 2) 摄像头探测
  showWakeSelfCheck(2, "正在抓取测试帧…");
  if (camera.isReady()) {
    if (!camera.probeFrame()) {
      Serial.println("[PWR] cam probe fail — recover once");
      showWakeSelfCheck(2, "摄像头异常，正在恢复…");
      if (!camera.wake() || !camera.probeFrame()) {
        ok = false;
        showWakeSelfCheck(2, "摄像头自检失败");
        delay(600);
      } else {
        showWakeSelfCheck(2, "摄像头恢复成功");
        delay(300);
      }
    } else {
      showWakeSelfCheck(2, "摄像头正常");
      delay(200);
    }
  } else {
    ok = false;
    showWakeSelfCheck(2, "摄像头未就绪");
    delay(600);
  }

  // 3) 蓝牙（休眠不关 BLE，但要确认仍在广播；屏上显示可搜名字）
  if (!runBleSelfCheck(showWakeSelfCheck, 3)) {
    ok = false;
  }

  // 4) 4G 射频
  showWakeSelfCheck(4, needModem ? "正在打开 4G 射频…" : "4G 已就绪");
  if (needModem) {
    if (!modem.wakeRadio()) {
      ok = false;
      showWakeSelfCheck(4, "4G 唤醒失败");
      delay(600);
    } else {
      showWakeSelfCheck(4, "4G 射频已打开");
      delay(200);
    }
  }

  // 5) 网络
  showWakeSelfCheck(5, "正在检查蜂窝网络…");
  if (modem.isCellReady()) {
    if (!modem.ensureCellNetwork()) {
      ok = false;
      showWakeSelfCheck(5, "网络未就绪，稍后重试");
      delay(600);
    } else {
      char tip[40];
      snprintf(tip, sizeof(tip), "网络正常 CSQ=%d", modem.lastCsq());
      showWakeSelfCheck(5, tip);
      delay(250);
    }
  } else {
    ok = false;
    showWakeSelfCheck(5, "模组未就绪");
    delay(600);
  }
  refreshNetBadge();
  // 4G 操作后可能碰射频，再踢一次 BLE 广播
  ble_gatt::restartAdvertising();

  // 6) 电池
  showWakeSelfCheck(6, "正在读取电量…");
  battery::setAdcFrozen(false);
  (void)battery::voltage();
  refreshBattBadge();
  {
    char tip[40];
    snprintf(tip, sizeof(tip), "电量 %s%s", battery::label(),
             battery::isCharging() ? " · 充电中" : "");
    showWakeSelfCheck(6, tip);
    delay(300);
  }

  Serial.printf("[PWR] selfcheck done ok=%d\n", ok ? 1 : 0);
  return ok;
}

void presentAnswer(const char* answer) {
  noteUiActivity();
  const bool apOk = answer_ap::start();
  if (apOk) {
    battery::setAdcFrozen(true);
    char hint[40];
    snprintf(hint, sizeof(hint), "全文 %s/answer", answer_ap::ip().c_str());
    display.setAnswerApHint(hint);
  } else {
    display.setAnswerApHint("完整答案可看串口输出");
  }
  display.showAnswer(answer);
  result_until = millis() + RESULT_HOLD_MS;
  setState(AppState::ShowResult);
  ble_gatt::notifyAnswerChanged();
}

void showErrorHold(const char* message) {
  noteUiActivity();
  answer_ap::stop();
  battery::setAdcFrozen(false);
  display.show(UiScreen::Error, uiError(message));
  error_until = millis() + ERROR_HOLD_MS;
  setState(AppState::Idle);
}

const char* idleHint() {
  static char buf[24];
#if BLE_GATT_ENABLE
  if (ble_gatt::isReady() && ble_gatt::advName()[0]) {
    // 空闲主行只显示蓝牙名，副行由显示层提示「请用 App」
    snprintf(buf, sizeof(buf), "%s", ble_gatt::advName());
    return buf;
  }
#endif
  return camera.isReady() ? "短按开始扫题" : "短按测试 AI";
}

// 顶部网络条：空闲显示可用链路；扫题时显示实际使用的通道
void refreshNetBadge(const char* forceVia) {
  if (forceVia && forceVia[0]) {
    if (strstr(forceVia, "WiFi") || strstr(forceVia, "wifi")) {
      display.setNetBadge("WiFi");
    } else {
      display.setNetBadge("4G");
    }
    return;
  }
#if NET_CELL_ONLY || !USE_WIFI_FALLBACK
  if (modem.radioSleeping()) {
    display.setNetBadge("4G休眠");
  } else {
    display.setNetBadge(modem.isCellReady() ? "4G" : "无网");
  }
#else
  if (modem.radioSleeping() && !modem.isWifiReady()) {
    display.setNetBadge("休眠");
  } else {
    const bool w = modem.isWifiReady();
    const bool c = modem.isCellReady() && !modem.radioSleeping();
    if (w && c) {
      display.setNetBadge("双网");
    } else if (w) {
      display.setNetBadge("WiFi");
    } else if (c) {
      display.setNetBadge("4G");
    } else {
      display.setNetBadge("无网");
    }
  }
#endif
}

void refreshBattBadge() {
  display.setBattBadge(battery::label());
  display.setCharging(battery::isCharging());
}

// 扫题分步（屏上中文进度）
constexpr int kScanStepCount = 6;
const char* const kScanSteps[kScanStepCount] = {
    "唤醒设备", "正在拍照", "检查图片", "连接网络", "上传题目", "AI 解答中",
};

void showScanStep(int step, const char* tip) {
  noteUiActivity();
  refreshBattBadge();
  if (step >= 4) {
    refreshNetBadge("4G");
  } else {
    refreshNetBadge();
  }
  display.showSteps(step, kScanStepCount, kScanSteps, tip);
}

void showIdle() {
  noteUiActivity();
  error_until = 0;
  answer_ap::stop();
  battery::setAdcFrozen(false);
  refreshBattBadge();
  refreshNetBadge();
  display.show(UiScreen::Idle, idleHint());
}

bool appPipelineBusy() {
  return gPipelineBusyFlag || state == AppState::Capturing ||
         state == AppState::Uploading;
}

struct PipelineGuard {
  PipelineGuard() {
    gPipelineBusyFlag = true;
    gPendingCapture = false;
    gPendingFixedAi = false;
  }
  ~PipelineGuard() { gPipelineBusyFlag = false; }
};

bool modemCommandsAllowed() {
  if (appPipelineBusy()) {
    return false;
  }
#if USB_STREAM_ENABLE
  if (usbStream.isActive()) {
    return false;
  }
#endif
  return true;
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
  showErrorHold("AI 未启用");
  return;
#else
  if (!wakePeripherals("ai-test")) {
    showErrorHold("wake failed");
    return;
  }
  PipelineGuard busyGuard;
#if USB_STREAM_ENABLE
  const bool resumeUsb = usbStream.isActive();
  usbStream.setActive(false);
  delay(50);
#endif

  setState(AppState::Uploading);
  showScanStep(2, "测试模式：正在拍照");

  // 纯 4G：内置测试图过大，改用实时拍照（更贴近真实扫题）
#if NET_CELL_ONLY || !USE_WIFI_FALLBACK
  Serial.println("[AI] cell-only: test via live camera capture");
  if (!camera.isReady()) {
    showErrorHold("camera offline");
#if USB_STREAM_ENABLE
    if (resumeUsb) {
      usbStream.setActive(true);
    }
#endif
    return;
  }
#if !USE_MOCK_CAMERA
  camera.setStreamingPaused(true);
#endif
  delay(200);
  const CaptureResult cap = camera.capture();
  ble_gatt::restartAdvertising();
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
  Serial.printf("[AI] live test jpeg %u bytes\n",
                static_cast<unsigned>(cap.jpeg.size()));
  showScanStep(3, "检查测试图片");
  showScanStep(4, "连接网络");
  showScanStep(5, "上传测试题");
  showScanStep(6, "AI 解答中…");
  const SolveResult solved = solver.solveJpeg(cap.jpeg.data(), cap.jpeg.size());
#else
  Serial.printf("[AI] fixed-image test (%u bytes jpeg)\n",
                static_cast<unsigned>(kTestQuestionJpegLen));
  showScanStep(4, "连接网络");
  showScanStep(5, "上传测试题");
  showScanStep(6, "AI 解答中…");
  const SolveResult solved =
      solver.solveJpeg(kTestQuestionJpeg, kTestQuestionJpegLen);
#endif
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

  presentAnswer(solved.answer.c_str());
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

  PipelineGuard busyGuard;
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
  // 未休眠时也会显示第 1 步，避免进度从中间跳出来
  showScanStep(1, "设备已就绪");
  showScanStep(2, "正在拍照，请勿移动…");
  const CaptureResult cap = camera.capture();
  ble_gatt::restartAdvertising();
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

  setState(AppState::Uploading);
  showScanStep(3, "正在压缩图片");
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

  showScanStep(4, "正在检查 4G 网络");
  if (!modem.ensureCellNetwork()) {
    showErrorHold("need 4G network");
#if USB_STREAM_ENABLE
    if (resumeUsb) {
      usbStream.setActive(true);
    }
#endif
    return;
  }

  showScanStep(5, "题目发送中，请稍候");
#if USE_OPENAI_SOLVER
  // 上传与等待合并显示：解题阶段最耗时
  showScanStep(6, "云端 AI 正在解答…");
  const SolveResult solved = solver.solveJpeg(cap.jpeg.data(), cap.jpeg.size());
  if (!solved.ok) {
    Serial.printf("[AI] solve failed: %s\n", solved.error ? solved.error : "?");
    refreshNetBadge();
    showErrorHold(solved.error ? solved.error : "AI failed");
#if USB_STREAM_ENABLE
    if (resumeUsb) {
      usbStream.setActive(true);
    }
#endif
    return;
  }
  refreshNetBadge("4G");
  presentAnswer(solved.answer.c_str());
#else
  const UploadResult up = modem.upload(cap.jpeg.data(), cap.jpeg.size());
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
  noteUiActivity();
  display.show(UiScreen::Result, "上传成功");
  result_until = millis() + RESULT_HOLD_MS;
  setState(AppState::ShowResult);
#endif
#if USB_STREAM_ENABLE
  if (resumeUsb) {
    usbStream.setActive(true);
  }
#endif
}

// BOOT / 's'：有摄像头就拍照解题；无摄像头则自动用内置固定题图测 AI。
void triggerScanOrTest() {
  if (!wakePeripherals("scan")) {
    showErrorHold("wake failed");
    return;
  }
  if (camera.isReady()) {
    runCapturePipeline();
    return;
  }
  Serial.println("[APP] camera offline — BOOT falls back to fixed-image AI test");
  refreshNetBadge();
  display.show(UiScreen::Idle, "无摄像头，改测 AI");
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
    if (!modemCommandsAllowed()) {
      Serial.println("{\"type\":\"apn\",\"ok\":false,\"reason\":\"busy\"}");
      return;
    }
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
    if (!modemCommandsAllowed()) {
      Serial.println("{\"type\":\"diag\",\"ok\":false,\"reason\":\"busy\"}");
      return;
    }
    modem.runDiagnostics();
    publishModuleStatus();
    return;
  }
  if (cmd == "FW" || cmd == "fw" || cmd == "SIMCOMATI" || cmd == "simcomati") {
    if (!modemCommandsAllowed()) {
      Serial.println("{\"type\":\"fw\",\"ok\":false,\"reason\":\"busy\"}");
      return;
    }
    modem.dumpFirmwareInfo();
    return;
  }
  if (cmd == "SCAN" || cmd == "scan" || cmd == "u" || cmd == "U") {
    if (!modemCommandsAllowed()) {
      Serial.println("{\"type\":\"scan\",\"ok\":false,\"reason\":\"busy\"}");
      return;
    }
    modem.scanUartPins();
    publishModuleStatus();
    return;
  }
  if (cmd == "NET" || cmd == "net") {
    if (!modemCommandsAllowed()) {
      Serial.println("{\"type\":\"net\",\"ok\":false,\"reason\":\"busy\"}");
      return;
    }
    modem.ensureInitialized();
    const bool ok = modem.runDiagnostics();
    Serial.printf("{\"type\":\"net\",\"ok\":%s,\"apn\":\"%s\",\"csq\":%d}\n",
                  ok ? "true" : "false", modem.activeApn().c_str(),
                  modem.lastCsq());
    return;
  }
  if (cmd == "MODEL" || cmd == "model" || cmd == "MODELS" || cmd == "models") {
    Serial.println(solverModelPoolStatus());
    return;
  }
  if (cmd == "MODEL=reset" || cmd == "model=reset" || cmd == "MODEL=RESET") {
    solverResetModelPool();
    Serial.println(solverModelPoolStatus());
    return;
  }
  if (cmd == "BAT" || cmd == "bat") {
    Serial.printf(
        "{\"type\":\"bat\",\"pin_v\":%.2f,\"cell_v\":%.2f,\"pct\":%d,"
        "\"label\":\"%s\",\"charging\":%s}\n",
        battery::pinVoltage(), battery::voltage(), battery::percent(),
        battery::label(), battery::isCharging() ? "true" : "false");
    return;
  }
  if (cmd == "BLE" || cmd == "ble" || cmd == "BLEDISC" || cmd == "bledisc") {
    ble_gatt::disconnectAll();
    ble_gatt::restartAdvertising();
    Serial.printf("{\"type\":\"ble\",\"ok\":true,\"name\":\"%s\",\"connected\":%s}\n",
                  ble_gatt::advName(),
                  ble_gatt::isConnected() ? "true" : "false");
    return;
  }
  if (cmd == "HTEST" || cmd == "htest") {
    if (!modemCommandsAllowed()) {
      Serial.println("{\"type\":\"htest\",\"ok\":false,\"reason\":\"busy\"}");
      return;
    }
    // 国内可达：打智谱（小 JSON，验证 ESP-TLS）
    const char* url = OPENAI_BASE_URL;
    char body[192];
    snprintf(body, sizeof(body),
             "{\"model\":\"%s\",\"messages\":[{\"role\":\"user\","
             "\"content\":\"ping\"}],\"max_tokens\":16}",
             OPENAI_MODEL);
    Serial.println("[NET] HTEST ESP-TLS → bigmodel.cn (tiny JSON) ...");
    const CellHttpResult hr = modem.httpsPostJson(
        url, OPENAI_API_KEY, reinterpret_cast<const uint8_t*>(body),
        strlen(body));
    Serial.printf("{\"type\":\"htest\",\"ok\":%s,\"http\":%d,\"blen\":%u}\n",
                  hr.ok ? "true" : "false", hr.httpCode,
                  static_cast<unsigned>(hr.body.length()));
    if (hr.body.length()) {
      Serial.println(hr.body.substring(0, 400));
    }
    return;
  }
  if (cmd == "C" || cmd == "c") {
#if !USE_MOCK_CAMERA
    if (appPipelineBusy()) {
      Serial.println("[CAM] colorbar ignored: busy");
      return;
    }
    colorbarOn = !colorbarOn;
    camera.setColorbar(colorbarOn);
    if (!colorbarOn) {
      applyOv5640StreamTuning();
    }
#endif
    return;
  }
  if (cmd == "CAMDIAG" || cmd == "camdiag" || cmd == "CAM" || cmd == "cam") {
    if (appPipelineBusy()) {
      Serial.println("{\"type\":\"camdiag\",\"ok\":false,\"reason\":\"busy\"}");
      return;
    }
#if USB_STREAM_ENABLE
    const bool resumeUsb = usbStream.isActive();
    usbStream.setActive(false);
    delay(80);
#endif
    noteUiActivity();
    camera.runBlackDiag();
#if USB_STREAM_ENABLE
    if (resumeUsb) {
      usbStream.setActive(true);
    }
#endif
    return;
  }
#if !USE_MOCK_CAMERA
  if (cmd == "FLASHON" || cmd == "flashon") {
    noteUiActivity();
    if (camera.isSleeping()) {
      (void)camera.wake();
    }
    setOv5640Flash(true);
    Serial.println("{\"type\":\"flash\",\"on\":true}");
    return;
  }
  if (cmd == "FLASHOFF" || cmd == "flashoff") {
    setOv5640Flash(false);
    Serial.println("{\"type\":\"flash\",\"on\":false}");
    return;
  }
#endif
}

}  // namespace

void appRequestFixedAiTest() {
  // 扫题进行中忽略，避免完成后自动再扫一次
  if (gPipelineBusyFlag || gPendingFixedAi) {
    Serial.println("[APP] fixed-AI request ignored: busy");
    return;
  }
  gPendingFixedAi = true;
}

bool appConsumeFixedAiTestRequest() {
  if (!gPendingFixedAi) {
    return false;
  }
  gPendingFixedAi = false;
  return true;
}

void appRequestCapture() {
  if (gPipelineBusyFlag || gPendingCapture) {
    Serial.println("[APP] capture request ignored: busy");
    return;
  }
  gPendingCapture = true;
}

bool appConsumeCaptureRequest() {
  if (!gPendingCapture) {
    return false;
  }
  gPendingCapture = false;
  return true;
}

void appRequestWake() { gPendingWake = true; }

bool appConsumeWakeRequest() {
  if (!gPendingWake) {
    return false;
  }
  gPendingWake = false;
  return true;
}

void appRequestThumb() { gPendingThumb = true; }

bool appConsumeThumbRequest() {
  if (!gPendingThumb) {
    return false;
  }
  gPendingThumb = false;
  return true;
}

void appRequestFlash(bool on) {
  gPendingFlashOn = on;
  gPendingFlash = true;
}

bool appConsumeFlashRequest(bool* onOut) {
  if (!gPendingFlash) {
    return false;
  }
  gPendingFlash = false;
  if (onOut) {
    *onOut = gPendingFlashOn;
  }
  return true;
}

String appStatusJson() {
  String j;
  j.reserve(420);
  j += F("{\"type\":\"status\",\"cam\":");
  j += camera.isReady() ? F("true") : F("false");
  j += F(",\"lcd\":");
  j += display.isReady() ? F("true") : F("false");
  j += F(",\"cell\":");
  j += modem.isCellReady() ? F("true") : F("false");
  j += F(",\"wifi\":");
  j += modem.isWifiReady() ? F("true") : F("false");
  j += F(",\"sta\":\"");
#if USE_WIFI_FALLBACK
  j += (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("");
#endif
  j += F("\",\"ap\":\"");
  j += modem.streamingIp();
  j += F("\",\"apn\":\"");
  j += modem.activeApn();
  j += F("\",\"model\":\"");
  j += solverActiveModel();
  j += F("\",\"bat_v\":");
  {
    const float bv = battery::voltage();
    if (bv < 0) {
      j += F("null");
    } else {
      j += String(bv, 2);
    }
  }
  j += F(",\"bat_pct\":");
  j += String(battery::percent());
  j += F(",\"charging\":");
  j += battery::isCharging() ? F("true") : F("false");
  j += F(",\"csq\":");
  j += String(modem.lastCsq());
  j += F(",\"usb\":");
  j += usbStream.isActive() ? F("true") : F("false");
  j += F(",\"has_answer\":");
  j += solverHasAnswer() ? F("true") : F("false");
  j += F(",\"answer_age_ms\":");
  j += String(solverAnswerAgeMs());
  j += F(",\"sleeping\":");
  j += periphSleeping() ? F("true") : F("false");
  j += F(",\"busy\":");
  j += gPipelineBusyFlag ? F("true") : F("false");
  j += F(",\"phase\":\"");
  j += ble_gatt::phase();
  j += F("\",\"fw\":\"");
  j += FW_VERSION;
  j += F("\",\"fw_proto\":");
  j += String(ble_gatt::kProtoVersion);
  j += F(",\"ble\":");
  j += ble_gatt::isConnected() ? F("true") : F("false");
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
  solverBegin();
  solverSetModem(&modem);
  display.begin();
  refreshBattBadge();
  display.show(UiScreen::Boot, "正在初始化...");

  preparePeripheralsForCamera();

#if !USE_MOCK_CAMERA && !USB_STREAM_ENABLE
  runHardwareDiag();
  Wire.end();
#endif
  // ---- 开机自检（屏上分步）----
  showBootSelfCheck(1, "摄像头启动中…");
  camera.begin();
  if (camera.isReady()) {
    showBootSelfCheck(1, "摄像头正常");
    delay(250);
  } else {
    showBootSelfCheck(1, "摄像头未就绪");
    delay(600);
  }

#if USB_STREAM_ENABLE
  if (camera.isReady()) {
    usbStream.begin(camera);
  }
#endif

#if STREAM_ENABLE
  streamServer.begin(camera, modem);
#endif

  // 2) 蓝牙（须在 4G 前；屏上显示可搜索名字）
  (void)runBleSelfCheck(showBootSelfCheck, 2);

#if !USB_STREAM_SKIP_MODEM_ON_BOOT
  showBootSelfCheck(3, "4G 模组启动中…");
  modem.begin();
  showBootSelfCheck(3, modem.isCellReady() ? "4G 模组就绪" : "4G 未就绪");
  delay(300);
  ble_gatt::restartAdvertising();

  showBootSelfCheck(4, "检查蜂窝网络…");
  if (modem.isCellReady() && modem.ensureCellNetwork()) {
    char tip[40];
    snprintf(tip, sizeof(tip), "网络正常 CSQ=%d", modem.lastCsq());
    showBootSelfCheck(4, tip);
  } else {
    showBootSelfCheck(4, "网络稍后可用");
  }
  delay(300);
  ble_gatt::restartAdvertising();
#else
  showBootSelfCheck(3, "4G 已跳过(调试)");
  showBootSelfCheck(4, "网络已跳过(调试)");
  Serial.println("[NET] modem init skipped (USB stream debug mode)");
  delay(200);
#endif

  showBootSelfCheck(5, "读取电量…");
  battery::setAdcFrozen(false);
  (void)battery::voltage();
  refreshBattBadge();
  {
    char tip[40];
    snprintf(tip, sizeof(tip), "电量 %s%s", battery::label(),
             battery::isCharging() ? " · 充电中" : "");
    showBootSelfCheck(5, tip);
    delay(350);
  }

#if BLE_GATT_ENABLE
  // 收尾再强调一次蓝牙名，方便对着屏搜 App
  if (ble_gatt::isReady()) {
    char tip[48];
    snprintf(tip, sizeof(tip), "App搜 %s", ble_gatt::advName());
    showBootSelfCheck(2, tip);
    delay(800);
  }
#endif

  printBootSummary();
  publishModuleStatus();
  showIdle();
}

void loop() {
  button.update();
  ble_gatt::loop();

  if (appConsumeWakeRequest()) {
    (void)wakePeripherals("ble");
  }
  bool flashOn = false;
  if (appConsumeFlashRequest(&flashOn)) {
#if !USE_MOCK_CAMERA
    if (camera.isSleeping()) {
      (void)camera.wake();
    }
    setOv5640Flash(flashOn);
#endif
  }
  if (appConsumeThumbRequest() && !gPipelineBusyFlag) {
    if (camera.isSleeping()) {
      (void)wakePeripherals("ble-thumb");
    }
#if USB_STREAM_ENABLE
    usbStream.setActive(false);
#endif
#if !USE_MOCK_CAMERA
    camera.setStreamingPaused(true);
#endif
    const CaptureResult thumb = camera.captureThumbnail();
#if !USE_MOCK_CAMERA
    camera.setStreamingPaused(false);
#endif
    if (thumb.ok) {
      ble_gatt::sendThumbJpeg(thumb.jpeg.data(), thumb.jpeg.size());
    } else {
      ble_gatt::notifyEventJson("{\"type\":\"thumb\",\"ok\":false}");
    }
  }

  if (state == AppState::ShowResult) {
    if (button.shortPressEdge() || button.longPressEdge()) {
      showIdle();
      setState(AppState::Idle);
    }
  } else if (state == AppState::Idle) {
    if (button.longPressEdge()) {
      Serial.println("[APP] long BOOT -> fixed-image AI test");
      runFixedImageSolvePipeline();
    } else if (button.shortPressEdge()) {
      triggerScanOrTest();
    } else if (appConsumeFixedAiTestRequest()) {
      runFixedImageSolvePipeline();
    } else if (appConsumeCaptureRequest()) {
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
            noteUiActivity();
            usbStream.setActive(true);
          } else if (ch == 'v') {
            usbStream.setActive(false);
          } else if (ch == 'C') {
#if !USE_MOCK_CAMERA
            if (!appPipelineBusy()) {
              colorbarOn = !colorbarOn;
              camera.setColorbar(colorbarOn);
              if (!colorbarOn) {
                applyOv5640StreamTuning();
              }
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
    // 仅无歧义的单字符即时命令；C/M 等必须等换行，避免与 CTEST/MODEL 冲突
    if (serialLine.length() == 0 &&
        (c == 'V' || c == 'v' || c == 's' || c == 'S' || c == 't' || c == 'T' ||
         c == '?')) {
      if ((c == 's' || c == 'S') && state == AppState::Idle) {
        triggerScanOrTest();
      } else if ((c == 't' || c == 'T') && state == AppState::Idle) {
        runFixedImageSolvePipeline();
      } else if (c == 'V') {
        noteUiActivity();
        usbStream.setActive(true);
      } else if (c == 'v') {
        usbStream.setActive(false);
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

  // 待机时电量/充电状态变化才整屏重绘；充电中只刷顶栏脉冲，避免闪屏
  static uint32_t lastBattUi = 0;
  static uint32_t lastChgAnim = 0;
  static int lastBattPct = -999;
  static bool lastCharging = false;
  if (state == AppState::Idle && error_until == 0) {
    if (millis() - lastBattUi > 2000) {
      lastBattUi = millis();
      refreshBattBadge();
      const int pct = battery::percent();
      const bool chg = battery::isCharging();
      if (pct != lastBattPct || chg != lastCharging) {
        lastBattPct = pct;
        lastCharging = chg;
        showIdle();
      }
    }
    if (battery::isCharging() && millis() - lastChgAnim > 400) {
      lastChgAnim = millis();
      refreshBattBadge();
      display.tickStatusBar();
      noteUiActivity();  // 充电时保持背光
    }
  }

  // 空闲：先关背光，再关摄像头/4G 射频；充电中/答案热点/USB 推流时不睡
  if (state == AppState::Idle && error_until == 0 && !battery::isCharging() &&
      !answer_ap::active()
#if USB_STREAM_ENABLE
      && !usbStream.isActive()
#endif
  ) {
    if (display.backlightOn() && IDLE_BACKLIGHT_MS > 0 &&
        (millis() - last_ui_activity) >= IDLE_BACKLIGHT_MS) {
      display.setBacklight(false);
    }
    if (IDLE_PERIPH_SLEEP_MS > 0 && !periphSleeping() &&
        (millis() - last_ui_activity) >= IDLE_PERIPH_SLEEP_MS) {
      enterPeriphSleep();
    }
  }

  delay(10);
}
