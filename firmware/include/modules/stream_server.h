#pragma once

#include <Arduino.h>

class Camera;
class Modem;

class StreamServer {
 public:
  bool begin(Camera& camera, Modem& modem);

 private:
  Camera* camera_ = nullptr;
  Modem* modem_ = nullptr;
  void* httpServer_ = nullptr;
};
