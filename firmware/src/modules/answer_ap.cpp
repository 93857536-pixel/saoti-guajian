#include "modules/answer_ap.h"

#include "config.h"
#include "modules/solver.h"

#if ANSWER_SOFTAP_ENABLE
#include <WiFi.h>
#include "esp_http_server.h"
#endif

namespace answer_ap {
namespace {

#if ANSWER_SOFTAP_ENABLE
bool gActive = false;
httpd_handle_t gServer = nullptr;

esp_err_t sendText(httpd_req_t* req, const char* type, const String& body) {
  httpd_resp_set_type(req, type);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, body.c_str(), body.length());
}

esp_err_t indexHandler(httpd_req_t* req) {
  String html;
  html.reserve(512);
  html += F("<!DOCTYPE html><html><head><meta charset=utf-8>"
            "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
            "<title>扫题挂件</title></head><body style=\"font-family:sans-serif;"
            "padding:16px;line-height:1.5\">"
            "<h1>扫题挂件</h1>"
            "<p><a href=\"/answer\">打开完整答案</a></p>"
            "<p style=\"color:#666\">答案页关闭或超时后热点会自动关闭。</p>"
            "</body></html>");
  return sendText(req, "text/html; charset=utf-8", html);
}

esp_err_t answerHandler(httpd_req_t* req) {
  return sendText(req, "text/html; charset=utf-8", solverLastAnswerHtml());
}
#endif

}  // namespace

bool start() {
#if !ANSWER_SOFTAP_ENABLE
  return false;
#else
#if STREAM_ENABLE
  // 已有完整 SoftAP 控制台时不必再起一套
  if (WiFi.getMode() & WIFI_AP) {
    gActive = true;
    return true;
  }
#endif
  if (gActive && gServer) {
    return true;
  }

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(STREAM_AP_SSID, STREAM_AP_PASS)) {
    Serial.println("[ANSWER-AP] softAP failed");
    WiFi.mode(WIFI_OFF);
    return false;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = STREAM_PORT;
  config.ctrl_port = STREAM_PORT + 1;
  config.max_uri_handlers = 4;
  config.lru_purge_enable = true;

  if (httpd_start(&gServer, &config) != ESP_OK) {
    Serial.println("[ANSWER-AP] httpd_start failed");
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    gServer = nullptr;
    return false;
  }

  httpd_uri_t indexUri = {
      .uri = "/", .method = HTTP_GET, .handler = indexHandler, .user_ctx = nullptr};
  httpd_uri_t answerUri = {.uri = "/answer",
                           .method = HTTP_GET,
                           .handler = answerHandler,
                           .user_ctx = nullptr};
  httpd_register_uri_handler(gServer, &indexUri);
  httpd_register_uri_handler(gServer, &answerUri);

  gActive = true;
  Serial.printf("[ANSWER-AP] WiFi \"%s\" / %s → http://%s/answer\n",
                STREAM_AP_SSID, STREAM_AP_PASS,
                WiFi.softAPIP().toString().c_str());
  return true;
#endif
}

void stop() {
#if !ANSWER_SOFTAP_ENABLE
  return;
#elif STREAM_ENABLE
  // 常驻 SoftAP 控制台时不要拆掉
  gActive = false;
#else
  if (gServer) {
    httpd_stop(gServer);
    gServer = nullptr;
  }
  if (gActive || (WiFi.getMode() & WIFI_AP)) {
    WiFi.softAPdisconnect(true);
#if NET_CELL_ONLY
    WiFi.mode(WIFI_OFF);
#endif
  }
  gActive = false;
  Serial.println("[ANSWER-AP] stopped");
#endif
}

bool active() {
#if ANSWER_SOFTAP_ENABLE
  return gActive;
#else
  return false;
#endif
}

String ip() {
#if ANSWER_SOFTAP_ENABLE
  if (gActive && (WiFi.getMode() & WIFI_AP)) {
    return WiFi.softAPIP().toString();
  }
#endif
  return String("0.0.0.0");
}

}  // namespace answer_ap
