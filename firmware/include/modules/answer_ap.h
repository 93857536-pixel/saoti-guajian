#pragma once

#include <Arduino.h>

// 解题后临时 SoftAP：手机连上打开 /answer 看完整中文答案
namespace answer_ap {

bool start();
void stop();
bool active();
String ip();

}  // namespace answer_ap
