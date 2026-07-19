// A7670：CIP 明文 TCP + ESP32 mbedTLS HTTPS（绕过模块内置 HTTPS 715）
#include "config.h"
#include "modules/modem.h"

#include <Arduino.h>
#include <cstring>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"

namespace modem_at {
HardwareSerial& uart();
void drainRx();
bool sendAt(const char* cmd, uint32_t timeoutMs, String* out = nullptr);
bool ensureNetwork();
bool lockBus(uint32_t timeoutMs = 180000);
void unlockBus();
}  // namespace modem_at

namespace {
struct AtBusGuard {
  bool ok;
  explicit AtBusGuard(uint32_t ms = 180000) : ok(modem_at::lockBus(ms)) {
    if (!ok) {
      Serial.println("[NET] ESP-TLS: modem bus busy");
    }
  }
  ~AtBusGuard() {
    if (ok) {
      modem_at::unlockBus();
    }
  }
  explicit operator bool() const { return ok; }
};
}  // namespace

namespace {

constexpr int kMux = 0;
constexpr size_t kSendChunk = 1024;
constexpr size_t kRecvChunk = 2048;  // 尽快抽干，避免模块 RX 溢出丢证书

// 最近一次 CIPRXGET 的 remain，便于连续抽干缓存
int gLastRemain = 0;

int cipRecvOnce(uint8_t* out, size_t maxLen, uint32_t waitMs);  // 前向声明

// 把模块缓存尽快抽到 ESP，避免大证书握手时模块 RX 溢出
constexpr size_t kRxQSize = 12288;
uint8_t gRxQ[kRxQSize];
size_t gRxQLen = 0;

void rxqClear() { gRxQLen = 0; }

bool waitCipDataUrc(uint32_t timeoutMs) {
  // +CIPRXGET: 1,<mux>,<len> 有数据通知
  String buf;
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (modem_at::uart().available()) {
      buf += static_cast<char>(modem_at::uart().read());
      if (buf.indexOf("+CIPRXGET: 1,") >= 0 || buf.indexOf("+CIPRXGET:1,") >= 0) {
        return true;
      }
      // 防止 buf 无限涨
      if (buf.length() > 400) {
        buf.remove(0, buf.length() - 80);
      }
    }
    if (gLastRemain > 0) {
      return true;
    }
    delay(5);
  }
  return false;
}

void rxqDrainModem(bool waitUrc) {
  if (waitUrc && gLastRemain <= 0) {
    waitCipDataUrc(3000);
  }
  int guard = 0;
  while (gRxQLen + 64 < kRxQSize && guard++ < 24) {
    size_t space = kRxQSize - gRxQLen;
    if (space > kRecvChunk) {
      space = kRecvChunk;
    }
    const int n = cipRecvOnce(gRxQ + gRxQLen, space, 3000);
    if (n <= 0) {
      break;
    }
    gRxQLen += static_cast<size_t>(n);
    if (gLastRemain <= 0) {
      break;
    }
  }
}

int rxqRead(uint8_t* out, size_t len) {
  if (gRxQLen == 0 || !out || len == 0) {
    return 0;
  }
  const size_t n = (len < gRxQLen) ? len : gRxQLen;
  memcpy(out, gRxQ, n);
  if (n < gRxQLen) {
    memmove(gRxQ, gRxQ + n, gRxQLen - n);
  }
  gRxQLen -= n;
  return static_cast<int>(n);
}

struct UrlParts {
  String host;
  String path;
  uint16_t port = 443;
  bool ok = false;
};

UrlParts parseHttpsUrl(const char* url) {
  UrlParts u;
  if (!url) {
    return u;
  }
  const char* p = url;
  if (strncmp(p, "https://", 8) == 0) {
    p += 8;
  } else if (strncmp(p, "http://", 7) == 0) {
    p += 7;
    u.port = 80;
  } else {
    return u;
  }
  const char* slash = strchr(p, '/');
  const char* colon = strchr(p, ':');
  if (colon && (!slash || colon < slash)) {
    u.host = String(p).substring(0, colon - p);
    u.port = static_cast<uint16_t>(atoi(colon + 1));
    u.path = slash ? String(slash) : String("/");
  } else if (slash) {
    u.host = String(p).substring(0, slash - p);
    u.path = String(slash);
  } else {
    u.host = String(p);
    u.path = "/";
  }
  u.ok = u.host.length() > 0;
  return u;
}

bool waitUrcErr(const char* tag, uint32_t timeoutMs, int* errOut) {
  String buf;
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (modem_at::uart().available()) {
      const char c = static_cast<char>(modem_at::uart().read());
      Serial.write(c);
      buf += c;
      const int idx = buf.indexOf(tag);
      if (idx >= 0) {
        const int nl = buf.indexOf('\n', idx);
        if (nl >= 0) {
          const int colon = buf.indexOf(':', idx);
          String rest = buf.substring(colon + 1, nl);
          rest.trim();
          const int comma = rest.indexOf(',');
          const int err =
              (comma >= 0) ? rest.substring(comma + 1).toInt() : rest.toInt();
          if (errOut) {
            *errOut = err;
          }
          Serial.printf("[NET] %s err=%d\n", tag, err);
          return err == 0;
        }
      }
    }
    delay(5);
  }
  if (errOut) {
    *errOut = -2;
  }
  return false;
}

bool waitPrompt(char ch, uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (modem_at::uart().available()) {
      const char c = static_cast<char>(modem_at::uart().read());
      if (c == ch) {
        return true;
      }
      Serial.write(c);
    }
    delay(2);
  }
  return false;
}

bool looksLikeIpv4(const String& ip) {
  if (ip.length() < 7 || ip.indexOf(':') >= 0) {
    return false;
  }
  int dots = 0;
  for (unsigned i = 0; i < ip.length(); ++i) {
    const char c = ip[i];
    if (c == '.') {
      ++dots;
    } else if (c < '0' || c > '9') {
      return false;
    }
  }
  return dots == 3;
}

String parseCdnsIpv4(const String& resp) {
  int search = 0;
  while (true) {
    const int idx = resp.indexOf("+CDNSGIP:", search);
    if (idx < 0) {
      break;
    }
    const int nl = resp.indexOf('\n', idx);
    if (nl < 0) {
      break;
    }
    const String line = resp.substring(idx, nl);
    if (line.indexOf("+CDNSGIP: 0,") >= 0 || line.indexOf("+CDNSGIP:0,") >= 0) {
      search = nl + 1;
      continue;
    }
    int q1 = line.indexOf('"');
    int q2 = (q1 >= 0) ? line.indexOf('"', q1 + 1) : -1;
    int q3 = (q2 >= 0) ? line.indexOf('"', q2 + 1) : -1;
    int q4 = (q3 >= 0) ? line.indexOf('"', q3 + 1) : -1;
    if (q3 >= 0 && q4 > q3) {
      String ip = line.substring(q3 + 1, q4);
      if (looksLikeIpv4(ip)) {
        return ip;
      }
    }
    search = nl + 1;
  }
  return "";
}

// 物联卡 DNS 经常失败（+CDNSGIP: 0,10），旧逻辑重试 3×20s 会白白等约一分钟。
String gDnsCacheHost;
String gDnsCacheIp;
uint32_t gDnsPreferCacheUntil = 0;

const char* dashFallbackIp() {
  static const char* kFallback[] = {"39.96.213.166", "8.152.159.24",
                                    "39.96.198.249", "8.140.217.18"};
  return kFallback[(millis() / 1000) % 4];
}

String resolveDns(const char* host) {
  const bool preferCache =
      host && (strstr(host, "dashscope.aliyuncs.com") ||
               strstr(host, "bigmodel.cn") || strstr(host, "open.bigmodel.cn"));

  // 近期 DNS 不稳时，优先用缓存/兜底，避免每次扫题卡几十秒
  if (preferCache && gDnsCacheIp.length() && millis() < gDnsPreferCacheUntil &&
      gDnsCacheHost == host) {
    Serial.printf("[NET] DNS cache %s → %s\n", host, gDnsCacheIp.c_str());
    return gDnsCacheIp;
  }

  modem_at::sendAt("AT+CDNSCFG=\"223.5.5.5\",\"114.114.114.114\"", 3000);

  // 只试 1 次、最多等 6s；失败立刻走兜底（日志里 ERROR 往往几秒内就返回）
  const String cmd = String("AT+CDNSGIP=\"") + host + "\"";
  modem_at::drainRx();
  modem_at::uart().print(cmd);
  modem_at::uart().print("\r\n");
  Serial.printf("[NET] >> %s (try 1)\n", cmd.c_str());

  String resp;
  const uint32_t start = millis();
  while (millis() - start < 6000) {
    while (modem_at::uart().available()) {
      resp += static_cast<char>(modem_at::uart().read());
    }
    if (resp.indexOf("OK") >= 0 || resp.indexOf("ERROR") >= 0) {
      delay(120);
      while (modem_at::uart().available()) {
        resp += static_cast<char>(modem_at::uart().read());
      }
      break;
    }
    delay(10);
  }
  Serial.print(resp);
  const String ip = parseCdnsIpv4(resp);
  if (ip.length()) {
    Serial.printf("[NET] DNS %s → %s\n", host, ip.c_str());
    gDnsCacheHost = host;
    gDnsCacheIp = ip;
    return ip;
  }

  // DNS 失败：2 分钟内下次扫题直接用缓存/兜底
  gDnsPreferCacheUntil = millis() + 120000UL;
  if (preferCache) {
    if (gDnsCacheIp.length() && gDnsCacheHost == host) {
      Serial.printf("[NET] DNS fail → cache %s\n", gDnsCacheIp.c_str());
      return gDnsCacheIp;
    }
    // 仅百炼保留硬编码兜底 IP；智谱依赖 CDNS 成功或已缓存
    if (host && strstr(host, "dashscope.aliyuncs.com")) {
      const char* fb = dashFallbackIp();
      gDnsCacheHost = host;
      gDnsCacheIp = fb;
      Serial.printf("[NET] DNS fallback %s → %s\n", host, fb);
      return String(fb);
    }
  }
  Serial.println("[NET] DNS failed (no IPv4)");
  return "";
}

bool cipClose() {
  const String cmd = String("AT+CIPCLOSE=") + String(kMux);
  modem_at::sendAt(cmd.c_str(), 5000);
  return true;
}

bool cipOpenOne(const String& ip, uint16_t port) {
  const String cmd = String("AT+CIPOPEN=") + String(kMux) + ",\"TCP\",\"" + ip +
                     "\"," + String(port);
  modem_at::drainRx();
  modem_at::uart().print(cmd);
  modem_at::uart().print("\r\n");
  Serial.printf("[NET] >> %s\n", cmd.c_str());
  int err = 0;
  // 建连失败别干等到 60s；物联卡失败通常几秒内回 err
  return waitUrcErr("+CIPOPEN:", 15000, &err);
}

bool cipOpen(const char* host, uint16_t port) {
  modem_at::sendAt("AT+HTTPTERM", 3000);
  // 不要 CCHSTOP：会拆掉 NETOPEN 的 PDP
  cipClose();
  delay(150);
  modem_at::sendAt("AT+CIPRXGET=1", 3000);  // 手动取数，禁止直吐 UART
  rxqClear();

  const String ip = resolveDns(host);
  if (!ip.length()) {
    Serial.println("[NET] no IPv4 for CIPOPEN");
    return false;
  }

  // 必须用 IPv4；域名在部分固件上 CIPOPEN=11
  if (cipOpenOne(ip, port)) {
    return true;
  }

  // 当前 IP 不通时换百炼其它节点再试（日志里出现过 CIPOPEN err=1）
  if (host && strstr(host, "dashscope.aliyuncs.com")) {
    static const char* kAlts[] = {"39.96.213.166", "8.152.159.24",
                                  "39.96.198.249", "8.140.217.18"};
    for (const char* alt : kAlts) {
      if (ip == alt) {
        continue;
      }
      Serial.printf("[NET] CIPOPEN retry alt IP %s\n", alt);
      cipClose();
      delay(100);
      if (cipOpenOne(String(alt), port)) {
        gDnsCacheHost = host;
        gDnsCacheIp = alt;
        return true;
      }
    }
  }
  return false;
}

bool cipSend(const uint8_t* data, size_t len) {
  size_t off = 0;
  while (off < len) {
    const size_t n = (len - off) > kSendChunk ? kSendChunk : (len - off);
    const String cmd = String("AT+CIPSEND=") + String(kMux) + "," +
                       String(static_cast<unsigned>(n));
    modem_at::drainRx();
    modem_at::uart().print(cmd);
    modem_at::uart().print("\r\n");
    if (!waitPrompt('>', 12000)) {
      Serial.println("[NET] CIPSEND no '>'");
      return false;
    }
    modem_at::uart().write(data + off, n);
    String resp;
    const uint32_t start = millis();
    bool ok = false;
    while (millis() - start < 30000) {
      while (modem_at::uart().available()) {
        resp += static_cast<char>(modem_at::uart().read());
      }
      if (resp.indexOf("DATA ACCEPT") >= 0) {
        ok = true;
        break;
      }
      if (resp.indexOf("ERROR") >= 0) {
        Serial.println("[NET] CIPSEND ERROR");
        Serial.print(resp);
        return false;
      }
      // 部分固件：+CIPSEND: 0,n  / SEND OK / 仅 OK（需等数据发出后）
      if (resp.indexOf("SEND OK") >= 0 || resp.indexOf("+CIPSEND:") >= 0) {
        ok = true;
        break;
      }
      if (millis() - start > 400 &&
          (resp.indexOf("\nOK") >= 0 || resp.endsWith("OK")) &&
          resp.indexOf("ERROR") < 0) {
        ok = true;
        break;
      }
      delay(2);
    }
    if (!ok) {
      Serial.println("[NET] CIPSEND timeout");
      Serial.print(resp);
      return false;
    }
    Serial.printf("[NET] CIPSEND %u ok\n", static_cast<unsigned>(n));
    off += n;
  }
  return true;
}

int cipRecvOnce(uint8_t* out, size_t maxLen, uint32_t waitMs) {
  size_t want = maxLen > kRecvChunk ? kRecvChunk : maxLen;
  // 按 remain 精确要，避免部分固件对超大 want 直接 ERROR
  if (gLastRemain > 0 && static_cast<size_t>(gLastRemain) < want) {
    want = static_cast<size_t>(gLastRemain);
  }
  if (want == 0) {
    want = 1;
  }
  const String cmd = String("AT+CIPRXGET=2,") + String(kMux) + "," +
                     String(static_cast<unsigned>(want));
  modem_at::uart().print(cmd);
  modem_at::uart().print("\r\n");

  String hdr;
  hdr.reserve(1600);
  const uint32_t start = millis();
  // 至少等完一条 AT 应答，避免半截响应污染后续命令
  if (waitMs < 2000) {
    waitMs = 2000;
  }
  while (millis() - start < waitMs) {
    while (modem_at::uart().available()) {
      hdr += static_cast<char>(modem_at::uart().read());
    }
    // 通知 URC：+CIPRXGET: 1,0,len — 忽略，继续等 mode=2
    int idx = -1;
    int search = 0;
    while (true) {
      int p = hdr.indexOf("+CIPRXGET:", search);
      if (p < 0) {
        break;
      }
      int nl = hdr.indexOf('\n', p);
      if (nl < 0) {
        break;
      }
      String line = hdr.substring(p, nl);
      int col = line.indexOf(':');
      String rest = line.substring(col + 1);
      rest.trim();
      const int mode = rest.toInt();
      if (mode == 2) {
        idx = p;
        break;
      }
      search = nl + 1;
    }
    if (idx < 0) {
      if (hdr.indexOf("ERROR") >= 0 && hdr.indexOf("+CIPRXGET: 2") < 0 &&
          hdr.indexOf("+CIPRXGET:2") < 0) {
        return 0;
      }
      delay(2);
      continue;
    }
    const int nl = hdr.indexOf('\n', idx);
    String line = hdr.substring(idx, nl);
    int col = line.indexOf(':');
    String rest = line.substring(col + 1);
    rest.trim();
    int c1 = rest.indexOf(',');
    int c2 = rest.indexOf(',', c1 + 1);
    int c3 = rest.indexOf(',', c2 + 1);
    if (c1 < 0 || c2 < 0 || c3 < 0) {
      return 0;
    }
    int n = rest.substring(c2 + 1, c3).toInt();
    gLastRemain = rest.substring(c3 + 1).toInt();
    if (n <= 0) {
      Serial.printf("[NET] CIPRXGET n=0 line=%s\n", line.c_str());
      const uint32_t t2 = millis();
      while (millis() - t2 < 500) {
        while (modem_at::uart().available()) {
          (void)modem_at::uart().read();
        }
        if (hdr.indexOf("OK") >= 0) {
          break;
        }
        delay(2);
      }
      return 0;
    }
    if (n > static_cast<int>(maxLen)) {
      n = static_cast<int>(maxLen);
    }
    int dataStart = nl + 1;
    while (dataStart < static_cast<int>(hdr.length()) &&
           (hdr[dataStart] == '\r' || hdr[dataStart] == '\n')) {
      dataStart++;
    }
    int copied = 0;
    const int already = static_cast<int>(hdr.length()) - dataStart;
    if (already > 0) {
      const int take = already > n ? n : already;
      memcpy(out, hdr.c_str() + dataStart, take);
      copied = take;
      // 关键键：多余字节不能丢（否则 TLS 证书被吞掉）
      int extraAt = dataStart + take;
      if (extraAt < static_cast<int>(hdr.length())) {
        // 先尽量识别 OK；其余进接收队列
        String rest = hdr.substring(extraAt);
        int okAt = rest.indexOf("OK");
        int errAt = rest.indexOf("ERROR");
        int cut = rest.length();
        if (okAt >= 0) {
          cut = okAt;
        }
        if (errAt >= 0 && errAt < cut) {
          cut = errAt;
        }
        // 跳过 OK 前的空白
        int bodyExtra = 0;
        while (bodyExtra < cut &&
               (rest[bodyExtra] == '\r' || rest[bodyExtra] == '\n' ||
                rest[bodyExtra] == ' ')) {
          bodyExtra++;
        }
        const int extraLen = cut - bodyExtra;
        if (extraLen > 0 && gRxQLen + static_cast<size_t>(extraLen) < kRxQSize) {
          memcpy(gRxQ + gRxQLen, rest.c_str() + bodyExtra, extraLen);
          gRxQLen += static_cast<size_t>(extraLen);
          Serial.printf("[NET] salvaged %d bytes into q\n", extraLen);
        }
      }
    }
    while (copied < n && millis() - start < 15000) {
      if (modem_at::uart().available()) {
        out[copied++] = static_cast<uint8_t>(modem_at::uart().read());
      } else {
        delay(1);
      }
    }
    // 等到 OK（不再把后续 TCP 载荷当垃圾丢）
    String tail;
    const uint32_t t2 = millis();
    while (millis() - t2 < 800) {
      while (modem_at::uart().available()) {
        const char c = static_cast<char>(modem_at::uart().read());
        tail += c;
        // 若 OK 之前混进二进制，尽量回收进队列
        if (tail.length() > 4 && tail.indexOf("OK") < 0 &&
            gRxQLen + 1 < kRxQSize && static_cast<uint8_t>(c) < 0x20 &&
            c != '\r' && c != '\n') {
          // keep in tail until OK
        }
      }
      const int okAt = tail.indexOf("\nOK");
      const int okAt2 = tail.indexOf("OK");
      if (okAt >= 0 || (okAt2 == 0) ||
          (okAt2 > 0 && tail[okAt2 - 1] == '\r')) {
        // OK 之前的非空白进队列
        int pre = (okAt >= 0) ? okAt : okAt2;
        int i = 0;
        while (i < pre && (tail[i] == '\r' || tail[i] == '\n')) {
          i++;
        }
        if (pre > i && gRxQLen + (pre - i) < kRxQSize) {
          memcpy(gRxQ + gRxQLen, tail.c_str() + i, pre - i);
          gRxQLen += static_cast<size_t>(pre - i);
        }
        break;
      }
      if (tail.indexOf("ERROR") >= 0) {
        break;
      }
      delay(2);
    }
    return copied;
  }
  // 超时/无数据：打印摘要便于诊断
  if (hdr.length()) {
    Serial.printf("[NET] CIPRXGET empty/fail: %s\n",
                  hdr.substring(0, 120).c_str());
  }
  const uint32_t t3 = millis();
  while (millis() - t3 < 300) {
    while (modem_at::uart().available()) {
      (void)modem_at::uart().read();
    }
    delay(2);
  }
  return 0;
}

int cipRecv(uint8_t* out, size_t maxLen, uint32_t waitMs) {
  if (!out || maxLen == 0) {
    return 0;
  }
  const uint32_t start = millis();
  while (millis() - start < waitMs) {
    const int n = cipRecvOnce(out, maxLen, 800);
    if (n > 0) {
      return n;
    }
    delay(gLastRemain > 0 ? 5 : 40);
  }
  return 0;
}

struct TcpBio {
  bool open = false;
};

int bioSend(void* ctx, const unsigned char* buf, size_t len) {
  (void)ctx;
  if (!buf || len == 0) {
    return 0;
  }
  if (!cipSend(buf, len)) {
    return MBEDTLS_ERR_NET_SEND_FAILED;
  }
  return static_cast<int>(len);
}

int bioRecv(void* ctx, unsigned char* buf, size_t len) {
  (void)ctx;
  if (!buf || len == 0) {
    return 0;
  }
  // 先吃本地队列，再直接 CIPRXGET（此路径曾成功收到证书）
  if (gRxQLen > 0) {
    return rxqRead(buf, len);
  }
  const uint32_t start = millis();
  while (millis() - start < 60000) {
    const int n = cipRecvOnce(buf, len, 4000);
    if (n > 0) {
      // 立刻抽干模块缓存到本地队列，降低溢出/丢包
      int guard = 0;
      while (gLastRemain > 0 && gRxQLen + 64 < kRxQSize && guard++ < 16) {
        delay(30);  // 连续 CIPRXGET 太快会 n=0
        size_t space = kRxQSize - gRxQLen;
        if (space > kRecvChunk) {
          space = kRecvChunk;
        }
        // 大块优先；部分固件单次喜欢 ~512
        if (space > 512 && gLastRemain > 512) {
          space = 512;
        }
        const int m = cipRecvOnce(gRxQ + gRxQLen, space, 3000);
        if (m <= 0) {
          delay(100);
          const int m2 = cipRecvOnce(gRxQ + gRxQLen, 256, 3000);
          if (m2 <= 0) {
            break;
          }
          gRxQLen += static_cast<size_t>(m2);
          continue;
        }
        gRxQLen += static_cast<size_t>(m);
      }
      return n;
    }
    delay(50);
  }
  return MBEDTLS_ERR_SSL_TIMEOUT;
}

int bioRecvTimeout(void* ctx, unsigned char* buf, size_t len,
                   uint32_t timeout) {
  return bioRecv(ctx, buf, len);
}

bool sslWriteAll(mbedtls_ssl_context* ssl, const uint8_t* data, size_t len) {
  size_t off = 0;
  while (off < len) {
    const int ret = mbedtls_ssl_write(ssl, data + off, len - off);
    if (ret > 0) {
      off += static_cast<size_t>(ret);
      continue;
    }
    if (ret == MBEDTLS_ERR_SSL_WANT_WRITE || ret == MBEDTLS_ERR_SSL_WANT_READ) {
      delay(5);
      continue;
    }
    char err[64];
    mbedtls_strerror(ret, err, sizeof(err));
    Serial.printf("[NET] ssl_write fail: %s\n", err);
    return false;
  }
  return true;
}

bool parseHttpResponse(const String& raw, int* httpCode, String* bodyOut) {
  const int sp1 = raw.indexOf(' ');
  if (sp1 < 0) {
    return false;
  }
  const int sp2 = raw.indexOf(' ', sp1 + 1);
  if (sp2 < 0) {
    return false;
  }
  const int code = raw.substring(sp1 + 1, sp2).toInt();
  if (httpCode) {
    *httpCode = code;
  }
  const int hdrEnd = raw.indexOf("\r\n\r\n");
  if (hdrEnd < 0) {
    if (bodyOut) {
      *bodyOut = "";
    }
    return code > 0;
  }
  if (bodyOut) {
    *bodyOut = raw.substring(hdrEnd + 4);
  }
  return code > 0;
}

}  // namespace

bool modemEspTlsHttpsPost(const char* url, const char* bearer, const uint8_t* data,
                          size_t len, int* httpCode, String* bodyOut) {
  if (!url || !bearer || !data || len == 0) {
    return false;
  }
  // 覆盖 CIPOPEN/CIPSEND/CIPRXGET 全程，避免与串口 AT 诊断交叉
  AtBusGuard bus;
  if (!bus) {
    if (httpCode) {
      *httpCode = -1;
    }
    return false;
  }
  const UrlParts u = parseHttpsUrl(url);
  if (!u.ok) {
    if (httpCode) {
      *httpCode = -1;
    }
    return false;
  }
  if (!modem_at::ensureNetwork()) {
    if (httpCode) {
      *httpCode = -1;
    }
    return false;
  }

  Serial.printf("[NET] ESP-TLS HTTPS %s:%u %s (%u bytes)\n", u.host.c_str(),
                u.port, u.path.c_str(), static_cast<unsigned>(len));

  if (!cipOpen(u.host.c_str(), u.port)) {
    Serial.println("[NET] ESP-TLS TCP open failed");
    if (httpCode) {
      *httpCode = -3;
    }
    return false;
  }

  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config conf;

  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctr_drbg);
  mbedtls_ssl_init(&ssl);
  mbedtls_ssl_config_init(&conf);

  bool ok = false;
  int code = -1;
  String resp;
  TcpBio bio;
  bio.open = true;

  do {
    const char* pers = "saoti-esp-tls";
    if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                              reinterpret_cast<const unsigned char*>(pers),
                              strlen(pers)) != 0) {
      break;
    }
    if (mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
      break;
    }
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
    mbedtls_ssl_conf_read_timeout(&conf, 60000);
    mbedtls_ssl_conf_max_version(&conf, MBEDTLS_SSL_MAJOR_VERSION_3,
                                 MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_min_version(&conf, MBEDTLS_SSL_MAJOR_VERSION_3,
                                 MBEDTLS_SSL_MINOR_VERSION_3);

    if (mbedtls_ssl_setup(&ssl, &conf) != 0) {
      break;
    }
    mbedtls_ssl_set_hostname(&ssl, u.host.c_str());
    mbedtls_ssl_set_bio(&ssl, &bio, bioSend, bioRecv, bioRecvTimeout);

    Serial.println("[NET] ESP-TLS handshake...");
    delay(200);
    int hs;
    const uint32_t hsStart = millis();
    while ((hs = mbedtls_ssl_handshake(&ssl)) != 0) {
      if (hs != MBEDTLS_ERR_SSL_WANT_READ && hs != MBEDTLS_ERR_SSL_WANT_WRITE) {
        char err[80];
        mbedtls_strerror(hs, err, sizeof(err));
        Serial.printf("[NET] ESP-TLS handshake fail: %s (%d)\n", err, hs);
        break;
      }
      if (millis() - hsStart > 90000) {
        Serial.println("[NET] ESP-TLS handshake timeout");
        hs = -1;
        break;
      }
      delay(5);
    }
    if (hs != 0) {
      break;
    }
    Serial.printf("[NET] ESP-TLS OK cipher=%s\n",
                  mbedtls_ssl_get_ciphersuite(&ssl));

    String hdr;
    hdr.reserve(280);
    hdr += "POST ";
    hdr += u.path;
    hdr += " HTTP/1.1\r\nHost: ";
    hdr += u.host;
    hdr += "\r\nAuthorization: Bearer ";
    hdr += bearer;
    hdr += "\r\nContent-Type: application/json\r\nContent-Length: ";
    hdr += String(static_cast<unsigned>(len));
    hdr += "\r\nConnection: close\r\n\r\n";

    if (!sslWriteAll(&ssl, reinterpret_cast<const uint8_t*>(hdr.c_str()),
                     hdr.length()) ||
        !sslWriteAll(&ssl, data, len)) {
      break;
    }
    Serial.println("[NET] ESP-TLS request sent, reading...");

    resp.reserve(4096);
    const uint32_t rdStart = millis();
    while (millis() - rdStart < static_cast<uint32_t>(OPENAI_TIMEOUT_MS)) {
      uint8_t buf[512];
      const int n = mbedtls_ssl_read(&ssl, buf, sizeof(buf));
      if (n > 0) {
        for (int i = 0; i < n; ++i) {
          resp += static_cast<char>(buf[i]);
        }
        continue;
      }
      if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
        delay(5);
        continue;
      }
      if (n == 0 || n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || resp.length() > 0) {
        break;
      }
      char err[80];
      mbedtls_strerror(n, err, sizeof(err));
      Serial.printf("[NET] ssl_read: %s\n", err);
      break;
    }

    if (!parseHttpResponse(resp, &code, bodyOut)) {
      Serial.printf("[NET] bad HTTP (%u bytes)\n",
                    static_cast<unsigned>(resp.length()));
      if (resp.length()) {
        Serial.println(resp.substring(0, 240));
      }
      break;
    }
    Serial.printf("[NET] ESP-TLS HTTP %d body %u\n", code,
                  static_cast<unsigned>(bodyOut ? bodyOut->length() : 0));
    ok = (code >= 200 && code < 300);
  } while (false);

  // 对端可能已关闭，close_notify / CIPCLOSE 失败可忽略
  (void)mbedtls_ssl_close_notify(&ssl);
  mbedtls_ssl_free(&ssl);
  mbedtls_ssl_config_free(&conf);
  mbedtls_ctr_drbg_free(&ctr_drbg);
  mbedtls_entropy_free(&entropy);
  cipClose();

  if (httpCode) {
    *httpCode = code;
  }
  return ok;
}
