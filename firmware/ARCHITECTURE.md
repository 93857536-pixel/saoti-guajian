# 扫题挂件 — 项目总体架构

## 1. 系统目标

ESP32-S3「扫题挂件」：BOOT 键/串口触发 OV5640 拍照 → WiFi SoftAP 或 4G 上传 → ST7789 显示状态；Mac **SaotiCam** 可通过 USB 或 WiFi MJPEG 预览。

## 2. 最终接线（YD-ESP32-S3 N16R8）

| 模块 | 信号 | ESP 丝印 | 备注 |
|------|------|----------|------|
| 微雪 ST7789 | VCC/GND | 3V3 / G | 勿用 5V 强行直灌裸屏 |
| | DIN/CLK/CS/DC/RST/BL | **1 / 3 / 38 / 39 / 40 / 41** | 勿用 35–37（OPI PSRAM） |
| OV5640 | SCCB/DVP | 4–5, 6–13, 15–18 | 既有映射 |
| FS-MCore 4G | TX/RX/PEN/PWK | 模TX→**2**，模RX→**21**，PEN→**48**，PWK→**47** | VIN 独立 5V≥2A；NET 悬空 |
| 拍照键 | 板载 BOOT | **0** | 短按拍照；3D 壳开孔露出，不另接按键 |

```
电源: 电池 → TP4056 → MT3608(5.0V) → ESP 5V + 4G VIN（共地，近 4G 加 1000µF）
```

## 3. 固件模块

```
main.cpp
  ├─ Display     ST7789（TFT_eSPI + HSPI）
  ├─ Camera      OV5640 JPEG
  ├─ Modem       A7670 AT + SoftAP WiFi 双路径
  ├─ Button      GPIO0
  ├─ UsbStream   SC 协议推流
  └─ StreamServer SoftAP MJPEG :80
```

流程：`Idle → 按键/s → Capture → Upload → Result → Idle`

## 4. 编译环境

| env | 用途 |
|-----|------|
| `esp32s3-devkitc-1` | **默认整机**：真屏+摄+4G 快探+USB/WiFi 推流 |
| `esp32s3-devkitc-1-nolcd` | 无屏 USB 推流联调 |
| `esp32s3-devkitc-1-4gtest` | 专注 4G AT |
| `esp32s3-modem-bridge` | 电脑直发 AT 透传 |
| `esp32s3-devkitc-1-mock` | 全 mock |

```bash
cd ~/Documents/saoti-guajian-fw
pio run -e esp32s3-devkitc-1 -t upload
```

## 5. 使用

1. 上电后屏显示 Boot → Ready / net offline  
2. 手机连 SoftAP `SaotiCam` / `saoti1234`，打开 `http://192.168.4.1/`  
3. 按 **BOOT** 或串口发 `s` 拍照上传  
4. Mac：原生 USB 或 CH343；`V` 开 USB 推流，`m` 做 4G 深诊断  

## 6. 已知限制

| 项 | 说明 |
|----|------|
| 4G 仅红灯 | 模块未开机，需独立 5V≥2A；程序已快失败，不阻塞整机 |
| GPIO35–37 | N16R8 OPI 占用，不能接屏 |
| GPIO47 | PSRAM 时钟干扰，不能接 4G RX |
