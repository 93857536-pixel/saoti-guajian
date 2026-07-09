#include "config.h"
#include "modules/camera.h"
#include "modules/modem.h"
#include "modules/stream_server.h"

#if STREAM_ENABLE

#include "esp_http_server.h"

namespace {

#define STREAM_BOUNDARY "123456789000000000000000000000"
#define STREAM_CONTENT_TYPE \
  "multipart/x-mixed-replace;boundary=" STREAM_BOUNDARY

struct StreamContext {
  Camera* camera;
};

esp_err_t sendJpeg(httpd_req_t* req, Camera* camera) {
  if (!camera || !camera->isReady()) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

#if USE_MOCK_CAMERA
  httpd_resp_send_500(req);
  return ESP_FAIL;
#else
  camera_fb_t* fb = camera->acquireFramebuffer();
  if (!fb) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  if (fb->format != PIXFORMAT_JPEG) {
    camera->releaseFramebuffer(fb);
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  esp_err_t res = ESP_OK;
  char partHeader[128];
  const int headerLen = snprintf(partHeader, sizeof(partHeader),
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
  if (!ctx || !ctx->camera) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
  httpd_resp_set_hdr(req, "Connection", "close");

  while (true) {
    if (sendJpeg(req, ctx->camera) != ESP_OK) {
      break;
    }
    if (httpd_resp_send_chunk(req, nullptr, 0) != ESP_OK) {
      break;
    }
    if (httpd_req_get_hdr_value_len(req, "Connection") > 0) {
      // client may have closed; next chunk send will fail if so
    }
    delay(1);
  }
  return ESP_OK;
}

esp_err_t captureHandler(httpd_req_t* req) {
  auto* ctx = static_cast<StreamContext*>(req->user_ctx);
  if (!ctx || !ctx->camera) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

#if USE_MOCK_CAMERA
  httpd_resp_send_500(req);
  return ESP_FAIL;
#else
  camera_fb_t* fb = ctx->camera->acquireFramebuffer();
  if (!fb) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  if (fb->format != PIXFORMAT_JPEG) {
    ctx->camera->releaseFramebuffer(fb);
    httpd_resp_send_500(req);
    return ESP_FAIL;
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
  const char* html =
      "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<title>SaotiCam</title></head><body>"
      "<h1>SaotiCam MJPEG</h1>"
      "<p><a href=\"/stream\">Live stream</a> | "
      "<a href=\"/capture\">Snapshot</a></p>"
      "<img src=\"/stream\" style=\"max-width:100%\">"
      "</body></html>";
  (void)ctx;
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

}  // namespace

bool StreamServer::begin(Camera& camera, Modem& modem) {
  camera_ = &camera;
  modem_ = &modem;

  if (!camera_->isReady()) {
    Serial.println("[STREAM] camera not ready");
    return false;
  }
  if (!modem_->ensureWifiForStreaming()) {
    Serial.println("[STREAM] WiFi/AP setup failed");
    return false;
  }

  static StreamContext ctx;
  ctx.camera = camera_;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = STREAM_PORT;
  config.ctrl_port = STREAM_PORT + 1;
  config.max_uri_handlers = 8;
  config.lru_purge_enable = true;

  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &config) != ESP_OK) {
    Serial.println("[STREAM] httpd_start failed");
    return false;
  }

  httpd_uri_t streamUri = {
      .uri = "/stream",
      .method = HTTP_GET,
      .handler = streamHandler,
      .user_ctx = &ctx,
  };
  httpd_uri_t indexUri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = indexHandler,
      .user_ctx = &ctx,
  };
  httpd_uri_t captureUri = {
      .uri = "/capture",
      .method = HTTP_GET,
      .handler = captureHandler,
      .user_ctx = &ctx,
  };

  httpd_register_uri_handler(server, &streamUri);
  httpd_register_uri_handler(server, &indexUri);
  httpd_register_uri_handler(server, &captureUri);

  httpServer_ = server;
  const String ip = modem_->streamingIp();
  Serial.printf("[STREAM] MJPEG http://%s:%d/stream\n", ip.c_str(), STREAM_PORT);
  Serial.printf("[STREAM] status  http://%s:%d/\n", ip.c_str(), STREAM_PORT);
  return true;
}

#else

bool StreamServer::begin(Camera& camera, Modem& modem) {
  (void)camera;
  (void)modem;
  return false;
}

#endif
