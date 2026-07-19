#include "config.h"
#include "pins.h"
#include "modules/camera.h"
#include "serial_lock.h"

#include <cstring>
#include <vector>

#if !USE_MOCK_CAMERA
#include "esp_camera.h"
#include "img_converters.h"
#include "esp_heap_caps.h"
#include <Wire.h>
#if CAM_AUTO_FOCUS
#include "ESP32_OV5640_AF.h"
#endif
#if USB_STREAM_ENABLE
#include "esp32-hal-psram.h"
#endif

namespace {
uint32_t jpegByteDiversity(const uint8_t* p, size_t n);
}  // namespace
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
  // 20MHz 在本板易触发 cam_hal EV-EOF-OVF / 近黑帧；10MHz 更稳
  config.xclk_freq_hz = 10000000;
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
#if NET_CELL_ONLY
  // 4G HTTPS 体积受限：QVGA + 更高压缩
  config.frame_size = FRAMESIZE_QVGA;
#else
  config.frame_size = FRAMESIZE_VGA;
#endif
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

#if !USE_MOCK_CAMERA && USB_STREAM_ENABLE
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
  // 提亮：本模组默认硬件 JPEG 常接近全黑
  sensor->set_brightness(sensor, 3);
  sensor->set_contrast(sensor, 1);
  sensor->set_saturation(sensor, 0);
  sensor->set_whitebal(sensor, 1);
  sensor->set_awb_gain(sensor, 1);
  sensor->set_exposure_ctrl(sensor, 1);
  sensor->set_aec2(sensor, 1);
  sensor->set_gain_ctrl(sensor, 1);
  sensor->set_gainceiling(sensor, GAINCEILING_128X);
  if (sensor->set_aec_value) {
    sensor->set_aec_value(sensor, USB_STREAM_AEC_VALUE > 0 ? USB_STREAM_AEC_VALUE : 1800);
  }
  if (sensor->set_agc_gain) {
    sensor->set_agc_gain(sensor, 24);
  }
  if (sensor->set_ae_level) {
    sensor->set_ae_level(sensor, 2);
  }
  if (sensor->set_quality) {
    sensor->set_quality(sensor, USB_STREAM_JPEG_QUALITY);
  }
#endif
}

void applyOv5640CaptureTuning() {
  sensor_t* sensor = esp_camera_sensor_get();
  if (!sensor) {
    return;
  }
  // 扫题向：更清晰的文字边缘；增益适中减轻噪点「假糊」
  sensor->set_brightness(sensor, 1);
  sensor->set_contrast(sensor, 2);
  sensor->set_saturation(sensor, -2);
  sensor->set_whitebal(sensor, 1);
  sensor->set_awb_gain(sensor, 1);
  sensor->set_exposure_ctrl(sensor, 1);
  sensor->set_aec2(sensor, 1);
  sensor->set_gain_ctrl(sensor, 1);
  sensor->set_gainceiling(sensor, GAINCEILING_32X);
  if (sensor->set_sharpness) {
    sensor->set_sharpness(sensor, 3);
  }
  if (sensor->set_denoise) {
    sensor->set_denoise(sensor, 1);
  }
  if (sensor->set_aec_value) {
    sensor->set_aec_value(sensor, USB_STREAM_AEC_VALUE > 0 ? USB_STREAM_AEC_VALUE : 1400);
  }
  if (sensor->set_agc_gain) {
    sensor->set_agc_gain(sensor, 8);
  }
  if (sensor->set_ae_level) {
    sensor->set_ae_level(sensor, 0);
  }
  if (sensor->set_quality) {
    sensor->set_quality(sensor, CAPTURE_JPEG_QUALITY);
  }
}

#if CAM_AUTO_FOCUS
namespace {
Ov5640Af gAf;
bool gAfFwLoaded = false;
}  // namespace

bool ensureOv5640Autofocus() {
  sensor_t* sensor = esp_camera_sensor_get();
  if (!sensor) {
    return false;
  }
  if (!gAf.start(sensor)) {
    Serial.println("[CAM] AF: not OV5640");
    return false;
  }
  if (gAfFwLoaded) {
    return true;
  }
  Serial.println("[CAM] AF: loading firmware...");
  const uint8_t rc = gAf.focusInit();
  if (rc != 0) {
    Serial.printf("[CAM] AF: focusInit rc=%u (check AF-VCC=3V3)\n", rc);
    gAfFwLoaded = false;
    return false;
  }
  gAfFwLoaded = true;
  Serial.println("[CAM] AF: firmware ready");
  return true;
}

bool triggerOv5640Focus() {
  if (!ensureOv5640Autofocus()) {
    return false;
  }
  Serial.println("[CAM] AF: single focus...");
  const uint8_t rc = gAf.singleAutoFocus(2200);
  const uint8_t st = gAf.getFWStatus();
  Serial.printf("[CAM] AF: done rc=%u status=0x%02X\n", rc, st);
  return rc == 0 || st == FW_STATUS_S_FOCUSED || st == FW_STATUS_S_IDLE;
}
#else
bool ensureOv5640Autofocus() { return false; }
bool triggerOv5640Focus() { return false; }
#endif

void setOv5640Flash(bool on) {
  sensor_t* sensor = esp_camera_sensor_get();
  if (!sensor || !sensor->set_reg) {
    return;
  }
  // 微雪 C：镜头两侧 LED 由传感器 STROBE 脚经 MOSFET 驱动
  (void)sensor->set_reg(sensor, 0x3004, 0x08, 0x08);  // STROBE clock
  (void)sensor->set_reg(sensor, 0x3016, 0x02, 0x02);  // STROBE pad OE
  if (on) {
    // LED3 持续点亮，直到清除 request 位
    (void)sensor->set_reg(sensor, 0x3B00, 0xFF, 0x83);
  } else {
    (void)sensor->set_reg(sensor, 0x3B00, 0xFF, 0x03);
  }
  Serial.printf("[CAM] flash %s\n", on ? "ON" : "OFF");
}

namespace {

bool sceneNeedsFlash() {
#if !CAM_AUTO_FLASH
  return false;
#else
  sensor_t* sensor = esp_camera_sensor_get();
  if (!sensor || !sensor->get_reg) {
    return false;
  }
  // AEC 开启时平均亮度常被拉到目标值，需结合增益判断「环境是否偏暗」
  const int avg = sensor->get_reg(sensor, 0x56A1, 0xFF);
  const int gain = sensor->get_reg(sensor, 0x350B, 0xFF);
  const bool byAvg = (avg >= 0 && avg < CAM_FLASH_AVG_THRESHOLD);
  const bool byGain = (gain >= CAM_FLASH_GAIN_THRESHOLD);
  const bool need = byAvg || byGain;
  Serial.printf("[CAM] light meter avg=%d gain=0x%02X need_flash=%d\n", avg,
                gain >= 0 ? gain : 0, need ? 1 : 0);
  return need;
#endif
}

bool beginCaptureMode(framesize_t target, framesize_t* prevOut) {
  sensor_t* sensor = esp_camera_sensor_get();
  if (!sensor || !sensor->set_framesize) {
    applyOv5640CaptureTuning();
    return false;
  }
  const framesize_t prev = static_cast<framesize_t>(sensor->status.framesize);
  if (prevOut) {
    *prevOut = prev;
  }
  applyOv5640CaptureTuning();
  if (prev == target) {
    return false;
  }
  if (sensor->set_framesize(sensor, target) != 0) {
    Serial.printf("[CAM] framesize %d failed — keep %d\n",
                  static_cast<int>(target), static_cast<int>(prev));
    return false;
  }
  Serial.printf("[CAM] capture mode → framesize=%d\n", static_cast<int>(target));
  delay(300);
  return true;
}

void endCaptureMode(bool switched, framesize_t prev) {
  sensor_t* sensor = esp_camera_sensor_get();
  if (switched && sensor && sensor->set_framesize) {
    sensor->set_framesize(sensor, prev);
    Serial.printf("[CAM] restore framesize=%d\n", static_cast<int>(prev));
  }
  applyOv5640StreamTuning();
}

}  // namespace

bool recoverJpegCamera() {
  Serial.println("[CAM] recover JPEG QVGA...");
#if CAM_AUTO_FOCUS
  gAfFwLoaded = false;
#endif
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
  applyOv5640StreamTuning();
  // 只试 1 帧：fb_get 无超时，多轮探测在 DMA 异常时会整机卡死
  delay(400);
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb) {
    Serial.printf("[CAM] recover fb fmt=%d %ux%u len=%u\n",
                  static_cast<int>(fb->format), fb->width, fb->height,
                  static_cast<unsigned>(fb->len));
    esp_camera_fb_return(fb);
  } else {
    Serial.println("[CAM] recover init OK but first fb null — continue");
  }
#if CAM_AUTO_FOCUS
  (void)ensureOv5640Autofocus();
#endif
  return true;
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
  if (!frameMutex_) {
    frameMutex_ = xSemaphoreCreateMutex();
  }
  if (!frameMutex_) {
    Serial.println("[CAM] frame mutex failed");
    ready_ = false;
    return false;
  }
  ready_ = true;
  sleeping_ = false;
  afReady_ = ensureOv5640Autofocus();
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
camera_fb_t* Camera::acquireFramebuffer(TickType_t timeout) {
  if (!ready_ || !frameMutex_) {
    return nullptr;
  }
  if (xSemaphoreTake(frameMutex_, timeout) != pdTRUE) {
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

void Camera::flushFrames(int count, TickType_t timeout) {
  for (int i = 0; i < count; ++i) {
    camera_fb_t* drop = acquireFramebuffer(timeout);
    if (drop) {
      releaseFramebuffer(drop);
    }
    delay(20);
  }
}

camera_fb_t* Camera::grabFrameWithRetry(int attempts) {
  // 少冲帧：每次 flush 也会走 fb_get，DMA 异常时会放大卡住概率
  flushFrames(1, pdMS_TO_TICKS(120));
  for (int attempt = 0; attempt < attempts; ++attempt) {
    camera_fb_t* fb = acquireFramebuffer(pdMS_TO_TICKS(600));
    if (fb) {
      return fb;
    }
    if (!ready_) {
      return nullptr;
    }
    delay(static_cast<uint32_t>(40 + attempt * 40));
  }
  return nullptr;
}

// 连抓多帧，选 JPEG 熵最高的一帧（抖动/未稳时更清晰）
camera_fb_t* Camera::grabSharpestFrame(int candidates) {
  if (candidates < 1) {
    candidates = 1;
  }
  camera_fb_t* best = nullptr;
  uint32_t bestScore = 0;
  flushFrames(1, pdMS_TO_TICKS(80));
  for (int i = 0; i < candidates; ++i) {
    camera_fb_t* fb = acquireFramebuffer(pdMS_TO_TICKS(700));
    if (!fb) {
      if (!ready_) {
        break;
      }
      delay(40);
      continue;
    }
    uint32_t score = 0;
    if (fb->format == PIXFORMAT_JPEG && fb->len >= 800) {
      // 熵 + 体积：细节更丰富的帧通常两者都更高
      score = jpegByteDiversity(fb->buf, fb->len) * 10u +
              static_cast<uint32_t>(fb->len / 64);
    } else {
      score = static_cast<uint32_t>(fb->len);
    }
    if (!best || score > bestScore) {
      if (best) {
        releaseFramebuffer(best);
      }
      best = fb;
      bestScore = score;
    } else {
      releaseFramebuffer(fb);
    }
    delay(30);
  }
  if (best) {
    Serial.printf("[CAM] sharpest score=%u len=%u %ux%u\n", bestScore,
                  static_cast<unsigned>(best->len), best->width, best->height);
  }
  return best;
}
#endif

CaptureResult Camera::capture() {
#if USE_MOCK_CAMERA
  return captureMock();
#else
  return captureHardware();
#endif
}

CaptureResult Camera::captureThumbnail() {
#if USE_MOCK_CAMERA
  return captureMock();
#else
  CaptureResult r;
  if (!ready_) {
    r.error = "camera not ready";
    return r;
  }
  framesize_t prev = FRAMESIZE_QVGA;
  sensor_t* sensor = esp_camera_sensor_get();
  if (sensor && sensor->status.framesize) {
    prev = static_cast<framesize_t>(sensor->status.framesize);
  }
  if (sensor && sensor->set_framesize) {
    (void)sensor->set_framesize(sensor, FRAMESIZE_QQVGA);
  }
  if (sensor && sensor->set_quality) {
    sensor->set_quality(sensor, 28);
  }
  delay(200);
  flushFrames(1, pdMS_TO_TICKS(80));
  camera_fb_t* fb = grabFrameWithRetry(3);
  if (!fb) {
    if (sensor && sensor->set_framesize) {
      (void)sensor->set_framesize(sensor, prev);
    }
    r.error = "thumb fb_get failed";
    return r;
  }
  if (fb->format == PIXFORMAT_JPEG && fb->len >= 400) {
    r.jpeg.assign(fb->buf, fb->buf + fb->len);
    r.ok = true;
    r.bytes = r.jpeg.size();
  } else {
    r.error = "thumb not jpeg";
  }
  releaseFramebuffer(fb);
  if (sensor && sensor->set_framesize) {
    (void)sensor->set_framesize(sensor, prev);
  }
  applyOv5640StreamTuning();
  Serial.printf("[CAM] thumb %s %u bytes\n", r.ok ? "OK" : "FAIL",
                static_cast<unsigned>(r.bytes));
  return r;
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
namespace {

// 本模组硬件 JPEG 经常损坏（~1.5KB、能开个头但读像素失败），解题前重编码。
size_t trimJpegInPlace(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_cap) {
  size_t soi = SIZE_MAX;
  for (size_t i = 0; i + 1 < src_len; ++i) {
    if (src[i] == 0xFF && src[i + 1] == 0xD8) {
      soi = i;
      break;
    }
  }
  if (soi == SIZE_MAX) {
    return 0;
  }
  size_t eoi = 0;
  for (size_t i = src_len; i >= soi + 2; --i) {
    if (src[i - 2] == 0xFF && src[i - 1] == 0xD9) {
      eoi = i;
      break;
    }
  }
  const size_t n = (eoi > soi + 128) ? (eoi - soi) : (src_len - soi);
  if (n < 128 || n > dst_cap) {
    return 0;
  }
  memcpy(dst, src + soi, n);
  return n;
}

bool encodeRgbJpeg(uint8_t* rgb, size_t w, size_t h, int quality,
                   std::vector<uint8_t>& out) {
  if (!rgb || w < 8 || h < 8) {
    return false;
  }
  uint8_t* jpg = nullptr;
  size_t jpg_len = 0;
  const bool ok =
      fmt2jpg(rgb, w * h * 3, w, h, PIXFORMAT_RGB888, quality, &jpg, &jpg_len) &&
      jpg && jpg_len >= 512;
  if (!ok) {
    free(jpg);
    return false;
  }
  out.assign(jpg, jpg + jpg_len);
  free(jpg);
  return true;
}

// 2x2 平均下采样，进一步缩小体积
uint8_t* downsampleRgb2x(const uint8_t* rgb, size_t w, size_t h, size_t* ow,
                         size_t* oh) {
  const size_t nw = w / 2;
  const size_t nh = h / 2;
  if (nw < 8 || nh < 8) {
    return nullptr;
  }
  const size_t nlen = nw * nh * 3;
  uint8_t* out = static_cast<uint8_t*>(
      heap_caps_malloc(nlen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!out) {
    out = static_cast<uint8_t*>(malloc(nlen));
  }
  if (!out) {
    return nullptr;
  }
  for (size_t y = 0; y < nh; ++y) {
    for (size_t x = 0; x < nw; ++x) {
      const size_t x0 = x * 2;
      const size_t y0 = y * 2;
      for (int c = 0; c < 3; ++c) {
        const int v =
            rgb[((y0)*w + x0) * 3 + c] + rgb[((y0)*w + x0 + 1) * 3 + c] +
            rgb[((y0 + 1) * w + x0) * 3 + c] +
            rgb[((y0 + 1) * w + x0 + 1) * 3 + c];
        out[(y * nw + x) * 3 + c] = static_cast<uint8_t>(v / 4);
      }
    }
  }
  *ow = nw;
  *oh = nh;
  return out;
}

bool compressRgbToLimit(uint8_t* rgb, size_t w, size_t h,
                        std::vector<uint8_t>& out) {
  // jpge：数值越大压缩越狠。优先保分辨率，尽量别半分辨率（伤 OCR）
  const int quals[] = {CAPTURE_JPEG_QUALITY, 16, 20, 26, 32, 40, 48, 56};
  for (int q : quals) {
    if (!encodeRgbJpeg(rgb, w, h, q, out)) {
      continue;
    }
    Serial.printf("[CAM] jpeg try q=%d → %u bytes (%ux%u)\n", q,
                  static_cast<unsigned>(out.size()),
                  static_cast<unsigned>(w), static_cast<unsigned>(h));
    if (out.size() <= CELL_AI_MAX_JPEG) {
      return true;
    }
  }

  // 仍超限：半分辨率再压一轮（最后手段）
  size_t nw = 0;
  size_t nh = 0;
  uint8_t* small = downsampleRgb2x(rgb, w, h, &nw, &nh);
  if (!small) {
    return out.size() > 0 && out.size() <= CELL_AI_MAX_JPEG;
  }
  bool ok = false;
  for (int q : quals) {
    if (!encodeRgbJpeg(small, nw, nh, q, out)) {
      continue;
    }
    Serial.printf("[CAM] jpeg half q=%d → %u bytes\n", q,
                  static_cast<unsigned>(out.size()));
    if (out.size() <= CELL_AI_MAX_JPEG) {
      ok = true;
      break;
    }
  }
  free(small);
  return ok;
}

bool decodeJpegToRgb(const uint8_t* buf, size_t len, uint16_t width,
                     uint16_t height, uint8_t** rgbOut) {
  *rgbOut = nullptr;
  const size_t w = width;
  const size_t h = height;
  const size_t rgb_len = w * h * 3;
  uint8_t* rgb = static_cast<uint8_t*>(
      heap_caps_malloc(rgb_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!rgb) {
    rgb = static_cast<uint8_t*>(malloc(rgb_len));
  }
  if (!rgb) {
    return false;
  }

  uint8_t* work = static_cast<uint8_t*>(
      heap_caps_malloc(len + 64, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!work) {
    work = static_cast<uint8_t*>(malloc(len + 64));
  }
  if (!work) {
    free(rgb);
    return false;
  }
  size_t work_len = trimJpegInPlace(buf, len, work, len + 64);
  if (work_len == 0) {
    free(work);
    free(rgb);
    return false;
  }

  const bool decoded = fmt2rgb888(work, work_len, PIXFORMAT_JPEG, rgb);
  free(work);
  if (!decoded) {
    free(rgb);
    return false;
  }
  *rgbOut = rgb;
  return true;
}

bool reencodeJpegBuffer(const uint8_t* buf, size_t len, uint16_t width,
                        uint16_t height, pixformat_t format,
                        std::vector<uint8_t>& out) {
  if (!buf || len < 128 || width < 8 || height < 8) {
    return false;
  }
  if (format != PIXFORMAT_JPEG) {
    camera_fb_t tmp = {};
    tmp.buf = const_cast<uint8_t*>(buf);
    tmp.len = len;
    tmp.width = width;
    tmp.height = height;
    tmp.format = format;
    uint8_t* jpg = nullptr;
    size_t jpg_len = 0;
    // 非 JPEG 帧：先压一版再视大小决定是否需 RGB 阶梯（少见路径）
    const bool ok =
        frame2jpg(&tmp, CAPTURE_JPEG_QUALITY, &jpg, &jpg_len) && jpg &&
        jpg_len >= 512;
    if (!ok) {
      free(jpg);
      return false;
    }
    if (jpg_len <= CELL_AI_MAX_JPEG) {
      out.assign(jpg, jpg + jpg_len);
      free(jpg);
      return true;
    }
    free(jpg);
    // 超限则走 RGB 阶梯：先转到 JPEG 再 decode（简化：直接失败让上层用其它路径）
    return false;
  }

  uint8_t* rgb = nullptr;
  if (!decodeJpegToRgb(buf, len, width, height, &rgb) || !rgb) {
    return false;
  }
  const bool ok = compressRgbToLimit(rgb, width, height, out);
  free(rgb);
  return ok;
}

bool sampleJpegLuminance(const uint8_t* jpeg, size_t len, uint16_t w, uint16_t h,
                         uint8_t* min_l, uint8_t* max_l, uint8_t* avg_l) {
  *min_l = 0;
  *max_l = 0;
  *avg_l = 0;
  uint8_t* rgb = nullptr;
  if (!decodeJpegToRgb(jpeg, len, w, h, &rgb) || !rgb) {
    return false;
  }
  const size_t pixels = static_cast<size_t>(w) * h;
  uint8_t mn = 255;
  uint8_t mx = 0;
  uint32_t sum = 0;
  uint32_t count = 0;
  const size_t step = pixels > 4000 ? pixels / 4000 : 1;
  for (size_t i = 0; i < pixels; i += step) {
    const uint8_t r = rgb[i * 3];
    const uint8_t g = rgb[i * 3 + 1];
    const uint8_t b = rgb[i * 3 + 2];
    const uint8_t y =
        static_cast<uint8_t>((static_cast<uint16_t>(r) * 30 +
                              static_cast<uint16_t>(g) * 59 +
                              static_cast<uint16_t>(b) * 11) /
                             100);
    if (y < mn) {
      mn = y;
    }
    if (y > mx) {
      mx = y;
    }
    sum += y;
    ++count;
  }
  free(rgb);
  if (!count) {
    return false;
  }
  *min_l = mn;
  *max_l = mx;
  *avg_l = static_cast<uint8_t>(sum / count);
  return true;
}

uint32_t jpegByteDiversity(const uint8_t* p, size_t n) {
  if (!p || n < 64) {
    return 0;
  }
  bool seen[256] = {};
  uint32_t uniq = 0;
  const size_t start = n > 200 ? 64 : 0;
  const size_t end = n > 8 ? n - 2 : n;
  for (size_t i = start; i < end; ++i) {
    if (!seen[p[i]]) {
      seen[p[i]] = true;
      ++uniq;
    }
  }
  return uniq;
}

void runBlackDiagImpl(Camera& cam) {
  Serial.println("[CAMDIAG] start (live vs colorbar)");
  if (cam.isSleeping() || !cam.isReady()) {
    Serial.println("[CAMDIAG] waking camera...");
    if (!cam.wake()) {
      Serial.println(
          "{\"type\":\"camdiag\",\"ok\":false,\"reason\":\"wake failed\"}");
      return;
    }
  }

  applyOv5640StreamTuning();
  delay(300);

  uint32_t liveLen = 0, barLen = 0, liveDiv = 0, barDiv = 0;
  uint8_t liveMin = 0, liveMax = 0, liveAvg = 0;
  uint8_t barMin = 0, barMax = 0, barAvg = 0;
  bool liveDec = false, barDec = false;
  bool liveGot = false, barGot = false;

  // 实景：走已验证的 capture()，避免额外 fb 轮询卡死
  {
    setOv5640Colorbar(false);
    applyOv5640StreamTuning();
    delay(400);
    const CaptureResult cap = cam.capture();
    liveGot = cap.ok && !cap.jpeg.empty();
    if (liveGot) {
      liveLen = static_cast<uint32_t>(cap.jpeg.size());
      liveDiv = jpegByteDiversity(cap.jpeg.data(), cap.jpeg.size());
      // 仅小图尝试解码测亮度；大图/坏图跳过以免卡死
      if (liveLen >= 800 && liveLen <= 10000) {
        liveDec = sampleJpegLuminance(cap.jpeg.data(), cap.jpeg.size(), 320, 240,
                                      &liveMin, &liveMax, &liveAvg);
      }
    }
    Serial.printf(
        "[CAMDIAG] LIVE ok=%d len=%u div=%u decode=%s lum min=%u max=%u avg=%u "
        "err=%s\n",
        liveGot ? 1 : 0, liveLen, liveDiv, liveDec ? "ok" : "skip/fail",
        liveMin, liveMax, liveAvg, cap.error ? cap.error : "-");
  }

  {
    setOv5640Colorbar(true);
    delay(500);
    const CaptureResult cap = cam.capture();
    barGot = cap.ok && !cap.jpeg.empty();
    if (barGot) {
      barLen = static_cast<uint32_t>(cap.jpeg.size());
      barDiv = jpegByteDiversity(cap.jpeg.data(), cap.jpeg.size());
      if (barLen >= 800 && barLen <= 10000) {
        barDec = sampleJpegLuminance(cap.jpeg.data(), cap.jpeg.size(), 320, 240,
                                     &barMin, &barMax, &barAvg);
      }
    }
    Serial.printf(
        "[CAMDIAG] COLORBAR ok=%d len=%u div=%u decode=%s lum min=%u max=%u "
        "avg=%u err=%s\n",
        barGot ? 1 : 0, barLen, barDiv, barDec ? "ok" : "skip/fail", barMin,
        barMax, barAvg, cap.error ? cap.error : "-");
  }

  setOv5640Colorbar(false);
  applyOv5640StreamTuning();

  const char* verdict = "unknown";
  if (!liveGot && !barGot) {
    verdict = "no_frames — DVP overflow/wiring; sensor may still ID on SCCB";
  } else if (barDec && barAvg >= 40 && liveDec && liveAvg < 8) {
    verdict = "sensor_ok_exposure_dark — not dead; fix AEC/light/lens";
  } else if (barDec && barAvg >= 40 && liveDec && liveAvg < 25) {
    verdict = "sensor_ok_very_dark — raise gain/exposure";
  } else if (barGot && barDiv >= 80 && liveGot && liveDiv < 40 &&
             liveLen < 3500) {
    verdict =
        "sensor_ok_live_near_black — colorbar diverse, live tiny/low-entropy";
  } else if (liveGot && barGot && liveDiv < 35 && barDiv < 35 &&
             liveLen < 4000 && barLen < 4000) {
    verdict = "likely_hardware — live+colorbar both tiny/low-entropy";
  } else if (liveDec && liveAvg >= 25) {
    verdict = "live_ok — image has light; check preview path";
  } else if (barDec && barAvg >= 40) {
    verdict = "sensor_ok_colorbar_only";
  } else if (liveGot || barGot) {
    verdict = "frames_ok_decode_weak — sensor responds; hw jpeg often dark";
  }

  Serial.printf(
      "{\"type\":\"camdiag\",\"live_len\":%u,\"live_div\":%u,\"live_avg\":%u,"
      "\"bar_len\":%u,\"bar_div\":%u,\"bar_avg\":%u,\"verdict\":\"%s\"}\n",
      liveLen, liveDiv, liveAvg, barLen, barDiv, barAvg, verdict);
  Serial.printf("[CAMDIAG] verdict: %s\n", verdict);
}

}  // namespace

void Camera::runBlackDiag() { runBlackDiagImpl(*this); }

bool Camera::probeFrame() {
#if USE_MOCK_CAMERA
  return ready_;
#else
  if (!ready_) {
    return false;
  }
  applyOv5640CaptureTuning();
  flushFrames(1, pdMS_TO_TICKS(80));
  camera_fb_t* fb = grabFrameWithRetry(3);
  if (!fb) {
    applyOv5640StreamTuning();
    return false;
  }
  const bool ok = fb->len >= 500 && fb->width >= 160;
  Serial.printf("[CAM] probe %s fmt=%d %ux%u len=%u\n", ok ? "OK" : "FAIL",
                static_cast<int>(fb->format), fb->width, fb->height,
                static_cast<unsigned>(fb->len));
  releaseFramebuffer(fb);
  applyOv5640StreamTuning();
  return ok;
#endif
}

// 智谱拒 OV5640 硬件 JPEG → 临时切 YUV/RGB 再 frame2jpg 生成标准基线 JPEG
bool captureCloudSafeJpeg(std::vector<uint8_t>& out) {
  out.clear();
  Serial.println("[CAM] cloud-safe: YUV422/RGB565 → JPEG");
#if CAM_AUTO_FOCUS
  gAfFwLoaded = false;
#endif
  esp_camera_deinit();
  delay(180);
  Wire.end();
  delay(80);

  auto tryFormat = [&](pixformat_t pf, const char* tag) -> bool {
    camera_config_t cfg = buildCameraConfig();
    cfg.frame_size = FRAMESIZE_QVGA;
    cfg.pixel_format = pf;
    cfg.fb_count = (pf == PIXFORMAT_JPEG) ? 2 : 1;
    cfg.grab_mode = CAMERA_GRAB_LATEST;
    cfg.fb_location = CAMERA_FB_IN_PSRAM;
    const esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
      Serial.printf("[CAM] cloud-safe %s init fail 0x%x\n", tag,
                    static_cast<unsigned>(err));
      esp_camera_deinit();
      delay(100);
      return false;
    }
    applyOv5640CaptureTuning();
    delay(350);
    for (int i = 0; i < 2; ++i) {
      camera_fb_t* warm = esp_camera_fb_get();
      if (warm) {
        esp_camera_fb_return(warm);
      }
    }
#if CAM_AUTO_FOCUS
    (void)triggerOv5640Focus();
    delay(200);
#endif
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.printf("[CAM] cloud-safe %s fb_get fail\n", tag);
      esp_camera_deinit();
      delay(100);
      return false;
    }
    uint8_t* jpg = nullptr;
    size_t jpg_len = 0;
    const bool ok =
        frame2jpg(fb, 12, &jpg, &jpg_len) && jpg && jpg_len >= 800 &&
        jpg_len <= CELL_AI_MAX_JPEG;
    Serial.printf("[CAM] cloud-safe %s frame %ux%u fmt=%d → jpg %u ok=%d\n",
                  tag, fb->width, fb->height, static_cast<int>(fb->format),
                  static_cast<unsigned>(jpg_len), ok ? 1 : 0);
    esp_camera_fb_return(fb);
    esp_camera_deinit();
    delay(120);
    if (!ok) {
      free(jpg);
      return false;
    }
    out.assign(jpg, jpg + jpg_len);
    free(jpg);
    return true;
  };

  bool ok = tryFormat(PIXFORMAT_YUV422, "YUV422");
  if (!ok) {
    ok = tryFormat(PIXFORMAT_RGB565, "RGB565");
  }
  // 无论成败都恢复日常 JPEG 模式
  if (!recoverJpegCamera()) {
    Serial.println("[CAM] cloud-safe: recover JPEG failed");
  }
  return ok;
}

CaptureResult Camera::captureHardware() {
  CaptureResult r;
  if (!ready_) {
    r.error = "camera not ready";
    return r;
  }

#if NET_CELL_ONLY
  // 4G+智谱：优先云端可解析的标准 JPEG
  if (captureCloudSafeJpeg(r.jpeg)) {
    r.ok = true;
    r.bytes = r.jpeg.size();
    Serial.printf("[CAM] capture OK %u bytes (cloud-safe)\n",
                  static_cast<unsigned>(r.bytes));
    return r;
  }
  Serial.println("[CAM] cloud-safe failed — fallback hw jpeg path");
  ready_ = recoverJpegCamera() || ready_;
#endif

  // 优先 HVGA 提升 OCR；失败再 QVGA。避免 VGA（本板易卡死）
  framesize_t prevSize = FRAMESIZE_QVGA;
#if CAPTURE_USE_HVGA
  framesize_t captureSize = FRAMESIZE_HVGA;
#else
  framesize_t captureSize = FRAMESIZE_QVGA;
#endif
  bool switched = beginCaptureMode(captureSize, &prevSize);
  if (captureSize != FRAMESIZE_QVGA) {
    // beginCaptureMode 失败时仍保持原尺寸；确认是否真的切到了目标
    sensor_t* s = esp_camera_sensor_get();
    if (!s || s->status.framesize != captureSize) {
      Serial.println("[CAM] HVGA unavailable — fallback QVGA");
      captureSize = FRAMESIZE_QVGA;
      switched = beginCaptureMode(FRAMESIZE_QVGA, &prevSize);
    }
  }
  applyOv5640CaptureTuning();
  flushFrames(2, pdMS_TO_TICKS(120));

  bool flashOn = false;
  auto ensureFlashOff = [&]() {
    if (flashOn) {
      setOv5640Flash(false);
      flashOn = false;
    }
  };

#if CAM_AUTO_FLASH
  if (sceneNeedsFlash()) {
    setOv5640Flash(true);
    flashOn = true;
    // 补光后降增益，减少噪点糊
    sensor_t* s = esp_camera_sensor_get();
    if (s && s->set_agc_gain) {
      s->set_agc_gain(s, 4);
    }
    if (s && s->set_gainceiling) {
      s->set_gainceiling(s, GAINCEILING_16X);
    }
    delay(400);
    flushFrames(2, pdMS_TO_TICKS(120));
  }
#endif

#if CAM_AUTO_FOCUS
  afReady_ = triggerOv5640Focus();
  flushFrames(1, pdMS_TO_TICKS(100));
#endif

  camera_fb_t* fb = grabSharpestFrame(3);
  if (!fb) {
    Serial.println("[CAM] fb_get failed — recover + retry");
    ensureFlashOff();
    endCaptureMode(switched, prevSize);
    ready_ = recoverJpegCamera();
    afReady_ = false;
    if (!ready_) {
      r.error = "esp_camera_fb_get failed";
      return r;
    }
    applyOv5640CaptureTuning();
    switched = beginCaptureMode(FRAMESIZE_QVGA, &prevSize);
    captureSize = FRAMESIZE_QVGA;
#if CAM_AUTO_FLASH
    if (sceneNeedsFlash()) {
      setOv5640Flash(true);
      flashOn = true;
      delay(350);
    }
#endif
#if CAM_AUTO_FOCUS
    (void)triggerOv5640Focus();
#endif
    flushFrames(2, pdMS_TO_TICKS(100));
    fb = grabSharpestFrame(2);
  }
  if (!fb) {
    ensureFlashOff();
    r.error = "esp_camera_fb_get failed";
    endCaptureMode(switched, prevSize);
    return r;
  }

#if CAM_AUTO_FLASH
  // 测光未触发但成片仍偏暗：开灯+对焦后重拍
  if (!flashOn && fb->format == PIXFORMAT_JPEG &&
      (fb->len < 2800 || jpegByteDiversity(fb->buf, fb->len) < 38)) {
    Serial.println("[CAM] frame still dark — flash ON and retake");
    releaseFramebuffer(fb);
    setOv5640Flash(true);
    flashOn = true;
    delay(400);
#if CAM_AUTO_FOCUS
    (void)triggerOv5640Focus();
#endif
    flushFrames(2, pdMS_TO_TICKS(100));
    fb = grabSharpestFrame(2);
    if (!fb) {
      ensureFlashOff();
      r.error = "esp_camera_fb_get failed";
      endCaptureMode(switched, prevSize);
      return r;
    }
  }
#endif

  const pixformat_t fmt = fb->format;
  const uint16_t fw = fb->width;
  const uint16_t fh = fb->height;
  std::vector<uint8_t> raw(fb->buf, fb->buf + fb->len);
  releaseFramebuffer(fb);

  auto finishOk = [&](const char* tag) {
    r.ok = true;
    r.bytes = r.jpeg.size();
    flushFrames(1, pdMS_TO_TICKS(40));
    Serial.printf("[CAM] capture OK %u bytes (%s %ux%u flash=%d af=%d)\n",
                  static_cast<unsigned>(r.bytes), tag, fw, fh, flashOn ? 1 : 0,
                  afReady_ ? 1 : 0);
    ensureFlashOff();
    endCaptureMode(switched, prevSize);
  };

  if (fmt == PIXFORMAT_JPEG) {
    size_t jpeg_len = 0;
    bool foundEoi = false;
    for (size_t i = raw.size(); i >= 2; --i) {
      if (raw[i - 2] == 0xFF && raw[i - 1] == 0xD9) {
        jpeg_len = i;
        foundEoi = true;
        break;
      }
    }
    const uint32_t div =
        foundEoi ? jpegByteDiversity(raw.data(), jpeg_len) : 0;

    // 智谱常拒 OV5640 硬件 JPEG（1210）。QVGA 软重编码稳；HVGA 软解易卡，跳过。
    if (fw <= 320 && fh <= 240 && foundEoi && jpeg_len >= 800 &&
        raw[0] == 0xFF && raw[1] == 0xD8) {
      if (reencodeJpegBuffer(raw.data(), jpeg_len, fw, fh, fmt, r.jpeg)) {
        finishOk("baseline jpeg");
        return r;
      }
      Serial.println("[CAM] QVGA baseline reencode failed — try raw");
    }

    if (foundEoi && jpeg_len >= 800 && raw[0] == 0xFF && raw[1] == 0xD8 &&
        jpeg_len <= CELL_AI_MAX_JPEG && div >= 40) {
      r.jpeg.assign(raw.begin(),
                    raw.begin() + static_cast<std::ptrdiff_t>(jpeg_len));
      finishOk("raw hw jpeg");
      return r;
    }

    if (fw <= 320 && fh <= 240 &&
        reencodeJpegBuffer(raw.data(), raw.size(), fw, fh, fmt, r.jpeg)) {
      finishOk("re-jpeg");
      return r;
    }

    Serial.printf("[CAM] jpeg reject raw=%u %ux%u eoi=%d div=%u\n",
                  static_cast<unsigned>(raw.size()), fw, fh, foundEoi ? 1 : 0,
                  div);
    ensureFlashOff();
    endCaptureMode(switched, prevSize);
    r.error = "retake closer";
    return r;
  }

  if (reencodeJpegBuffer(raw.data(), raw.size(), fw, fh, fmt, r.jpeg)) {
    finishOk("sw jpeg");
    return r;
  }

  ensureFlashOff();
  endCaptureMode(switched, prevSize);
  r.error = "retake closer";
  return r;
}

#else  // USE_MOCK_CAMERA

bool Camera::probeFrame() { return ready_; }

void Camera::runBlackDiag() {
  Serial.println("[CAMDIAG] mock camera — skip");
}

#endif  // !USE_MOCK_CAMERA

void Camera::sleep() {
#if USE_MOCK_CAMERA
  sleeping_ = true;
  ready_ = false;
#else
  if (!ready_ && sleeping_) {
    return;
  }
  if (ready_) {
    setOv5640Flash(false);
    esp_camera_deinit();
    Serial.println("[CAM] sleep (deinit)");
  }
  ready_ = false;
  sleeping_ = true;
  afReady_ = false;
#if CAM_AUTO_FOCUS
  gAfFwLoaded = false;
#endif
#endif
}

bool Camera::wake() {
#if USE_MOCK_CAMERA
  ready_ = true;
  sleeping_ = false;
  return true;
#else
  streamingPaused_ = false;
  if (ready_) {
    sleeping_ = false;
    return true;
  }
  Serial.println("[CAM] wake...");
  const bool ok = recoverJpegCamera();
  ready_ = ok;
  sleeping_ = !ok;
  if (ok) {
    applyOv5640StreamTuning();
    afReady_ = ensureOv5640Autofocus();
    Serial.println("[CAM] wake OK");
  } else {
    Serial.println("[CAM] wake failed — full begin()");
    sleeping_ = false;
    return begin();
  }
  return ok;
#endif
}
