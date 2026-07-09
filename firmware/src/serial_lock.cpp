#include "serial_lock.h"

namespace {

SemaphoreHandle_t gSerialMu = nullptr;

}  // namespace

void SerialLock::init() {
  if (!gSerialMu) {
    gSerialMu = xSemaphoreCreateMutex();
  }
}

void SerialLock::lock() {
  if (gSerialMu) {
    xSemaphoreTake(gSerialMu, portMAX_DELAY);
  }
}

void SerialLock::unlock() {
  if (gSerialMu) {
    xSemaphoreGive(gSerialMu);
  }
}

bool SerialLock::tryLock() {
  if (!gSerialMu) {
    return true;
  }
  return xSemaphoreTake(gSerialMu, 0) == pdTRUE;
}
