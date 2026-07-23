#include "service_tick.h"

#include <Arduino.h>

namespace {
void (*gServiceFn)() = nullptr;
}

void serviceTickRegister(void (*fn)()) { gServiceFn = fn; }

void serviceTick() {
  if (gServiceFn) {
    gServiceFn();
  } else {
    yield();
  }
}

void serviceDelay(uint32_t ms) {
  const uint32_t start = millis();
  while (millis() - start < ms) {
    serviceTick();
    delay(5);
  }
}
