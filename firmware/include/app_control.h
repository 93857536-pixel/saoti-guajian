#pragma once

#include <Arduino.h>

// SoftAP / 串口 / BLE 与主循环之间的轻量控制面
void appRequestFixedAiTest();
bool appConsumeFixedAiTestRequest();

void appRequestCapture();
bool appConsumeCaptureRequest();

void appRequestWake();
bool appConsumeWakeRequest();

void appRequestThumb();
bool appConsumeThumbRequest();

void appRequestFlash(bool on);
bool appConsumeFlashRequest(bool* onOut);

// JSON 状态（供 SoftAP /status / 串口 / BLE）
String appStatusJson();
