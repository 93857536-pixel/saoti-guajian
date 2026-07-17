#include "config.h"
#include "app_control.h"
#include "modules/camera.h"
#include "modules/modem.h"
#include "modules/solver.h"
#include "modules/stream_server.h"

#if STREAM_ENABLE

#include <WiFi.h>
#include "esp_http_server.h"

namespace {

#define STREAM_BOUNDARY "123456789000000000000000000000"
#define STREAM_CONTENT_TYPE \
  "multipart/x-mixed-replace;boundary=" STREAM_BOUNDARY

struct StreamContext {
  Camera* camera;
  Modem* modem;
};

esp_err_t sendText(httpd_req_t* req, const char* type, const String& body,
                   int status = 200) {
  httpd_resp_set_status(req, status == 200 ? HTTPD_200 : "503 Service Unavailable");
  httpd_resp_set_type(req, type);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, body.c_str(), body.length());
}

esp_err_t sendJpeg(httpd_req_t* req, Camera* camera) {
  if (!camera || !camera->isReady()) {
    return ESP_FAIL;
  }

#if USE_MOCK_CAMERA
  return ESP_FAIL;
#else
  // 拍照期间让出 framebuffer，不结束 MJPEG 连接
  while (camera->isStreamingPaused()) {
    delay(40);
  }

  // 短超时，避免拍照路径拿不到 frameMutex_
  camera_fb_t* fb = camera->acquireFramebuffer(pdMS_TO_TICKS(150));
  if (!fb) {
    return ESP_FAIL;
  }
  if (fb->format != PIXFORMAT_JPEG) {
    camera->releaseFramebuffer(fb);
    return ESP_FAIL;
  }

  esp_err_t res = ESP_OK;
  char partHeader[128];
  const int headerLen = snprintf(
      partHeader, sizeof(partHeader),
      "\r\n--" STREAM_BOUNDARY
      "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
      static_cast<unsigned>(fb->len));
  if (headerLen <= 0 ||
      httpd_resp_send_chunk(req, partHeader, headerLen) != ESP_OK) {
    res = ESP_FAIL;
  } else if (httpd_resp_send_chunk(req, reinterpret_cast<const char*>(fb->buf),
                                   fb->len) != ESP_OK) {
    res = ESP_FAIL;
  }
  camera->releaseFramebuffer(fb);
  return res;
#endif
}

esp_err_t streamHandler(httpd_req_t* req) {
  auto* ctx = static_cast<StreamContext*>(req->user_ctx);
  if (!ctx || !ctx->camera || !ctx->camera->isReady()) {
    return sendText(req, "text/plain; charset=utf-8",
                    F("camera offline — 接好摄像头后再打开 /stream"), 503);
  }

  httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
  httpd_resp_set_hdr(req, "Connection", "close");

  while (true) {
#if !USE_MOCK_CAMERA
    if (ctx->camera->isStreamingPaused()) {
      delay(50);
      continue;
    }
#endif
    if (sendJpeg(req, ctx->camera) != ESP_OK) {
      break;
    }
    if (httpd_resp_send_chunk(req, nullptr, 0) != ESP_OK) {
      break;
    }
    delay(1);
  }
  return ESP_OK;
}

esp_err_t captureHandler(httpd_req_t* req) {
  auto* ctx = static_cast<StreamContext*>(req->user_ctx);
  if (!ctx || !ctx->camera || !ctx->camera->isReady()) {
    return sendText(req, "application/json",
                    F("{\"ok\":false,\"error\":\"camera offline\"}"), 503);
  }

#if USE_MOCK_CAMERA
  return sendText(req, "application/json",
                  F("{\"ok\":false,\"error\":\"mock camera\"}"), 503);
#else
  if (ctx->camera->isStreamingPaused()) {
    return sendText(req, "application/json",
                    F("{\"ok\":false,\"error\":\"capture busy\"}"), 503);
  }
  camera_fb_t* fb = ctx->camera->acquireFramebuffer(pdMS_TO_TICKS(500));
  if (!fb) {
    return sendText(req, "application/json",
                    F("{\"ok\":false,\"error\":\"no frame\"}"), 503);
  }
  if (fb->format != PIXFORMAT_JPEG) {
    ctx->camera->releaseFramebuffer(fb);
    return sendText(req, "application/json",
                    F("{\"ok\":false,\"error\":\"not jpeg\"}"), 503);
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  const esp_err_t res =
      httpd_resp_send(req, reinterpret_cast<const char*>(fb->buf), fb->len);
  ctx->camera->releaseFramebuffer(fb);
  return res;
#endif
}

esp_err_t indexHandler(httpd_req_t* req) {
  auto* ctx = static_cast<StreamContext*>(req->user_ctx);
  const bool camOk = ctx && ctx->camera && ctx->camera->isReady();
  const bool wifiOk = WiFi.status() == WL_CONNECTED;
  const bool cellOk = ctx && ctx->modem && ctx->modem->isCellReady();
  const String ip = (ctx && ctx->modem) ? ctx->modem->streamingIp() : String("?");

  String html;
  html.reserve(1800);
  html += F("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>扫题挂件</title><style>"
            "body{font-family:-apple-system,sans-serif;padding:16px;background:#f7f7f5;"
            "color:#111;line-height:1.5}"
            ".ok{color:#0a7a32}.bad{color:#b00020}"
            "a{color:#0b57d0}img{max-width:100%;border-radius:8px;margin-top:12px}"
            "ul{padding-left:18px}</style></head><body>");
  html += F("<h1>扫题挂件</h1><p>SoftAP 控制台 · ");
  html += ip;
  html += F("</p><ul>");
  html += F("<li>摄像头：<span class=");
  html += camOk ? F("ok>OK") : F("bad>离线");
  html += F("</span></li><li>WiFi STA：<span class=");
  html += wifiOk ? F("ok>OK") : F("bad>未连");
  html += F("</span></li><li>4G：<span class=");
  html += cellOk ? F("ok>OK") : F("bad>未通");
  html += F("</span></li></ul>");
  html += F("<p><a href=\"/answer\">最新答案</a> · "
            "<a href=\"/test\">固定题图测 AI</a> · "
            "<a href=\"/status\">状态 JSON</a>");
  if (camOk) {
    html += F(" · <a href=\"/stream\">实时预览</a> · "
              "<a href=\"/capture\">拍照 JPEG</a>");
  }
  html += F("</p><p>设备操作：短按 BOOT 扫题；长按 BOOT ≈3s 测 AI；"
            "串口 <code>s</code>/<code>t</code>/<code>?</code></p>");
  if (camOk) {
    html += F("<img src=\"/stream\" alt=\"preview\">");
  } else {
    html += F("<p class=bad>摄像头离线：可先用 /test 验证 WiFi+AI。"
              "微雪 C 型请按丝印 D2–D9 接到 GPIO6–13。</p>");
  }
  html += F("</body></html>");
  return sendText(req, "text/html; charset=utf-8", html);
}

esp_err_t answerHandler(httpd_req_t* req) {
  return sendText(req, "text/html; charset=utf-8", solverLastAnswerHtml());
}

esp_err_t statusHandler(httpd_req_t* req) {
  return sendText(req, "application/json", appStatusJson());
}

esp_err_t testHandler(httpd_req_t* req) {
  appRequestFixedAiTest();
  return sendText(
      req, "application/json",
      F("{\"ok\":true,\"queued\":true,\"hint\":\"fixed-image AI queued; "
        "open /answer in ~30s\"}"));
}

esp_err_t captureSolveHandler(httpd_req_t* req) {
  appRequestCapture();
  return sendText(
      req, "application/json",
      F("{\"ok\":true,\"queued\":true,\"hint\":\"capture/solve queued; "
        "open /answer when done\"}"));
}

}  // namespace

bool StreamServer::begin(Camera& camera, Modem& modem) {
  camera_ = &camera;
  modem_ = &modem;

  if (!modem_->ensureWifiForStreaming()) {
    Serial.println("[STREAM] WiFi/AP setup failed");
    return false;
  }

  if (!camera_->isReady()) {
    Serial.println("[STREAM] camera offline — SoftAP console + /answer + /test");
  }

  static StreamContext ctx;
  ctx.camera = camera_;
  ctx.modem = modem_;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = STREAM_PORT;
  config.ctrl_port = STREAM_PORT + 1;
  config.max_uri_handlers = 12;
  config.lru_purge_enable = true;

  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &config) != ESP_OK) {
    Serial.println("[STREAM] httpd_start failed");
    return false;
  }

  httpd_uri_t streamUri = {.uri = "/stream",
                           .method = HTTP_GET,
                           .handler = streamHandler,
                           .user_ctx = &ctx};
  httpd_uri_t indexUri = {
      .uri = "/", .method = HTTP_GET, .handler = indexHandler, .user_ctx = &ctx};
  httpd_uri_t captureUri = {.uri = "/capture",
                            .method = HTTP_GET,
                            .handler = captureHandler,
                            .user_ctx = &ctx};
  httpd_uri_t answerUri = {.uri = "/answer",
                           .method = HTTP_GET,
                           .handler = answerHandler,
                           .user_ctx = nullptr};
  httpd_uri_t statusUri = {.uri = "/status",
                           .method = HTTP_GET,
                           .handler = statusHandler,
                           .user_ctx = nullptr};
  httpd_uri_t testUri = {
      .uri = "/test", .method = HTTP_GET, .handler = testHandler, .user_ctx = nullptr};
  httpd_uri_t scanUri = {.uri = "/scan",
                         .method = HTTP_GET,
                         .handler = captureSolveHandler,
                         .user_ctx = nullptr};

  httpd_register_uri_handler(server, &streamUri);
  httpd_register_uri_handler(server, &indexUri);
  httpd_register_uri_handler(server, &captureUri);
  httpd_register_uri_handler(server, &answerUri);
  httpd_register_uri_handler(server, &statusUri);
  httpd_register_uri_handler(server, &testUri);
  httpd_register_uri_handler(server, &scanUri);

  httpServer_ = server;
  const String ip = modem_->streamingIp();
  Serial.printf("[STREAM] console http://%s:%d/\n", ip.c_str(), STREAM_PORT);
  Serial.printf("[STREAM] answer  http://%s:%d/answer\n", ip.c_str(), STREAM_PORT);
  Serial.printf("[STREAM] test AI http://%s:%d/test\n", ip.c_str(), STREAM_PORT);
  Serial.printf("[STREAM] status  http://%s:%d/status\n", ip.c_str(), STREAM_PORT);
  return true;
}

#else

bool StreamServer::begin(Camera& camera, Modem& modem) {
  (void)camera;
  (void)modem;
  return false;
}

#endif
