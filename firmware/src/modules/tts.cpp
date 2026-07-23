#include "config.h"
#include "pins.h"
#include "modules/tts.h"

#if TTS_ENABLE

namespace {

HardwareSerial& uart() { return Serial1; }

bool gReady = false;
String gPending;
size_t gPendingOff = 0;
uint32_t gWaitUntilMs = 0;
bool gFrameSent = false;

constexpr size_t kMaxChunk = 180;  // 单帧文本上限（留编码字节）

bool busyPinActive() {
  if (pins::TTS_BUSY < 0) {
    return false;
  }
  const int v = digitalRead(pins::TTS_BUSY);
#if TTS_BUSY_ACTIVE_HIGH
  return v == HIGH;
#else
  return v == LOW;
#endif
}

uint8_t xorChecksum(const uint8_t* p, size_t n) {
  uint8_t x = 0;
  for (size_t i = 0; i < n; ++i) {
    x ^= p[i];
  }
  return x;
}

// 帧：FD | lenH | lenL | cmd | data... | xor(lenH..data)
bool sendFrame(uint8_t cmd, const uint8_t* data, size_t dataLen) {
  if (!gReady || dataLen > 250) {
    return false;
  }
  const uint16_t payload = static_cast<uint16_t>(1 + dataLen);  // cmd + data
  uint8_t buf[8 + 256];
  size_t n = 0;
  buf[n++] = 0xFD;
  buf[n++] = static_cast<uint8_t>((payload >> 8) & 0xFF);
  buf[n++] = static_cast<uint8_t>(payload & 0xFF);
  buf[n++] = cmd;
  for (size_t i = 0; i < dataLen; ++i) {
    buf[n++] = data[i];
  }
  buf[n++] = xorChecksum(buf + 1, n - 1);
  const size_t wrote = uart().write(buf, n);
  uart().flush();
  return wrote == n;
}

bool sendStop() {
  return sendFrame(0x02, nullptr, 0);
}

bool sendSpeakChunk(const uint8_t* text, size_t len) {
#if TTS_UTF8
  // XFS5152：cmd=0x01，首字节 0x04 = UTF-8
  uint8_t data[1 + kMaxChunk];
  if (len > kMaxChunk) {
    len = kMaxChunk;
  }
  data[0] = 0x04;
  memcpy(data + 1, text, len);
  return sendFrame(0x01, data, 1 + len);
#else
  // SYN6288：cmd=0x01，正文为模块编码（通常 GBK）
  return sendFrame(0x01, text, len > kMaxChunk ? kMaxChunk : len);
#endif
}

size_t nextUtf8ChunkLen(const char* s, size_t avail, size_t maxBytes) {
  if (avail <= maxBytes) {
    return avail;
  }
  size_t n = maxBytes;
  // 不截断 UTF-8 多字节字符
  while (n > 0 && (static_cast<uint8_t>(s[n]) & 0xC0) == 0x80) {
    --n;
  }
  return n == 0 ? maxBytes : n;
}

uint32_t estimatePlayMs(size_t utf8Bytes) {
  // 粗估：约 4 字/秒，UTF-8 中文约 3 字节/字
  const size_t chars = utf8Bytes / 3 + 1;
  uint32_t ms = static_cast<uint32_t>(chars) * 280u + 400u;
  if (ms < 800) {
    ms = 800;
  }
  if (ms > 60000) {
    ms = 60000;
  }
  return ms;
}

void pump() {
  if (!gReady) {
    return;
  }
  if (gPending.isEmpty()) {
    gFrameSent = false;
    return;
  }
  if (gWaitUntilMs != 0 && millis() < gWaitUntilMs) {
    return;
  }
  if (pins::TTS_BUSY >= 0 && gFrameSent && busyPinActive()) {
    return;
  }

  const char* s = gPending.c_str() + gPendingOff;
  const size_t left = gPending.length() - gPendingOff;
  if (left == 0) {
    gPending = "";
    gPendingOff = 0;
    gFrameSent = false;
    gWaitUntilMs = 0;
    return;
  }

#if TTS_UTF8
  const size_t maxText = kMaxChunk;
#else
  const size_t maxText = kMaxChunk;
#endif
  const size_t chunk = nextUtf8ChunkLen(s, left, maxText);
  if (!sendSpeakChunk(reinterpret_cast<const uint8_t*>(s), chunk)) {
    Serial.println("[TTS] send fail");
    gPending = "";
    gPendingOff = 0;
    return;
  }
  gPendingOff += chunk;
  gFrameSent = true;
  if (pins::TTS_BUSY < 0) {
    gWaitUntilMs = millis() + estimatePlayMs(chunk);
  } else {
    // 等 BUSY 拉高再等拉低；先给一点启动时间
    gWaitUntilMs = millis() + 80;
  }
  Serial.printf("[TTS] chunk %u/%u\n", static_cast<unsigned>(gPendingOff),
                static_cast<unsigned>(gPending.length()));
}

}  // namespace

namespace tts {

void begin() {
  gReady = false;
  gPending = "";
  gPendingOff = 0;
  gWaitUntilMs = 0;
  gFrameSent = false;

  if (pins::TTS_TX < 0) {
    Serial.println("[TTS] disabled (no TX pin)");
    return;
  }
  // RX 可不接：rxPin=-1
  uart().begin(TTS_BAUD, SERIAL_8N1, pins::TTS_RX, pins::TTS_TX);
  if (pins::TTS_BUSY >= 0) {
    pinMode(pins::TTS_BUSY, INPUT_PULLUP);
  }
  delay(50);
  sendStop();
  gReady = true;
  Serial.printf("[TTS] Serial1 TX=GPIO%d BUSY=GPIO%d baud=%d utf8=%d\n",
                pins::TTS_TX, pins::TTS_BUSY, TTS_BAUD, TTS_UTF8);
}

void loop() { pump(); }

bool ready() { return gReady; }

bool busy() {
  if (!gReady) {
    return false;
  }
  if (!gPending.isEmpty()) {
    return true;
  }
  return pins::TTS_BUSY >= 0 && busyPinActive();
}

void stop() {
  if (!gReady) {
    return;
  }
  gPending = "";
  gPendingOff = 0;
  gWaitUntilMs = 0;
  gFrameSent = false;
  sendStop();
  Serial.println("[TTS] stop");
}

void speak(const char* utf8) {
  if (!gReady || !utf8 || !utf8[0]) {
    return;
  }
  stop();
  delay(30);
  gPending = utf8;
  gPendingOff = 0;
  gFrameSent = false;
  gWaitUntilMs = 0;
  Serial.printf("[TTS] speak %u bytes\n",
                static_cast<unsigned>(gPending.length()));
  pump();
}

void speak(const String& utf8) { speak(utf8.c_str()); }

}  // namespace tts

#else  // !TTS_ENABLE

namespace tts {
void begin() {}
void loop() {}
bool ready() { return false; }
bool busy() { return false; }
void speak(const char*) {}
void speak(const String&) {}
void stop() {}
}  // namespace tts

#endif
