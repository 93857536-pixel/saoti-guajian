#pragma once

#include <Arduino.h>

class Button {
 public:
  void begin(int gpio);
  void update();
  bool pressedEdge() const { return edge_; }
  bool longPress() const { return long_; }

 private:
  int gpio_ = -1;
  bool last_ = true;
  bool edge_ = false;
  bool long_ = false;
  uint32_t down_at_ = 0;
};
