#include "config.h"
#include "pins.h"
#include "modules/modem.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#if USE_WIFI_FALLBACK || STREAM_ENABLE || NET_CELL_ONLY
#include <WiFi.h>
#endif
#if USE_WIFI_FALLBACK
#include <HTTPClient.h>
#endif

// ESP-TLS（modem_esp_tls.cpp）使用的 AT 原语
bool modemEspTlsHttpsPost(const char* url, const char* bearer, const uint8_t* data,
                          size_t len, int* httpCode, String* bodyOut);

namespace modem_at {

HardwareSerial& kModemUart = Serial2;
bool kNetworkOpen = false;
String kActiveApn = CELL_APN;
int kLastCsq = -1;
SemaphoreHandle_t kModemMtx = nullptr;

void modemLockInit() {
  if (!kModemMtx) {
    kModemMtx = xSemaphoreCreateRecursiveMutex();
  }
}

bool lockBus(uint32_t timeoutMs) {
  modemLockInit();
  if (!kModemMtx) {
    return false;
  }
  return xSemaphoreTakeRecursive(kModemMtx, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void unlockBus() {
  if (kModemMtx) {
    xSemaphoreGiveRecursive(kModemMtx);
  }
}

struct BusGuard {
  bool ok;
  explicit BusGuard(uint32_t timeoutMs = 180000) : ok(lockBus(timeoutMs)) {
    if (!ok) {
      Serial.println("[NET] modem bus busy — command dropped");
    }
  }
  ~BusGuard() {
    if (ok) {
      unlockBus();
    }
  }
  explicit operator bool() const { return ok; }
};

HardwareSerial& uart() { return kModemUart; }

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
  // UART 空闲高 + PEN 使能（FS-MCore 店家要求 PEN 常接 3.3V）
  pinMode(pins::MODEM_TX, OUTPUT);
  digitalWrite(pins::MODEM_TX, HIGH);
  pinMode(pins::MODEM_RX, INPUT_PULLUP);
  if (pins::MODEM_PEN >= 0) {
    pinMode(pins::MODEM_PEN, OUTPUT);
    digitalWrite(pins::MODEM_PEN, HIGH);
    Serial.printf("[NET] PEN GPIO%d = HIGH (enable)\n", pins::MODEM_PEN);
  }
  if (pins::MODEM_PWRKEY >= 0) {
    pinMode(pins::MODEM_PWRKEY, OUTPUT);
    digitalWrite(pins::MODEM_PWRKEY, HIGH);
  }
  if (pins::MODEM_RESET >= 0) {
    pinMode(pins::MODEM_RESET, OUTPUT);
    digitalWrite(pins::MODEM_RESET, HIGH);
  }
}

void modemPulsePwk() {
  if (pins::MODEM_PWRKEY < 0) {
    return;
  }
  digitalWrite(pins::MODEM_PWRKEY, HIGH);
  delay(300);
  digitalWrite(pins::MODEM_PWRKEY, LOW);
  delay(1200);
  digitalWrite(pins::MODEM_PWRKEY, HIGH);
  Serial.printf("[NET] PWK GPIO%d pulse done, wait boot...\n", pins::MODEM_PWRKEY);
}

void modemPowerOn() {
  // 只拉 PEN；不默认脉冲 PWK（模块已开机时脉冲会反复重启 → AT 无响应）
  modemPreparePins();
  Serial.println("[NET] PEN on, wait UART (no PWK yet)...");
  delay(3000);
  modemDrainRx();
  delay(200);
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
        // 等齐 +CME ERROR: xx 整行，避免只看到 ERROR 截断
        delay(80);
        while (kModemUart.available()) {
          buf += static_cast<char>(kModemUart.read());
        }
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
  // 递归锁：长会话（TLS/拨号）外层已持锁时仍可嵌套
  BusGuard guard(timeoutMs + 5000);
  if (!guard) {
    return false;
  }
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
        if (buf.indexOf("ERROR") >= 0) {
          delay(80);
          while (kModemUart.available()) {
            const char e = static_cast<char>(kModemUart.read());
            Serial.write(e);
            buf += e;
          }
        }
        if (out) {
          *out = buf;
        }
        return buf.indexOf("OK") >= 0 && buf.indexOf("ERROR") < 0;
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
  delay(MODEM_FAST_PROBE ? 80 : 200);
  Serial.printf("[NET] probe TX=%d RX=%d @ %lu\n", txPin, rxPin,
                static_cast<unsigned long>(baud));
  const int tries = MODEM_FAST_PROBE ? 1 : 2;
  const uint32_t atTimeout = MODEM_FAST_PROBE ? 800 : 2500;
  for (int i = 0; i < tries; ++i) {
    if (modemSendAt("AT", atTimeout)) {
      Serial.printf("[NET] AT OK TX=%d RX=%d @ %lu\n", txPin, rxPin,
                    static_cast<unsigned long>(baud));
      return true;
    }
    if (!MODEM_FAST_PROBE) {
      delay(400);
    }
  }
  return false;
}

bool modemProbeAll() {
  const int primaryRx = pins::MODEM_RX;
  const int primaryTx = pins::MODEM_TX;

  Serial.printf("[NET] probe primary TX=%d RX=%d @ 115200\n", primaryTx,
                primaryRx);
  if (modemProbePins(115200, primaryRx, primaryTx)) {
    modemSendAt("ATE0", 2000);
    modemSendAt("AT+IFC=0,0", 2000);
    return true;
  }

  // 交叉接反再试一次
  if (modemProbePins(115200, primaryTx, primaryRx)) {
    modemSendAt("ATE0", 2000);
    modemSendAt("AT+IFC=0,0", 2000);
    return true;
  }

#if !MODEM_FAST_PROBE
  const uint32_t primaryBauds[] = {9600, 57600};
  const int pairs[][2] = {
      {primaryRx, primaryTx},
      {primaryTx, primaryRx},
      {pins::MODEM_RX_ALT, pins::MODEM_TX_ALT},
      {pins::MODEM_TX_ALT, pins::MODEM_RX_ALT},
  };
  for (size_t pi = 0; pi < sizeof(pairs) / sizeof(pairs[0]); ++pi) {
    for (uint32_t baud : primaryBauds) {
      if (modemProbePins(baud, pairs[pi][0], pairs[pi][1])) {
        modemSendAt("ATE0", 2000);
        modemSendAt("AT+IFC=0,0", 2000);
        return true;
      }
    }
  }
#endif
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

String modemExtractDigits(const String& s) {
  String out;
  for (size_t i = 0; i < s.length(); ++i) {
    const char c = s[i];
    if (c >= '0' && c <= '9') {
      out += c;
    }
  }
  return out;
}

// 蜗牛是转售品牌：按 IMSI 前缀选底层运营商 APN
void modemApplyApnForImsi(const String& imsi) {
  if (imsi.length() < 5) {
    return;
  }
  const String p = imsi.substring(0, 5);
  const char* apn = CELL_APN;
  const char* tag = "default";
  if (p == "46000" || p == "46002" || p == "46004" || p == "46007" ||
      p == "46008") {
    apn = "cmiot";
    tag = "移动物联(蜗牛常见)";
  } else if (p == "46001" || p == "46006" || p == "46009") {
    apn = "wonet";
    tag = "联通";
  } else if (p == "46003" || p == "46005" || p == "46011") {
    apn = "ctnet";
    tag = "电信";
  }
  kActiveApn = apn;
  Serial.printf("[NET] IMSI=%s → %s APN=%s\n", imsi.c_str(), tag, apn);
}

bool modemWaitSimReady(uint32_t timeoutMs = 45000) {
  modemSendAt("AT+CMEE=2", 2000);  // 详细 CME 错误文本
  modemSendAt("AT+CFUN=1", 10000);
  const uint32_t start = millis();
  int attempt = 0;
  while (millis() - start < timeoutMs) {
    ++attempt;
    String resp;
    modemSendAt("AT+CPIN?", 5000, &resp);
    if (resp.indexOf("READY") >= 0) {
      Serial.printf("[NET] SIM READY (attempt %d)\n", attempt);
      String imsiResp;
      modemSendAt("AT+CIMI", 5000, &imsiResp);
      const String imsi = modemExtractDigits(imsiResp);
      if (imsi.length() >= 15) {
        modemApplyApnForImsi(imsi.substring(0, 15));
      }
      modemSendAt("AT+CICCID", 5000);
      modemSendAt("AT+CCID", 5000);
      // 本机号码（很多物联卡 SIM 里未写入，可能为空）
      String cnum;
      modemSendAt("AT+CNUM", 5000, &cnum);
      if (cnum.indexOf("+CNUM:") >= 0) {
        Serial.printf("[NET] phone/CNUM raw: %s\n", cnum.c_str());
      } else {
        Serial.println("[NET] CNUM empty — 物联卡号码多在蜗牛 App/卡板印刷，SIM 内常无号");
      }
      return true;
    }
    if (resp.indexOf("not inserted") >= 0 || resp.indexOf("NOT INSERTED") >= 0) {
      Serial.printf("[NET] SIM not inserted (%d) — 重新插紧 Nano 卡\n", attempt);
    } else {
      Serial.printf("[NET] SIM not ready yet (%d): %s\n", attempt,
                    resp.length() ? resp.c_str() : "(empty)");
    }
    delay(2000);
  }
  Serial.println("[NET] SIM timeout — 卡座/方向/激活；蜗牛 App 开通流量后再测");
  return false;
}

bool modemWaitRegistered(uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    String r;
    modemSendAt("AT+CEREG?", 3000, &r);
    if (r.indexOf(",1") >= 0 || r.indexOf(",5") >= 0) {
      Serial.println("[NET] LTE registered");
      return true;
    }
    modemSendAt("AT+CREG?", 3000, &r);
    if (r.indexOf(",1") >= 0 || r.indexOf(",5") >= 0) {
      Serial.println("[NET] CS registered");
      return true;
    }
    modemSendAt("AT+CSQ", 3000);
    delay(3000);
  }
  Serial.println("[NET] register timeout (CREG/CEREG still searching)");
  return false;
}

bool modemEnsureNetwork() {
  if (kNetworkOpen) {
    // 复核 IP：NETOPEN 可能已掉，避免 ESP-TLS 空连
    String ip;
    if (modemSendAt("AT+IPADDR", 5000, &ip) && ip.indexOf("+IPADDR:") >= 0 &&
        ip.indexOf("0.0.0.0") < 0) {
      return true;
    }
    Serial.println("[NET] IP lost — reopen PDP");
    kNetworkOpen = false;
  }
  modemSendAt("ATE0", 2000);
  if (!modemWaitSimReady(45000)) {
    return false;
  }
  String csqResp;
  modemSendAt("AT+CSQ", 3000, &csqResp);
  {
    const int idx = csqResp.indexOf("+CSQ:");
    if (idx >= 0) {
      kLastCsq = csqResp.substring(idx + 5).toInt();
    }
  }
  modemSendAt("AT+COPS=0", 30000);
  modemSendAt("AT+COPS?", 5000);
  (void)modemWaitRegistered(60000);
  modemSendAt("AT+CREG?", 3000);
  modemSendAt("AT+CGREG?", 3000);
  modemSendAt("AT+CEREG?", 3000);
  if (!modemSendAt("AT+CGATT=1", 30000)) {
    Serial.println("[NET] CGATT failed");
  }

  // 蜗牛移动制式优先 cmiot → cmmtm → cmnet，再试编译期备用
  const char* apns[] = {kActiveApn.c_str(), "cmiot", "cmmtm", "cmnet", CELL_APN,
                        CELL_APN_FALLBACKS};
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

bool parseHttpAction(const String& resp, int* httpCode, int* dataLen) {
  const int idx = resp.indexOf("+HTTPACTION:");
  if (idx < 0) {
    return false;
  }
  const int comma1 = resp.indexOf(',', idx);
  const int comma2 = resp.indexOf(',', comma1 + 1);
  if (comma1 < 0) {
    return false;
  }
  if (httpCode) {
    if (comma2 >= 0) {
      *httpCode = resp.substring(comma1 + 1, comma2).toInt();
    } else {
      *httpCode = resp.substring(comma1 + 1).toInt();
    }
  }
  if (dataLen) {
    *dataLen = 0;
    if (comma2 >= 0) {
      *dataLen = resp.substring(comma2 + 1).toInt();
    }
  }
  return true;
}

int parseHttpActionCode(const String& resp) {
  int code = -1;
  parseHttpAction(resp, &code, nullptr);
  return code;
}

String extractHttpReadBody(const String& resp) {
  // 兼容：+HTTPREAD: N  / +HTTPREAD: DATA,N  之后跟正文
  int idx = resp.indexOf("+HTTPREAD:");
  if (idx < 0) {
    return "";
  }
  int lineEnd = resp.indexOf('\n', idx);
  if (lineEnd < 0) {
    return "";
  }
  String body = resp.substring(lineEnd + 1);
  // 去掉尾部 OK / ERROR
  const int okAt = body.lastIndexOf("\nOK");
  if (okAt >= 0) {
    body = body.substring(0, okAt);
  }
  const int errAt = body.lastIndexOf("\nERROR");
  if (errAt >= 0) {
    body = body.substring(0, errAt);
  }
  body.trim();
  // 偶发前缀空行
  while (body.startsWith("\r") || body.startsWith("\n")) {
    body.remove(0, 1);
  }
  return body;
}

bool modemHttpWriteBody(const uint8_t* data, size_t len, uint32_t timeoutMs) {
  const String dataCmd =
      String("AT+HTTPDATA=") + String(static_cast<unsigned>(len)) + "," +
      String(static_cast<unsigned>(timeoutMs));
  // HTTPDATA 常先回 DOWNLOAD/> 再收数据，不走普通 OK 判定
  modemDrainRx();
  kModemUart.print(dataCmd);
  kModemUart.print("\r\n");
  Serial.printf("[NET] >> %s\n", dataCmd.c_str());
  String prompt;
  const uint32_t start = millis();
  bool ready = false;
  while (millis() - start < 15000) {
    while (kModemUart.available()) {
      const char c = static_cast<char>(kModemUart.read());
      Serial.write(c);
      prompt += c;
      if (prompt.indexOf("DOWNLOAD") >= 0 || prompt.indexOf(">") >= 0) {
        ready = true;
        break;
      }
      if (prompt.indexOf("ERROR") >= 0) {
        return false;
      }
    }
    if (ready) {
      break;
    }
    delay(5);
  }
  if (!ready) {
    return false;
  }
  kModemUart.write(data, len);
  Serial.printf("[NET] >> [%u bytes body]\n", static_cast<unsigned>(len));
  return modemWaitToken("OK", timeoutMs, nullptr);
}

bool modemHttpPost(const uint8_t* data, size_t len, int* httpCode) {
  const String url = String("http://") + UPLOAD_HOST + UPLOAD_PATH;
  modemSendAt("AT+HTTPTERM", 3000);
  if (!modemSendAt("AT+HTTPINIT", 5000)) {
    return false;
  }
  if (!modemSendAt((String("AT+HTTPPARA=\"URL\",\"") + url + "\"").c_str(), 5000)) {
    modemSendAt("AT+HTTPTERM", 3000);
    return false;
  }
  modemSendAt("AT+HTTPPARA=\"CONTENT\",\"application/octet-stream\"", 5000);

  if (!modemHttpWriteBody(data, len, 60000)) {
    modemSendAt("AT+HTTPTERM", 3000);
    return false;
  }

  String actionResp;
  if (!modemSendAt("AT+HTTPACTION=1", 90000, &actionResp)) {
    // 可能先回 OK 再异步 +HTTPACTION
  }
  if (actionResp.indexOf("+HTTPACTION:") < 0) {
    modemWaitToken("+HTTPACTION:", 60000, &actionResp);
  }

  const int code = parseHttpActionCode(actionResp);
  if (httpCode) {
    *httpCode = code;
  }
  modemSendAt("AT+HTTPTERM", 5000);
  return code >= 200 && code < 400;
}

bool modemHttpsPostJson(const char* url, const char* bearer, const uint8_t* data,
                        size_t len, int* httpCode, String* bodyOut) {
  // ESP mbedTLS + 模块明文 TCP（绕过 A7670 内置 HTTPS 715）
  return modemEspTlsHttpsPost(url, bearer, data, len, httpCode, bodyOut);
}

void drainRx() { modemDrainRx(); }

bool sendAt(const char* cmd, uint32_t timeoutMs, String* out) {
  return modemSendAt(cmd, timeoutMs, out);
}

bool waitToken(const char* token, uint32_t timeoutMs, String* out) {
  BusGuard guard(timeoutMs + 5000);
  if (!guard) {
    return false;
  }
  return modemWaitToken(token, timeoutMs, out);
}

bool ensureNetwork() {
  BusGuard guard;
  if (!guard) {
    return false;
  }
  return modemEnsureNetwork();
}

}  // namespace modem_at

using namespace modem_at;

void prepareModemPins() { modemPreparePins(); }

void Modem::wifiBegin() {
#if USE_WIFI_FALLBACK
  if (WIFI_SSID[0] == '\0' || strcmp(WIFI_SSID, "YOUR_SSID") == 0) {
    Serial.println("[NET] WiFi STA skipped (SoftAP may already be up)");
    return;
  }
  wifiReady_ = false;
  WiFi.mode(WIFI_AP_STA);
  Serial.printf("[NET] WiFi connecting %s ...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
#endif
}

bool Modem::wifiEnsureConnected() {
#if USE_WIFI_FALLBACK
  if (WIFI_SSID[0] == '\0' || strcmp(WIFI_SSID, "YOUR_SSID") == 0) {
    if (streamingApMode_) {
      wifiReady_ = true;
      return true;
    }
    wifiReady_ = false;
    return false;
  }
  if (WiFi.status() == WL_CONNECTED) {
    wifiReady_ = true;
    return true;
  }
  // IDLE/失败等状态都要主动 begin，否则永远等不到连接
  WiFi.persistent(false);
  WiFi.mode(streamingApMode_ ? WIFI_AP_STA : WIFI_STA);
  Serial.printf("[NET] WiFi connecting %s ...\n", WIFI_SSID);
  WiFi.disconnect(false, true);
  delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  const uint32_t start = millis();
  while (millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    const wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
      wifiReady_ = true;
      Serial.printf("[NET] WiFi OK %s\n", WiFi.localIP().toString().c_str());
      return true;
    }
    delay(200);
  }
  wifiReady_ = streamingApMode_;
  Serial.printf("[NET] WiFi FAIL status=%d\n", static_cast<int>(WiFi.status()));
  return streamingApMode_;
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
  BusGuard guard;
  if (!guard) {
    return false;
  }
  Serial.println("[NET] ==== UART full pin scan start ====");
  Serial.println("[NET] tip: 主控板红/蓝灯≠4G模块灯；模块常只有电源红灯");
  modemPowerOn();

  // 避开摄像头 DVP/SCCB(4-13,15-18)；41/42 若曾接 PWK/RST 也避开乱扫
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
  Serial.println("[NET] 请确认: VIN≥5V/2A+共地、PEN=3V3/GPIO48、PWK=GPIO47、TX-RX交叉；"
                 "红灯=上电，蓝灯≈已开机；FreeAT/USB 可先验模块");
  return false;
#endif
}

bool Modem::dumpFirmwareInfo() {
  ensureInitialized();
  if (!cellReady_) {
    Serial.println("[NET] FW: UART not ready");
    return false;
  }
  BusGuard guard;
  if (!guard) {
    return false;
  }
  Serial.println("[NET] ==== modem firmware (ATI / SIMCOMATI) ====");
  modemSendAt("ATI", 3000);
  modemSendAt("AT+CGMR", 3000);
  modemSendAt("AT+SIMCOMATI", 5000);
  Serial.println("[NET] ==== end firmware info ====");
  Serial.println("[NET] 升级须匹配 Model=A7670G-LABE；勿刷 LASE/LLSE/其它型号包");
  return true;
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
  BusGuard guard;
  if (!guard) {
    return false;
  }
  Serial.println("[NET] diag: SIM/network probe (蜗牛→按IMSI选APN, 默认cmiot)...");
  modemSendAt("ATI", 3000);
  modemSendAt("AT+SIMCOMATI", 5000);
  modemSendAt("AT+CMEE=2", 2000);
  kNetworkOpen = false;
  const bool net = modemEnsureNetwork();
  Serial.printf("[NET] diag: network %s (APN primary=%s active=%s)\n",
                net ? "OK" : "FAIL", CELL_APN, kActiveApn.c_str());
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
#if USE_WIFI_FALLBACK
  wifiBegin();
  wifiEnsureConnected();
#endif
  Serial.printf("[NET] status: WiFi=%s 4G=%s\n",
                isWifiReady() ? "OK" : "FAIL", isCellReady() ? "OK" : "FAIL");
  return isReady();
#else
#if USE_WIFI_FALLBACK
  wifiBegin();
#else
  wifiReady_ = false;
  Serial.println("[NET] WiFi disabled (NET_CELL_ONLY) — AI/upload via 4G only");
#if defined(ESP32)
  // 彻底关掉 WiFi 射频，省电且避免误连
  WiFi.mode(WIFI_OFF);
#endif
#endif
  if (cellReady_) {
    Serial.println("[NET] A7670G already ready (from UART scan)");
    Serial.printf("[NET] status: WiFi=%s 4G=%s\n",
                  isWifiReady() ? "OK" : "FAIL", isCellReady() ? "OK" : "FAIL");
    return isReady();
  }
  Serial.println("[NET] FS-MCore-A7670G init...");
  Serial.printf("[NET] UART TX=%d RX=%d PEN=%d PWK=%d APN=%s\n", pins::MODEM_TX,
                pins::MODEM_RX, pins::MODEM_PEN, pins::MODEM_PWRKEY, CELL_APN);
  BusGuard guard;
  if (!guard) {
    cellReady_ = false;
    return false;
  }
  modemPowerOn();
  bool ok = modemProbeAll();
#if MODEM_PULSE_PWRKEY
  if (!ok && pins::MODEM_PWRKEY >= 0) {
    Serial.println("[NET] AT silent — try one PWK pulse...");
    modemPulsePwk();
    delay(MODEM_BOOT_WAIT_MS);
    modemDrainRx();
    ok = modemProbeAll();
  }
#endif
  if (!ok) {
    Serial.println("[NET] modem not responding — check VIN5V/共地/PEN→48高/PWK→47/"
                   "TX-RX交叉(21↔模RX,2↔模TX)；或用 FreeAT/USB 先验模块");
    cellReady_ = false;
  } else {
    modemSendAt("ATE0", 2000);
    cellReady_ = true;
    Serial.println("[NET] A7670 UART ready");
    delay(2000);
    (void)modemWaitSimReady(20000);
    modemSendAt("AT+CSQ", 3000);
    modemSendAt("AT+COPS?", 5000);
    // 开机即拨号，后面扫题更快
    (void)modemEnsureNetwork();
  }
#if USE_WIFI_FALLBACK
  if (streamingApMode_) {
    wifiReady_ = true;
  } else {
    wifiEnsureConnected();
  }
#endif
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

#if USE_WIFI_FALLBACK
  const bool wifiConfigured =
      strlen(WIFI_SSID) > 0 && strcmp(WIFI_SSID, "YOUR_SSID") != 0;

#if NET_PREFER_WIFI
  if (wifiConfigured && (isWifiReady() || wifiEnsureConnected())) {
    Serial.println("[NET] upload via WiFi");
    r = uploadWifi(data, len);
    r.via = "WiFi";
    if (r.ok) {
      return r;
    }
    if (isCellReady()) {
      Serial.println("[NET] failover to 4G");
      r = uploadHardware(data, len);
      r.via = "SIM";
      if (r.ok) {
        return r;
      }
    }
    r.error = r.error ? r.error : "upload failed";
    return r;
  }
#endif
  if (isCellReady()) {
    Serial.println("[NET] upload via 4G");
    r = uploadHardware(data, len);
    r.via = "SIM";
    if (r.ok) {
      return r;
    }
#if !NET_PREFER_WIFI
    if (wifiEnsureConnected()) {
      Serial.println("[NET] failover to WiFi");
      r = uploadWifi(data, len);
      r.via = "WiFi";
      return r;
    }
#endif
    return r;
  }
  if (wifiEnsureConnected()) {
    Serial.println("[NET] upload via WiFi");
    r = uploadWifi(data, len);
    r.via = "WiFi";
    return r;
  }
#else
  // 纯 4G
  if (isCellReady()) {
    Serial.println("[NET] upload via 4G");
    r = uploadHardware(data, len);
    r.via = "SIM";
    return r;
  }
#endif

  r.ok = false;
  r.error = "no network";
  r.via = nullptr;
  return r;
}

bool Modem::ensureCellNetwork() {
  ensureInitialized();
  if (!cellReady_) {
    return false;
  }
  if (radioSleeping_) {
    return wakeRadio();
  }
  BusGuard guard;
  if (!guard) {
    return false;
  }
  return modemEnsureNetwork();
}

bool Modem::sleepRadio() {
#if USE_MOCK_MODEM
  radioSleeping_ = true;
  Serial.println("[NET] mock radio sleep");
  return true;
#else
  ensureInitialized();
  if (!cellReady_) {
    return false;
  }
  if (radioSleeping_) {
    return true;
  }
  BusGuard guard;
  if (!guard) {
    return false;
  }
  Serial.println("[NET] radio sleep AT+CFUN=0");
  modemSendAt("AT+CFUN=0", 15000);
  kNetworkOpen = false;
  radioSleeping_ = true;
  return true;
#endif
}

bool Modem::wakeRadio() {
#if USE_MOCK_MODEM
  radioSleeping_ = false;
  return true;
#else
  ensureInitialized();
  if (!cellReady_) {
    return false;
  }
  BusGuard guard;
  if (!guard) {
    return false;
  }
  if (!radioSleeping_) {
    return modemEnsureNetwork();
  }
  Serial.println("[NET] radio wake AT+CFUN=1");
  if (!modemSendAt("AT+CFUN=1", 20000)) {
    Serial.println("[NET] CFUN=1 failed");
    return false;
  }
  radioSleeping_ = false;
  delay(2000);
  return modemEnsureNetwork();
#endif
}

CellHttpResult Modem::httpsPostJson(const char* url, const char* bearerToken,
                                    const uint8_t* json, size_t jsonLen) {
  CellHttpResult r;
  ensureInitialized();
  if (!cellReady_) {
    r.error = "modem not ready";
    return r;
  }
  // 整段 TLS/CIP 会话占住总线，防止串口 DIAG/HTEST 插入
  BusGuard guard;
  if (!guard) {
    r.error = "modem busy";
    return r;
  }
  int code = 0;
  String body;
  Serial.printf("[NET] 4G ESP-TLS POST %u bytes → %s\n",
                static_cast<unsigned>(jsonLen), url ? url : "?");
  if (!modemHttpsPostJson(url, bearerToken, json, jsonLen, &code, &body)) {
    r.httpCode = code;
    r.body = body;
    r.error = (code > 0) ? "4G ESP-TLS HTTP error" : "4G ESP-TLS failed";
    return r;
  }
  r.httpCode = code;
  r.body = body;
  r.ok = true;
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
  BusGuard guard;
  if (!guard) {
    r.error = "modem busy";
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
  // 已配置可上网 WiFi 时用 AP_STA，避免 SoftAP 把 STA 踢掉（ChatGPT 需要上网）
  const bool hasSta =
      WIFI_SSID[0] != '\0' && strcmp(WIFI_SSID, "YOUR_SSID") != 0;
  WiFi.mode(hasSta ? WIFI_AP_STA : WIFI_AP);
  if (!WiFi.softAP(STREAM_AP_SSID, STREAM_AP_PASS)) {
    Serial.println("[NET] SoftAP start failed");
    streamingApMode_ = false;
    wifiReady_ = false;
    return false;
  }
  streamingApMode_ = true;
  wifiReady_ = true;
  Serial.printf("[NET] SoftAP %s / %s IP %s%s\n", STREAM_AP_SSID, STREAM_AP_PASS,
                WiFi.softAPIP().toString().c_str(),
                hasSta ? " (AP+STA)" : "");
  return true;
#else
  return false;
#endif
}

bool Modem::ensureWifiForStreaming() {
#if STREAM_ENABLE
  streamingApMode_ = false;

  const bool hasSta = WIFI_SSID[0] != '\0' && strcmp(WIFI_SSID, "YOUR_SSID") != 0;
  if (hasSta) {
    if (wifiEnsureConnected()) {
      Serial.printf("[NET] streaming via STA %s\n",
                    WiFi.localIP().toString().c_str());
      // 同时开 SoftAP，手机可直连看预览/答案
      startStreamingSoftAp();
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
