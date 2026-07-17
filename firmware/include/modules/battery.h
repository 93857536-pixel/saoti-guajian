#pragma once

#include <Arduino.h>

// 锂电池电压采样（需外接分压，见 WIRING.md）
namespace battery {

void begin();
// 电池电压（V）；无效返回 <0
float voltage();
// 0–100；无效返回 -1
int percent();
// 如 "85%" / "--%"
const char* label();

}  // namespace battery
