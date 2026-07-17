#include "config.h"
#include "modules/usb_stream.h"
#include "serial_lock.h"

#if USB_STREAM_ENABLE && !USE_MOCK_CAMERA

#include "esp_camera.h"
#include "img_converters.h"
#include "esp_heap_caps.h"

namespace {

constexpr uint8_t kMagicJpeg[] = {0x53, 0x43, 0x01, 0xFE};
constexpr uint8_t kMagicRgb565[] = {0x53, 0x43, 0x01, 0xFC};

bool writeAll(const uint8_t* data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    const size_t n = Serial.write(data + sent, len - sent);
    if (n == 0) {
      delay(1);
      continue;
    }
    sent += n;
  }
  return sent == len;
}

bool writeU32LE(uint32_t value) {
  const uint8_t bytes[4] = {
      static_cast<uint8_t>(value & 0xFF),
      static_cast<uint8_t>((value >> 8) & 0xFF),
      static_cast<uint8_t>((value >> 16) & 0xFF),
      static_cast<uint8_t>((value >> 24) & 0xFF),
  };
  return writeAll(bytes, sizeof(bytes));
}

uint8_t* allocPacketBuffer(uint32_t total_len) {
  uint8_t* buf = static_cast<uint8_t*>(
      heap_caps_malloc(total_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) {
    buf = static_cast<uint8_t*>(malloc(total_len));
  }
  return buf;
}

bool packRawRgb565(uint16_t width, uint16_t height, const uint8_t* pixels,
                   size_t pixel_bytes, uint8_t** out, uint32_t* out_len) {
  if (!pixels || width < 2 || height < 2 ||
      pixel_bytes < static_cast<size_t>(width) * height * 2) {
    return false;
  }
  const uint32_t total = 4 + static_cast<uint32_t>(width) * height * 2;
  uint8_t* packet = allocPacketBuffer(total);
  if (!packet) {
    return false;
  }
  packet[0] = static_cast<uint8_t>(width & 0xFF);
  packet[1] = static_cast<uint8_t>((width >> 8) & 0xFF);
  packet[2] = static_cast<uint8_t>(height & 0xFF);
  packet[3] = static_cast<uint8_t>((height >> 8) & 0xFF);
  memcpy(packet + 4, pixels, static_cast<size_t>(width) * height * 2);
  *out = packet;
  *out_len = total;
  return true;
}

bool convertFrameToRgb565Packet(const camera_fb_t* fb, uint8_t** out,
                                uint32_t* out_len) {
  if (!fb || !fb->buf || fb->width < 2 || fb->height < 2) {
    return false;
  }
  const uint16_t width = fb->width;
  const uint16_t height = fb->height;
  const size_t pixel_count = static_cast<size_t>(width) * height;

  if (fb->format == PIXFORMAT_RGB565 &&
      fb->len >= pixel_count * 2) {
    return packRawRgb565(width, height, fb->buf, fb->len, out, out_len);
  }

  const size_t rgb888_len = pixel_count * 3;
  uint8_t* rgb888 = static_cast<uint8_t*>(
      heap_caps_malloc(rgb888_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!rgb888) {
    rgb888 = static_cast<uint8_t*>(malloc(rgb888_len));
  }
  if (!rgb888) {
    return false;
  }

  const bool converted =
      fmt2rgb888(fb->buf, fb->len, fb->format, rgb888);
  if (!converted) {
    free(rgb888);
    return false;
  }

  const uint32_t total = 4 + static_cast<uint32_t>(pixel_count * 2);
  uint8_t* packet = allocPacketBuffer(total);
  if (!packet) {
    free(rgb888);
    return false;
  }
  packet[0] = static_cast<uint8_t>(width & 0xFF);
  packet[1] = static_cast<uint8_t>((width >> 8) & 0xFF);
  packet[2] = static_cast<uint8_t>(height & 0xFF);
  packet[3] = static_cast<uint8_t>((height >> 8) & 0xFF);

  uint8_t* dst = packet + 4;
  for (size_t i = 0; i < pixel_count; ++i) {
    const size_t src = i * 3;
    const uint8_t r = rgb888[src];
    const uint8_t g = rgb888[src + 1];
    const uint8_t b = rgb888[src + 2];
    const uint16_t rgb565 =
        static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    dst[i * 2] = static_cast<uint8_t>(rgb565 & 0xFF);
    dst[i * 2 + 1] = static_cast<uint8_t>((rgb565 >> 8) & 0xFF);
  }
  free(rgb888);
  *out = packet;
  *out_len = total;
  return true;
}

size_t normalizeJpeg(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_cap) {
  size_t soi = SIZE_MAX;
  for (size_t i = 0; i + 1 < src_len; ++i) {
    if (src[i] == 0xFF && src[i + 1] == 0xD8) {
      soi = i;
      break;
    }
  }
  if (soi == SIZE_MAX || src_len - soi < 128) {
    return 0;
  }
  size_t eoi = 0;
  for (size_t i = src_len; i >= soi + 2; --i) {
    if (src[i - 2] == 0xFF && src[i - 1] == 0xD9) {
      eoi = i;
      break;
    }
  }
  if (eoi <= soi || eoi - soi < 128) {
    return 0;
  }
  const size_t trimmed_len = eoi - soi;
  if (trimmed_len > dst_cap) {
    return 0;
  }
  memcpy(dst, src + soi, trimmed_len);
  return trimmed_len;
}

size_t repairJpeg(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_cap) {
  const size_t trimmed_len = normalizeJpeg(src, src_len, dst, dst_cap);
  if (trimmed_len == 0) {
    return 0;
  }
  uint8_t* tmp = static_cast<uint8_t*>(malloc(trimmed_len));
  if (!tmp) {
    return trimmed_len;
  }
  memcpy(tmp, dst, trimmed_len);
  size_t out = 0;
  for (size_t i = 0; i < trimmed_len && out + 2 < dst_cap; ++i) {
    const uint8_t b = tmp[i];
    if (b == 0 && i + 1 < trimmed_len && tmp[i + 1] == 0xFF) {
      ++i;
      continue;
    }
    if (b == 0xFF && i + 2 < trimmed_len && tmp[i + 1] == 0xFF) {
      const uint8_t m = tmp[i + 2];
      if (m != 0x00 && m != 0xFF && m != 0xD8) {
        dst[out++] = 0xFF;
        dst[out++] = m;
        i += 2;
        continue;
      }
    }
    dst[out++] = b;
  }
  free(tmp);
  return out >= 128 ? out : 0;
}

bool convertJpegFrameToRgb565Packet(const camera_fb_t* fb, uint8_t** out,
                                    uint32_t* out_len) {
  if (!fb || fb->format != PIXFORMAT_JPEG || !fb->buf || fb->len < 128) {
    return false;
  }

  size_t jpeg_len = fb->len;
  uint8_t* jpeg_buf = static_cast<uint8_t*>(
      heap_caps_malloc(jpeg_len + 64, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!jpeg_buf) {
    jpeg_buf = static_cast<uint8_t*>(malloc(jpeg_len + 64));
  }
  if (!jpeg_buf) {
    return false;
  }
  jpeg_len = repairJpeg(fb->buf, fb->len, jpeg_buf, jpeg_len + 64);
  if (jpeg_len < 128) {
    free(jpeg_buf);
    return false;
  }

  const uint16_t width = fb->width;
  const uint16_t height = fb->height;
  if (width < 2 || height < 2) {
    free(jpeg_buf);
    return false;
  }

  const size_t pixel_count = static_cast<size_t>(width) * height;
  const size_t rgb888_len = pixel_count * 3;
  uint8_t* rgb888 = static_cast<uint8_t*>(
      heap_caps_malloc(rgb888_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!rgb888) {
    rgb888 = static_cast<uint8_t*>(malloc(rgb888_len));
  }
  if (!rgb888) {
    free(jpeg_buf);
    return false;
  }

  bool converted = fmt2rgb888(jpeg_buf, jpeg_len, PIXFORMAT_JPEG, rgb888);
  if (!converted) {
    const size_t rgb565_bytes = pixel_count * 2;
    uint8_t* rgb565 = static_cast<uint8_t*>(
        heap_caps_malloc(rgb565_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!rgb565) {
      rgb565 = static_cast<uint8_t*>(malloc(rgb565_bytes));
    }
    if (rgb565 && jpg2rgb565(jpeg_buf, jpeg_len, rgb565, JPG_SCALE_NONE)) {
      free(rgb888);
      free(jpeg_buf);
      const bool ok = packRawRgb565(width, height, rgb565, rgb565_bytes, out, out_len);
      free(rgb565);
      return ok;
    }
    free(rgb565);
    free(rgb888);
    free(jpeg_buf);
    return false;
  }
  free(jpeg_buf);

  const uint32_t total = 4 + static_cast<uint32_t>(pixel_count * 2);
  uint8_t* packet = allocPacketBuffer(total);
  if (!packet) {
    free(rgb888);
    return false;
  }
  packet[0] = static_cast<uint8_t>(width & 0xFF);
  packet[1] = static_cast<uint8_t>((width >> 8) & 0xFF);
  packet[2] = static_cast<uint8_t>(height & 0xFF);
  packet[3] = static_cast<uint8_t>((height >> 8) & 0xFF);

  uint8_t* dst = packet + 4;
  for (size_t i = 0; i < pixel_count; ++i) {
    const size_t src = i * 3;
    const uint8_t r = rgb888[src];
    const uint8_t g = rgb888[src + 1];
    const uint8_t b = rgb888[src + 2];
    const uint16_t rgb565 =
        static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    dst[i * 2] = static_cast<uint8_t>(rgb565 & 0xFF);
    dst[i * 2 + 1] = static_cast<uint8_t>((rgb565 >> 8) & 0xFF);
  }
  free(rgb888);
  *out = packet;
  *out_len = total;
  return true;
}

size_t findJpegEoiLength(const uint8_t* src, size_t src_len) {
  if (!src || src_len < 128 || src[0] != 0xFF || src[1] != 0xD8) {
    return 0;
  }
  for (size_t i = src_len; i >= 2; --i) {
    if (src[i - 2] == 0xFF && src[i - 1] == 0xD9) {
      const size_t trimmed = i;
      return trimmed >= 128 ? trimmed : 0;
    }
  }
  return 0;
}

bool copyValidJpegPacket(const uint8_t* src, size_t src_len, uint8_t** out,
                         uint32_t* out_len) {
  if (!src || src_len < 128) {
    return false;
  }
  size_t soi = SIZE_MAX;
  for (size_t i = 0; i + 1 < src_len; ++i) {
    if (src[i] == 0xFF && src[i + 1] == 0xD8) {
      soi = i;
      break;
    }
  }
  if (soi == SIZE_MAX) {
    return false;
  }
  size_t eoi = 0;
  for (size_t i = src_len; i >= soi + 2; --i) {
    if (src[i - 2] == 0xFF && src[i - 1] == 0xD9) {
      eoi = i;
      break;
    }
  }
  // Prefer complete SOI..EOI; otherwise send from SOI to end (some OV5640
  // frames miss EOI but ImageIO/Pillow can still decode).
  const size_t trimmed =
      (eoi > soi + 128) ? (eoi - soi) : (src_len - soi);
  if (trimmed < 512) {
    return false;
  }
  uint8_t* packet = static_cast<uint8_t*>(
      heap_caps_malloc(trimmed, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!packet) {
    packet = static_cast<uint8_t*>(malloc(trimmed));
  }
  if (!packet) {
    return false;
  }
  memcpy(packet, src + soi, trimmed);
  *out = packet;
  *out_len = static_cast<uint32_t>(trimmed);
  return true;
}

void captureLuminanceStats(const uint8_t* packet, uint32_t packet_len,
                           uint8_t* min_lum, uint8_t* max_lum,
                           uint8_t* avg_lum) {
  if (!packet || packet_len < 8) {
    return;
  }
  const uint16_t width = packet[0] | (static_cast<uint16_t>(packet[1]) << 8);
  const uint16_t height = packet[2] | (static_cast<uint16_t>(packet[3]) << 8);
  const size_t pixel_count = static_cast<size_t>(width) * height;
  if (width < 2 || height < 2 || packet_len < 4 + pixel_count * 2) {
    return;
  }
  uint8_t min_l = 255;
  uint8_t max_l = 0;
  uint32_t sum = 0;
  for (size_t i = 0; i < pixel_count; ++i) {
    const uint16_t rgb565 =
        packet[4 + i * 2] | (static_cast<uint16_t>(packet[4 + i * 2 + 1]) << 8);
    const uint8_t r = static_cast<uint8_t>(((rgb565 >> 11) & 0x1F) * 255 / 31);
    const uint8_t g = static_cast<uint8_t>(((rgb565 >> 5) & 0x3F) * 255 / 63);
    const uint8_t b = static_cast<uint8_t>((rgb565 & 0x1F) * 255 / 31);
    const uint8_t lum = static_cast<uint8_t>((static_cast<uint16_t>(r) + g + b) / 3);
    if (lum < min_l) {
      min_l = lum;
    }
    if (lum > max_l) {
      max_l = lum;
    }
    sum += lum;
  }
  *min_lum = min_l;
  *max_lum = max_l;
  *avg_lum = static_cast<uint8_t>(sum / pixel_count);
}

bool encodeSoftwareJpeg(const camera_fb_t* fb, uint8_t** out_jpg, size_t* out_len) {
  if (!fb || !fb->buf || fb->width < 2 || fb->height < 2) {
    return false;
  }
  if (fb->format == PIXFORMAT_JPEG) {
    const size_t pixel_count = static_cast<size_t>(fb->width) * fb->height;
    const size_t rgb_len = pixel_count * 3;
    uint8_t* rgb = static_cast<uint8_t*>(
        heap_caps_malloc(rgb_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!rgb) {
      rgb = static_cast<uint8_t*>(malloc(rgb_len));
    }
    if (!rgb) {
      return false;
    }
    if (!fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, rgb)) {
      free(rgb);
      return false;
    }
    const bool ok = fmt2jpg(rgb, rgb_len, fb->width, fb->height, PIXFORMAT_RGB888,
                            USB_STREAM_JPEG_QUALITY, out_jpg, out_len);
    free(rgb);
    return ok && *out_jpg && *out_len >= 512;
  }
  if (fb->format == PIXFORMAT_YUV422 || fb->format == PIXFORMAT_RGB565 ||
      fb->format == PIXFORMAT_GRAYSCALE) {
    const bool ok =
        frame2jpg(const_cast<camera_fb_t*>(fb), USB_STREAM_JPEG_QUALITY, out_jpg, out_len);
    return ok && *out_jpg && *out_len >= 512;
  }
  return false;
}

}  // namespace

bool UsbStream::begin(Camera& camera) {
  camera_ = &camera;
  if (!camera_->isReady()) {
    if (SerialLock::tryLock()) {
      Serial.println("[USB] camera not ready");
      SerialLock::unlock();
    }
    return false;
  }

  active_ = false;
  failCount_ = 0;
  sentCount_ = 0;
  warmupLeft_ = USB_STREAM_WARMUP_FRAMES;
  xTaskCreatePinnedToCore(taskEntry, "usb_stream", 16384, this, 1, &task_, 0);
  if (SerialLock::tryLock()) {
    Serial.printf("[USB] ready @ %d baud — Mac App 连接后自动发 V 开始推流\n",
                  USB_STREAM_BAUD);
    SerialLock::unlock();
  }
  return true;
}

void UsbStream::setActive(bool on) {
  if (on && !active_) {
    resetWarmup_ = true;
    failCount_ = 0;
    sentCount_ = 0;
    warmupLeft_ = USB_STREAM_WARMUP_FRAMES;
    lastLumMin_ = 0;
    lastLumMax_ = 0;
    lastLumAvg_ = 0;
    needRecover_ = false;
  } else if (!on && active_) {
    if (SerialLock::tryLock()) {
      Serial.printf("[USB] stream off sent=%u fail=%u lum min=%u max=%u avg=%u\n",
                    sentCount_, failCount_, lastLumMin_, lastLumMax_, lastLumAvg_);
      SerialLock::unlock();
    }
  }
  active_ = on;
}

void UsbStream::reconfigureForRawStream() {
  // Intentionally unused: runtime RGB565/YUV422 switch hangs on this OV5640.
  (void)0;
}

void UsbStream::taskEntry(void* arg) {
  static_cast<UsbStream*>(arg)->taskLoop();
}

void UsbStream::taskLoop() {
  const TickType_t interval = pdMS_TO_TICKS(1000 / USB_STREAM_FPS);
  while (true) {
    if (!active_) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (!sendFrame()) {
      vTaskDelay(pdMS_TO_TICKS(200));
    } else {
      vTaskDelay(interval);
    }
  }
}

bool UsbStream::sendFrame() {
  if (!camera_ || !camera_->isReady()) {
    return false;
  }
  if (camera_->isStreamingPaused()) {
    return false;
  }

  static uint8_t warmupLeft = 8;
  static uint8_t warmupNulls = 0;
  if (resetWarmup_) {
    warmupLeft = warmupLeft_;
    warmupNulls = 0;
    resetWarmup_ = false;
  }
  if (warmupLeft > 0) {
    camera_fb_t* warm = camera_->acquireFramebuffer(pdMS_TO_TICKS(200));
    if (warm) {
      if (SerialLock::tryLock()) {
        Serial.printf("[USB] warmup ok fmt=%d len=%u\n",
                      static_cast<int>(warm->format),
                      static_cast<unsigned>(warm->len));
        SerialLock::unlock();
      }
      camera_->releaseFramebuffer(warm);
      --warmupLeft;
      warmupNulls = 0;
    } else {
      ++warmupNulls;
      if (warmupNulls == 1 || warmupNulls % 10 == 0) {
        if (SerialLock::tryLock()) {
          Serial.printf("[USB] warmup fb_get null x%u\n", warmupNulls);
          SerialLock::unlock();
        }
      }
      if (warmupNulls >= 30) {
        warmupLeft = 0;
      }
    }
    return false;
  }

  camera_fb_t* fb = camera_->acquireFramebuffer(pdMS_TO_TICKS(300));
  if (!fb) {
    ++failCount_;
    if (failCount_ == 1 || failCount_ % 20 == 0) {
      if (SerialLock::tryLock()) {
        Serial.printf("[USB] fb_get null x%u\n", failCount_);
        SerialLock::unlock();
      }
    }
    return false;
  }

  const size_t fb_len = fb->len;
  const pixformat_t fb_fmt = fb->format;
  const uint16_t width = fb->width;
  const uint16_t height = fb->height;

  uint8_t* jpg_buf = nullptr;
  size_t jpg_len = 0;
  bool encoded = false;
  const uint8_t* magic = kMagicJpeg;
  size_t magic_len = sizeof(kMagicJpeg);
  uint32_t packet_len = 0;
  uint8_t* packet_buf = nullptr;

  if (fb_fmt == PIXFORMAT_YUV422 || fb_fmt == PIXFORMAT_RGB565 ||
      fb_fmt == PIXFORMAT_GRAYSCALE) {
    if (convertFrameToRgb565Packet(fb, &packet_buf, &packet_len)) {
      encoded = true;
      magic = kMagicRgb565;
      magic_len = sizeof(kMagicRgb565);
    }
  } else if (fb_fmt == PIXFORMAT_JPEG) {
    // Send trimmed JPEG as-is. Aggressive repairJpeg can truncate valid frames.
    if (copyValidJpegPacket(fb->buf, fb_len, &packet_buf, &packet_len)) {
      encoded = true;
      magic = kMagicJpeg;
      magic_len = sizeof(kMagicJpeg);
    }
  }

  camera_->releaseFramebuffer(fb);

  if (!encoded || !packet_buf || packet_len < 128) {
    if (packet_buf) {
      free(packet_buf);
    }
    free(jpg_buf);
    ++failCount_;
    if (failCount_ == 1 || failCount_ % 20 == 0) {
      if (SerialLock::tryLock()) {
        Serial.printf("[USB] frame fail x%u fmt=%d fb=%u %ux%u\n", failCount_,
                      static_cast<int>(fb_fmt), static_cast<unsigned>(fb_len),
                      width, height);
        SerialLock::unlock();
      }
    }
    return false;
  }

  if (magic[3] == 0xFC) {
    captureLuminanceStats(packet_buf, packet_len, &lastLumMin_, &lastLumMax_,
                          &lastLumAvg_);
  }

  const bool ok = [&]() {
    SerialLock::lock();
    const bool sent = writeAll(magic, magic_len) && writeU32LE(packet_len) &&
                      writeAll(packet_buf, packet_len);
    SerialLock::unlock();
    free(packet_buf);
    return sent;
  }();
  if (ok) {
    ++sentCount_;
    if (sentCount_ == 1 && SerialLock::tryLock()) {
      Serial.printf("[USB] first frame %u bytes (%s) lum avg=%u\n", packet_len,
                    magic[3] == 0xFC ? "RGB565" : "JPEG", lastLumAvg_);
      SerialLock::unlock();
    } else if (magic[3] == 0xFC && lastLumAvg_ < 3 &&
               (sentCount_ == 5 || sentCount_ % 30 == 0) &&
               SerialLock::tryLock()) {
      Serial.printf("[USB] near-black frame #%u lum avg=%u — try serial 'C' colorbar\n",
                    sentCount_, lastLumAvg_);
      SerialLock::unlock();
    }
  }
  return ok;
}

#else

bool UsbStream::begin(Camera& camera) {
  (void)camera;
  return false;
}

void UsbStream::setActive(bool on) { (void)on; }

#endif
