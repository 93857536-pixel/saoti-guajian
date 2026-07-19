#pragma once

#include <Arduino.h>

#ifndef BLE_GATT_ENABLE
#define BLE_GATT_ENABLE 1
#endif

// 扫题挂件 BLE Companion 协议（与 Apple App 共用 UUID）
// Service  6E400001-B5A3-F393-E0A9-E50E24DCCA9E
// STATUS   6E400002-...  Read + Notify  JSON
// COMMAND  6E400003-...  Write          文本命令
// ANSWER   6E400004-...  Read + Notify  UTF-8（可分片）
// THUMB    6E400005-...  Notify         JPEG 分片
// EVENT    6E400006-...  Notify         JSON 事件

namespace ble_gatt {

bool begin();
void loop();
bool isConnected();
bool isReady();
// 广播名，如 Saoti-F79D；未启动时返回 ""
const char* advName();
// 自检：确保 WiFi 关闭并重新广播；成功返回 true
bool selfCheck();
void setPhase(const char* phase);
const char* phase();
void notifyAnswerChanged();
void notifyEventJson(const char* json);
void sendThumbJpeg(const uint8_t* data, size_t len);
// SoftAP/WiFi 操作后调用，重新广播
void restartAdvertising();
// 断开当前中央设备并重新广播（解决「被占线扫不到」）
void disconnectAll();

// 协议版本（STATUS JSON 内 fw_proto 字段）
constexpr int kProtoVersion = 1;

}  // namespace ble_gatt
