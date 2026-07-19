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
  // 扫题分步进度：step 从 1 开始；labels 为 UTF-8 中文数组
  void showSteps(int step, int total, const char* const* labels,
                 const char* tip = nullptr);
  // 多行答案（中文）；完整长文可看临时 SoftAP /answer
  void showAnswer(const char* answer);
  // SoftAP 提示文案（如 IP）；空则用默认
  void setAnswerApHint(const char* hint);
  // 顶部网络条：如 "4G" / "无网"
  void setNetBadge(const char* label);
  // 左上角电量：如 "85%" / "--%"
  void setBattBadge(const char* label);
  // 充电指示（顶栏闪电 + Idle 提示）
  void setCharging(bool on);
  bool charging() const { return charging_; }
  // 仅重绘顶栏（充电脉冲动画，避免整屏闪）
  void tickStatusBar();
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
  bool charging_ = false;
  char netBadge_[24] = "";
  char battBadge_[8] = "";
  char answerApHint_[40] = "";
};
