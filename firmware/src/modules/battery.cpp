#include "modules/battery.h"

#include "config.h"
#include "pins.h"

namespace battery {
namespace {

char kLabel[8] = "--%";
float kLastV = -1.f;
int kLastPct = -1;
uint32_t kLastMs = 0;

float readAdcVolts() {
  if (pins::BAT_ADC < 0) {
    return -1.f;
  }
  // 多次采样平均；ADC2 在 WiFi 下可能偶发偏差，取中位更稳
  int samples[8];
  for (int i = 0; i < 8; ++i) {
    samples[i] = analogReadMilliVolts(pins::BAT_ADC);
    delay(2);
  }
  // 简单插入排序取中位
  for (int i = 1; i < 8; ++i) {
    const int key = samples[i];
    int j = i - 1;
    while (j >= 0 && samples[j] > key) {
      samples[j + 1] = samples[j];
      --j;
    }
    samples[j + 1] = key;
  }
  const float vAdc = samples[4] / 1000.0f;
  return vAdc * BAT_DIVIDER_RATIO;
}

int voltsToPercent(float v) {
  if (v < 2.5f || v > 5.0f) {
    return -1;  // 未接分压或接线异常
  }
  if (v <= BAT_V_EMPTY) {
    return 0;
  }
  if (v >= BAT_V_FULL) {
    return 100;
  }
  return static_cast<int>((v - BAT_V_EMPTY) * 100.0f /
                          (BAT_V_FULL - BAT_V_EMPTY) +
                          0.5f);
}

void refresh(bool force) {
  if (pins::BAT_ADC < 0) {
    snprintf(kLabel, sizeof(kLabel), "--%%");
    kLastPct = -1;
    return;
  }
  const uint32_t now = millis();
  if (!force && kLastMs != 0 && (now - kLastMs) < 3000) {
    return;
  }
  kLastMs = now;
  kLastV = readAdcVolts();
  kLastPct = voltsToPercent(kLastV);
  if (kLastPct < 0) {
    snprintf(kLabel, sizeof(kLabel), "--%%");
  } else {
    snprintf(kLabel, sizeof(kLabel), "%d%%", kLastPct);
  }
}

}  // namespace

void begin() {
  if (pins::BAT_ADC < 0) {
    Serial.println("[BAT] ADC disabled");
    return;
  }
  analogReadResolution(12);
  analogSetPinAttenuation(pins::BAT_ADC, ADC_11db);
  pinMode(pins::BAT_ADC, INPUT);
  refresh(true);
  Serial.printf("[BAT] ADC GPIO%d divider=%.2f  V=%.2f pct=%s\n", pins::BAT_ADC,
                BAT_DIVIDER_RATIO, kLastV, kLabel);
}

float voltage() {
  refresh(false);
  return kLastV;
}

int percent() {
  refresh(false);
  return kLastPct;
}

const char* label() {
  refresh(false);
  return kLabel;
}

}  // namespace battery
