#pragma once

#include <Arduino.h>

// 锂电池电压采样（需外接分压，见 WIRING.md）
namespace battery {

void begin();
// 归一后的电芯电压（V）；无效返回 <0
float voltage();
// ADC 脚实测电压（V）；无效返回 <0
float pinVoltage();
// 0–100；无效返回 -1
int percent();
// 如 "85%" / "--%"
const char* label();
// 是否在充电（无 CHG 脚时用电压/趋势推断）
bool isCharging();
// SoftAP/WiFi 期间冻结 ADC2 采样，避免射频干扰导致电量/充电乱跳
void setAdcFrozen(bool frozen);

}  // namespace battery
