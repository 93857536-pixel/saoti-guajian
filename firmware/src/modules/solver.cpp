#include "config.h"
#include "modules/solver.h"

#if USE_OPENAI_SOLVER

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "mbedtls/base64.h"
#include "esp_heap_caps.h"

namespace {

String gSharedAnswer;
String gLastError;
uint32_t gAnswerAtMs = 0;

bool wifiConfigured() {
  return WIFI_SSID[0] != '\0' && strcmp(WIFI_SSID, "YOUR_SSID") != 0;
}

bool ensureStaInternet() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }
  if (!wifiConfigured()) {
    return false;
  }
  // 保留 SoftAP 预览，同时连可上网 WiFi
  WiFi.mode(WIFI_AP_STA);
  // IDLE 也需要 begin（仅 DISCONNECTED 时 begin 会漏掉部分状态）
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

// 从 JSON 文本中提取指定 key 的字符串值（支持转义与 \\uXXXX 基本 BMP）
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

// 优先解析 OpenAI/DashScope: choices[0].message.content
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

}  // namespace

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

uint32_t solverAnswerAgeMs() {
  if (gAnswerAtMs == 0) {
    return 0;
  }
  return millis() - gAnswerAtMs;
}

String solverLastAnswerHtml() {
  String html;
  html.reserve(gSharedAnswer.length() + gLastError.length() + 512);
  html += F("<!doctype html><html><head><meta charset=utf-8>"
            "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
            "<title>扫题答案</title><style>"
            "body{font-family:-apple-system,sans-serif;padding:16px;line-height:1.5;"
            "background:#f7f7f5;color:#111}"
            "pre{white-space:pre-wrap;background:#111;color:#eee;padding:12px;"
            "border-radius:8px}"
            ".err{color:#b00020}.meta{color:#666;font-size:14px}"
            "a{color:#0b57d0}</style></head><body><h2>扫题答案</h2>");
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
  if (strncmp(OPENAI_API_KEY, "sk-", 3) != 0 ||
      strcmp(OPENAI_API_KEY, "sk-REPLACE_ME") == 0) {
    r.error = "set API key";
    solverSetLastError(r.error);
    return r;
  }
  if (!ensureStaInternet()) {
    r.error = "need WiFi (secrets.h)";
    solverSetLastError(r.error);
    return r;
  }

  char* b64 = nullptr;
  size_t b64Len = 0;
  Serial.printf("[AI] jpeg %u bytes, base64...\n", static_cast<unsigned>(len));
  if (!encodeBase64(jpeg, len, &b64, &b64Len)) {
    r.error = "base64 OOM";
    solverSetLastError(r.error);
    return r;
  }

  const char* prefix =
      "{\"model\":\"" OPENAI_MODEL "\","
      "\"max_tokens\":800,"
      "\"messages\":[{\"role\":\"user\",\"content\":["
      "{\"type\":\"text\",\"text\":\"你是扫题挂件助手。请识别图片中的题目，"
      "用中文给出：1)最终答案 2)简要解题步骤（尽量简洁，适合小屏幕）。"
      "若看不清请说明。\"},"
      "{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,";
  const char* suffix = "\"}}]}]}";

  const size_t jsonLen = strlen(prefix) + b64Len + strlen(suffix);
  char* json = mallocPsram(jsonLen + 1);
  if (!json) {
    free(b64);
    r.error = "json OOM";
    solverSetLastError(r.error);
    return r;
  }
  memcpy(json, prefix, strlen(prefix));
  memcpy(json + strlen(prefix), b64, b64Len);
  memcpy(json + strlen(prefix) + b64Len, suffix, strlen(suffix) + 1);
  free(b64);
  b64 = nullptr;

  Serial.printf("[AI] POST %s (%u bytes) model=%s\n", OPENAI_BASE_URL,
                static_cast<unsigned>(jsonLen), OPENAI_MODEL);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(OPENAI_TIMEOUT_MS / 1000);

  HTTPClient http;
  http.setTimeout(OPENAI_TIMEOUT_MS);
  http.setReuse(false);
  if (!http.begin(client, OPENAI_BASE_URL)) {
    free(json);
    r.error = "http begin fail";
    solverSetLastError(r.error);
    return r;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + OPENAI_API_KEY);

  const int code = http.POST(reinterpret_cast<uint8_t*>(json), jsonLen);
  free(json);
  r.httpCode = code;

  String body = http.getString();
  http.end();
  Serial.printf("[AI] HTTP %d, body %u\n", code, static_cast<unsigned>(body.length()));

  if (code < 200 || code >= 300) {
    r.error = "ai http error";
    solverSetLastError(r.error);
    if (body.length() > 0) {
      Serial.println(body.substring(0, 400));
    }
    return r;
  }

  String answer = extractAssistantContent(body);
  if (answer.isEmpty()) {
    r.error = "parse answer fail";
    solverSetLastError(r.error);
    Serial.println(body.substring(0, 500));
    return r;
  }

  answer.trim();
  lastAnswer_ = answer;
  solverSetSharedAnswer(answer);
  r.ok = true;
  r.answer = answer;
  Serial.println("[AI] ---- answer ----");
  Serial.println(answer);
  Serial.println("[AI] ----------------");
  return r;
}

#else  // !USE_OPENAI_SOLVER

namespace {
String gSharedAnswer;
String gLastError;
uint32_t gAnswerAtMs = 0;
}  // namespace

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

uint32_t solverAnswerAgeMs() {
  return gAnswerAtMs ? (millis() - gAnswerAtMs) : 0;
}

String solverLastAnswerHtml() {
  return F("<html><body>solver disabled</body></html>");
}

#endif
