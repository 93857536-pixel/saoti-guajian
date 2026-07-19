/*
  ESP32_OV5640_AF.h - Library for OV5640 Auto Focus (ESP32 Camera)
  Created by Eric Nam, December 08, 2021.
  Released into the public domain.
*/

#ifndef ESP32_OV5640_AF_h
#define ESP32_OV5640_AF_h

#include <Arduino.h>
#include "ESP32_OV5640_cfg.h"
#include "esp_camera.h"

// 原库类名 OV5640 易混淆，改为 Ov5640Af
class Ov5640Af {
private:
  sensor_t* sensor;
  bool isOV5640;

public:
  Ov5640Af();
  bool start(sensor_t* _sensor);
  uint8_t focusInit();
  uint8_t autoFocusMode();
  // 单次对焦并等待完成（扫题用）
  uint8_t singleAutoFocus(uint16_t timeoutMs = 2500);
  uint8_t getFWStatus();
};

#endif