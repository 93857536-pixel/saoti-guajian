#include "config.h"
#include "modules/ble_gatt.h"

#if !BLE_GATT_ENABLE

namespace ble_gatt {
bool begin() { return false; }
void loop() {}
bool isConnected() { return false; }
bool isReady() { return false; }
const char* advName() { return ""; }
bool selfCheck() { return false; }
void setPhase(const char*) {}
const char* phase() { return "idle"; }
void notifyAnswerChanged() {}
void notifyEventJson(const char*) {}
void sendThumbJpeg(const uint8_t*, size_t) {}
void restartAdvertising() {}
void disconnectAll() {}
}  // namespace ble_gatt

#else

#include "app_control.h"
#include "modules/ble_uuids.h"
#include "modules/solver.h"

#include <BLE2902.h>
#include <BLEAdvertising.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <WiFi.h>
#include <esp_bt.h>
#include <esp_mac.h>

namespace {

BLEServer* gServer = nullptr;
BLECharacteristic* gStatus = nullptr;
BLECharacteristic* gCommand = nullptr;
BLECharacteristic* gAnswer = nullptr;
BLECharacteristic* gThumb = nullptr;
BLECharacteristic* gEvent = nullptr;

volatile bool gConnected = false;
volatile bool gNeedStatus = false;
volatile bool gNeedAnswer = false;
bool gStarted = false;
char gAdvName[20] = "Saoti";
char gPhase[24] = "idle";
uint32_t gLastStatusMs = 0;
uint32_t gLastAdvKickMs = 0;
String gPendingEvent;

void notifyRaw(BLECharacteristic* ch, const uint8_t* data, size_t len) {
  if (!ch || !gConnected || !data || !len) {
    return;
  }
  const size_t chunk = 180;
  size_t off = 0;
  while (off < len) {
    const size_t n = (len - off) > chunk ? chunk : (len - off);
    ch->setValue(const_cast<uint8_t*>(data + off), n);
    ch->notify();
    off += n;
    delay(8);
  }
}

void notifyText(BLECharacteristic* ch, const String& s) {
  notifyRaw(ch, reinterpret_cast<const uint8_t*>(s.c_str()), s.length());
}

void pushStatus() {
  if (!gStatus || !gConnected) {
    return;
  }
  String j = appStatusJson();
  gStatus->setValue(j.c_str());
  gStatus->notify();
}

void pushAnswer() {
  if (!gAnswer || !gConnected) {
    return;
  }
  const String ans = solverLastAnswerText();
  if (ans.isEmpty()) {
    gAnswer->setValue("");
    gAnswer->notify();
    return;
  }
  char hdr[16];
  snprintf(hdr, sizeof(hdr), "LEN:%u\n", static_cast<unsigned>(ans.length()));
  String packet = String(hdr) + ans;
  notifyText(gAnswer, packet);
}

void startAdvInternal() {
  if (!gServer) {
    return;
  }
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->stop();
  delay(30);

  // 31B 限制：ADV 只放 Flags+完整名，让安卓「设置→蓝牙」尽量能扫到名字；
  // Scan Response 放 Service UUID + 厂商魔数（App / iOS 主动扫描可读）。
  // 注意：iPhone「设置→蓝牙」永远不会列出自定义 GATT 设备，必须用 Companion App。
  BLEAdvertisementData advData;
  advData.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
  advData.setName(gAdvName);
  adv->setAdvertisementData(advData);

  BLEAdvertisementData scanData;
  scanData.setCompleteServices(BLEUUID(SAOTI_BLE_SERVICE_UUID));
  {
    std::string mfg;
    mfg.push_back(static_cast<char>(0xFF));
    mfg.push_back(static_cast<char>(0xFF));
    mfg += "SAOT";
    scanData.setManufacturerData(mfg);
  }
  adv->setScanResponseData(scanData);

  adv->setScanResponse(true);
  adv->setAdvertisementType(ADV_TYPE_IND);
  // 单位 0.625ms：约 20–40ms，提高发现率
  adv->setMinInterval(0x20);
  adv->setMaxInterval(0x40);
  adv->setMinPreferred(0x06);
  adv->setMaxPreferred(0x12);
  // 三信道广播
  adv->setAdvertisementChannelMap(ADV_CHNL_ALL);
  BLEDevice::startAdvertising();
  Serial.printf("[BLE] advertising name=%s (use Companion App; iOS Settings will NOT list it)\n",
                gAdvName);
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* server) override {
    gConnected = true;
    gNeedStatus = true;
    Serial.println("[BLE] connected");
    // 连接后继续广播，避免「已占线」导致第二台手机永远扫不到
    delay(80);
    startAdvInternal();
  }
  void onDisconnect(BLEServer*) override {
    gConnected = false;
    Serial.println("[BLE] disconnected — restart advertising");
    delay(120);
    startAdvInternal();
  }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* ch) override {
    std::string v = ch->getValue();
    if (v.empty()) {
      return;
    }
    String cmd;
    cmd.reserve(static_cast<unsigned>(v.size()));
    for (char c : v) {
      cmd += c;
    }
    cmd.trim();
    cmd.toLowerCase();
    Serial.printf("[BLE] cmd: %s\n", cmd.c_str());

    if (cmd == "ping") {
      gPendingEvent = "{\"type\":\"pong\",\"ok\":true}";
      return;
    }
    if (cmd == "scan" || cmd == "s") {
      appRequestCapture();
      gPendingEvent = "{\"type\":\"ack\",\"cmd\":\"scan\"}";
      return;
    }
    if (cmd == "wake") {
      appRequestWake();
      gPendingEvent = "{\"type\":\"ack\",\"cmd\":\"wake\"}";
      return;
    }
    if (cmd == "thumb" || cmd == "preview") {
      appRequestThumb();
      gPendingEvent = "{\"type\":\"ack\",\"cmd\":\"thumb\"}";
      return;
    }
    if (cmd == "flash=1" || cmd == "flashon") {
      appRequestFlash(true);
      gPendingEvent = "{\"type\":\"ack\",\"cmd\":\"flash\",\"on\":true}";
      return;
    }
    if (cmd == "flash=0" || cmd == "flashoff") {
      appRequestFlash(false);
      gPendingEvent = "{\"type\":\"ack\",\"cmd\":\"flash\",\"on\":false}";
      return;
    }
    if (cmd == "status") {
      gNeedStatus = true;
      return;
    }
    if (cmd == "answer") {
      gNeedAnswer = true;
      return;
    }
    if (cmd == "adv" || cmd == "advertise") {
      startAdvInternal();
      gPendingEvent = "{\"type\":\"ack\",\"cmd\":\"adv\"}";
      return;
    }
    gPendingEvent = "{\"type\":\"error\",\"reason\":\"unknown_cmd\"}";
  }
};

}  // namespace

namespace ble_gatt {

bool begin() {
  // WiFi SoftAP/STA 会占射频；BLE 前强制关掉 WiFi
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  delay(50);

  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(gAdvName, sizeof(gAdvName), "Saoti-%02X%02X", mac[4], mac[5]);

  Serial.printf("[BLE] init BT name=%s free_heap=%u\n", gAdvName,
                static_cast<unsigned>(ESP.getFreeHeap()));

  BLEDevice::init(gAdvName);
  // 最高发射功率（可发现性）
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_CONN_HDL0, ESP_PWR_LVL_P9);

  // Just Works 绑定：部分安卓「设置→蓝牙」配对后会留在已保存设备
  {
    BLESecurity* sec = new BLESecurity();
    sec->setAuthenticationMode(ESP_LE_AUTH_BOND);
    sec->setCapability(ESP_IO_CAP_NONE);
    sec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  }

  gServer = BLEDevice::createServer();
  gServer->setCallbacks(new ServerCallbacks());

  // 标准设备信息服务，便于系统/调试工具识别
  {
    BLEService* dis = gServer->createService(BLEUUID((uint16_t)0x180A));
    BLECharacteristic* manu = dis->createCharacteristic(
        BLEUUID((uint16_t)0x2A29), BLECharacteristic::PROPERTY_READ);
    manu->setValue("Saoti");
    BLECharacteristic* model = dis->createCharacteristic(
        BLEUUID((uint16_t)0x2A24), BLECharacteristic::PROPERTY_READ);
    model->setValue("Guajian");
    dis->start();
  }

  BLEService* svc = gServer->createService(BLEUUID(SAOTI_BLE_SERVICE_UUID), 30);

  gStatus = svc->createCharacteristic(
      SAOTI_BLE_STATUS_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  gStatus->addDescriptor(new BLE2902());

  gCommand = svc->createCharacteristic(SAOTI_BLE_COMMAND_UUID,
                                       BLECharacteristic::PROPERTY_WRITE |
                                           BLECharacteristic::PROPERTY_WRITE_NR);
  gCommand->setCallbacks(new CommandCallbacks());

  gAnswer = svc->createCharacteristic(
      SAOTI_BLE_ANSWER_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  gAnswer->addDescriptor(new BLE2902());

  gThumb = svc->createCharacteristic(SAOTI_BLE_THUMB_UUID,
                                     BLECharacteristic::PROPERTY_NOTIFY);
  gThumb->addDescriptor(new BLE2902());

  gEvent = svc->createCharacteristic(SAOTI_BLE_EVENT_UUID,
                                     BLECharacteristic::PROPERTY_NOTIFY);
  gEvent->addDescriptor(new BLE2902());

  gStatus->setValue("{}");
  gAnswer->setValue("");
  svc->start();

  startAdvInternal();
  gStarted = true;
  gLastAdvKickMs = millis();
  Serial.printf("[BLE] ready proto=%d\n", kProtoVersion);
  return true;
}

void restartAdvertising() {
  if (!gStarted) {
    return;
  }
  WiFi.mode(WIFI_OFF);
  // 即使已连接也刷新广播，保证可被重新发现
  startAdvInternal();
}

void disconnectAll() {
  if (!gStarted || !gServer) {
    return;
  }
  if (gConnected) {
    const uint16_t id = gServer->getConnId();
    Serial.printf("[BLE] disconnect connId=%u\n", static_cast<unsigned>(id));
    gServer->disconnect(id);
    gConnected = false;
    delay(150);
  }
  startAdvInternal();
}

bool isReady() { return gStarted; }

const char* advName() { return gStarted ? gAdvName : ""; }

bool selfCheck() {
  if (!gStarted) {
    return begin();
  }
  WiFi.mode(WIFI_OFF);
  if (!gConnected) {
    startAdvInternal();
  }
  gLastAdvKickMs = millis();
  Serial.printf("[BLE] selfcheck ok name=%s connected=%d\n", gAdvName,
                gConnected ? 1 : 0);
  return true;
}

void loop() {
  if (!gStarted) {
    return;
  }
  if (!gPendingEvent.isEmpty() && gEvent && gConnected) {
    notifyText(gEvent, gPendingEvent);
    gPendingEvent = "";
  }
  if (gNeedAnswer) {
    gNeedAnswer = false;
    pushAnswer();
  }
  if (gNeedStatus || (gConnected && millis() - gLastStatusMs > 1500)) {
    gNeedStatus = false;
    gLastStatusMs = millis();
    pushStatus();
  }
  // 未连接时周期性重启广播，防止被 WiFi/休眠弄丢
  if (!gConnected && millis() - gLastAdvKickMs > 8000) {
    gLastAdvKickMs = millis();
    startAdvInternal();
  }
}

void sendThumbJpeg(const uint8_t* data, size_t len) {
  if (!gThumb || !gConnected) {
    return;
  }
  if (!data || len < 400) {
    const char* err = "{\"type\":\"thumb\",\"ok\":false}";
    notifyRaw(gEvent, reinterpret_cast<const uint8_t*>(err), strlen(err));
    return;
  }
  uint8_t hdr[8];
  hdr[0] = 'T';
  hdr[1] = 'H';
  hdr[2] = 'M';
  hdr[3] = 'B';
  const uint32_t n = static_cast<uint32_t>(len);
  hdr[4] = static_cast<uint8_t>(n & 0xFF);
  hdr[5] = static_cast<uint8_t>((n >> 8) & 0xFF);
  hdr[6] = static_cast<uint8_t>((n >> 16) & 0xFF);
  hdr[7] = static_cast<uint8_t>((n >> 24) & 0xFF);
  gThumb->setValue(hdr, sizeof(hdr));
  gThumb->notify();
  delay(10);
  notifyRaw(gThumb, data, len);
  Serial.printf("[BLE] thumb %u bytes\n", static_cast<unsigned>(n));
}

bool isConnected() { return gConnected; }

void setPhase(const char* phase) {
  if (!phase) {
    phase = "idle";
  }
  strncpy(gPhase, phase, sizeof(gPhase) - 1);
  gPhase[sizeof(gPhase) - 1] = '\0';
  gNeedStatus = true;
  char buf[64];
  snprintf(buf, sizeof(buf), "{\"type\":\"state\",\"phase\":\"%s\"}", gPhase);
  gPendingEvent = buf;
}

const char* phase() { return gPhase; }

void notifyAnswerChanged() {
  gNeedAnswer = true;
  gNeedStatus = true;
}

void notifyEventJson(const char* json) {
  if (json && json[0]) {
    gPendingEvent = json;
  }
}

}  // namespace ble_gatt

#endif  // BLE_GATT_ENABLE
