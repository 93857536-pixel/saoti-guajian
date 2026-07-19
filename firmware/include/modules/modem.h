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

struct CellHttpResult {
  bool ok = false;
  int httpCode = 0;
  String body;
  const char* error = nullptr;
};

class Modem {
 public:
  bool begin();
  bool ensureInitialized();
  bool runDiagnostics();
  // 打印 ATI / AT+SIMCOMATI / CGMR（升级前后核对型号用）
  bool dumpFirmwareInfo();
  // 穷举候选 GPIO 的 TX/RX 组合，寻找能 AT→OK 的脚位
  bool scanUartPins();
  bool isReady() const;
  bool isCellReady() const;
  bool isWifiReady() const;
  bool ensureWifiForStreaming();
  bool ensureCellNetwork();
  // 省电：AT+CFUN=0；扫题前 wakeRadio()
  bool sleepRadio();
  bool wakeRadio();
  bool radioSleeping() const { return radioSleeping_; }
  String streamingIp() const;
  String activeApn() const;
  bool setApn(const char* apn);
  int lastCsq() const;
  UploadResult upload(const uint8_t* data, size_t len);

  // 4G HTTPS JSON POST（智谱等）；body 可为 PSRAM 大缓冲
  CellHttpResult httpsPostJson(const char* url, const char* bearerToken,
                               const uint8_t* json, size_t jsonLen);

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
  bool radioSleeping_ = false;
  String apn_ = CELL_APN;
};
