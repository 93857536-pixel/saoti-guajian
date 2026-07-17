#pragma once

#include <Arduino.h>

enum class UiScreen {
  Boot,
  Idle,
  Capturing,
  Uploading,
  Result,
  Error,
};

class Display {
 public:
  bool begin();
  bool isReady() const { return ready_; }
  void show(UiScreen screen, const char* message = nullptr);
  void showProgress(int percent);
  // 多行答案（小屏）；中文完整内容请看 SoftAP /answer
  void showAnswer(const char* answer);
  // 顶部网络条：如 "WiFi" / "SIM" / "WiFi+SIM" / "Via WiFi"
  void setNetBadge(const char* label);
  // 左上角电量：如 "85%" / "--%"
  void setBattBadge(const char* label);
  // 背光：空闲超时可关，按键/刷新时再开
  void setBacklight(bool on);
  bool backlightOn() const { return bl_on_; }

 private:
  void logScreen(UiScreen screen, const char* message);
  void drawHardware(UiScreen screen, const char* message);
  void drawNetBadge();
  void drawBattBadge();

  bool ready_ = false;
  bool bl_on_ = true;
  char netBadge_[24] = "";
  char battBadge_[8] = "";
};
