#include "modules/battery.h"

#include "config.h"
#include "pins.h"

namespace battery {
namespace {

char kLabel[8] = "--%";
float kLastV = -1.f;
float kLastPinV = -1.f;
float kLastRatioed = -1.f;
float kPrevV = -1.f;
int kLastPct = -1;
bool kCharging = false;
bool kAdcFrozen = false;
int kRiseStreak = 0;
int kExitStreak = 0;
uint32_t kLastMs = 0;

int medianMilliVolts() {
  if (pins::BAT_ADC < 0) {
    return -1;
  }
  // ADC2 在 WiFi 下偶发毛刺，取中位
  int samples[8];
  for (int i = 0; i < 8; ++i) {
    samples[i] = analogReadMilliVolts(pins::BAT_ADC);
    delay(2);
  }
  for (int i = 1; i < 8; ++i) {
    const int key = samples[i];
    int j = i - 1;
    while (j >= 0 && samples[j] > key) {
      samples[j + 1] = samples[j];
      --j;
    }
    samples[j + 1] = key;
  }
  return samples[4];
}

// 把各种接线读数归一成「等效电芯电压」再算百分比
float normalizeCellVolts(float pinV, float ratioedV) {
  // 1) 正常分压：脚电压约 1.6–2.3V → ratioed ≈ 3.2–4.6V
  if (pinV >= 1.40f && pinV <= 2.45f && ratioedV >= 2.70f && ratioedV <= 5.10f) {
    return ratioedV;
  }

  // 2) 读数被放大一倍（常见：接到约 5V 轨 + 分压，或 ADC 偏高）
  if (ratioedV > 5.10f && ratioedV <= 12.0f) {
    return ratioedV * 0.5f;
  }

  // 3) 脚电压已接近满量程：按分压还原，勿直接把 pinV 当电芯电压（会低估到 ~2.5V）
  if (pinV >= 2.45f && pinV <= 3.50f) {
    const float scaled = pinV * BAT_DIVIDER_RATIO;
    if (scaled > 5.0f) {
      return 4.20f;
    }
    if (scaled >= 2.70f) {
      return scaled;
    }
    return -1.f;
  }

  // 4) 其它：若 ratioed 落在锂电合理区也接受
  if (ratioedV >= 2.70f && ratioedV <= 5.20f) {
    return ratioedV;
  }
  return -1.f;
}

int voltsToPercent(float v) {
  if (v < 0) {
    return -1;
  }
  if (v >= BAT_V_FULL) {
    return 100;
  }
  if (v <= BAT_V_EMPTY) {
    return 0;
  }
  return static_cast<int>((v - BAT_V_EMPTY) * 100.0f / (BAT_V_FULL - BAT_V_EMPTY) +
                          0.5f);
}

void updateCharging(float cellV, float pinV, float ratioedV) {
  if (cellV < 0 && pinV < 0) {
    kCharging = false;
    kPrevV = -1.f;
    kRiseStreak = 0;
    kExitStreak = 0;
    return;
  }

  // 明显接到充电/5V 轨。勿用「ADC 顶格 3.3V」推断——夹紧后 pin≈3.3、ratioed≈6.6 会误报充电。
  const bool onChargeRail = (ratioedV >= BAT_RATIOED_CHARGE_RAIL);

  // 上升趋势只用于「维持」充电态；扫题后负载卸掉的电压回升不能单独进入充电
  if (kPrevV > 0.f && cellV > 0.f) {
    const float d = cellV - kPrevV;
    if (d >= BAT_V_CHARGE_RISE && cellV >= 3.80f) {
      if (kRiseStreak < 8) {
        ++kRiseStreak;
      }
    } else if (d <= -0.015f) {
      kRiseStreak = 0;
    }
  }
  const bool risingCharge = kRiseStreak >= BAT_CHARGE_RISE_COUNT;

  if (kCharging) {
    if (onChargeRail || risingCharge) {
      kExitStreak = 0;
    } else {
      ++kExitStreak;
      // 连续两次采样都无充电特征 → 退出，避免闪烁
      if (kExitStreak >= 2) {
        kCharging = false;
        kRiseStreak = 0;
        kExitStreak = 0;
      }
    }
  } else if (onChargeRail) {
    kCharging = true;
    kExitStreak = 0;
  }

  if (cellV > 0.f) {
    kPrevV = cellV;
  }
}

void refresh(bool force) {
  if (pins::BAT_ADC < 0) {
    snprintf(kLabel, sizeof(kLabel), "--%%");
    kLastPct = -1;
    kLastV = -1.f;
    kLastPinV = -1.f;
    kLastRatioed = -1.f;
    kCharging = false;
    return;
  }
  // WiFi SoftAP 开启时 ADC2 不可靠：保持上次有效读数
  if (kAdcFrozen && !force) {
    return;
  }
  const uint32_t now = millis();
  const uint32_t interval = kCharging ? 1500u : 3000u;
  if (!force && kLastMs != 0 && (now - kLastMs) < interval) {
    return;
  }
  kLastMs = now;

  const int rawMv = medianMilliVolts();
  // ADC2 偶发读数 >3.3V；充电判定与电量都用夹紧值，避免 ratioed 虚高误报充电
  const int clampedMv = rawMv > 3300 ? 3300 : (rawMv < 0 ? 0 : rawMv);
  kLastPinV = clampedMv / 1000.0f;
  kLastRatioed = (clampedMv / 1000.0f) * BAT_DIVIDER_RATIO;
  kLastV = normalizeCellVolts(kLastPinV, kLastRatioed);
  if (kLastV < 0) {
    kLastV = normalizeCellVolts(kLastPinV, kLastPinV * BAT_DIVIDER_RATIO);
  }
  kLastPct = voltsToPercent(kLastV);
  if (kLastPct < 0) {
    snprintf(kLabel, sizeof(kLabel), "--%%");
  } else {
    snprintf(kLabel, sizeof(kLabel), "%d%%", kLastPct);
  }
  updateCharging(kLastV, kLastPinV, kLastRatioed);
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
  Serial.printf("[BAT] GPIO%d pin=%.2fV cell=%.2fV pct=%s chg=%d\n",
                pins::BAT_ADC, kLastPinV, kLastV, kLabel, kCharging ? 1 : 0);
}

float voltage() {
  refresh(false);
  return kLastV;
}

float pinVoltage() {
  refresh(false);
  return kLastPinV;
}

int percent() {
  refresh(false);
  return kLastPct;
}

const char* label() {
  refresh(false);
  return kLabel;
}

bool isCharging() {
  refresh(false);
  return kCharging;
}

void setAdcFrozen(bool frozen) {
  kAdcFrozen = frozen;
  if (!frozen) {
    // 解冻后下一拍强制重采样
    kLastMs = 0;
  }
}

}  // namespace battery
