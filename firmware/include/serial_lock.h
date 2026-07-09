#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// 串口互斥：推流二进制帧期间禁止其它任务插入日志，避免 JPEG 损坏。
class SerialLock {
 public:
  static void init();
  static void lock();
  static void unlock();
  static bool tryLock();
};

inline void serialLockInit() { SerialLock::init(); }
