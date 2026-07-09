#include "config.h"
#include "pins.h"
#include "modules/modem.h"

#if USE_WIFI_FALLBACK || STREAM_ENABLE
#include <HTTPClient.h>
#include <WiFi.h>
#endif

namespace {

HardwareSerial& kModemUart = Serial2;
bool kNetworkOpen = false;
String kActiveApn = CELL_APN;
int kLastCsq = -1;

void modemDrainRx() {
  while (kModemUart.available()) {
    (void)kModemUart.read();
  }
}

void modemLogHex(const char* label, const String& buf) {
  Serial.printf("[NET] %s hex (%u):", label, static_cast<unsigned>(buf.length()));
  for (size_t i = 0; i < buf.length() && i < 32; ++i) {
    Serial.printf(" %02X", static_cast<uint8_t>(buf[i]));
  }
  Serial.println();
}

void modemPreparePins() {
  pinMode(pins::MODEM_TX, OUTPUT);
  digitalWrite(pins::MODEM_TX, HIGH);
  pinMode(pins::MODEM_RX, INPUT_PULLUP);
  if (pins::MODEM_RESET >= 0) {
    pinMode(pins::MODEM_RESET, OUTPUT);
    digitalWrite(pins::MODEM_RESET, HIGH);
  }
  pinMode(pins::MODEM_PWRKEY, OUTPUT);
  digitalWrite(pins::MODEM_PWRKEY, HIGH);
}

void modemPowerOn() {
  modemPreparePins();
  // Optional hardware reset pulse (active low on many A7670 boards).
  if (pins::MODEM_RESET >= 0) {
    digitalWrite(pins::MODEM_RESET, LOW);
    delay(200);
    digitalWrite(pins::MODEM_RESET, HIGH);
    delay(500);
  }
  digitalWrite(pins::MODEM_PWRKEY, HIGH);
  delay(300);
  // SIMCom A7670: PWRKEY low >= ~1s to power on.
  digitalWrite(pins::MODEM_PWRKEY, LOW);
  delay(1200);
  digitalWrite(pins::MODEM_PWRKEY, HIGH);
  Serial.println("[NET] PWRKEY done, wait boot...");
  delay(8000);
  modemDrainRx();
  delay(500);
  modemDrainRx();
}

bool modemWaitToken(const char* token, uint32_t timeoutMs, String* out) {
  String buf;
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (kModemUart.available()) {
      const char c = static_cast<char>(kModemUart.read());
      Serial.write(c);
      buf += c;
      if (token && buf.indexOf(token) >= 0) {
        if (out) {
          *out = buf;
        }
        return true;
      }
      if (buf.indexOf("ERROR") >= 0) {
        if (out) {
          *out = buf;
        }
        return false;
      }
    }
    delay(10);
  }
  if (out) {
    *out = buf;
  }
  return false;
}

bool modemSendAt(const char* cmd, uint32_t timeoutMs, String* out = nullptr) {
  modemDrainRx();
  if (cmd && cmd[0] != '\0') {
    kModemUart.print(cmd);
    kModemUart.print("\r\n");
    Serial.printf("[NET] >> %s\n", cmd);
  }
  String buf;
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (kModemUart.available()) {
      const char c = static_cast<char>(kModemUart.read());
      Serial.write(c);
      buf += c;
      if (buf.indexOf("OK") >= 0 || buf.indexOf("ERROR") >= 0) {
        if (out) {
          *out = buf;
        }
        return buf.indexOf("OK") >= 0;
      }
    }
    delay(10);
  }
  if (buf.length() > 0) {
    modemLogHex("partial", buf);
  } else if (cmd) {
    Serial.println("[NET] no bytes from module");
  }
  if (out) {
    *out = buf;
  }
  return false;
}

bool modemProbePins(uint32_t baud, int rxPin, int txPin) {
  kModemUart.end();
  kModemUart.begin(baud, SERIAL_8N1, rxPin, txPin);
  delay(200);
  Serial.printf("[NET] probe TX=%d RX=%d @ %lu\n", txPin, rxPin,
                static_cast<unsigned long>(baud));
  for (int i = 0; i < 2; ++i) {
    if (modemSendAt("AT", 2500)) {
      Serial.printf("[NET] AT OK TX=%d RX=%d @ %lu\n", txPin, rxPin,
                    static_cast<unsigned long>(baud));
      return true;
    }
    delay(400);
  }
  return false;
}

bool modemProbeAll() {
  // pair = {rxPin, txPin}; also try swapped TX/RX and ASSEMBLY vs alt pins.
  const uint32_t bauds[] = {115200, 9600, 57600, 460800, 921600};
  const int pairs[][2] = {
      {pins::MODEM_RX, pins::MODEM_TX},          // 47/21 (default, away from USB UART)
      {pins::MODEM_TX, pins::MODEM_RX},          // swapped
      {pins::MODEM_RX_ALT, pins::MODEM_TX_ALT},  // 44/43 (ASSEMBLY old table)
      {pins::MODEM_TX_ALT, pins::MODEM_RX_ALT},  // alt swapped
  };
  for (const auto& pair : pairs) {
    for (uint32_t baud : bauds) {
      if (modemProbePins(baud, pair[0], pair[1])) {
        modemSendAt("ATE0", 2000);
        modemSendAt("AT+IFC=0,0", 2000);
        return true;
      }
    }
  }
  return false;
}

bool modemTryApn(const char* apn) {
  Serial.printf("[NET] try APN=%s\n", apn);
  modemSendAt("AT+NETCLOSE", 5000);
  delay(500);
  const String apnCmd = String("AT+CGDCONT=1,\"IP\",\"") + apn + "\"";
  if (!modemSendAt(apnCmd.c_str(), 5000)) {
    return false;
  }
  // Some firmwares need explicit PDP activate before NETOPEN.
  modemSendAt("AT+CGACT=1,1", 15000);
  if (!modemSendAt("AT+NETOPEN", 45000)) {
    return false;
  }
  String ip;
  modemSendAt("AT+IPADDR", 5000, &ip);
  modemSendAt("AT+CGPADDR=1", 5000);
  Serial.printf("[NET] network open with APN=%s\n", apn);
  return true;
}

bool modemEnsureNetwork() {
  if (kNetworkOpen) {
    return true;
  }
  modemSendAt("ATE0", 2000);
  modemSendAt("AT+CPIN?", 8000);
  String csqResp;
  modemSendAt("AT+CSQ", 3000, &csqResp);
  {
    const int idx = csqResp.indexOf("+CSQ:");
    if (idx >= 0) {
      kLastCsq = csqResp.substring(idx + 5).toInt();
    }
  }
  modemSendAt("AT+COPS?", 5000);
  modemSendAt("AT+CREG?", 3000);
  modemSendAt("AT+CGREG?", 3000);
  modemSendAt("AT+CEREG?", 3000);
  if (!modemSendAt("AT+CGATT=1", 20000)) {
    Serial.println("[NET] CGATT failed");
  }

  // Prefer runtime APN (set from Mac App), then compile-time fallbacks.
  const char* apns[] = {kActiveApn.c_str(), CELL_APN, CELL_APN_FALLBACKS};
  for (const char* apn : apns) {
    if (!apn || apn[0] == '\0') {
      continue;
    }
    if (modemTryApn(apn)) {
      kNetworkOpen = true;
      kActiveApn = apn;
      return true;
    }
  }
  Serial.println("[NET] all APN attempts failed");
  return false;
}

int parseHttpActionCode(const String& resp) {
  const int idx = resp.indexOf("+HTTPACTION:");
  if (idx < 0) {
    return -1;
  }
  const int comma1 = resp.indexOf(',', idx);
  const int comma2 = resp.indexOf(',', comma1 + 1);
  if (comma1 < 0 || comma2 < 0) {
    return -1;
  }
  return resp.substring(comma1 + 1, comma2).toInt();
}

bool modemHttpPost(const uint8_t* data, size_t len, int* httpCode) {
  const String url = String("http://") + UPLOAD_HOST + UPLOAD_PATH;
  if (!modemSendAt("AT+HTTPINIT", 5000)) {
    return false;
  }
  if (!modemSendAt((String("AT+HTTPPARA=\"URL\",\"") + url + "\"").c_str(), 5000)) {
    modemSendAt("AT+HTTPTERM", 3000);
    return false;
  }
  modemSendAt("AT+HTTPPARA=\"CONTENT\",\"application/octet-stream\"", 5000);

  const String dataCmd =
      String("AT+HTTPDATA=") + String(static_cast<unsigned>(len)) + ",60000";
  if (!modemSendAt(dataCmd.c_str(), 5000)) {
    modemSendAt("AT+HTTPTERM", 3000);
    return false;
  }
  if (!modemWaitToken("DOWNLOAD", 10000, nullptr)) {
    modemSendAt("AT+HTTPTERM", 3000);
    return false;
  }
  if (!modemWaitToken(">", 10000, nullptr)) {
    modemSendAt("AT+HTTPTERM", 3000);
    return false;
  }

  kModemUart.write(data, len);
  Serial.printf("[NET] >> [%u bytes binary]\n", static_cast<unsigned>(len));
  if (!modemWaitToken("OK", 60000, nullptr)) {
    modemSendAt("AT+HTTPTERM", 3000);
    return false;
  }

  String actionResp;
  if (!modemSendAt("AT+HTTPACTION=1", 90000, &actionResp)) {
    modemSendAt("AT+HTTPTERM", 3000);
    return false;
  }
  if (actionResp.indexOf("+HTTPACTION:") < 0) {
    modemWaitToken("+HTTPACTION:", 30000, &actionResp);
  }

  const int code = parseHttpActionCode(actionResp);
  if (httpCode) {
    *httpCode = code;
  }
  modemSendAt("AT+HTTPTERM", 5000);
  return code >= 200 && code < 400;
}

}  // namespace

void prepareModemPins() { modemPreparePins(); }

void Modem::wifiBegin() {
#if USE_WIFI_FALLBACK
  wifiReady_ = false;
  if (strlen(WIFI_SSID) == 0 || strcmp(WIFI_SSID, "YOUR_SSID") == 0) {
    Serial.println("[NET] WiFi skipped (SSID not configured)");
    return;
  }
  WiFi.mode(WIFI_STA);
  Serial.printf("[NET] WiFi connecting %s ...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
#endif
}

bool Modem::wifiEnsureConnected() {
#if USE_WIFI_FALLBACK
  if (strlen(WIFI_SSID) == 0 || strcmp(WIFI_SSID, "YOUR_SSID") == 0) {
    wifiReady_ = false;
    return false;
  }
  if (WiFi.status() == WL_CONNECTED) {
    wifiReady_ = true;
    return true;
  }
  const wl_status_t st = WiFi.status();
  if (st == WL_DISCONNECTED || st == WL_CONNECTION_LOST ||
      st == WL_NO_SSID_AVAIL) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
  const uint32_t start = millis();
  while (millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiReady_ = true;
      Serial.printf("[NET] WiFi OK %s\n", WiFi.localIP().toString().c_str());
      return true;
    }
    delay(100);
  }
  wifiReady_ = false;
  Serial.println("[NET] WiFi FAIL");
  return false;
#else
  wifiReady_ = false;
  return false;
#endif
}

bool Modem::ensureInitialized() {
  if (!initialized_) {
    initialized_ = true;
    begin();
  }
  return initialized_;
}

bool Modem::scanUartPins() {
#if USE_MOCK_MODEM
  Serial.println("[NET] scan skipped (mock modem)");
  return false;
#else
  Serial.println("[NET] ==== UART full pin scan start ====");
  Serial.println("[NET] tip: 主控板红/蓝灯≠4G模块灯；模块常只有电源红灯");
  modemPowerOn();

  // 避开摄像头 DVP/SCCB(4-13,15-18)、PWRKEY(41)、RESET(42)
  const int cands[] = {1, 2, 3, 14, 19, 20, 21, 38, 39, 40, 43, 44, 45, 46, 47, 48};
  const uint32_t bauds[] = {115200, 9600};
  int hits = 0;

  // Phase A: 只听 RX —— 开机后模块常会吐启动日志
  Serial.println("[NET] phase A: listen RX for boot noise...");
  for (int rx : cands) {
    const int dummyTx = (rx == 21) ? 47 : 21;
    kModemUart.end();
    kModemUart.begin(115200, SERIAL_8N1, rx, dummyTx);
    delay(30);
    modemDrainRx();
    delay(200);
    int n = 0;
    uint8_t sample[8] = {};
    while (kModemUart.available() && n < 64) {
      const int b = kModemUart.read();
      if (n < 8) {
        sample[n] = static_cast<uint8_t>(b);
      }
      ++n;
    }
    if (n > 0) {
      Serial.printf("[NET] RX activity GPIO%d bytes=%d sample=", rx, n);
      for (int i = 0; i < n && i < 8; ++i) {
        Serial.printf("%02X ", sample[i]);
      }
      Serial.println();
    }
  }

  // Phase B: 优先常见组合，再穷举
  Serial.println("[NET] phase B: priority pairs then brute-force...");
  const int priority[][2] = {
      {47, 21}, {21, 47}, {44, 43}, {43, 44}, {48, 47}, {47, 48},
      {14, 21}, {21, 14}, {2, 1}, {1, 2}, {19, 20}, {20, 19},
  };
  auto tryPair = [&](int rx, int tx, uint32_t baud) -> bool {
    kModemUart.end();
    kModemUart.begin(baud, SERIAL_8N1, rx, tx);
    delay(30);
    modemDrainRx();
    kModemUart.print("AT\r\n");
    String buf;
    const uint32_t start = millis();
    while (millis() - start < 160) {
      while (kModemUart.available()) {
        buf += static_cast<char>(kModemUart.read());
        if (buf.indexOf("OK") >= 0) {
          Serial.printf("[NET] *** HIT AT OK  ESP_TX=GPIO%d -> modRX | "
                        "ESP_RX=GPIO%d <- modTX  @ %lu ***\n",
                        tx, rx, static_cast<unsigned long>(baud));
          ++hits;
          modemSendAt("ATE0", 1500);
          modemSendAt("ATI", 2000);
          modemSendAt("AT+CPIN?", 5000);
          modemSendAt("AT+CSQ", 3000);
          cellReady_ = true;
          Serial.println("[NET] ==== UART scan SUCCESS ====");
          return true;
        }
      }
      delay(5);
    }
    return false;
  };

  for (uint32_t baud : bauds) {
    for (const auto& p : priority) {
      if (tryPair(p[0], p[1], baud)) {
        return true;
      }
    }
  }
  for (uint32_t baud : bauds) {
    for (int rx : cands) {
      for (int tx : cands) {
        if (rx == tx) {
          continue;
        }
        if (tryPair(rx, tx, baud)) {
          return true;
        }
      }
    }
  }

  Serial.printf("[NET] ==== UART scan FAIL (hits=%d) ====\n", hits);
  Serial.println("[NET] 请确认: 模块5V/GND/PEN、TX-RX交叉、天线；红灯常亮仅表示上电");
  return false;
#endif
}

bool Modem::runDiagnostics() {
  ensureInitialized();
  if (!cellReady_) {
    Serial.println("[NET] diag: UART not ready — auto full pin scan...");
    if (scanUartPins()) {
      // fall through to SIM/network probe
    } else {
      Serial.println("[NET] expected wiring: ESP21→模RX, ESP47←模TX (勿占用电脑串口43/44)");
      return false;
    }
  }
  Serial.println("[NET] diag: SIM/network probe (蜗牛移动)...");
  modemSendAt("ATI", 3000);
  modemSendAt("AT+CPIN?", 8000);
  modemSendAt("AT+CIMI", 5000);
  modemSendAt("AT+CCID", 5000);
  modemSendAt("AT+CSQ", 3000);
  modemSendAt("AT+COPS?", 5000);
  modemSendAt("AT+CREG?", 3000);
  modemSendAt("AT+CGATT?", 5000);
  kNetworkOpen = false;
  const bool net = modemEnsureNetwork();
  Serial.printf("[NET] diag: network %s (APN primary=%s)\n", net ? "OK" : "FAIL",
                CELL_APN);
  return net;
}

bool Modem::begin() {
#if USE_MOCK_MODEM
  return beginMock();
#else
  return beginHardware();
#endif
}

bool Modem::isReady() const { return isCellReady() || isWifiReady(); }

bool Modem::isCellReady() const { return cellReady_; }

bool Modem::isWifiReady() const { return wifiReady_; }

String Modem::activeApn() const { return kActiveApn; }

bool Modem::setApn(const char* apn) {
  if (!apn || apn[0] == '\0' || strlen(apn) > 31) {
    return false;
  }
  kActiveApn = apn;
  apn_ = apn;
  kNetworkOpen = false;
  Serial.printf("[NET] APN set to %s\n", apn);
  return true;
}

int Modem::lastCsq() const { return kLastCsq; }

bool Modem::beginMock() {
  Serial.println("[NET] mock modem ready");
  cellReady_ = true;
  wifiBegin();
  wifiEnsureConnected();
  Serial.printf("[NET] status: WiFi=%s 4G=%s\n",
                isWifiReady() ? "OK" : "FAIL", isCellReady() ? "OK" : "FAIL");
  return isReady();
}

bool Modem::beginHardware() {
#if MODEM_PC_PASSTHROUGH
  pinMode(pins::MODEM_TX, INPUT);
  pinMode(pins::MODEM_RX, INPUT);
  modemPowerOn();
  cellReady_ = false;
  wifiBegin();
  wifiEnsureConnected();
  Serial.printf("[NET] status: WiFi=%s 4G=%s\n",
                isWifiReady() ? "OK" : "FAIL", isCellReady() ? "OK" : "FAIL");
  return isReady();
#else
  wifiBegin();
  if (cellReady_) {
    Serial.println("[NET] A7670G already ready (from UART scan)");
    Serial.printf("[NET] status: WiFi=%s 4G=%s\n",
                  isWifiReady() ? "OK" : "FAIL", isCellReady() ? "OK" : "FAIL");
    return isReady();
  }
  Serial.println("[NET] A7670G init...");
  Serial.printf("[NET] UART TX=%d RX=%d PWRKEY=%d APN=%s\n", pins::MODEM_TX,
                pins::MODEM_RX, pins::MODEM_PWRKEY, CELL_APN);
  modemPowerOn();
  if (!modemProbeAll()) {
    Serial.println("[NET] modem not responding (check 5V/PEN/TX/RX)");
    cellReady_ = false;
  } else {
    modemSendAt("ATE0", 2000);
    cellReady_ = true;
    Serial.println("[NET] A7670G UART ready");
    modemSendAt("AT+CPIN?", 8000);
    modemSendAt("AT+CSQ", 3000);
    modemSendAt("AT+COPS?", 5000);
  }
  wifiEnsureConnected();
  Serial.printf("[NET] status: WiFi=%s 4G=%s\n",
                isWifiReady() ? "OK" : "FAIL", isCellReady() ? "OK" : "FAIL");
  return isReady();
#endif
}

UploadResult Modem::upload(const uint8_t* data, size_t len) {
#if USE_MOCK_MODEM
  return uploadMock(data, len);
#else
  return uploadWithFailover(data, len);
#endif
}

UploadResult Modem::uploadWithFailover(const uint8_t* data, size_t len) {
  UploadResult r;
  ensureInitialized();

  const bool wifiConfigured =
      strlen(WIFI_SSID) > 0 && strcmp(WIFI_SSID, "YOUR_SSID") != 0;

#if NET_PREFER_WIFI
  if (wifiConfigured && (isWifiReady() || wifiEnsureConnected())) {
    Serial.println("[NET] upload via WiFi");
    r = uploadWifi(data, len);
    if (r.ok) {
      return r;
    }
    if (isCellReady()) {
      Serial.println("[NET] failover to 4G");
      r = uploadHardware(data, len);
      if (r.ok) {
        return r;
      }
    }
    r.error = r.error ? r.error : "upload failed";
    return r;
  }
  if (isCellReady()) {
    Serial.println("[NET] upload via 4G");
    r = uploadHardware(data, len);
    if (r.ok) {
      return r;
    }
    if (wifiEnsureConnected()) {
      Serial.println("[NET] failover to WiFi");
      return uploadWifi(data, len);
    }
    return r;
  }
#else
  if (isCellReady()) {
    Serial.println("[NET] upload via 4G");
    r = uploadHardware(data, len);
    if (r.ok) {
      return r;
    }
    if (wifiEnsureConnected()) {
      Serial.println("[NET] failover to WiFi");
      return uploadWifi(data, len);
    }
    return r;
  }
  if (wifiEnsureConnected()) {
    Serial.println("[NET] upload via WiFi");
    return uploadWifi(data, len);
  }
#endif

  r.ok = false;
  r.error = "no network";
  return r;
}

UploadResult Modem::uploadMock(const uint8_t* data, size_t len) {
#if USE_WIFI_FALLBACK
  if (WiFi.status() == WL_CONNECTED) {
    return uploadWifi(data, len);
  }
#endif
  UploadResult r;
  r.ok = true;
  r.httpCode = 200;
  Serial.printf("[NET] mock upload OK (%u bytes)\n", static_cast<unsigned>(len));
  (void)data;
  return r;
}

UploadResult Modem::uploadHardware(const uint8_t* data, size_t len) {
  UploadResult r;
  if (!cellReady_) {
    r.error = "modem not ready";
    return r;
  }
  if (!modemEnsureNetwork()) {
    r.error = "network open failed";
    return r;
  }
  int code = 0;
  if (!modemHttpPost(data, len, &code)) {
    r.httpCode = code;
    r.error = "4G HTTP upload failed";
    return r;
  }
  r.httpCode = code;
  r.ok = true;
  Serial.printf("[NET] 4G upload HTTP %d (%u bytes)\n", code,
                static_cast<unsigned>(len));
  return r;
}

UploadResult Modem::uploadWifi(const uint8_t* data, size_t len) {
#if USE_WIFI_FALLBACK
  UploadResult r;
  HTTPClient http;
  String url = String("http://") + UPLOAD_HOST + UPLOAD_PATH;
  http.begin(url);
  http.addHeader("Content-Type", "application/octet-stream");
  int code = http.POST(const_cast<uint8_t*>(data), len);
  r.httpCode = code;
  r.ok = (code > 0 && code < 400);
  if (!r.ok) {
    r.error = "http upload failed";
  }
  Serial.printf("[NET] WiFi upload HTTP %d\n", code);
  http.end();
  return r;
#else
  UploadResult r;
  r.ok = false;
  r.error = "WiFi fallback disabled";
  (void)data;
  (void)len;
  return r;
#endif
}

bool Modem::startStreamingSoftAp() {
#if STREAM_ENABLE
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(STREAM_AP_SSID, STREAM_AP_PASS)) {
    Serial.println("[NET] SoftAP start failed");
    streamingApMode_ = false;
    wifiReady_ = false;
    return false;
  }
  streamingApMode_ = true;
  wifiReady_ = true;
  Serial.printf("[NET] SoftAP %s / %s IP %s\n", STREAM_AP_SSID, STREAM_AP_PASS,
                WiFi.softAPIP().toString().c_str());
  return true;
#else
  return false;
#endif
}

bool Modem::ensureWifiForStreaming() {
#if STREAM_ENABLE
  streamingApMode_ = false;

  if (strlen(WIFI_SSID) > 0 && strcmp(WIFI_SSID, "YOUR_SSID") != 0) {
    if (wifiEnsureConnected()) {
      Serial.printf("[NET] streaming via STA %s\n",
                    WiFi.localIP().toString().c_str());
      return true;
    }
    Serial.println("[NET] STA failed, fallback to SoftAP");
  } else {
    Serial.println("[NET] WiFi not configured, starting SoftAP");
  }
  return startStreamingSoftAp();
#else
  return false;
#endif
}

String Modem::streamingIp() const {
#if STREAM_ENABLE
  if (streamingApMode_) {
    return WiFi.softAPIP().toString();
  }
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP().toString();
  }
#endif
  return String("0.0.0.0");
}
