#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <vector>

#if !USE_MOCK_CAMERA
#include "esp_camera.h"
#endif

struct CaptureResult {
  bool ok = false;
  size_t bytes = 0;
  std::vector<uint8_t> jpeg;
  const char* error = nullptr;
};

#if !USE_MOCK_CAMERA
void applyOv5640StreamTuning();
void setOv5640Colorbar(bool enable);
bool recoverJpegCamera();
#endif

class Camera {
 public:
  bool begin();
  bool isReady() const { return ready_; }
  CaptureResult capture();
#if USB_STREAM_ENABLE && !USE_MOCK_CAMERA
  bool reinitForUsbStream();
  bool recoverForStream();
#endif

#if !USE_MOCK_CAMERA
  // SoftAP/USB 预览应在拍照前暂停，避免占用 framebuffer。
  void setStreamingPaused(bool paused) { streamingPaused_ = paused; }
  bool isStreamingPaused() const { return streamingPaused_; }

  camera_fb_t* acquireFramebuffer(TickType_t timeout = portMAX_DELAY);
  void releaseFramebuffer(camera_fb_t* fb);
  void setColorbar(bool enable);
#endif

 private:
  CaptureResult captureMock();
#if !USE_MOCK_CAMERA
  CaptureResult captureHardware();
  void flushFrames(int count, TickType_t timeout);
  camera_fb_t* grabFrameWithRetry(int attempts);
#endif

  bool ready_ = false;
#if !USE_MOCK_CAMERA
  SemaphoreHandle_t frameMutex_ = nullptr;
  volatile bool streamingPaused_ = false;
#endif
};
