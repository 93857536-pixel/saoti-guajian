#include "config.h"
#include "pins.h"
#include "modules/camera.h"
#include "serial_lock.h"

#include <cstring>

#if !USE_MOCK_CAMERA
#include "esp_camera.h"
#include "img_converters.h"
#include <Wire.h>
#if USB_STREAM_ENABLE
#include "esp32-hal-psram.h"
#endif
#endif

namespace {

#if !USE_MOCK_CAMERA
camera_config_t buildCameraConfig() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = pins::CAM_D0;
  config.pin_d1 = pins::CAM_D1;
  config.pin_d2 = pins::CAM_D2;
  config.pin_d3 = pins::CAM_D3;
  config.pin_d4 = pins::CAM_D4;
  config.pin_d5 = pins::CAM_D5;
  config.pin_d6 = pins::CAM_D6;
  config.pin_d7 = pins::CAM_D7;
  config.pin_xclk = pins::CAM_XCLK;
  config.pin_pclk = pins::CAM_PCLK;
  config.pin_vsync = pins::CAM_VSYNC;
  config.pin_href = pins::CAM_HREF;
  config.pin_sccb_sda = pins::CAM_SIOD;
  config.pin_sccb_scl = pins::CAM_SIOC;
  config.pin_pwdn = pins::CAM_PWDN;
  config.pin_reset = pins::CAM_RESET;
  config.xclk_freq_hz = 20000000;
#if USB_STREAM_ENABLE
  // JPEG QVGA is the only stable boot format on this OV5640 module.
  // Hardware JPEG may be dark/corrupt; RGB565/YUV422 full-init is unstable.
  config.frame_size = FRAMESIZE_QVGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.jpeg_quality = USB_STREAM_JPEG_QUALITY;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.fb_count = 2;
#else
  config.frame_size = FRAMESIZE_VGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.jpeg_quality = CAPTURE_JPEG_QUALITY;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.fb_count = 2;
#endif
  return config;
}

const char* sensorName() {
  sensor_t* sensor = esp_camera_sensor_get();
  if (!sensor) {
    return "unknown";
  }
  switch (sensor->id.PID) {
    case OV5640_PID:
      return "OV5640";
    case OV3660_PID:
      return "OV3660";
    case OV2640_PID:
      return "OV2640";
    default:
      return "other";
  }
}
#endif

#if USB_STREAM_ENABLE
camera_config_t buildUsbStreamCameraConfig() {
  camera_config_t config = buildCameraConfig();
  config.frame_size = FRAMESIZE_QQVGA;
  config.pixel_format = PIXFORMAT_YUV422;
  config.fb_count = 2;
  config.grab_mode = CAMERA_GRAB_LATEST;
  return config;
}
#endif

}  // namespace

#if !USE_MOCK_CAMERA
namespace {

constexpr uint8_t kOv5640Addr = 0x3C;
constexpr uint16_t kSystemControl0 = 0x3008;
constexpr uint8_t kSoftwareReset = 0x82;

void releaseWireBus() {
  Wire.end();
  delay(30);
}

bool probeOv5640Addr() {
  Wire.begin(pins::CAM_SIOD, pins::CAM_SIOC);
  Wire.setClock(100000);
  Wire.beginTransmission(kOv5640Addr);
  const uint8_t ack = Wire.endTransmission();
  Wire.end();
  return ack == 0;
}

bool softResetOv5640() {
  Wire.begin(pins::CAM_SIOD, pins::CAM_SIOC);
  Wire.setClock(100000);
  delay(20);
  Wire.beginTransmission(kOv5640Addr);
  Wire.write(static_cast<uint8_t>(kSystemControl0 >> 8));
  Wire.write(static_cast<uint8_t>(kSystemControl0 & 0xFF));
  Wire.write(kSoftwareReset);
  const uint8_t ack = Wire.endTransmission();
  Wire.end();
  if (ack != 0) {
    return false;
  }
  delay(300);
  return true;
}

bool waitForOv5640Bus(uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (probeOv5640Addr()) {
      return true;
    }
    delay(100);
  }
  return false;
}

void logPreflightSccb() {
  Wire.begin(pins::CAM_SIOD, pins::CAM_SIOC);
  Wire.setClock(100000);
  const uint8_t addrs[] = {0x21, 0x30, 0x3C};
  for (uint8_t addr : addrs) {
    Wire.beginTransmission(addr);
    const uint8_t ack = Wire.endTransmission();
    Serial.printf("[CAM] preflight 0x%02X ack=%u\n", addr, ack);
  }
  Wire.end();
}

void recoverCameraHardware() {
  releaseWireBus();
  softResetOv5640();
  waitForOv5640Bus(2500);
  delay(500);
}

}  // namespace

void setOv5640Colorbar(bool enable) {
  sensor_t* sensor = esp_camera_sensor_get();
  if (!sensor || !sensor->set_colorbar) {
    return;
  }
  sensor->set_colorbar(sensor, enable ? 1 : 0);
  Serial.printf("[CAM] colorbar %s\n", enable ? "ON" : "OFF");
}

void applyOv5640StreamTuning() {
  sensor_t* sensor = esp_camera_sensor_get();
  if (!sensor) {
    return;
  }

#if USB_STREAM_COLORBAR_TEST
  setOv5640Colorbar(true);
#else
  // Do not touch AE/AGC registers here — some OV5640 modules stop
  // producing frames after aggressive sensor writes.
  (void)sensor;
#endif
}

bool recoverJpegCamera() {
  Serial.println("[CAM] recover JPEG QVGA...");
  esp_camera_deinit();
  delay(200);
  Wire.end();
  delay(200);
  const camera_config_t config = buildCameraConfig();
  const esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] recover init failed: 0x%x\n", static_cast<unsigned>(err));
    return false;
  }
  delay(300);
  uint8_t ok = 0;
  for (int i = 0; i < 5; ++i) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
      ++ok;
      if (ok == 1) {
        Serial.printf("[CAM] recover fb fmt=%d %ux%u len=%u\n",
                      static_cast<int>(fb->format), fb->width, fb->height,
                      static_cast<unsigned>(fb->len));
      }
      esp_camera_fb_return(fb);
    }
    delay(50);
  }
  Serial.printf("[CAM] recover probe ok=%u/5\n", ok);
  return ok > 0;
}

#endif

bool Camera::begin() {
#if USE_MOCK_CAMERA
  Serial.println("[CAM] mock mode ready");
  ready_ = true;
  return true;
#else
  Serial.println("[CAM] OV5640 init...");
  Serial.printf("[CAM] pins SDA=%d SCL=%d XCLK=%d D0-7=%d-%d VSYNC=%d HREF=%d PCLK=%d\n",
                pins::CAM_SIOD, pins::CAM_SIOC, pins::CAM_XCLK, pins::CAM_D0,
                pins::CAM_D7, pins::CAM_VSYNC, pins::CAM_HREF, pins::CAM_PCLK);
  releaseWireBus();
  delay(1500);
  logPreflightSccb();
  if (!waitForOv5640Bus(2500)) {
    Serial.println("[CAM] OV5640 not on SCCB yet, waiting...");
    delay(1000);
    waitForOv5640Bus(2000);
  }

  const camera_config_t config = buildCameraConfig();
  esp_err_t err = ESP_FAIL;
  for (int attempt = 1; attempt <= 6; ++attempt) {
    if (attempt > 1) {
      Serial.printf("[CAM] recovery before attempt %d...\n", attempt);
      recoverCameraHardware();
    }
    Serial.printf("[CAM] esp_camera_init attempt %d/6...\n", attempt);
    err = esp_camera_init(&config);
    if (err == ESP_OK) {
      break;
    }
    Serial.printf("[CAM] attempt %d failed: 0x%x\n", attempt, static_cast<unsigned>(err));
    delay(static_cast<uint32_t>(800 + attempt * 400));
    recoverCameraHardware();
  }
  if (err != ESP_OK) {
    Serial.printf("[CAM] init failed: 0x%x\n", static_cast<unsigned>(err));
    ready_ = false;
    return false;
  }
  Serial.printf("[CAM] sensor: %s (PID 0x%04x)\n", sensorName(),
                esp_camera_sensor_get() ? esp_camera_sensor_get()->id.PID : 0);
  applyOv5640StreamTuning();
#if USB_STREAM_ENABLE
  Serial.printf("[CAM] PSRAM %s stream boot=JPEG QVGA\n",
                psramFound() ? "OK" : "MISSING");
#endif
  delay(500);
  frameMutex_ = xSemaphoreCreateMutex();
  if (!frameMutex_) {
    Serial.println("[CAM] frame mutex failed");
    ready_ = false;
    return false;
  }
  ready_ = true;
  return true;
#endif
}

#if !USE_MOCK_CAMERA
void Camera::setColorbar(bool enable) { setOv5640Colorbar(enable); }
#endif

#if USB_STREAM_ENABLE && !USE_MOCK_CAMERA
bool Camera::reinitForUsbStream() {
  Serial.println("[CAM] reinit for USB RGB565 stream...");
  esp_camera_deinit();
  delay(400);
  recoverCameraHardware();
  const camera_config_t config = buildUsbStreamCameraConfig();
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] stream reinit failed: 0x%x\n", static_cast<unsigned>(err));
    ready_ = false;
    return false;
  }
  applyOv5640StreamTuning();
  delay(400);
  uint8_t ok = 0;
  for (int i = 0; i < 12; ++i) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
      ++ok;
      if (ok == 1 && SerialLock::tryLock()) {
        Serial.printf("[CAM] stream fb fmt=%d %ux%u len=%u\n",
                      static_cast<int>(fb->format), fb->width, fb->height,
                      static_cast<unsigned>(fb->len));
        SerialLock::unlock();
      }
      esp_camera_fb_return(fb);
    }
    delay(100);
  }
  Serial.printf("[CAM] stream probe ok=%u/12\n", ok);
  ready_ = ok > 0;
  return ready_;
}

bool Camera::recoverForStream() {
  ready_ = recoverJpegCamera();
  return ready_;
}
#endif

#if !USE_MOCK_CAMERA
camera_fb_t* Camera::acquireFramebuffer() {
  if (!ready_ || !frameMutex_) {
    return nullptr;
  }
  if (xSemaphoreTake(frameMutex_, portMAX_DELAY) != pdTRUE) {
    return nullptr;
  }
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    xSemaphoreGive(frameMutex_);
  }
  return fb;
}

void Camera::releaseFramebuffer(camera_fb_t* fb) {
  if (fb) {
    esp_camera_fb_return(fb);
  }
  if (frameMutex_) {
    xSemaphoreGive(frameMutex_);
  }
}
#endif

CaptureResult Camera::capture() {
#if USE_MOCK_CAMERA
  return captureMock();
#else
  return captureHardware();
#endif
}

CaptureResult Camera::captureMock() {
  CaptureResult r;
  r.ok = true;
  r.jpeg.assign(128, 0xFF);
  const char* fake = "MOCK_JPEG";
  r.jpeg.insert(r.jpeg.begin(), fake, fake + strlen(fake));
  r.bytes = r.jpeg.size();
  Serial.printf("[CAM] mock capture %u bytes\n", static_cast<unsigned>(r.bytes));
  return r;
}

#if !USE_MOCK_CAMERA
CaptureResult Camera::captureHardware() {
  CaptureResult r;
  if (!ready_) {
    r.error = "camera not ready";
    return r;
  }
  camera_fb_t* fb = acquireFramebuffer();
  if (!fb) {
    r.error = "esp_camera_fb_get failed";
    return r;
  }

  if (fb->format == PIXFORMAT_JPEG) {
    size_t jpeg_len = fb->len;
    for (size_t i = jpeg_len; i >= 2; --i) {
      if (fb->buf[i - 2] == 0xFF && fb->buf[i - 1] == 0xD9) {
        jpeg_len = i;
        break;
      }
    }
    if (jpeg_len < 512 || fb->buf[0] != 0xFF || fb->buf[1] != 0xD8) {
      releaseFramebuffer(fb);
      r.error = "JPEG invalid";
      return r;
    }
    r.jpeg.assign(fb->buf, fb->buf + jpeg_len);
    r.bytes = jpeg_len;
    r.ok = true;
    releaseFramebuffer(fb);
    Serial.printf("[CAM] capture OK %u bytes\n", static_cast<unsigned>(r.bytes));
    return r;
  }

  // USB stream mode may boot RGB565 — encode JPEG in software for upload.
  uint8_t* jpg = nullptr;
  size_t jpg_len = 0;
  const bool ok =
      frame2jpg(fb, CAPTURE_JPEG_QUALITY, &jpg, &jpg_len) && jpg && jpg_len >= 512;
  releaseFramebuffer(fb);
  if (!ok) {
    free(jpg);
    r.error = "frame2jpg failed";
    return r;
  }
  r.jpeg.assign(jpg, jpg + jpg_len);
  free(jpg);
  r.bytes = jpg_len;
  r.ok = true;
  Serial.printf("[CAM] capture OK %u bytes (sw jpeg)\n", static_cast<unsigned>(r.bytes));
  return r;
}
#endif
