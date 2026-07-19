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
void applyOv5640CaptureTuning();
void setOv5640Colorbar(bool enable);
void setOv5640Flash(bool on);
bool ensureOv5640Autofocus();
bool triggerOv5640Focus();
bool recoverJpegCamera();
#endif

class Camera {
 public:
  bool begin();
  bool isReady() const { return ready_; }
  CaptureResult capture();
  // BLE 取景：QQVGA 小图，不走 AI 流水线
  CaptureResult captureThumbnail();
  // 省电：deinit 摄像头；扫题前 wake()
  void sleep();
  bool wake();
  bool isSleeping() const { return sleeping_; }
  // 唤醒自检：轻量抓一帧，确认 DMA 未卡死
  bool probeFrame();
  // 串口诊断：抓实景 + colorbar 亮度，判断模组是否坏
  void runBlackDiag();
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
  void flushFrames(int count, TickType_t timeout);
  void setColorbar(bool enable);
#endif

 private:
  CaptureResult captureMock();
#if !USE_MOCK_CAMERA
  CaptureResult captureHardware();
  camera_fb_t* grabFrameWithRetry(int attempts);
  camera_fb_t* grabSharpestFrame(int candidates);
#endif

  bool ready_ = false;
  bool sleeping_ = false;
#if !USE_MOCK_CAMERA
  SemaphoreHandle_t frameMutex_ = nullptr;
  volatile bool streamingPaused_ = false;
  bool afReady_ = false;
#endif
};
