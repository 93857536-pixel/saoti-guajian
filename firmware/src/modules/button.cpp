#include "config.h"
#include "modules/button.h"

void Button::begin(int gpio) {
  gpio_ = gpio;
  pinMode(gpio_, INPUT_PULLUP);
  last_raw_ = digitalRead(gpio_);
  stable_ = last_raw_;
  press_edge_ = false;
  short_edge_ = false;
  long_edge_ = false;
  long_fired_ = false;
  down_at_ = 0;
  debounce_at_ = millis();
}

void Button::update() {
  press_edge_ = false;
  short_edge_ = false;
  long_edge_ = false;
  if (gpio_ < 0) {
    return;
  }

  const bool raw = digitalRead(gpio_);
  if (raw != last_raw_) {
    last_raw_ = raw;
    debounce_at_ = millis();
  }
  if ((millis() - debounce_at_) < BUTTON_DEBOUNCE_MS) {
    return;
  }

  const bool now = last_raw_;
  // 按下
  if (stable_ == HIGH && now == LOW) {
    press_edge_ = true;
    down_at_ = millis();
    long_fired_ = false;
  }
  // 按住达长按阈值：触发一次
  if (stable_ == LOW && now == LOW && !long_fired_ &&
      (millis() - down_at_) >= BUTTON_LONG_PRESS_MS) {
    long_edge_ = true;
    long_fired_ = true;
  }
  // 松开：未触发长按时算短按
  if (stable_ == LOW && now == HIGH) {
    if (!long_fired_) {
      short_edge_ = true;
    }
    long_fired_ = false;
  }
  stable_ = now;
}
