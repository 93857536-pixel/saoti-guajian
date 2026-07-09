#include "config.h"
#include "modules/button.h"

void Button::begin(int gpio) {
  gpio_ = gpio;
  pinMode(gpio_, INPUT_PULLUP);
  last_ = digitalRead(gpio_);
}

void Button::update() {
  edge_ = false;
  long_ = false;
  if (gpio_ < 0) return;

  const bool now = digitalRead(gpio_);
  if (last_ == HIGH && now == LOW) {
    edge_ = true;
    down_at_ = millis();
  }
  if (last_ == LOW && now == LOW && (millis() - down_at_) >= BUTTON_LONG_PRESS_MS) {
    long_ = true;
  }
  last_ = now;
}
