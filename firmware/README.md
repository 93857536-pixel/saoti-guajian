# 扫题挂件固件

YD-ESP32-S3（N16R8）+ ST7789 + OV5640 + A7670。默认烧录即可用真屏整机固件。

3D 外壳（打印）：见 [`cad/PRINT.md`](cad/PRINT.md)，交付 `cad/stl/saoti_front.stl` + `saoti_back.stl`。

**AI 解题**：家用 WiFi + 阿里云通义 VL（`qwen-vl-plus`）。  
**4G**：开机探测 + 上传兜底（需模块蓝灯/独立 5V；硬件未通时不影响 WiFi 扫题）。

## 功能清单

| 功能 | 状态 | 说明 |
|------|------|------|
| ST7789 显示 | 可用 | 状态页 + ASCII 答案摘要 |
| OV5640 拍照扫题 | 可用* | 短按 BOOT / 串口 `s` / SoftAP `/scan` |
| 无摄像头降级 | 可用 | 固定题图测 AI；长按 BOOT / `t` / SoftAP `/test` |
| SoftAP 控制台 | 可用 | `/` `/answer` `/status` `/test` `/stream` |
| WiFi + 通义 VL | 可用 | 填 `secrets.h` |
| 4G A7670 | 代码就绪 | 依赖独立 5V 与模块开机；串口 `NET`/`DIAG` |
| USB 推流 | 可选 | 串口 `V`/`v` |

\* 成像质量取决于摄像头硬件与接线；微雪 C 型按 **D2–D9→GPIO6–13**（见 [WIRING.md](WIRING.md)）。

## 快速开始

```bash
cd ~/Documents/saoti-guajian-fw
cp include/secrets.example.h include/secrets.h   # 首次
# 编辑 secrets.h：OPENAI_API_KEY（百炼）+ 可上网 WiFi
pio run -e esp32s3-devkitc-1 -t upload
```

1. 屏显示 Idle 提示（`BOOT to scan` 或 `BOOT=AI test`）
2. **短按 BOOT**：有摄像头则拍照解题；无摄像头则固定图测 AI  
3. **长按 BOOT ≈3s**：固定题图测 AI  
4. 手机连 SoftAP `SaotiCam` / `saoti1234` → `http://192.168.4.1/`  
5. 完整中文答案看 `/answer`

## 串口命令（115200）

| 命令 | 作用 |
|------|------|
| `s` | 扫题（同短按 BOOT） |
| `t` | 固定图 AI 测试 |
| `?` | JSON 状态 |
| `NET` / `DIAG` | 4G 诊断 |
| `V` / `v` | 开/关 USB 推流 |
| `APN=xxx` | 设置蜂窝 APN |

## AI 配置

| 项 | 说明 |
|----|------|
| `include/secrets.h` | 百炼 Key + WiFi（已 gitignore） |
| 接口 | `dashscope.../compatible-mode/v1` |
| 模型 | `qwen-vl-plus`（`config.h` 可改） |

## 接线

详见 **[WIRING.md](WIRING.md)** 与 `include/pins.h`。

摘要：屏 SPI `1/3/38/39/40/41`；摄像 `4/5/6–13/15–18`；4G `ESP21→模RX`、`ESP2←模TX`、`PEN→48`、`PWK→47`，VIN 独立 5V。

## 环境

| env | 用途 |
|-----|------|
| `esp32s3-devkitc-1` | 整机（默认，启动探测 4G） |
| `esp32s3-devkitc-1-nolcd` | 无屏 USB 推流（跳过 modem） |
| `esp32s3-devkitc-1-4gtest` | 4G 联调 |
| `esp32s3-modem-bridge` | AT 透传 |
| `esp32s3-devkitc-1-mock` | 全 mock |

未配置 WiFi 时仍可 SoftAP 预览；解题需 STA 可上网。
