#pragma once

#include <Arduino.h>

// UART 中文 TTS：XFS5152 / SYN6288 同类（Serial1，见 pins.h）
namespace tts {

void begin();
// 主循环调用：发送排队文本、等 BUSY
void loop();
bool ready();
bool busy();
// 播报 UTF-8 文本（解题答案）；打断当前播报
void speak(const char* utf8);
void speak(const String& utf8);
// 立即停止
void stop();

}  // namespace tts
