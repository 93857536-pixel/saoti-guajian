#pragma once

#include "modules/camera.h"

class UsbStream {
 public:
  bool begin(Camera& camera);
  void setActive(bool on);
  bool isActive() const { return active_; }

 private:
  static void taskEntry(void* arg);
  void taskLoop();
  bool sendFrame();
  void reconfigureForRawStream();

  Camera* camera_ = nullptr;
  TaskHandle_t task_ = nullptr;
  volatile bool active_ = false;
  volatile bool resetWarmup_ = false;
  volatile bool needRecover_ = false;
  uint32_t failCount_ = 0;
  uint32_t sentCount_ = 0;
  uint8_t warmupLeft_ = 8;
  uint8_t warmupNulls_ = 0;
  uint8_t lastLumMin_ = 0;
  uint8_t lastLumMax_ = 0;
  uint8_t lastLumAvg_ = 0;
};
