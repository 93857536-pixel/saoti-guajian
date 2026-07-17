#pragma once

#include <Arduino.h>
#include <vector>

void prepareModemPins();

struct UploadResult {
  bool ok = false;
  int httpCode = 0;
  const char* error = nullptr;
  const char* via = nullptr;  // "WiFi" or "SIM"
};

class Modem {
 public:
  bool begin();
  bool ensureInitialized();
  bool runDiagnostics();
  // 穷举候选 GPIO 的 TX/RX 组合，寻找能 AT→OK 的脚位
  bool scanUartPins();
  bool isReady() const;
  bool isCellReady() const;
  bool isWifiReady() const;
  bool ensureWifiForStreaming();
  String streamingIp() const;
  String activeApn() const;
  bool setApn(const char* apn);
  int lastCsq() const;
  UploadResult upload(const uint8_t* data, size_t len);

 private:
  bool beginMock();
  bool beginHardware();
  UploadResult uploadMock(const uint8_t* data, size_t len);
  UploadResult uploadHardware(const uint8_t* data, size_t len);
  UploadResult uploadWifi(const uint8_t* data, size_t len);
  UploadResult uploadWithFailover(const uint8_t* data, size_t len);

  void wifiBegin();
  bool wifiEnsureConnected();
  bool startStreamingSoftAp();

  bool cellReady_ = false;
  bool wifiReady_ = false;
  bool streamingApMode_ = false;
  bool initialized_ = false;
  String apn_ = CELL_APN;
};
