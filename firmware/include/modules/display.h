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

 private:
  void logScreen(UiScreen screen, const char* message);
  void drawHardware(UiScreen screen, const char* message);

  bool ready_ = false;
};
