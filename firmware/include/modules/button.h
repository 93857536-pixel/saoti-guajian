#pragma once

#include <Arduino.h>

class Button {
 public:
  void begin(int gpio);
  void update();
  // 短按：松开时触发（时长 < 长按阈值）
  bool shortPressEdge() const { return short_edge_; }
  // 长按：按住达到阈值时触发一次
  bool longPressEdge() const { return long_edge_; }

  // 兼容旧接口：按下瞬间（不推荐再用于扫题）
  bool pressedEdge() const { return press_edge_; }
  bool longPress() const { return long_edge_; }

 private:
  int gpio_ = -1;
  bool last_raw_ = true;
  bool stable_ = true;
  bool press_edge_ = false;
  bool short_edge_ = false;
  bool long_edge_ = false;
  bool long_fired_ = false;
  uint32_t down_at_ = 0;
  uint32_t debounce_at_ = 0;
};
