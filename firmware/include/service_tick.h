#pragma once

#include <stdint.h>

// 长阻塞路径里定期调用，避免 BLE/TTS 饿死
void serviceTickRegister(void (*fn)());
void serviceTick();
void serviceDelay(uint32_t ms);
