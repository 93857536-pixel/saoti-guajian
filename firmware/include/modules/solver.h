#pragma once

#include <Arduino.h>

struct SolveResult {
  bool ok = false;
  String answer;
  const char* error = nullptr;
  int httpCode = 0;
};

class Solver {
 public:
  // 拍到的 JPEG → 通义 VL / OpenAI 兼容 Vision → 中文答案
  SolveResult solveJpeg(const uint8_t* jpeg, size_t len);
  const String& lastAnswer() const { return lastAnswer_; }
  void setLastAnswer(const String& a) { lastAnswer_ = a; }

 private:
  String lastAnswer_;
};

// SoftAP /answer 页面读取
String solverLastAnswerHtml();
void solverSetSharedAnswer(const String& answer);
void solverSetLastError(const char* error);
String solverLastError();
bool solverHasAnswer();
uint32_t solverAnswerAgeMs();
