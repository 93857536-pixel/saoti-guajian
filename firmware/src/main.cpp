#include "config.h"
#include "pins.h"
#include "modules/button.h"
#include "modules/camera.h"
#include "modules/display.h"
#include "modules/modem.h"
#include "modules/stream_server.h"
#include "modules/usb_stream.h"
#include "serial_lock.h"

#if !USE_MOCK_CAMERA
#include <Wire.h>
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

uint32_t result_until = 0;
bool system_ready = false;
bool colorbarOn = false;

void setState(AppState next) { state = next; }

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
  Serial.printf("[DIAG] 4G: ESP-TX=%d -> module RX | ESP-RX=%d <- module TX | PWK=%d\n",
                pins::MODEM_TX, pins::MODEM_RX, pins::MODEM_PWRKEY);
  Serial.printf("[DIAG] 4G: PEN must tie 3.3V | VIN must be 5V\n");
  i2cScan(pins::CAM_SIOD, pins::CAM_SIOC, "normal");
  i2cScan(pins::CAM_SIOC, pins::CAM_SIOD, "swapped");
  Serial.println("[DIAG] ---- end probe ----");
}
#endif

void runCapturePipeline() {
  if (!camera.isReady()) {
    display.show(UiScreen::Error, "camera offline");
    setState(AppState::Idle);
    return;
  }

#if USB_STREAM_ENABLE
  const bool resumeUsb = usbStream.isActive();
  usbStream.setActive(false);
  delay(200);
#endif

  setState(AppState::Capturing);
  display.show(UiScreen::Capturing, "shooting...");
  const CaptureResult cap = camera.capture();
  if (!cap.ok) {
    display.show(UiScreen::Error, cap.error ? cap.error : "capture failed");
    setState(AppState::Idle);
#if USB_STREAM_ENABLE
    if (resumeUsb) {
      usbStream.setActive(true);
    }
#endif
    return;
  }

  setState(AppState::Uploading);
  display.show(UiScreen::Uploading, "uploading...");
  display.showProgress(30);
  const UploadResult up = modem.upload(cap.jpeg.data(), cap.jpeg.size());
  display.showProgress(100);
  if (!up.ok) {
    display.show(UiScreen::Error, up.error ? up.error : "upload failed");
    setState(AppState::Idle);
#if USB_STREAM_ENABLE
    if (resumeUsb) {
      usbStream.setActive(true);
    }
#endif
    return;
  }

  display.show(UiScreen::Result, "scan OK");
  result_until = millis() + 2500;
  setState(AppState::ShowResult);
#if USB_STREAM_ENABLE
  if (resumeUsb) {
    usbStream.setActive(true);
  }
#endif
}

void printBootSummary() {
  Serial.println("[APP] ---- boot summary ----");
  Serial.printf("[APP] display=%s camera=%s\n",
                display.isReady() ? "OK" : "FAIL",
                camera.isReady() ? "OK" : "FAIL");
  Serial.printf("[APP] network: WiFi=%s 4G=%s\n",
                modem.isWifiReady() ? "OK" : "FAIL",
                modem.isCellReady() ? "OK" : "FAIL");
  system_ready = display.isReady() && camera.isReady();
  if (system_ready) {
    if (modem.isReady()) {
      Serial.println("[APP] ready — press BOOT or send 's'");
    } else {
      Serial.println("[APP] ready (net offline) — capture OK, upload may fail");
    }
  } else {
    Serial.println("[APP] degraded — fix wiring/SIM then reboot");
  }
}

void publishModuleStatus() {
  // Machine-readable status for Mac App (one line JSON).
  Serial.printf(
      "{\"type\":\"status\",\"cam\":%s,\"lcd\":%s,\"cell\":%s,\"wifi\":%s,"
      "\"usb\":%s,\"apn\":\"%s\",\"csq\":%d,\"stream\":%s,\"mock_cam\":%d,"
      "\"mock_lcd\":%d,\"mock_net\":%d}\n",
      camera.isReady() ? "true" : "false",
      display.isReady() ? "true" : "false",
      modem.isCellReady() ? "true" : "false",
      modem.isWifiReady() ? "true" : "false",
      usbStream.isActive() ? "true" : "false",
      modem.activeApn().c_str(),
      modem.lastCsq(),
#if USB_STREAM_ENABLE
      "true",
#else
      "false",
#endif
      USE_MOCK_CAMERA, USE_MOCK_DISPLAY, USE_MOCK_MODEM);
}

void handleSerialLine(const String& line) {
  const String cmd = line;
  if (cmd == "?" || cmd == "STATUS" || cmd == "status") {
    publishModuleStatus();
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
  Serial.printf("build mock lcd=%d cam=%d net=%d\n", USE_MOCK_DISPLAY,
                USE_MOCK_CAMERA, USE_MOCK_MODEM);

  button.begin(pins::BUTTON);
  display.begin();
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
  if (camera.isReady()) {
    streamServer.begin(camera, modem);
  }
#endif

#if !USB_STREAM_SKIP_MODEM_ON_BOOT
  modem.begin();
#else
  Serial.println("[NET] modem init skipped (USB stream mode)");
#endif

  printBootSummary();
  publishModuleStatus();
  if (system_ready) {
    if (modem.isReady()) {
      display.show(UiScreen::Idle, "press to scan");
    } else {
      display.show(UiScreen::Idle, "net offline");
    }
  } else if (!camera.isReady()) {
    display.show(UiScreen::Error, "camera fail");
  } else {
    display.show(UiScreen::Idle, "press to scan");
  }
}

void loop() {
  button.update();

  if (button.pressedEdge()) {
    if (state == AppState::Idle) {
      runCapturePipeline();
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
            runCapturePipeline();
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
    // Single-char hotkeys without newline (legacy Mac stream client).
    if (serialLine.length() == 0 &&
        (c == 'V' || c == 'v' || c == 's' || c == 'S' || c == 'C' || c == 'm' ||
         c == 'M' || c == 'u' || c == 'U' || c == '?')) {
      if ((c == 's' || c == 'S') && state == AppState::Idle) {
        runCapturePipeline();
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
    display.show(UiScreen::Idle, "press to scan");
    setState(AppState::Idle);
  }

  delay(10);
}
