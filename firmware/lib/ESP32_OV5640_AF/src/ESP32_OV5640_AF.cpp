/*
  ESP32_OV5640_AF.cpp - Library for OV5640 Auto Focus (ESP32 Camera)
  Created by Eric Nam, December 08, 2021.
  Released into the public domain.
  Extended: singleAutoFocus + rename Ov5640Af
*/

#include "ESP32_OV5640_AF.h"

Ov5640Af::Ov5640Af() {
  isOV5640 = false;
  sensor = nullptr;
}

bool Ov5640Af::start(sensor_t* _sensor) {
  sensor = _sensor;
  if (!sensor || !sensor->get_reg) {
    isOV5640 = false;
    return false;
  }
  const uint8_t vid = sensor->get_reg(sensor, OV5640_CHIPID_HIGH, 0xff);
  const uint8_t pid = sensor->get_reg(sensor, OV5640_CHIPID_LOW, 0xff);
  isOV5640 = (vid == 0x56) && (pid == 0x40);
  return isOV5640;
}

uint8_t Ov5640Af::focusInit() {
  if (!isOV5640 || !sensor || !sensor->set_reg) {
    return 0xFF;
  }

  uint16_t addr = 0x8000;
  if (sensor->set_reg(sensor, 0x3000, 0xff, 0x20) < 0) {  // reset MCU
    return 0xFF;
  }

  for (uint16_t i = 0; i < sizeof(OV5640_AF_Config); i++) {
    if (sensor->set_reg(sensor, addr, 0xff, OV5640_AF_Config[i]) < 0) {
      return 0xFF;
    }
    addr++;
  }

  sensor->set_reg(sensor, OV5640_CMD_MAIN, 0xff, 0x00);
  sensor->set_reg(sensor, OV5640_CMD_ACK, 0xff, 0x00);
  sensor->set_reg(sensor, OV5640_CMD_PARA0, 0xff, 0x00);
  sensor->set_reg(sensor, OV5640_CMD_PARA1, 0xff, 0x00);
  sensor->set_reg(sensor, OV5640_CMD_PARA2, 0xff, 0x00);
  sensor->set_reg(sensor, OV5640_CMD_PARA3, 0xff, 0x00);
  sensor->set_reg(sensor, OV5640_CMD_PARA4, 0xff, 0x00);
  sensor->set_reg(sensor, OV5640_CMD_FW_STATUS, 0xff, 0x7f);
  sensor->set_reg(sensor, 0x3000, 0xff, 0x00);

  for (uint16_t i = 0; i < 1000; i++) {
    const uint8_t state = sensor->get_reg(sensor, OV5640_CMD_FW_STATUS, 0xff);
    if (state == FW_STATUS_S_IDLE) {
      return 0;
    }
    delay(5);
  }
  return 1;
}

uint8_t Ov5640Af::autoFocusMode() {
  if (!isOV5640 || !sensor || !sensor->set_reg) {
    return 0xFF;
  }

  sensor->set_reg(sensor, OV5640_CMD_MAIN, 0xff, 0x01);
  sensor->set_reg(sensor, OV5640_CMD_MAIN, 0xff, 0x08);
  for (uint16_t retry = 0; retry < 1000; retry++) {
    if (sensor->get_reg(sensor, OV5640_CMD_ACK, 0xff) == 0x00) {
      break;
    }
    delay(5);
  }
  sensor->set_reg(sensor, OV5640_CMD_ACK, 0xff, 0x01);
  sensor->set_reg(sensor, OV5640_CMD_MAIN, 0xff, AF_CONTINUE_AUTO_FOCUS);
  for (uint16_t retry = 0; retry < 1000; retry++) {
    if (sensor->get_reg(sensor, OV5640_CMD_ACK, 0xff) == 0x00) {
      return 0;
    }
    delay(5);
  }
  return 2;
}

uint8_t Ov5640Af::singleAutoFocus(uint16_t timeoutMs) {
  if (!isOV5640 || !sensor || !sensor->set_reg) {
    return 0xFF;
  }

  sensor->set_reg(sensor, OV5640_CMD_MAIN, 0xff, 0x01);
  sensor->set_reg(sensor, OV5640_CMD_MAIN, 0xff, 0x08);
  for (uint16_t retry = 0; retry < 200; retry++) {
    if (sensor->get_reg(sensor, OV5640_CMD_ACK, 0xff) == 0x00) {
      break;
    }
    delay(5);
  }
  sensor->set_reg(sensor, OV5640_CMD_ACK, 0xff, 0x01);
  sensor->set_reg(sensor, OV5640_CMD_MAIN, 0xff, AF_TRIG_SINGLE_AUTO_FOCUS);

  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    const uint8_t st = sensor->get_reg(sensor, OV5640_CMD_FW_STATUS, 0xff);
    if (st == FW_STATUS_S_FOCUSED || st == FW_STATUS_S_IDLE) {
      return 0;
    }
    delay(20);
  }
  return 3;  // timeout — 仍可能已部分对焦
}

uint8_t Ov5640Af::getFWStatus() {
  if (!isOV5640 || !sensor || !sensor->get_reg) {
    return 0xFF;
  }
  return sensor->get_reg(sensor, OV5640_CMD_FW_STATUS, 0xff);
}
