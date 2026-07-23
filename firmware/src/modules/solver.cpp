#include "config.h"
#include "modules/modem.h"
#include "modules/solver.h"

#if USE_OPENAI_SOLVER

#include <Preferences.h>
#include "esp_heap_caps.h"
#include "mbedtls/base64.h"
#if USE_WIFI_FALLBACK
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#endif

namespace {

String gSharedAnswer;
String gLastError;
uint32_t gAnswerAtMs = 0;
Modem* gModem = nullptr;

// 智谱视觉模型池：付费最强 → 失败直接免费 flash（无中间档）
const char* kVisionModels[] = {
    "glm-4.6v",      // 付费最强视觉（首选）
    "glm-4v-flash",  // 用不了就直接换这个
};
constexpr int kVisionModelCount =
    static_cast<int>(sizeof(kVisionModels) / sizeof(kVisionModels[0]));

Preferences gPrefs;
bool gPrefsReady = false;
uint32_t gExhaustedMask = 0;
int gPreferredIndex = 0;

int findModelIndex(const char* name) {
  if (!name || !name[0]) {
    return -1;
  }
  for (int i = 0; i < kVisionModelCount; ++i) {
    if (strcmp(kVisionModels[i], name) == 0) {
      return i;
    }
  }
  return -1;
}

void persistPool() {
  if (!gPrefsReady) {
    return;
  }
  gPrefs.putUInt("exh", gExhaustedMask);
  gPrefs.putChar("pref", static_cast<int8_t>(gPreferredIndex));
}

void loadPool() {
  if (!gPrefsReady) {
    // 独立命名空间，避免沿用百炼时代的耗尽位图
    gPrefs.begin("saoti-zhipu", false);
    gPrefsReady = true;
  }
  gExhaustedMask = gPrefs.getUInt("exh", 0);
  const int8_t pref = gPrefs.getChar("pref", -1);
  const int configured = findModelIndex(OPENAI_MODEL);
  // 编译配置的 OPENAI_MODEL 优先于 NVS 旧首选（避免一直停在以前的 flash）
  if (configured >= 0) {
    gPreferredIndex = configured;
  } else if (pref >= 0 && pref < kVisionModelCount) {
    gPreferredIndex = pref;
  } else {
    gPreferredIndex = 0;
  }
  // 若首选已被标记耗尽，找下一个可用
  if (gExhaustedMask & (1u << gPreferredIndex)) {
    for (int i = 0; i < kVisionModelCount; ++i) {
      const int idx = (gPreferredIndex + i) % kVisionModelCount;
      if ((gExhaustedMask & (1u << idx)) == 0) {
        gPreferredIndex = idx;
        break;
      }
    }
  }
  Serial.printf("[AI] model pool ready: prefer=%s exhausted=0x%04lX\n",
                kVisionModels[gPreferredIndex],
                static_cast<unsigned long>(gExhaustedMask));
}

// 永久耗尽/不可用：写入 exhausted，后续优先跳过
bool isPermanentModelFail(const String& body) {
  return body.indexOf("FreeTierOnly") >= 0 ||
         body.indexOf("Free quota exhausted") >= 0 ||
         body.indexOf("use free tier only") >= 0 ||
         body.indexOf("AllocationQuota.FreeTierOnly") >= 0 ||
         body.indexOf("余额不足") >= 0 ||
         body.indexOf("额度不足") >= 0 ||
         body.indexOf("insufficient balance") >= 0 ||
         body.indexOf("insufficient_quota") >= 0 ||
         body.indexOf("\"code\":\"1113\"") >= 0 ||
         body.indexOf("\"code\":1113") >= 0 ||
         body.indexOf("does not exist") >= 0 ||
         body.indexOf("model_not_found") >= 0 ||
         body.indexOf("无权限") >= 0;
}

// 临时繁忙/限流：换模型重试，不永久拉黑（智谱 429/访问量过大）
bool isTransientBusy(int httpCode, const String& body) {
  if (httpCode == 429 || httpCode == 503 || httpCode == 502) {
    return true;
  }
  return body.indexOf("访问量过大") >= 0 || body.indexOf("稍后再试") >= 0 ||
         body.indexOf("rate limit") >= 0 || body.indexOf("RateLimit") >= 0 ||
         body.indexOf("\"code\":\"1302\"") >= 0 ||
         body.indexOf("\"code\":1302") >= 0;
}

bool ensureInternet() {
#if USE_WIFI_FALLBACK
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }
  if (WIFI_SSID[0] == '\0' || strcmp(WIFI_SSID, "YOUR_SSID") == 0) {
    return false;
  }
  WiFi.mode(WIFI_AP_STA);
  if (WiFi.status() == WL_IDLE_STATUS || WiFi.status() == WL_DISCONNECTED ||
      WiFi.status() == WL_NO_SSID_AVAIL || WiFi.status() == WL_CONNECT_FAILED) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
  const uint32_t start = millis();
  while (millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[AI] STA OK %s\n", WiFi.localIP().toString().c_str());
      return true;
    }
    delay(200);
  }
  Serial.println("[AI] STA connect FAIL");
  return false;
#else
  if (!gModem) {
    Serial.println("[AI] modem ptr missing");
    return false;
  }
  if (!gModem->ensureCellNetwork()) {
    Serial.println("[AI] 4G network FAIL");
    return false;
  }
  Serial.println("[AI] 4G network OK");
  return true;
#endif
}

char* mallocPsram(size_t n) {
  char* p = static_cast<char*>(
      heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!p) {
    p = static_cast<char*>(malloc(n));
  }
  return p;
}

bool encodeBase64(const uint8_t* data, size_t len, char** outB64, size_t* outLen) {
  size_t olen = 0;
  mbedtls_base64_encode(nullptr, 0, &olen, data, len);
  char* buf = mallocPsram(olen + 1);
  if (!buf) {
    return false;
  }
  size_t written = 0;
  if (mbedtls_base64_encode(reinterpret_cast<unsigned char*>(buf), olen + 1, &written,
                            data, len) != 0) {
    free(buf);
    return false;
  }
  buf[written] = '\0';
  *outB64 = buf;
  *outLen = written;
  return true;
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

String extractJsonStringField(const String& body, const char* key, int startFrom = 0) {
  const String pattern = String("\"") + key + "\":";
  int i = body.indexOf(pattern, startFrom);
  if (i < 0) {
    return "";
  }
  i = body.indexOf('"', i + pattern.length());
  if (i < 0) {
    return "";
  }
  ++i;
  String out;
  out.reserve(256);
  bool escape = false;
  for (; i < static_cast<int>(body.length()); ++i) {
    const char c = body[i];
    if (escape) {
      if (c == 'n') {
        out += '\n';
      } else if (c == 't') {
        out += '\t';
      } else if (c == 'r') {
        // skip
      } else if (c == 'u' && i + 4 < static_cast<int>(body.length())) {
        const int h0 = hexNibble(body[i + 1]);
        const int h1 = hexNibble(body[i + 2]);
        const int h2 = hexNibble(body[i + 3]);
        const int h3 = hexNibble(body[i + 4]);
        if (h0 >= 0 && h1 >= 0 && h2 >= 0 && h3 >= 0) {
          const uint16_t code = static_cast<uint16_t>((h0 << 12) | (h1 << 8) |
                                                      (h2 << 4) | h3);
          if (code < 0x80) {
            out += static_cast<char>(code);
          } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
          } else {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
          }
          i += 4;
        } else {
          out += 'u';
        }
      } else {
        out += c;
      }
      escape = false;
      continue;
    }
    if (c == '\\') {
      escape = true;
      continue;
    }
    if (c == '"') {
      break;
    }
    out += c;
  }
  return out;
}

String extractAssistantContent(const String& body) {
  const int choices = body.indexOf("\"choices\"");
  if (choices >= 0) {
    const int message = body.indexOf("\"message\"", choices);
    if (message >= 0) {
      String c = extractJsonStringField(body, "content", message);
      if (!c.isEmpty()) {
        return c;
      }
    }
  }
  return extractJsonStringField(body, "content");
}

String htmlEscape(const String& in) {
  String out;
  out.reserve(in.length() + 32);
  for (size_t i = 0; i < in.length(); ++i) {
    const char c = in[i];
    if (c == '<') {
      out += F("&lt;");
    } else if (c == '>') {
      out += F("&gt;");
    } else if (c == '&') {
      out += F("&amp;");
    } else {
      out += c;
    }
  }
  return out;
}

char* buildRequestJson(const char* model, const char* b64, size_t b64Len,
                       size_t* outLen) {
  // 智谱视觉：图片在前；不用 max_tokens（部分 VL 会 1210）
  const char* p0 = "{\"model\":\"";
  const char* p1 =
      "\",\"messages\":[{\"role\":\"user\",\"content\":["
      "{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,";
  const char* p2 =
      "\"}},"
      "{\"type\":\"text\",\"text\":\"你是扫题挂件助手。请尽力识别图片中的题目文字"
      "（即使略模糊/倾斜也请尝试辨认）。用中文给出：1)最终答案 "
      "2)简要解题步骤（尽量简洁，适合小屏幕）。"
      "仅当完全无法辨认任何题目内容时，才回复看不清请重新拍照。\"}]}]}";

  const size_t jsonLen =
      strlen(p0) + strlen(model) + strlen(p1) + b64Len + strlen(p2);
  char* json = mallocPsram(jsonLen + 1);
  if (!json) {
    return nullptr;
  }
  char* w = json;
  memcpy(w, p0, strlen(p0));
  w += strlen(p0);
  memcpy(w, model, strlen(model));
  w += strlen(model);
  memcpy(w, p1, strlen(p1));
  w += strlen(p1);
  memcpy(w, b64, b64Len);
  w += b64Len;
  memcpy(w, p2, strlen(p2) + 1);
  *outLen = jsonLen;
  return json;
}

bool postOnce(const char* model, char* json, size_t jsonLen, int* httpCode,
              String* bodyOut) {
  Serial.printf("[AI] POST model=%s (%u bytes) via=%s\n", model,
                static_cast<unsigned>(jsonLen),
#if USE_WIFI_FALLBACK
                "WiFi"
#else
                "4G"
#endif
  );

#if USE_WIFI_FALLBACK
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(OPENAI_TIMEOUT_MS / 1000);

  HTTPClient http;
  http.setTimeout(OPENAI_TIMEOUT_MS);
  http.setReuse(false);
  if (!http.begin(client, OPENAI_BASE_URL)) {
    *httpCode = -1;
    *bodyOut = "";
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + OPENAI_API_KEY);

  const int code = http.POST(reinterpret_cast<uint8_t*>(json), jsonLen);
  *httpCode = code;
  *bodyOut = http.getString();
  http.end();
  Serial.printf("[AI] HTTP %d, body %u\n", code,
                static_cast<unsigned>(bodyOut->length()));
  return code >= 200 && code < 300;
#else
  if (!gModem) {
    *httpCode = -1;
    *bodyOut = "";
    return false;
  }
  const CellHttpResult hr = gModem->httpsPostJson(
      OPENAI_BASE_URL, OPENAI_API_KEY, reinterpret_cast<uint8_t*>(json),
      jsonLen);
  *httpCode = hr.httpCode;
  *bodyOut = hr.body;
  Serial.printf("[AI] HTTP %d, body %u\n", hr.httpCode,
                static_cast<unsigned>(bodyOut->length()));
  return hr.ok && hr.httpCode >= 200 && hr.httpCode < 300;
#endif
}

}  // namespace

void solverBegin() { loadPool(); }

void solverSetModem(Modem* modem) { gModem = modem; }

const char* solverActiveModel() {
  if (gPreferredIndex < 0 || gPreferredIndex >= kVisionModelCount) {
    return OPENAI_MODEL;
  }
  return kVisionModels[gPreferredIndex];
}

void solverResetModelPool() {
  gExhaustedMask = 0;
  const int configured = findModelIndex(OPENAI_MODEL);
  gPreferredIndex = configured >= 0 ? configured : 0;
  persistPool();
  Serial.printf("[AI] model pool reset → prefer=%s\n", solverActiveModel());
}

String solverModelPoolStatus() {
  String s;
  s.reserve(256);
  s += F("{\"type\":\"models\",\"active\":\"");
  s += solverActiveModel();
  s += F("\",\"exhausted\":[");
  bool first = true;
  for (int i = 0; i < kVisionModelCount; ++i) {
    if (gExhaustedMask & (1u << i)) {
      if (!first) {
        s += ',';
      }
      first = false;
      s += '"';
      s += kVisionModels[i];
      s += '"';
    }
  }
  s += F("],\"pool\":[");
  for (int i = 0; i < kVisionModelCount; ++i) {
    if (i) {
      s += ',';
    }
    s += '"';
    s += kVisionModels[i];
    s += '"';
  }
  s += F("]}");
  return s;
}

void solverSetSharedAnswer(const String& answer) {
  gSharedAnswer = answer;
  gAnswerAtMs = millis();
  if (!answer.isEmpty()) {
    gLastError = "";
  }
}

void solverSetLastError(const char* error) {
  gLastError = error ? error : "";
}

String solverLastError() { return gLastError; }

bool solverHasAnswer() { return gSharedAnswer.length() > 0; }

String solverLastAnswerText() { return gSharedAnswer; }

uint32_t solverAnswerAgeMs() {
  if (gAnswerAtMs == 0) {
    return 0;
  }
  return millis() - gAnswerAtMs;
}

String solverLastAnswerHtml() {
  String html;
  html.reserve(gSharedAnswer.length() + gLastError.length() + 640);
  html += F("<!doctype html><html><head><meta charset=utf-8>"
            "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
            "<title>扫题答案</title><style>"
            "body{font-family:-apple-system,sans-serif;padding:16px;line-height:1.5;"
            "background:#f7f7f5;color:#111}"
            "pre{white-space:pre-wrap;background:#111;color:#eee;padding:12px;"
            "border-radius:8px}"
            ".err{color:#b00020}.meta{color:#666;font-size:14px}"
            "a{color:#0b57d0}</style></head><body><h2>扫题答案</h2>");
  html += F("<p class=meta>当前模型：");
  html += htmlEscape(String(solverActiveModel()));
  html += F("</p>");
  if (gSharedAnswer.isEmpty()) {
    html += F("<p>暂无答案。</p>");
    if (gLastError.length() > 0) {
      html += F("<p class=err>最近错误：");
      html += htmlEscape(gLastError);
      html += F("</p>");
    }
    html += F("<p class=meta>操作：按 BOOT 拍照，或打开 "
              "<a href=/test>/test</a> 用固定题图测 AI，"
              "再回来刷新本页。</p>");
  } else {
    if (gAnswerAtMs > 0) {
      html += F("<p class=meta>更新于 ");
      html += String(solverAnswerAgeMs() / 1000);
      html += F(" 秒前</p>");
    }
    html += F("<pre>");
    html += htmlEscape(gSharedAnswer);
    html += F("</pre>");
  }
  html += F("<p><a href=/>返回控制台</a> · <a href=/status>状态 JSON</a></p>"
            "</body></html>");
  return html;
}

SolveResult Solver::solveJpeg(const uint8_t* jpeg, size_t len) {
  SolveResult r;
  if (!jpeg || len == 0) {
    r.error = "empty jpeg";
    solverSetLastError(r.error);
    return r;
  }
#if !USE_WIFI_FALLBACK
  if (len > CELL_AI_MAX_JPEG) {
    Serial.printf("[AI] jpeg %u > %u — too large for 4G HTTP\n",
                  static_cast<unsigned>(len),
                  static_cast<unsigned>(CELL_AI_MAX_JPEG));
    r.error = "jpeg too large for 4G";
    solverSetLastError(r.error);
    return r;
  }
#endif
  // 智谱 Key 多为 id.secret；也兼容 sk- 前缀。拒绝占位符。
  if (OPENAI_API_KEY[0] == '\0' ||
      strcmp(OPENAI_API_KEY, "sk-REPLACE_ME") == 0 ||
      strcmp(OPENAI_API_KEY, "YOUR_ZHIPU_API_KEY") == 0 ||
      strlen(OPENAI_API_KEY) < 16) {
    r.error = "set Zhipu API key";
    solverSetLastError(r.error);
    return r;
  }
  if (len < 800 || jpeg[0] != 0xFF || jpeg[1] != 0xD8 ||
      jpeg[len - 2] != 0xFF || jpeg[len - 1] != 0xD9) {
    r.error = "bad image";
    solverSetLastError(r.error);
    Serial.printf("[AI] reject jpeg magic len=%u head=%02X%02X tail=%02X%02X\n",
                  static_cast<unsigned>(len), jpeg[0], jpeg[1],
                  len >= 2 ? jpeg[len - 2] : 0, len >= 1 ? jpeg[len - 1] : 0);
    return r;
  }
  if (!ensureInternet()) {
#if USE_WIFI_FALLBACK
    r.error = "need WiFi (secrets.h)";
#else
    r.error = "need 4G network";
#endif
    solverSetLastError(r.error);
    return r;
  }
  if (!gPrefsReady) {
    loadPool();
  }

  char* b64 = nullptr;
  size_t b64Len = 0;
  Serial.printf("[AI] jpeg %u bytes, base64...\n", static_cast<unsigned>(len));
  if (!encodeBase64(jpeg, len, &b64, &b64Len)) {
    r.error = "base64 OOM";
    solverSetLastError(r.error);
    return r;
  }

  const int maxTries =
      OPENAI_MODEL_MAX_TRIES < kVisionModelCount ? OPENAI_MODEL_MAX_TRIES
                                                 : kVisionModelCount;
  int tried = 0;
  String lastBody;
  int lastCode = 0;
  bool sawTransientBusy = false;

  for (int step = 0; step < kVisionModelCount && tried < maxTries; ++step) {
    const int idx = (gPreferredIndex + step) % kVisionModelCount;
    if (gExhaustedMask & (1u << idx)) {
      Serial.printf("[AI] skip exhausted %s\n", kVisionModels[idx]);
      continue;
    }
    ++tried;
    const char* model = kVisionModels[idx];

    size_t jsonLen = 0;
    char* json = buildRequestJson(model, b64, b64Len, &jsonLen);
    if (!json) {
      free(b64);
      r.error = "json OOM";
      solverSetLastError(r.error);
      return r;
    }

    int code = 0;
    String body;
    bool okHttp = postOnce(model, json, jsonLen, &code, &body);
    // 4G TLS 偶发 HTTP -1/空包：同模型立即重试一次
    if (!okHttp && (code < 0 || body.length() == 0)) {
      Serial.println("[AI] empty/transport fail — retry once");
      delay(500);
      okHttp = postOnce(model, json, jsonLen, &code, &body);
    }
    free(json);
    r.httpCode = code;
    lastCode = code;
    lastBody = body;

    if (okHttp) {
      String answer = extractAssistantContent(body);
      if (answer.isEmpty()) {
        free(b64);
        r.error = "parse answer fail";
        solverSetLastError(r.error);
        Serial.println(body.substring(0, 500));
        return r;
      }
      answer.trim();
      if (gPreferredIndex != idx) {
        Serial.printf("[AI] switched model → %s\n", model);
      }
      gPreferredIndex = idx;
      persistPool();
      lastAnswer_ = answer;
      solverSetSharedAnswer(answer);
      r.ok = true;
      r.answer = answer;
      Serial.println("[AI] ---- answer ----");
      Serial.println(answer);
      Serial.println("[AI] ----------------");
      free(b64);
      return r;
    }

    if (isTransientBusy(code, body)) {
      sawTransientBusy = true;
      Serial.printf("[AI] busy/429 on %s → try next model\n", model);
      delay(400);
      continue;
    }

    // 图片格式错误：换模型通常无效，直接提示重拍
    if (body.indexOf("\"code\":\"1210\"") >= 0 ||
        body.indexOf("\"code\":1210") >= 0 ||
        (body.indexOf("图片") >= 0 && body.indexOf("解析") >= 0)) {
      free(b64);
      r.error = "bad image";
      solverSetLastError(r.error);
      Serial.println(body.substring(0, 300));
      return r;
    }

    if (isPermanentModelFail(body)) {
      Serial.printf("[AI] permanent fail on %s → try next\n", model);
      gExhaustedMask |= (1u << idx);
      persistPool();
      continue;
    }

    // 鉴权等致命错误：停止
    free(b64);
    r.error = "ai http error";
    solverSetLastError(r.error);
    if (body.length() > 0) {
      Serial.println(body.substring(0, 400));
    }
    return r;
  }

  free(b64);
  if ((gExhaustedMask & ((1u << kVisionModelCount) - 1)) ==
      ((1u << kVisionModelCount) - 1)) {
    Serial.println("[AI] all models marked exhausted; clearing mask");
    gExhaustedMask = 0;
    persistPool();
  }
  if (sawTransientBusy || lastCode == 429) {
    r.error = "ai busy";
  } else {
    r.error = "all VL free tiers exhausted";
  }
  solverSetLastError(r.error);
  if (lastBody.length() > 0) {
    Serial.println(lastBody.substring(0, 400));
  }
  return r;
}

#else  // !USE_OPENAI_SOLVER

namespace {
String gSharedAnswer;
String gLastError;
uint32_t gAnswerAtMs = 0;
}  // namespace

void solverBegin() {}
void solverSetModem(Modem* modem) { (void)modem; }
const char* solverActiveModel() { return OPENAI_MODEL; }
void solverResetModelPool() {}
String solverModelPoolStatus() {
  return F("{\"type\":\"models\",\"active\":\"disabled\"}");
}

SolveResult Solver::solveJpeg(const uint8_t* jpeg, size_t len) {
  (void)jpeg;
  (void)len;
  SolveResult r;
  r.error = "solver disabled";
  solverSetLastError(r.error);
  return r;
}

void solverSetSharedAnswer(const String& answer) {
  gSharedAnswer = answer;
  gAnswerAtMs = millis();
}

void solverSetLastError(const char* error) { gLastError = error ? error : ""; }

String solverLastError() { return gLastError; }

bool solverHasAnswer() { return gSharedAnswer.length() > 0; }

String solverLastAnswerText() { return gSharedAnswer; }

uint32_t solverAnswerAgeMs() {
  return gAnswerAtMs ? (millis() - gAnswerAtMs) : 0;
}

String solverLastAnswerHtml() {
  return F("<html><body>solver disabled</body></html>");
}

#endif
