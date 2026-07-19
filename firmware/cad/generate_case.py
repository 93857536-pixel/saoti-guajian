#!/usr/bin/env python3
"""
扫题挂件外壳 v7.1 — 全面审计后修正

v7 审计发现的致命问题（已修）：
  1) ESP 悬在仓中部 → 底边 USB 孔对空气（距内壁 ~55mm）
  2) BOOT 孔打在电源区，不在 ESP 板上
  3) 4G 限位与 ESP 杜邦上通道 XY 重叠

v7.1 摆法：
  - 电源贴底，USB-C 出后壳底边（前壳不开无效孔）
  - ESP 右移贴右边，USB-C 出后壳右侧壁
  - BOOT 改后壳，对准 ESP USB 端板载键
  - 4G 改左上，避开 ESP 杜邦通道
  - 屏：方屏居中，杜邦/PH2.0 出左右短边

用法:
  python3 cad/generate_case.py
"""

from __future__ import annotations

from pathlib import Path

import cadquery as cq

# ---------- 外廓 ----------
OUTER_L = 106.0  # X：ESP64 贴右后两侧杜邦/余量
OUTER_W = 140.0  # Y：屏/摄缝 + 电源/ESP
OUTER_H = 70.0
WALL = 2.0
CORNER_R = 8.0
LIP = 1.6
SPLIT_Z = 34.0
TOL = 0.30

INNER_X = OUTER_L / 2 - WALL  # 51
INNER_Y = OUTER_W / 2 - WALL  # 68

# ---------- 卡扣 ----------
CLIP_W = 12.0
CLIP_T = 1.4
CLIP_H = 3.4
HOOK = 1.1
CLIPS = [
    (0.0, OUTER_W / 2 - WALL - 12.0),
    (0.0, -(OUTER_W / 2 - WALL - 12.0)),
    (OUTER_L / 2 - WALL - 9.0, 0.0),
    (-(OUTER_L / 2 - WALL - 9.0), 0.0),
]

# ---------- 杜邦 ----------
DUPONT_STACK_H = 12.0
DUPONT_SIDE_CLEAR = 17.0
WIRE_LOFT_Z = 18.0
CAM_BUNDLE_W = 12.0
CAM_BUNDLE_T = 10.0

# ---------- 电源：贴底，USB-C 朝 -Y ----------
PWR_LEN_X = 64.0
PWR_WID_Y = 35.8
PWR_H = 20.0
PWR_CLEAR = 0.8
# 底边贴内壁（USB-C 才能出孔）
PWR_CY = -INNER_Y + PWR_WID_Y / 2 + 0.6  # ≈ -48.5
PWR_CX = -6.0  # 略偏左，给右侧 ESP 让位
USBC_W, USBC_H = 10.5, 4.2
USBC_X = PWR_CX  # 底边孔对准模块中部（常见 USB-C 居中偏）
# 电源在后仓：合盖后 USB≈世界Z58 → 后壳局部Z≈12（从外后脸量）
USBC_Z_B = 12.0
# 拨动开关：在 USB-C 对边（模块 +Y）；从后壳背面指拨。
# 槽加长以兼容开关偏左/偏右（各品牌位置不一）。
SW_SLOT_W, SW_SLOT_H = 28.0, 10.0
SW_CX = PWR_CX + 6.0
SW_CY = PWR_CY + PWR_WID_Y / 2 - 2.5

# ---------- ESP：64(X)×28.4(Y)，USB 在 +X 短边 → 出右侧壁 ----------
ESP_LEN_X, ESP_WID_Y = 64.0, 28.4
ESP_CX = INNER_X - ESP_LEN_X / 2 - 0.4  # 右缘贴内壁 ≈ 18.6
# 与电源顶边留 ≥18mm 给杜邦下沿
_pwr_hi = PWR_CY + PWR_WID_Y / 2
ESP_CY = _pwr_hi + 18.0 + ESP_WID_Y / 2
BOOT_D = 5.0
# BOOT 在 USB 端（右端）附近；孔开在后壳（器件面朝后）
BOOT_CX = ESP_CX + ESP_LEN_X / 2 - 8.0
BOOT_CY = ESP_CY - 6.0
ESP_USB_W, ESP_USB_H = 11.0, 4.8
# ESP 在后仓：USB≈世界Z61 → 后壳局部Z≈9（只开后壳，前壳开孔无效）
ESP_USB_Z = 9.5

# ---------- 屏 / 摄 ----------
# 微雪 1.3inch LCD Module：PCB 45(长)×31(短)；方形屏大致居中；
# 接口在短边：一侧 PH2.0，一侧 8 孔排针（杜邦）——出线沿 ±X，不是朝摄像的长边。
LCD_BOARD_W, LCD_BOARD_H = 45.0, 31.0
LCD_VIEW = 24.0  # 可视 23.4，开窗略大；与板同心
LCD_CX, LCD_CY = 0.0, 52.0  # Y: 36.5~67.5

# 微雪 OV5640 Camera Board (C)：PCB 矩形 35.7×23.9（不是正方形）
# 方形的是镜头模组；排针在短边一端，镜头偏在另一端（非板心）。
# 装法：长边沿 X；排针朝 -X；镜头偏 +X。
CAM_BOARD_W, CAM_BOARD_H = 35.7, 23.9
CAM_CX, CAM_CY = 0.0, 8.0  # 板中心；与屏缝 ≈16.5
CAM_POCKET_W = CAM_BOARD_W + 1.6  # 略加大，含排针侧凸出
CAM_POCKET_H = CAM_BOARD_H + 1.2
CAM_LENS_DX = 8.5  # 镜头相对板心偏 +X（朝模组端，远离排针）
CAM_LENS_DY = 0.0
CAM_D = 8.5  # 圆孔看镜头
CAM_SQ = 11.0  # 方形模组凹槽边长

# 4G：左上，避开 ESP（右侧）与电源
MODEM_W, MODEM_H = 28.0, 26.0
MODEM_CX, MODEM_CY = -22.0, 36.0

ANT_CABLE_W, ANT_CABLE_H = 6.0, 3.0
ANT_CABLE_X = -18.0  # 靠近 4G
STRAP_D = 5.5
ANT_PAD_W, ANT_PAD_H, ANT_PAD_D = 48.0, 14.0, 0.8
ANT_PAD_CX, ANT_PAD_CY = 0.0, 52.0


def rounded_box(l: float, w: float, h: float, r: float) -> cq.Workplane:
    return (
        cq.Workplane("XY")
        .box(l, w, h, centered=(True, True, False))
        .edges("|Z")
        .fillet(min(r, l / 2 - 0.1, w / 2 - 0.1))
    )


def make_clip_hook(cx: float, cy: float, outward_x: float, outward_y: float) -> cq.Workplane:
    arm = (
        cq.Workplane("XY")
        .workplane(offset=SPLIT_Z - 0.2)
        .center(cx, cy)
        .rect(
            CLIP_W if abs(outward_y) > abs(outward_x) else CLIP_T,
            CLIP_T if abs(outward_y) > abs(outward_x) else CLIP_W,
        )
        .extrude(CLIP_H)
    )
    if abs(outward_y) > abs(outward_x):
        hook = (
            cq.Workplane("XY")
            .workplane(offset=SPLIT_Z - 0.2 + CLIP_H - 1.2)
            .center(cx, cy + outward_y * (CLIP_T / 2 + HOOK / 2))
            .rect(CLIP_W, HOOK)
            .extrude(1.2)
        )
    else:
        hook = (
            cq.Workplane("XY")
            .workplane(offset=SPLIT_Z - 0.2 + CLIP_H - 1.2)
            .center(cx + outward_x * (CLIP_T / 2 + HOOK / 2), cy)
            .rect(HOOK, CLIP_W)
            .extrude(1.2)
        )
    return arm.union(hook)


def make_clip_slot(cx: float, cy: float, along_y: bool) -> cq.Workplane:
    h = OUTER_H - SPLIT_Z
    if along_y:
        return (
            cq.Workplane("XY")
            .workplane(offset=h - LIP - 0.2)
            .center(cx, cy)
            .rect(CLIP_W + TOL * 2, CLIP_T + HOOK + TOL * 2)
            .extrude(LIP + CLIP_H + 0.5)
        )
    return (
        cq.Workplane("XY")
        .workplane(offset=h - LIP - 0.2)
        .center(cx, cy)
        .rect(CLIP_T + HOOK + TOL * 2, CLIP_W + TOL * 2)
        .extrude(LIP + CLIP_H + 0.5)
    )


def shell_front() -> cq.Workplane:
    h = SPLIT_Z
    body = rounded_box(OUTER_L, OUTER_W, h, CORNER_R).cut(
        cq.Workplane("XY")
        .workplane(offset=WALL)
        .box(
            OUTER_L - 2 * WALL,
            OUTER_W - 2 * WALL,
            h - WALL + 0.1,
            centered=(True, True, False),
        )
        .edges("|Z")
        .fillet(max(0.6, CORNER_R - WALL))
    )

    body = body.cut(
        cq.Workplane("XY")
        .workplane(offset=-0.1)
        .center(LCD_CX, LCD_CY)
        .rect(LCD_VIEW + TOL, LCD_VIEW + TOL)
        .extrude(WALL + 0.4)
    )
    body = body.cut(
        cq.Workplane("XY")
        .workplane(offset=WALL - 0.5)
        .center(LCD_CX, LCD_CY)
        .rect(LCD_BOARD_W + 0.8, LCD_BOARD_H + 0.8)
        .extrude(-(WALL - 0.35))
    )
    # 屏左右短边出线槽（PH2.0 / 排针杜邦，与微雪实物一致）
    for side in (-1.0, 1.0):
        body = body.cut(
            cq.Workplane("XY")
            .workplane(offset=WALL)
            .center(
                LCD_CX + side * (LCD_BOARD_W / 2 + DUPONT_SIDE_CLEAR / 2 - 0.5),
                LCD_CY,
            )
            .rect(DUPONT_SIDE_CLEAR, LCD_BOARD_H * 0.8)
            .extrude(WIRE_LOFT_Z)
        )

    # 摄像板矩形浅凹
    body = body.cut(
        cq.Workplane("XY")
        .workplane(offset=WALL - 0.7)
        .center(CAM_CX, CAM_CY)
        .rect(CAM_POCKET_W, CAM_POCKET_H)
        .extrude(-(WALL - 0.4))
    )
    lens_x = CAM_CX + CAM_LENS_DX
    lens_y = CAM_CY + CAM_LENS_DY
    # 方形模组凹进前壁（模组不是圆的）
    body = body.cut(
        cq.Workplane("XY")
        .workplane(offset=WALL - 0.9)
        .center(lens_x, lens_y)
        .rect(CAM_SQ + TOL, CAM_SQ + TOL)
        .extrude(-(WALL - 0.25))
    )
    # 镜头圆孔（偏在模组端，不在板心）
    body = body.cut(
        cq.Workplane("XY")
        .workplane(offset=-0.1)
        .center(lens_x, lens_y)
        .circle(CAM_D / 2 + TOL / 2)
        .extrude(WALL + 0.4)
    )
    # 排针在 -X 短边 → 杜邦出左侧（不是板下沿）
    body = body.cut(
        cq.Workplane("XY")
        .workplane(offset=WALL + 1.0)
        .center(
            CAM_CX - CAM_BOARD_W / 2 - DUPONT_SIDE_CLEAR / 2 + 1.0,
            CAM_CY,
        )
        .rect(DUPONT_SIDE_CLEAR, max(CAM_BOARD_H + 4.0, CAM_BUNDLE_T + 8.0))
        .extrude(WIRE_LOFT_Z)
    )
    # 中央线舱（偏左：ESP 在右，线束往中左汇合）
    body = body.cut(
        cq.Workplane("XY")
        .workplane(offset=WALL + 2.0)
        .center(-6.0, (LCD_CY + CAM_CY) / 2 - 4.0)
        .rect(OUTER_L - 2 * WALL - 16.0, 40.0)
        .extrude(h - WALL - 4.0)
    )

    # 电源/ESP 的 USB 均在后仓高度，前壳底边/右侧不开孔（避免无效孔）

    lip = (
        cq.Workplane("XY")
        .workplane(offset=h - 0.05)
        .box(
            OUTER_L - 2 * WALL - TOL,
            OUTER_W - 2 * WALL - TOL,
            LIP + 0.05,
            centered=(True, True, False),
        )
        .edges("|Z")
        .fillet(max(0.5, CORNER_R - WALL - 0.5))
    )
    lip_i = (
        cq.Workplane("XY")
        .workplane(offset=h - 0.05)
        .box(
            OUTER_L - 2 * WALL - TOL - 2.6,
            OUTER_W - 2 * WALL - TOL - 2.6,
            LIP + 0.3,
            centered=(True, True, False),
        )
    )
    body = body.union(lip.cut(lip_i))
    body = body.union(make_clip_hook(CLIPS[0][0], CLIPS[0][1], 0, 1))
    body = body.union(make_clip_hook(CLIPS[1][0], CLIPS[1][1], 0, -1))
    body = body.union(make_clip_hook(CLIPS[2][0], CLIPS[2][1], 1, 0))
    body = body.union(make_clip_hook(CLIPS[3][0], CLIPS[3][1], -1, 0))

    ear = (
        cq.Workplane("XY")
        .workplane(offset=h * 0.25)
        .center(0, OUTER_W / 2 + 4.2)
        .box(16, 9, h * 0.45, centered=(True, True, False))
        .edges("|Z")
        .fillet(2.2)
    )
    ear = ear.cut(
        cq.Workplane("XY")
        .workplane(offset=h * 0.25 - 0.1)
        .center(0, OUTER_W / 2 + 4.2)
        .circle(STRAP_D / 2)
        .extrude(h)
    )
    return body.union(ear)


def shell_back() -> cq.Workplane:
    h = OUTER_H - SPLIT_Z
    body = rounded_box(OUTER_L, OUTER_W, h, CORNER_R).cut(
        cq.Workplane("XY")
        .workplane(offset=WALL)
        .box(
            OUTER_L - 2 * WALL,
            OUTER_W - 2 * WALL,
            h - WALL + 0.1,
            centered=(True, True, False),
        )
        .edges("|Z")
        .fillet(max(0.6, CORNER_R - WALL))
    )

    groove = (
        cq.Workplane("XY")
        .workplane(offset=h - LIP - TOL)
        .box(
            OUTER_L - 2 * WALL + TOL,
            OUTER_W - 2 * WALL + TOL,
            LIP + TOL + 0.2,
            centered=(True, True, False),
        )
        .edges("|Z")
        .fillet(max(0.4, CORNER_R - WALL - 0.3))
    )
    body = body.cut(groove)
    body = body.cut(make_clip_slot(CLIPS[0][0], CLIPS[0][1], True))
    body = body.cut(make_clip_slot(CLIPS[1][0], CLIPS[1][1], True))
    body = body.cut(make_clip_slot(CLIPS[2][0], CLIPS[2][1], False))
    body = body.cut(make_clip_slot(CLIPS[3][0], CLIPS[3][1], False))

    rail = (
        cq.Workplane("XY")
        .workplane(offset=WALL)
        .center(PWR_CX, PWR_CY)
        .rect(PWR_LEN_X + PWR_CLEAR * 2 + 2.4, PWR_WID_Y + PWR_CLEAR * 2 + 2.4)
        .extrude(2.4)
        .faces(">Z")
        .shell(-1.2)
    )
    try:
        body = body.union(rail)
    except Exception:
        pass

    esp_rail = (
        cq.Workplane("XY")
        .workplane(offset=WALL)
        .center(ESP_CX, ESP_CY)
        .rect(ESP_LEN_X + 1.2, ESP_WID_Y + 1.2)
        .extrude(2.0)
        .faces(">Z")
        .shell(-1.0)
    )
    try:
        body = body.union(esp_rail)
    except Exception:
        pass

    # ESP 长边杜邦：±Y；右侧贴墙无空间 → 右侧不挖，只保证上/下/朝左
    for dy in (
        ESP_CY + ESP_WID_Y / 2 + DUPONT_SIDE_CLEAR / 2,
        ESP_CY - ESP_WID_Y / 2 - DUPONT_SIDE_CLEAR / 2,
    ):
        body = body.cut(
            cq.Workplane("XY")
            .workplane(offset=WALL)
            .center(ESP_CX - 6.0, dy)
            .rect(ESP_LEN_X * 0.7, DUPONT_SIDE_CLEAR)
            .extrude(h - WALL - 2.0)
        )
    # 朝左（-X）主线束汇入中央线舱
    body = body.cut(
        cq.Workplane("XY")
        .workplane(offset=WALL)
        .center(ESP_CX - ESP_LEN_X / 2 - DUPONT_SIDE_CLEAR / 2 + 1.0, ESP_CY)
        .rect(DUPONT_SIDE_CLEAR, ESP_WID_Y + 8.0)
        .extrude(h - WALL - 2.0)
    )

    modem_rail = (
        cq.Workplane("XY")
        .workplane(offset=WALL)
        .center(MODEM_CX, MODEM_CY)
        .rect(MODEM_W + 4.0, MODEM_H + DUPONT_SIDE_CLEAR * 0.35 + 2.0)
        .extrude(1.8)
        .faces(">Z")
        .shell(-1.0)
    )
    try:
        body = body.union(modem_rail)
    except Exception:
        pass

    # 底：电源 USB-C
    body = body.cut(
        cq.Workplane("XZ")
        .workplane(offset=-OUTER_W / 2 + 0.05)
        .center(USBC_X, USBC_Z_B)
        .rect(USBC_W + TOL, USBC_H + TOL)
        .extrude(WALL + 5)
    )
    # 右侧：ESP USB（后壳段）
    body = body.cut(
        cq.Workplane("YZ")
        .workplane(offset=OUTER_L / 2 - 0.05)
        .center(ESP_CY, ESP_USB_Z)
        .rect(ESP_USB_W + TOL, ESP_USB_H + TOL)
        .extrude(-(WALL + 5))
    )

    # BOOT（后壳外底面，对准板载键）
    body = body.cut(
        cq.Workplane("XY")
        .workplane(offset=-0.05)
        .center(BOOT_CX, BOOT_CY)
        .circle(BOOT_D / 2 + TOL / 2)
        .extrude(WALL + 1.2)
    )

    # 开关指拨槽（后壳背面，对准电源 +Y 边；加长兼容偏位）
    body = body.cut(
        cq.Workplane("XY")
        .workplane(offset=-0.05)
        .center(SW_CX, SW_CY)
        .rect(SW_SLOT_W, SW_SLOT_H)
        .extrude(WALL + 1.5)
    )

    # 顶：天线馈线（靠近左上 4G）
    body = body.cut(
        cq.Workplane("XZ")
        .workplane(offset=OUTER_W / 2 - 0.05)
        .center(ANT_CABLE_X, 11.0)
        .rect(ANT_CABLE_W, ANT_CABLE_H)
        .extrude(-(WALL + 4))
    )

    body = body.cut(
        cq.Workplane("XY")
        .workplane(offset=-0.05)
        .center(ANT_PAD_CX, ANT_PAD_CY)
        .rect(ANT_PAD_W, ANT_PAD_H)
        .extrude(ANT_PAD_D + 0.1)
    )

    ear = (
        cq.Workplane("XY")
        .workplane(offset=0)
        .center(0, OUTER_W / 2 + 4.2)
        .box(16, 9, h * 0.85, centered=(True, True, False))
        .edges("|Z")
        .fillet(2.2)
    )
    ear = ear.cut(
        cq.Workplane("XY")
        .workplane(offset=-0.1)
        .center(0, OUTER_W / 2 + 4.2)
        .circle(STRAP_D / 2)
        .extrude(h + 1)
    )
    body = body.union(ear)

    for ox in (-22, 10):
        body = body.cut(
            cq.Workplane("XY")
            .workplane(offset=-0.1)
            .center(ox, -8)
            .rect(12, 1.6)
            .extrude(WALL + 0.3)
        )
    return body


def _gap(a_lo: float, a_hi: float, b_lo: float, b_hi: float) -> float:
    if a_hi < b_lo:
        return b_lo - a_hi
    if b_hi < a_lo:
        return a_lo - b_hi
    return 0.0


def _overlap(a_lo: float, a_hi: float, b_lo: float, b_hi: float) -> float:
    return max(0.0, min(a_hi, b_hi) - max(a_lo, b_lo))


def audit() -> dict:
    lcd_lo, lcd_hi = LCD_CY - LCD_BOARD_H / 2, LCD_CY + LCD_BOARD_H / 2
    cam_lo, cam_hi = CAM_CY - CAM_BOARD_H / 2, CAM_CY + CAM_BOARD_H / 2
    esp_lo, esp_hi = ESP_CY - ESP_WID_Y / 2, ESP_CY + ESP_WID_Y / 2
    esp_x0, esp_x1 = ESP_CX - ESP_LEN_X / 2, ESP_CX + ESP_LEN_X / 2
    pwr_lo, pwr_hi = PWR_CY - PWR_WID_Y / 2, PWR_CY + PWR_WID_Y / 2
    pwr_x0, pwr_x1 = PWR_CX - PWR_LEN_X / 2, PWR_CX + PWR_LEN_X / 2
    mod_x0 = MODEM_CX - (MODEM_W + 4) / 2
    mod_x1 = MODEM_CX + (MODEM_W + 4) / 2
    mod_y0 = MODEM_CY - (MODEM_H + DUPONT_SIDE_CLEAR * 0.35 + 2) / 2
    mod_y1 = MODEM_CY + (MODEM_H + DUPONT_SIDE_CLEAR * 0.35 + 2) / 2

    bottom_inner = -INNER_Y
    right_inner = INNER_X
    issues: list[str] = []

    if _overlap(lcd_lo, lcd_hi, cam_lo, cam_hi) > 0.1:
        issues.append("LCD/CAM Y 重叠")
    gap_lc = _gap(lcd_lo, lcd_hi, cam_lo, cam_hi)
    if gap_lc < 16.0:
        issues.append(f"LCD-CAM 缝 {gap_lc:.1f} < 16")

    gap_ep = _gap(esp_lo, esp_hi, pwr_lo, pwr_hi)
    if gap_ep < 17.0:
        issues.append(f"ESP-PWR 缝 {gap_ep:.1f} < 17（杜邦）")

    # USB 可达
    if abs(pwr_lo - bottom_inner) > 2.5:
        issues.append(f"电源底边距内壁 {pwr_lo - bottom_inner:.1f}，USB-C 可能够不到")
    if abs(esp_x1 - right_inner) > 2.5:
        issues.append(f"ESP 右边距内壁 {right_inner - esp_x1:.1f}，侧 USB 可能够不到")

    # BOOT 在 ESP 板上
    if not (esp_x0 <= BOOT_CX <= esp_x1 and esp_lo <= BOOT_CY <= esp_hi):
        issues.append("BOOT 孔不在 ESP 板范围内")

    # 4G 与 ESP 本体
    if _overlap(mod_x0, mod_x1, esp_x0, esp_x1) > 1 and _overlap(mod_y0, mod_y1, esp_lo, esp_hi) > 1:
        issues.append("4G 与 ESP 板 XY 重叠")

    # 部件不超出内腔
    for name, x0, x1, y0, y1 in [
        ("LCD", LCD_CX - LCD_BOARD_W / 2, LCD_CX + LCD_BOARD_W / 2, lcd_lo, lcd_hi),
        ("CAM", CAM_CX - CAM_BOARD_W / 2, CAM_CX + CAM_BOARD_W / 2, cam_lo, cam_hi),
        ("ESP", esp_x0, esp_x1, esp_lo, esp_hi),
        ("PWR", pwr_x0, pwr_x1, pwr_lo, pwr_hi),
        ("MOD", mod_x0, mod_x1, mod_y0, mod_y1),
    ]:
        if x0 < -INNER_X - 0.2 or x1 > INNER_X + 0.2 or y0 < -INNER_Y - 0.2 or y1 > INNER_Y + 0.2:
            issues.append(f"{name} 超出内腔")

    back_clear = OUTER_H - SPLIT_Z - WALL
    if back_clear < PWR_H + 4:
        issues.append("后仓电源高度不够")
    if SPLIT_Z - WALL < WIRE_LOFT_Z + 6:
        issues.append("前线舱过浅")

    # 镜头孔须落在摄像板范围内（偏模组端，非板心）
    lens_x, lens_y = CAM_CX + CAM_LENS_DX, CAM_CY + CAM_LENS_DY
    cam_x0, cam_x1 = CAM_CX - CAM_BOARD_W / 2, CAM_CX + CAM_BOARD_W / 2
    if not (cam_x0 + 3 <= lens_x <= cam_x1 - 3 and cam_lo + 3 <= lens_y <= cam_hi - 3):
        issues.append("镜头孔偏离摄像板有效区")
    # 开关槽落在电源板 +Y 边附近
    if not (pwr_x0 <= SW_CX <= pwr_x1 and abs(SW_CY - pwr_hi) < 6):
        issues.append("开关槽未对准电源板顶边区域")

    return {
        "issues": issues,
        "gap_lcd_cam": gap_lc,
        "gap_esp_pwr": gap_ep,
        "pwr_to_bottom": pwr_lo - bottom_inner,
        "esp_to_right": right_inner - esp_x1,
        "lcd": (lcd_lo, lcd_hi),
        "cam": (cam_lo, cam_hi),
        "esp": (esp_lo, esp_hi, esp_x0, esp_x1),
        "pwr": (pwr_lo, pwr_hi),
        "back_clear": back_clear,
        "lens": (lens_x, lens_y),
        "switch": (SW_CX, SW_CY),
    }


def main() -> None:
    a = audit()
    if a["issues"]:
        print("AUDIT FAIL:")
        for i in a["issues"]:
            print("  !!", i)
        raise SystemExit(1)

    out = Path(__file__).resolve().parent / "stl"
    out.mkdir(parents=True, exist_ok=True)
    front = shell_front()
    back = shell_back()
    preview = front.union(back.translate((0, 0, SPLIT_Z + 0.8)))
    cq.exporters.export(front, str(out / "saoti_front.stl"))
    cq.exporters.export(back, str(out / "saoti_back.stl"))
    cq.exporters.export(preview, str(out / "saoti_assembled_preview.stl"))

    print("OK v7.1 ->", out)
    print(f"  外廓 {OUTER_L}×{OUTER_W}×{OUTER_H}  前{SPLIT_Z}/后{OUTER_H - SPLIT_Z}")
    print(f"  LCD-CAM缝 {a['gap_lcd_cam']:.1f}  ESP-PWR缝 {a['gap_esp_pwr']:.1f}")
    print(f"  电源→底内壁 {a['pwr_to_bottom']:.1f}mm  ESP→右内壁 {a['esp_to_right']:.1f}mm")
    print(f"  BOOT @ ({BOOT_CX:.1f},{BOOT_CY:.1f}) 后壳  ESP USB→右侧壁")
    print(f"  摄像板 {CAM_BOARD_W}×{CAM_BOARD_H} 矩形；镜头孔偏 ({a['lens'][0]:.1f},{a['lens'][1]:.1f})")
    print(f"  开关槽 @ ({a['switch'][0]:.1f},{a['switch'][1]:.1f}) 后壳背面 尺寸{SW_SLOT_W}×{SW_SLOT_H}")
    print(f"  后仓净深 {a['back_clear']:.1f}  杜邦侧向 {DUPONT_SIDE_CLEAR} 线舱Z {WIRE_LOFT_Z}")
    print("AUDIT PASS")


if __name__ == "__main__":
    main()
