# FS-MCore A7670G 基带固件升级（解决 HTTPS 715）

本机实测（2026-07-18 `FW`）：

| 项 | 值 |
|----|-----|
| Model | **A7670G-LABE** |
| Revision | **A110B06A7670M7**（`A7670M7_B06V01_241111`） |
| ATI 显示 | V1.11.2 |
| IMEI | `867284069506675` |
| 现象 | `httpbin.org` HTTPS OK；百炼 → **715 SSL 失败** |

**结论：本机已是公开渠道最新的 LABE 包（A110B06），再刷同一包通常无效。**  
若店家另有更新私有包可再试；否则应改走 **ESP 侧 TLS（方案 1）**，不要刷 LASE/LLSE。

---

## 警告（必读）

1. **只能刷 A7670G-LABE 包**。刷成 LASE / LLSE / A7670E 等会导致变砖或反复重启。
2. 升级工具是 **Windows** 程序；Mac 不能直接烧录基带。
3. 用 **V1.32** 烧录工具，**不要**用有 bug 的 V1.13。
4. 升级前记下 `FW` 输出的完整 `AT+SIMCOMATI` 和 IMEI；个别包会清 IMEI，可用 `AT+SIMEI="你的号码"` 写回。
5. **优先问店家**要「A7670G-LABE」官方升级包；第三方包仅作备选。

---

## 需要准备

- Windows 电脑
- 模块 **Type-C USB** 线
- 模块 **VIN 独立 5V ≥2A**（升级时也建议外供），GND 与电脑共地
- PEN 接 3.3V（或 ESP 的 3V3）
- 驱动 + 烧录工具（LilyGo 汇总，与 SIMCom MADL 同源）：
  - 驱动：[Windows USB Drivers](https://drive.google.com/drive/folders/1-7x2z00a5VO7GZS96C6mDupNTBXIh--H)
  - 工具（选 **V1.32**）：[A7670X/A7608X Flash Tools](https://drive.google.com/file/d/12nt5-wcsUT6bRaEhfOMBSq0EhOl8R2by/view?usp=sharing)
- 固件包（**仅 LABE**，备选）：
  - 🆕 [A7670G-LABE A110B06](https://drive.google.com/file/d/10fsorFI8SuTlcffufgphpYbt2JjUpqtg/view?usp=sharing)
  - 回退用 [A7670G-LABE A069B01](https://drive.google.com/file/d/10oSTTqhw7ZiiZ_LiqAb3WYV3LXwbpKtV/view?usp=sharing)

图文总览（LilyGo）：  
https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/docs/cn/update_fw.md  
视频参考：https://youtu.be/AZkm-Z7mKn8

---

## 步骤

### 1. 升级前备份版本

ESP 已接好时，串口（115200）发：

```text
FW
```

应看到 `Model: A7670G-LABE`。把整段 `SIMCOMATI` 复制保存。

或用店家 **FreeAT**：模块 USB 插 Windows → `AT+SIMCOMATI`。

### 2. 进入升级模式（BOOT）

FS-MCore 上找 **BOOT / SBOOT / BOOT_N** 焊盘或按键（店家资料图为准）：

1. **按住 BOOT 不放**
2. 插上模块 Type-C（必要时再接 VIN 5V）
3. Windows「设备管理器」出现下载口 / 未知设备后，再松开 BOOT
4. 按工具说明给所有未知设备装驱动（可能有多个，逐个装完）

若没有按键：用杜邦线在上电瞬间把 BOOT 脚拉到说明书要求的电平（通常对地或对 1.8V，以店家为准）。

### 3. 用 MADL V1.32 烧录

1. 解压固件包，打开 **`A76XX_A79XX_MADL V1.32 Only for Update`**
2. 选择解压出的 **A7670G-LABE** 镜像目录
3. 选中设备管理器里的下载 COM 口
4. 点 **GO**，等进度条跑完，不要中途拔线

### 4. 升级后核对

正常上电（不必再按 BOOT），串口发：

```text
FW
```

期望类似：

```text
Model: A7670G-LABE
Revision: A110B06A7670M7
...
```

然后：

```text
HTEST
t
```

- `HTEST` → httpbin 仍应 200  
- `t` → 若百炼不再 715，说明 TLS 已改善  

若 IMEI 变空：在 FreeAT / 透传里执行 `AT+SIMEI="原IMEI"`（以 SIMCom 手册为准）。

---

## 失败时

| 情况 | 处理 |
|------|------|
| 刷错成 LLSE/LASE | 立刻用 **LABE** 包再刷回去 |
| A110B06 后不稳定 | 回退 **A069B01**，再考虑方案 1（ESP 侧 TLS） |
| 仍 715 | 升级未必能覆盖百炼的密码套件；改走 ESP mbedTLS over 4G TCP |

升级完成后把串口 `FW` + `HTEST` / `t` 日志发我，我帮你看是否打通百炼。
