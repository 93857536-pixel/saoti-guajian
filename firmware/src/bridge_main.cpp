// 4G 模块单独透传测试：电脑 USB 串口 <-> 模块 UART（等同 FreeAT 的 Mac 侧）
// 接线见 WIRING.md：VIN 独立 5V、PEN→48、PWK→47、模TX→2、模RX→21
#include <Arduino.h>
#include "pins.h"

namespace {
HardwareSerial& kModem = Serial2;
constexpr uint32_t kBaud = 115200;

void modemEnableAndBoot() {
  if (pins::MODEM_PEN >= 0) {
    pinMode(pins::MODEM_PEN, OUTPUT);
    digitalWrite(pins::MODEM_PEN, HIGH);
  }
  if (pins::MODEM_PWRKEY >= 0) {
    pinMode(pins::MODEM_PWRKEY, OUTPUT);
    digitalWrite(pins::MODEM_PWRKEY, HIGH);
    delay(300);
    digitalWrite(pins::MODEM_PWRKEY, LOW);
    delay(1200);
    digitalWrite(pins::MODEM_PWRKEY, HIGH);
  }
}
}  // namespace

void setup() {
  Serial.begin(kBaud);
  delay(300);

  modemEnableAndBoot();
  kModem.begin(kBaud, SERIAL_8N1, pins::MODEM_RX, pins::MODEM_TX);

  Serial.println();
  Serial.println("=== FS-MCore-A7670G UART bridge (FreeAT 替代) ===");
  Serial.printf("USB<->modem @ %lu baud\n", static_cast<unsigned long>(kBaud));
  Serial.printf("ESP-TX(GPIO%d)->mod RX | ESP-RX(GPIO%d)<-mod TX\n", pins::MODEM_TX,
                pins::MODEM_RX);
  Serial.printf("PEN=GPIO%d HIGH | PWK=GPIO%d pulsed\n", pins::MODEM_PEN,
                pins::MODEM_PWRKEY);
  Serial.println("Power: module VIN=external 5V>=2A, common GND");
  Serial.println("Wait ~10s for BLUE LED, then type: AT");
  Serial.println("----------------------------------------");
  delay(8000);
}

void loop() {
  while (Serial.available()) {
    kModem.write(static_cast<uint8_t>(Serial.read()));
  }
  while (kModem.available()) {
    Serial.write(static_cast<uint8_t>(kModem.read()));
  }
}
