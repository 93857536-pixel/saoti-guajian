#pragma once

#include <Arduino.h>

// SoftAP / 串口 与主循环之间的轻量控制面
void appRequestFixedAiTest();
bool appConsumeFixedAiTestRequest();

void appRequestCapture();
bool appConsumeCaptureRequest();

// JSON 状态（供 SoftAP /status 与串口）
String appStatusJson();
