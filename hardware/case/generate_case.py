#!/usr/bin/env python3
"""
扫题挂件外壳 v7.36 — 全检后卡勾外壁余量≥0.5

v7.6～v7.35：见历史。
v7.36：卡勾尖距外表面留壁 ≥0.5mm（原 0.45）。

用法:
  python3 generate_case.py
"""

from __future__ import annotations

import math
from pathlib import Path

import cadquery as cq

# ---------- 外廓（刚好装下 + 少量容错）----------
OUTER_L = 76.0
OUTER_W = 110.0
OUTER_H = 58.0
WALL = 2.0
CORNER_R = 5.0
LIP = 2.2
# 合盖止口：外圈留壁 + 前唇/后槽间隙（槽圆角 = CORNER_R − 留壁，避免四角削薄）
MIN_RIM_WALL = 0.8  # ≥0.5；直边与圆角处最小壁厚
LIP_GROOVE_CLEAR = 0.35  # 前止口外沿相对后槽单边间隙
SPLIT_Z = 26.0  # 前壳；后壳 32
TOL = 0.60
POCKET = 1.6
WALL_GAP = 1.5
BACK_STACK_H = 30.0  # 电源 24 + 余量
CABLE_LEN = 200.0
CABLE_BUDGET = 190.0

INNER_X = OUTER_L / 2 - WALL
INNER_Y = OUTER_W / 2 - WALL

# 止口外廓（相对外壳外尺寸内缩），圆角等距偏移
_GROOVE_L = OUTER_L - 2 * MIN_RIM_WALL
_GROOVE_W = OUTER_W - 2 * MIN_RIM_WALL
_GROOVE_R = max(0.5, CORNER_R - MIN_RIM_WALL)
_LIP_OUT_L = _GROOVE_L - 2 * LIP_GROOVE_CLEAR
_LIP_OUT_W = _GROOVE_W - 2 * LIP_GROOVE_CLEAR
_LIP_OUT_R = max(0.5, _GROOVE_R - LIP_GROOVE_CLEAR)
_LIP_RING_T = 2.2
_LIP_IN_L = _LIP_OUT_L - 2 * _LIP_RING_T
_LIP_IN_W = _LIP_OUT_W - 2 * _LIP_RING_T

CLIP_W = 10.0
CLIP_T = 1.6
CLIP_H = 5.0
HOOK = 0.90  # 勾尖；须满足 WALL−EMBED−HOOK ≥ 0.5
CLIP_SLOT_EXTRA = 0.15
CLIP_EMBED = 0.55  # 腔内筋与内壁重叠，不留缝
CLIP_RIM_KEEP = 3.0
CLIP_SEAT = 0.30
CLIP_PILLAR_W = 2.2
MIN_HOOK_SKIN = 0.5  # 勾尖到外表面最小留壁
_yw = INNER_Y - CLIP_T / 2 + CLIP_EMBED
_xw = INNER_X - CLIP_T / 2 + CLIP_EMBED
CLIPS = [
    (-16.0, _yw, 0.0, 1.0),
    (16.0, _yw, 0.0, 1.0),
    (-16.0, -_yw, 0.0, -1.0),
    (16.0, -_yw, 0.0, -1.0),
    (_xw, -22.0, 1.0, 0.0),
    (_xw, 22.0, 1.0, 0.0),
    (-_xw, -22.0, -1.0, 0.0),
    (-_xw, 22.0, -1.0, 0.0),
]

DUPONT_STACK_H = 12.0
DUPONT_SIDE_CLEAR = 8.0
WIRE_LOFT_Z = 10.0
CAM_BUNDLE_W = 12.0
CAM_BUNDLE_T = 10.0

PWR_LEN_X = 63.0
PWR_WID_Y = 40.0
PWR_H = 24.0
PWR_CLEAR = 2.0
PWR_CY = -INNER_Y + PWR_WID_Y / 2 + WALL_GAP
PWR_CX = 0.0
USBC_W, USBC_H = 13.5, 6.2
USBC_X = PWR_CX
USBC_Z_B = 11.0

ESP_LEN_X, ESP_WID_Y = 64.0, 28.4
ESP_CX = INNER_X - ESP_LEN_X / 2 - WALL_GAP
_pwr_hi = PWR_CY + PWR_WID_Y / 2
_ROW_GAP = 6.0
ESP_CY = _pwr_hi + _ROW_GAP + ESP_WID_Y / 2
ESP_USB_W, ESP_USB_H = 12.0, 5.0
ESP_DECK_H = 3.5
ESP_USB_ABOVE_DECK = 3.5
ESP_USB_Z = WALL + ESP_DECK_H + ESP_USB_ABOVE_DECK
ESP_RIM_H = 3.0
ESP_RIM_T = 1.5

LCD_BOARD_W, LCD_BOARD_H = 45.0, 31.0
LCD_AA = 23.40
LCD_VIEW = LCD_AA + 1.4
LCD_CX, LCD_CY = 0.0, INNER_Y - LCD_BOARD_H / 2 - 3.0
FACE_LIP = 1.0

# 摄像横放叠在 ESP 上方：X=排针→镜头，Y=板宽；镜头偏 −X
CAM_PCB_L = 35.70
CAM_PCB_W = 23.90
CAM_BOARD_W = CAM_PCB_L
CAM_BOARD_H = CAM_PCB_W
_esp_hi = ESP_CY + ESP_WID_Y / 2
_TOP_GAP = 4.0
CAM_CX = -INNER_X + CAM_BOARD_W / 2 + WALL_GAP
CAM_CY = _esp_hi + _TOP_GAP + CAM_BOARD_H / 2
CAM_POCKET_W = CAM_BOARD_W + POCKET + 1.2
CAM_POCKET_H = CAM_BOARD_H + POCKET + 1.0
CAM_LENS_DX = -8.5
CAM_LENS_DY = 0.0
# 外通孔：摄像金属罩 + 闪光灯合一窗 2.5×1 cm
# 长边沿 Y（罩上闪光下），短边沿 X
CAM_WIN_H = 25.0
CAM_WIN_W = 10.0
CAM_WIN_DY = -2.0  # 相对镜头中心略下移


def cam_win_center(lens_x: float, lens_y: float) -> tuple[float, float]:
    return lens_x, lens_y + CAM_WIN_DY


MODEM_W, MODEM_H = 28.0, 26.0
MODEM_CX = INNER_X - MODEM_W / 2 - WALL_GAP
MODEM_CY = CAM_CY

ANT_CABLE_W, ANT_CABLE_H = 10.0, 5.0
ANT_CABLE_X = -8.0
STRAP_D = 6.0
EAR_W, EAR_D = 16.0, 9.0
EAR_Y = OUTER_W / 2 + EAR_D / 2 - 0.2
EAR_FRONT_H = 11.0  # 前耳顶到合盖面
EAR_BACK_H = 12.0   # 后耳顶到合盖面

SW_FROM_PWR_BOTTOM = 13.0
SW_BTN_LEN = 10.0
SW_SLOT_W = 28.0
SW_SLOT_H = SW_BTN_LEN + 5.0
SW_CX = PWR_CX + 6.0
SW_CY = (PWR_CY - PWR_WID_Y / 2) + SW_FROM_PWR_BOTTOM

ESP_PWR_GAP_MIN = 5.5


def rounded_box(l: float, w: float, h: float, r: float) -> cq.Workplane:
    return (
        cq.Workplane("XY")
        .box(l, w, h, centered=(True, True, False))
        .edges("|Z")
        .fillet(min(r, l / 2 - 0.1, w / 2 - 0.1))
    )


def make_bay_deck(
    cx: float,
    cy: float,
    len_x: float,
    wid_y: float,
    deck_h: float,
    rim_h: float,
    rim_t: float = 1.6,
    clear: float = 1.5,
    open_px: bool = False,
    open_nx: bool = False,
    open_py: bool = False,
    open_ny: bool = False,
) -> cq.Workplane:
    """后壳模块舱：底板支撑 + 围边（开口侧不设挡板，便于抽板/对接口）。"""
    ox = len_x + clear * 2
    oy = wid_y + clear * 2
    z0 = WALL
    deck = (
        cq.Workplane("XY")
        .workplane(offset=z0)
        .center(cx, cy)
        .rect(ox, oy)
        .extrude(deck_h)
    )
    # 围边：外框减内框
    outer = (
        cq.Workplane("XY")
        .workplane(offset=z0 + deck_h)
        .center(cx, cy)
        .rect(ox, oy)
        .extrude(rim_h)
    )
    inner = (
        cq.Workplane("XY")
        .workplane(offset=z0 + deck_h - 0.05)
        .center(cx, cy)
        .rect(ox - 2 * rim_t, oy - 2 * rim_t)
        .extrude(rim_h + 0.2)
    )
    rim = outer.cut(inner)
    # 开口侧切掉围边
    cut_d = rim_t + 1.0
    if open_px:
        rim = rim.cut(
            cq.Workplane("XY")
            .workplane(offset=z0 + deck_h - 0.1)
            .center(cx + ox / 2 - cut_d / 2, cy)
            .rect(cut_d + 0.2, oy + 1)
            .extrude(rim_h + 0.4)
        )
    if open_nx:
        rim = rim.cut(
            cq.Workplane("XY")
            .workplane(offset=z0 + deck_h - 0.1)
            .center(cx - ox / 2 + cut_d / 2, cy)
            .rect(cut_d + 0.2, oy + 1)
            .extrude(rim_h + 0.4)
        )
    if open_py:
        rim = rim.cut(
            cq.Workplane("XY")
            .workplane(offset=z0 + deck_h - 0.1)
            .center(cx, cy + oy / 2 - cut_d / 2)
            .rect(ox + 1, cut_d + 0.2)
            .extrude(rim_h + 0.4)
        )
    if open_ny:
        rim = rim.cut(
            cq.Workplane("XY")
            .workplane(offset=z0 + deck_h - 0.1)
            .center(cx, cy - oy / 2 + cut_d / 2)
            .rect(ox + 1, cut_d + 0.2)
            .extrude(rim_h + 0.4)
        )
    return deck.union(rim)


def make_clip_hook(cx: float, cy: float, outward_x: float, outward_y: float) -> cq.Workplane:
    """前壳卡勾：自内壁长出的整块筋（勾与壁之间无缝），外向勾齿。"""
    along_y = abs(outward_y) > abs(outward_x)
    z0 = SPLIT_Z - 5.5
    zh = 5.5 + CLIP_H + 0.2  # 根到勾顶连成一根

    if along_y:
        sgn = 1.0 if outward_y > 0 else -1.0
        # 一整块：从腔内贴壁一直长进壁厚，Z 向连续，不留缝
        y_cav = sgn * (INNER_Y - CLIP_T - 0.3)
        y_out = sgn * (OUTER_W / 2 - 0.12)
        pad = (
            cq.Workplane("XY")
            .workplane(offset=z0)
            .center(cx, (y_cav + y_out) / 2)
            .rect(CLIP_W + 2.4, abs(y_out - y_cav))
            .extrude(zh)
        )
        hook = (
            cq.Workplane("XY")
            .workplane(offset=SPLIT_Z - 0.35 + CLIP_H - 1.35)
            .center(cx, sgn * (INNER_Y + HOOK / 2))
            .rect(CLIP_W * 0.88, HOOK)
            .extrude(1.35)
        )
        return pad.union(hook)

    sgn = 1.0 if outward_x > 0 else -1.0
    x_cav = sgn * (INNER_X - CLIP_T - 0.3)
    x_out = sgn * (OUTER_L / 2 - 0.12)
    pad = (
        cq.Workplane("XY")
        .workplane(offset=z0)
        .center((x_cav + x_out) / 2, cy)
        .rect(abs(x_out - x_cav), CLIP_W + 2.4)
        .extrude(zh)
    )
    hook = (
        cq.Workplane("XY")
        .workplane(offset=SPLIT_Z - 0.35 + CLIP_H - 1.35)
        .center(sgn * (INNER_X + HOOK / 2), cy)
        .rect(HOOK, CLIP_W * 0.88)
        .extrude(1.35)
    )
    return pad.union(hook)


def apply_clip_latch(body: cq.Workplane, cx: float, cy: float, along_y: bool, h: float) -> cq.Workplane:
    """后壳卡扣（v7.30 外观）：窄导入 + 外壁卡窗 + 口实心台肩；窗两端立柱填实过梁。"""
    lead = TOL * 0.35 + CLIP_SLOT_EXTRA
    if along_y:
        lead_sx, lead_sy = CLIP_W + TOL * 0.5 + 0.2, CLIP_T + lead
    else:
        lead_sx, lead_sy = CLIP_T + lead, CLIP_W + TOL * 0.5 + 0.2
    body = body.cut(
        cq.Workplane("XY")
        .workplane(offset=h + 0.2)
        .center(cx, cy)
        .rect(lead_sx, lead_sy)
        .extrude(-(CLIP_RIM_KEEP + CLIP_H + 1.0))
    )

    z1 = h - CLIP_RIM_KEEP
    win_z = CLIP_H + 0.5
    z0 = z1 - win_z
    win_along = CLIP_W + 1.2
    # 中间净空给勾齿；两端立柱宽度
    clear_mid = max(4.0, win_along - 2 * CLIP_PILLAR_W)

    if along_y:
        sgn = 1.0 if cy > 0 else -1.0
        wyc = sgn * (OUTER_W / 2 - WALL / 2)
        body = body.cut(
            cq.Workplane("XY")
            .workplane(offset=z0)
            .center(cx, wyc)
            .box(win_along, WALL + 1.0, win_z, centered=(True, True, False))
        )
        # 两端立柱：从窗底连到台肩，把过梁撑住（中间留空给勾）
        for s in (-1.0, 1.0):
            px = cx + s * (win_along / 2 - CLIP_PILLAR_W / 2)
            pillar = (
                cq.Workplane("XY")
                .workplane(offset=z0)
                .center(px, wyc)
                .box(CLIP_PILLAR_W, WALL + 0.6, win_z + 0.15, centered=(True, True, False))
            )
            body = body.union(pillar)
        # 保险：中间勾区再清一次，防止立柱布尔吃进中缝
        body = body.cut(
            cq.Workplane("XY")
            .workplane(offset=z0 - 0.05)
            .center(cx, wyc)
            .box(clear_mid, WALL + 1.2, win_z + 0.2, centered=(True, True, False))
        )
    else:
        sgn = 1.0 if cx > 0 else -1.0
        wxc = sgn * (OUTER_L / 2 - WALL / 2)
        body = body.cut(
            cq.Workplane("XY")
            .workplane(offset=z0)
            .center(wxc, cy)
            .box(WALL + 1.0, win_along, win_z, centered=(True, True, False))
        )
        for s in (-1.0, 1.0):
            py = cy + s * (win_along / 2 - CLIP_PILLAR_W / 2)
            pillar = (
                cq.Workplane("XY")
                .workplane(offset=z0)
                .center(wxc, py)
                .box(WALL + 0.6, CLIP_PILLAR_W, win_z + 0.15, centered=(True, True, False))
            )
            body = body.union(pillar)
        body = body.cut(
            cq.Workplane("XY")
            .workplane(offset=z0 - 0.05)
            .center(wxc, cy)
            .box(WALL + 1.2, clear_mid, win_z + 0.2, centered=(True, True, False))
        )
    return body


def make_strap_ear(z0: float, zh: float) -> cq.Workplane:
    """挂绳耳：从 z0 起高 zh，圆孔沿 Z，前后半在合盖面齐平对接。"""
    ear = (
        cq.Workplane("XY")
        .workplane(offset=z0)
        .center(0, EAR_Y)
        .box(EAR_W, EAR_D, zh, centered=(True, True, False))
        .edges("|Z")
        .fillet(2.0)
    )
    return ear.cut(
        cq.Workplane("XY")
        .workplane(offset=z0 - 0.2)
        .center(0, EAR_Y)
        .circle(STRAP_D / 2)
        .extrude(zh + 0.5)
    )


def mate_back(back: cq.Workplane) -> cq.Workplane:
    """后壳绕 Y 翻 180° 与前壳合盖（顶耳保持在 +Y）。"""
    hb = OUTER_H - SPLIT_Z
    return back.rotate((0, 0, 0), (0, 1, 0), 180).translate((0, 0, SPLIT_Z + hb))


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

    # --- 屏：外表面平整，只开官网可视窗；PCB 仅内侧浅定位 ---
    body = body.cut(
        cq.Workplane("XY")
        .workplane(offset=-0.8)
        .center(LCD_CX, LCD_CY)
        .rect(LCD_VIEW + TOL, LCD_VIEW + TOL)
        .extrude(WALL + 2.0)
    )
    # 内侧浅槽：从内壁往外挖，保留 FACE_LIP，不打穿外表面
    body = body.cut(
        cq.Workplane("XY")
        .workplane(offset=WALL + 0.05)
        .center(LCD_CX, LCD_CY)
        .rect(LCD_BOARD_W + POCKET, LCD_BOARD_H + POCKET)
        .extrude(-(WALL - FACE_LIP))
    )
    # 屏左侧短边出线槽
    body = body.cut(
        cq.Workplane("XY")
        .workplane(offset=WALL)
        .center(
            LCD_CX - (LCD_BOARD_W / 2 + DUPONT_SIDE_CLEAR / 2 - 0.5),
            LCD_CY,
        )
        .rect(DUPONT_SIDE_CLEAR, LCD_BOARD_H * 0.8)
        .extrude(WIRE_LOFT_Z)
    )
    # 前线舱（仅屏线；摄像已改后壳）
    body = body.cut(
        cq.Workplane("XY")
        .workplane(offset=WALL + 2.0)
        .center(-6.0, LCD_CY - 10.0)
        .rect(OUTER_L - 2 * WALL - 16.0, 28.0)
        .extrude(h - WALL - 4.0)
    )

    # 电源/ESP USB 均在后壳，前壳不开

    # 深止口：外廓按后槽内缩，圆角等距；外沿仍嵌入内壁与外壳连体
    lip = (
        cq.Workplane("XY")
        .workplane(offset=h - 1.2)
        .box(_LIP_OUT_L, _LIP_OUT_W, LIP + 1.25, centered=(True, True, False))
        .edges("|Z")
        .fillet(_LIP_OUT_R)
    )
    lip_i = (
        cq.Workplane("XY")
        .workplane(offset=h - 1.25)
        .box(_LIP_IN_L, _LIP_IN_W, LIP + 1.5, centered=(True, True, False))
    )
    body = body.union(lip.cut(lip_i))
    for cx, cy, ox, oy in CLIPS:
        body = body.union(make_clip_hook(cx, cy, ox, oy))

    return body.union(make_strap_ear(SPLIT_Z - EAR_FRONT_H, EAR_FRONT_H))


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

    # 止口槽：相对外壳外廓内缩 MIN_RIM_WALL，圆角 = CORNER_R−留壁（四角不削薄）
    groove = (
        cq.Workplane("XY")
        .workplane(offset=h - LIP - TOL)
        .box(_GROOVE_L, _GROOVE_W, LIP + TOL + 0.3, centered=(True, True, False))
        .edges("|Z")
        .fillet(_GROOVE_R)
    )
    body = body.cut(groove)
    for cx, cy, ox, oy in CLIPS:
        body = apply_clip_latch(body, cx, cy, abs(oy) > abs(ox), h)

    # ---- 模块舱：支撑板 + 围边（可落位安装）----
    # 电源舱：底板支撑，底边开口对准充电 USB
    body = body.union(
        make_bay_deck(
            PWR_CX,
            PWR_CY,
            PWR_LEN_X,
            PWR_WID_Y,
            deck_h=2.2,
            rim_h=4.0,
            rim_t=1.6,
            clear=PWR_CLEAR,
            open_ny=True,  # 底边开口 → USB
            open_px=True,  # 一侧开口方便落板/走线
        )
    )

    # ESP 开发板舱：必须有支撑板，否则无处安放；右侧开口对准烧录 USB
    body = body.union(
        make_bay_deck(
            ESP_CX,
            ESP_CY,
            ESP_LEN_X,
            ESP_WID_Y,
            deck_h=ESP_DECK_H,
            rim_h=ESP_RIM_H,
            rim_t=ESP_RIM_T,
            clear=POCKET,
            open_px=True,  # +X 开口 → 烧录口
            open_nx=True,  # -X 开口 → 杜邦出线
        )
    )

    # 4G 舱：浅支撑板，朝 ESP 侧开口走线
    body = body.union(
        make_bay_deck(
            MODEM_CX,
            MODEM_CY,
            MODEM_W + 2.0,
            MODEM_H + 2.0,
            deck_h=2.0,
            rim_h=3.0,
            rim_t=1.4,
            clear=1.5,
            open_px=True,
        )
    )

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

    # 底：电源 USB-C（XZ 法向为 -Y：offset 正值落在 -Y 底面，负挤出穿入）
    body = body.cut(
        cq.Workplane("XZ")
        .workplane(offset=OUTER_W / 2 + 0.8)
        .center(USBC_X, USBC_Z_B)
        .rect(USBC_W + TOL * 1.5, USBC_H + TOL)
        .extrude(-(WALL + 6))
    )
    # 右侧：ESP 烧录口 12×5（再加 TOL）
    body = body.cut(
        cq.Workplane("YZ")
        .workplane(offset=OUTER_L / 2 + 0.8)
        .center(ESP_CY, ESP_USB_Z)
        .rect(ESP_USB_W + TOL, ESP_USB_H + TOL * 0.5)
        .extrude(-(WALL + 6))
    )

    # （已取消左壁双键开孔——开发板按键不穿壳）

    # 开关指拨槽：只打穿后壁，禁止挖穿电源舱底板
    body = body.cut(
        cq.Workplane("XY")
        .workplane(offset=-0.05)
        .center(SW_CX, SW_CY)
        .rect(SW_SLOT_W, SW_SLOT_H)
        .extrude(WALL + 0.25)
    )

    # 顶：天线馈线（XZ 法向 -Y：offset 负值落在 +Y 顶面，正挤出穿入）
    body = body.cut(
        cq.Workplane("XZ")
        .workplane(offset=-(OUTER_W / 2 + 0.8))
        .center(ANT_CABLE_X, 11.0)
        .rect(ANT_CABLE_W + TOL, ANT_CABLE_H + TOL * 0.5)
        .extrude(WALL + 6)
    )

    # --- 摄像在后壳外侧（与前屏相对）：镜头向外拍照 ---
    lens_x = CAM_CX + CAM_LENS_DX
    lens_y = CAM_CY + CAM_LENS_DY
    win_x, win_y = cam_win_center(lens_x, lens_y)
    body = body.cut(
        cq.Workplane("XY")
        .workplane(offset=WALL + 0.05)
        .center(CAM_CX, CAM_CY)
        .rect(CAM_POCKET_W, CAM_POCKET_H)
        .extrude(-(WALL - FACE_LIP))
    )
    # 外通孔 25×10：金属罩 + 闪光灯同一窗
    body = body.cut(
        cq.Workplane("XY")
        .workplane(offset=-0.8)
        .center(win_x, win_y)
        .rect(CAM_WIN_W + TOL * 0.5, CAM_WIN_H + TOL * 0.5)
        .extrude(WALL + 2.0)
    )
    # 内侧略放大让位，避免卡罩
    body = body.cut(
        cq.Workplane("XY")
        .workplane(offset=WALL + 0.05)
        .center(win_x, win_y)
        .rect(CAM_WIN_W + TOL + 0.8, CAM_WIN_H + TOL + 0.8)
        .extrude(-(WALL - 0.35))
    )
    # 排针在 +X，线束向下汇入 ESP
    body = body.cut(
        cq.Workplane("XY")
        .workplane(offset=WALL + 1.0)
        .center(CAM_CX + CAM_BOARD_W / 4, CAM_CY - CAM_BOARD_H / 2 - DUPONT_SIDE_CLEAR / 2 + 1.0)
        .rect(max(CAM_BOARD_W / 2 + 8.0, CAM_BUNDLE_T + 10.0), DUPONT_SIDE_CLEAR + 4.0)
        .extrude(WIRE_LOFT_Z)
    )

    body = body.union(make_strap_ear(h - EAR_BACK_H, EAR_BACK_H))
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
    mod_x0 = MODEM_CX - MODEM_W / 2 - 1.0
    mod_x1 = MODEM_CX + MODEM_W / 2 + 1.0
    mod_y0 = MODEM_CY - MODEM_H / 2 - 1.0
    mod_y1 = MODEM_CY + MODEM_H / 2 + 1.0

    bottom_inner = -INNER_Y
    right_inner = INNER_X
    issues: list[str] = []

    # 屏与摄已分前后壳，不再要求同面 Y 缝
    gap_lc = abs(LCD_CY - CAM_CY)

    # 后壳：摄像不得与 ESP/电源/4G XY 重叠
    cam_x0, cam_x1 = CAM_CX - CAM_BOARD_W / 2, CAM_CX + CAM_BOARD_W / 2
    if _overlap(cam_x0, cam_x1, esp_x0, esp_x1) > 1 and _overlap(cam_lo, cam_hi, esp_lo, esp_hi) > 1:
        issues.append("摄像与 ESP 后壳 XY 重叠")
    if _overlap(cam_x0, cam_x1, pwr_x0, pwr_x1) > 1 and _overlap(cam_lo, cam_hi, pwr_lo, pwr_hi) > 1:
        issues.append("摄像与电源后壳 XY 重叠")
    if _overlap(cam_x0, cam_x1, mod_x0, mod_x1) > 1 and _overlap(cam_lo, cam_hi, mod_y0, mod_y1) > 1:
        issues.append("摄像与 4G 后壳 XY 重叠")

    gap_ep = _gap(esp_lo, esp_hi, pwr_lo, pwr_hi)
    if gap_ep < ESP_PWR_GAP_MIN:
        issues.append(f"ESP-PWR 缝 {gap_ep:.1f} < {ESP_PWR_GAP_MIN}（走线）")

    # USB 可达（允许更大浮动）
    if not (0.3 <= (pwr_lo - bottom_inner) <= 3.5):
        issues.append(f"电源底边距内壁 {pwr_lo - bottom_inner:.1f}，USB-C 可能够不到或过松")
    if not (0.3 <= (right_inner - esp_x1) <= 3.5):
        issues.append(f"ESP 右边距内壁 {right_inner - esp_x1:.1f}，侧 USB 可能够不到或过松")

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
    if back_clear < BACK_STACK_H:
        issues.append(f"后仓净高 {back_clear:.1f} < {BACK_STACK_H:.0f}（开发板+杜邦栈）")
    if SPLIT_Z - WALL < WIRE_LOFT_Z + 6:
        issues.append("前线舱过浅")

    # 摄像外窗 25×10：中心对准镜头区，允许略超出 PCB 轮廓（外孔大于光学区）
    lens_x, lens_y = CAM_CX + CAM_LENS_DX, CAM_CY + CAM_LENS_DY
    win_x, win_y = cam_win_center(lens_x, lens_y)
    cam_x0, cam_x1 = CAM_CX - CAM_BOARD_W / 2, CAM_CX + CAM_BOARD_W / 2
    if not (cam_x0 + 2 <= lens_x <= cam_x1 - 2 and cam_lo + 3 <= lens_y <= cam_hi - 3):
        issues.append("镜头偏离摄像板有效区")
    if abs(CAM_WIN_H - 25.0) > 0.05 or abs(CAM_WIN_W - 10.0) > 0.05:
        issues.append("摄像窗外廓应为 25×10 mm")
    half_w, half_h = CAM_WIN_W / 2, CAM_WIN_H / 2
    if (
        win_x - half_w < -INNER_X - 0.2
        or win_x + half_w > INNER_X + 0.2
        or win_y - half_h < -INNER_Y - 0.2
        or win_y + half_h > INNER_Y + 0.2
    ):
        issues.append("摄像窗超出后壳内廓")
    # 窗须盖住镜头中心
    if not (win_x - half_w + 1 <= lens_x <= win_x + half_w - 1):
        issues.append("摄像窗未盖住镜头 X")
    if not (win_y - half_h + 1 <= lens_y <= win_y + half_h - 1):
        issues.append("摄像窗未盖住镜头 Y")
    # 开关槽：距电源底边名义 13mm，槽加大后允许 ±2mm 偏差
    if not (pwr_x0 - 2.0 <= SW_CX <= pwr_x1 + 2.0):
        issues.append("开关槽 X 严重偏离电源板")
    if not (pwr_lo + 1.0 <= SW_CY <= pwr_hi - 1.0):
        issues.append("开关槽 Y 不在电源板范围内")
    sw_from_bottom = SW_CY - pwr_lo
    if abs(sw_from_bottom - SW_FROM_PWR_BOTTOM) > 0.5:
        issues.append(f"开关距电源底 {sw_from_bottom:.1f} ≠ 名义 {SW_FROM_PWR_BOTTOM}")
    if SW_SLOT_H < SW_BTN_LEN + 3.0:
        issues.append("开关槽高度余量不足")

    # 卡扣必须贴内壁：臂外沿应压进壁厚，禁止悬空在腔内
    for i, (cx, cy, ox, oy) in enumerate(CLIPS):
        if abs(oy) > abs(ox):
            # 沿 Y 壁：中心距内壁 ≈ CLIP_T/2 − EMBED（负=嵌入）
            dist = abs(abs(cy) - INNER_Y)
            expect = CLIP_T / 2 - CLIP_EMBED
            if abs(dist - expect) > 0.15 or abs(cy) < INNER_Y - CLIP_T:
                issues.append(f"卡扣{i} Y悬空/未贴壁 dist={dist:.2f}")
            tip = abs(cy) + CLIP_T / 2 + HOOK
            skin = OUTER_W / 2 - tip
            if skin < MIN_HOOK_SKIN:
                issues.append(f"卡扣{i} 勾尖外壁余量仅 {skin:.2f}（须≥{MIN_HOOK_SKIN}）")
        else:
            dist = abs(abs(cx) - INNER_X)
            expect = CLIP_T / 2 - CLIP_EMBED
            if abs(dist - expect) > 0.15 or abs(cx) < INNER_X - CLIP_T:
                issues.append(f"卡扣{i} X悬空/未贴壁 dist={dist:.2f}")
            tip = abs(cx) + CLIP_T / 2 + HOOK
            skin = OUTER_L / 2 - tip
            if skin < MIN_HOOK_SKIN:
                issues.append(f"卡扣{i} 勾尖外壁余量仅 {skin:.2f}（须≥{MIN_HOOK_SKIN}）")

    # 20cm 杜邦：曼哈顿估算（含弯折余量），前壳板 z≈6，后仓板 z≈后壁内收一截
    z_front = 6.0
    z_back = OUTER_H - WALL - 12.0
    bend = 25.0

    def _cable(ax: float, ay: float, az: float, bx: float, by: float, bz: float) -> float:
        return abs(ax - bx) + abs(ay - by) + abs(az - bz) + bend

    cables = {
        "LCD左→ESP": _cable(
            LCD_CX - LCD_BOARD_W / 2, LCD_CY, z_front, esp_x0 + 4.0, ESP_CY, z_back
        ),
        "CAM→ESP": _cable(
            CAM_CX + CAM_BOARD_W / 2, CAM_CY, z_back, esp_x0 + 4.0, ESP_CY, z_back
        ),
        "MOD→ESP": _cable(
            MODEM_CX + MODEM_W / 2, MODEM_CY, z_back, esp_x0 + 4.0, ESP_CY, z_back
        ),
        "PWR→ESP": _cable(PWR_CX, pwr_hi - 2.0, z_back, ESP_CX - 10.0, ESP_CY, z_back),
    }
    for name, length in cables.items():
        if length > CABLE_BUDGET:
            tag = "超标" if length > CABLE_LEN else "余量不足"
            issues.append(f"杜邦 {name} 约 {length:.0f}mm（预算{CABLE_BUDGET:.0f}/{CABLE_LEN:.0f}）{tag}")

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
        "cam_win": (win_x, win_y),
        "switch": (SW_CX, SW_CY),
        "cables": cables,
    }


def _point_in_solid(shape, pt: tuple[float, float, float], tol: float = 1e-4) -> bool:
    try:
        return bool(shape.isInside(pt, tol))
    except Exception:
        from OCP.BRepClass3d import BRepClass3d_SolidClassifier
        from OCP.gp import gp_Pnt
        from OCP.TopAbs import TopAbs_IN, TopAbs_ON

        c = BRepClass3d_SolidClassifier(shape.wrapped, gp_Pnt(*pt), tol)
        return c.State() in (TopAbs_IN, TopAbs_ON)


def audit_through_holes(front: cq.Workplane, back: cq.Workplane) -> list[str]:
    """外开孔壁中采样点必须为空，禁止留皮。"""
    issues: list[str] = []
    f, b = front.val(), back.val()

    def check(shape, mid_pt, name: str) -> None:
        if _point_in_solid(shape, mid_pt):
            issues.append(f"外孔未贯通（留皮）: {name}")

    lx = CAM_CX + CAM_LENS_DX
    ly = CAM_CY + CAM_LENS_DY
    check(f, (LCD_CX, LCD_CY, 1.0), "LCD视窗")
    # 板区外表面应保持实体（禁止整板外凹打穿）
    if not _point_in_solid(f, (LCD_CX + LCD_BOARD_W / 2 - 3.0, LCD_CY, 0.5)):
        issues.append("屏板区外表面被打穿（应平整留壁）")
    check(f, (0.0, EAR_Y, SPLIT_Z - EAR_FRONT_H / 2), "前壳挂绳孔")

    check(b, (USBC_X, -OUTER_W / 2 + 1.0, USBC_Z_B), "底USB-C")
    check(b, (OUTER_L / 2 - 1.0, ESP_CY, ESP_USB_Z), "右ESP-USB")
    check(b, (SW_CX, SW_CY, 1.0), "背开关槽")
    # 开关槽不得挖穿电源舱底板
    if not _point_in_solid(b, (SW_CX, SW_CY, WALL + 1.2)):
        issues.append("开关槽过深，电源舱底板被挖穿")
    check(b, (lx, ly, 1.0), "后壳镜头区")
    wx, wy = cam_win_center(lx, ly)
    check(b, (wx, wy, 1.0), "后壳摄像窗25×10")
    # 窗四边中点也须贯通
    for name, pt in [
        ("摄像窗上", (wx, wy + CAM_WIN_H / 2 - 1.0, 1.0)),
        ("摄像窗下", (wx, wy - CAM_WIN_H / 2 + 1.0, 1.0)),
        ("摄像窗左", (wx - CAM_WIN_W / 2 + 1.0, wy, 1.0)),
        ("摄像窗右", (wx + CAM_WIN_W / 2 - 1.0, wy, 1.0)),
    ]:
        check(b, pt, name)
    if not _point_in_solid(b, (CAM_CX, CAM_CY - CAM_BOARD_H / 2 + 5.0, 0.5)):
        issues.append("摄像板区后壳外表面被打穿（应平整留壁）")
    check(b, (ANT_CABLE_X, OUTER_W / 2 - 1.0, 11.0), "顶天线孔")
    check(b, (0.0, EAR_Y, (OUTER_H - SPLIT_Z) - EAR_BACK_H / 2), "后壳挂绳孔")
    return issues


def audit_clip_fit(front: cq.Workplane, back: cq.Workplane) -> list[str]:
    """合盖后：勾在卡窗中空；口上台肩够厚；窗端立柱实心（过梁不悬空）。"""
    issues: list[str] = []
    if CLIP_RIM_KEEP < 2.5:
        issues.append(f"卡扣台肩厚 CLIP_RIM_KEEP={CLIP_RIM_KEEP} < 2.5")
    back_m = mate_back(back)
    f, b = front.val(), back_m.val()
    hook_bot_w = SPLIT_Z - 0.35 + CLIP_H - 1.35
    catch_w = SPLIT_Z + CLIP_RIM_KEEP
    seat_gap = hook_bot_w - catch_w
    if not (0.12 <= seat_gap <= 0.55):
        issues.append(f"卡扣台肩间隙 {seat_gap:.2f}mm（目标≈{CLIP_SEAT}）")
    hz = hook_bot_w + 0.55
    rim_mid_w = SPLIT_Z + CLIP_RIM_KEEP / 2
    hb = OUTER_H - SPLIT_Z
    z1 = hb - CLIP_RIM_KEEP
    win_z = CLIP_H + 0.5
    z0 = z1 - win_z
    # 立柱中段合盖坐标
    pillar_z_w = SPLIT_Z + hb - (z0 + win_z / 2)
    win_along = CLIP_W + 1.2
    for i, (cx, cy, ox, oy) in enumerate(CLIPS):
        if abs(oy) > abs(ox):
            sgn = 1.0 if cy > 0 else -1.0
            tip = (cx, sgn * (INNER_Y + HOOK - 0.15), hz)
            ledge = (cx, sgn * (OUTER_W / 2 - 0.45), rim_mid_w)
            # 筋与壁交界处必须实心（两段已拼上）
            join = (cx, sgn * (INNER_Y - 0.05), SPLIT_Z - 1.0)
            root = (cx, sgn * (INNER_Y + WALL / 2 - 0.2), SPLIT_Z - 2.5)
            pillar = (
                cx + (win_along / 2 - CLIP_PILLAR_W / 2),
                sgn * (OUTER_W / 2 - WALL / 2),
                pillar_z_w,
            )
        else:
            sgn = 1.0 if cx > 0 else -1.0
            tip = (sgn * (INNER_X + HOOK - 0.15), cy, hz)
            ledge = (sgn * (OUTER_L / 2 - 0.45), cy, rim_mid_w)
            join = (sgn * (INNER_X - 0.05), cy, SPLIT_Z - 1.0)
            root = (sgn * (INNER_X + WALL / 2 - 0.2), cy, SPLIT_Z - 2.5)
            pillar = (
                sgn * (OUTER_L / 2 - WALL / 2),
                cy + (win_along / 2 - CLIP_PILLAR_W / 2),
                pillar_z_w,
            )
        if not _point_in_solid(f, tip):
            issues.append(f"卡扣{i} 勾尖不在前壳实体")
            continue
        if not _point_in_solid(f, join):
            issues.append(f"卡扣{i} 勾与内壁未拼上（仍有缝）")
        if not _point_in_solid(f, root):
            issues.append(f"卡扣{i} 根部悬空（未连前壳壁）")
        if _point_in_solid(b, tip):
            issues.append(f"卡扣{i} 勾尖与后壳干涉")
        elif not _point_in_solid(b, ledge):
            issues.append(f"卡扣{i} 口上台肩缺失")
        elif not _point_in_solid(b, pillar):
            issues.append(f"卡扣{i} 过梁立柱缺失（仍会悬空）")
        elif hz < catch_w:
            issues.append(f"卡扣{i} 勾齿未越过台肩")
    return issues


def audit_strap_ear(front: cq.Workplane, back: cq.Workplane) -> list[str]:
    """挂绳耳合盖后应对接，圆孔同轴贯通。"""
    issues: list[str] = []
    back_m = mate_back(back)
    f, b = front.val(), back_m.val()
    # 合盖面两侧各 1mm 应都有耳实体
    if not _point_in_solid(f, (EAR_W / 3, EAR_Y, SPLIT_Z - 1.0)):
        issues.append("前壳挂绳耳未到达合盖面")
    if not _point_in_solid(b, (EAR_W / 3, EAR_Y, SPLIT_Z + 1.0)):
        issues.append("后壳挂绳耳未到达合盖面（合不上）")
    # 孔心应空
    if _point_in_solid(f, (0.0, EAR_Y, SPLIT_Z - EAR_FRONT_H / 2)):
        issues.append("前壳挂绳孔未打通")
    if _point_in_solid(b, (0.0, EAR_Y, SPLIT_Z + EAR_BACK_H / 2)):
        issues.append("后壳挂绳孔未打通")
    return issues


def audit_lip_joined(front: cq.Workplane) -> list[str]:
    """前壳止口外沿必须与内壁连成一体（无缩进缝）。"""
    issues: list[str] = []
    f = front.val()
    z = SPLIT_Z + LIP / 2
    # 止口嵌入量：外廓相对内腔
    embed = (_LIP_OUT_L - (OUTER_L - 2 * WALL)) / 2
    if embed < 0.35:
        issues.append(f"前壳止口嵌入内壁仅 {embed:.2f}mm（易留缝）")
    join_pts = [
        (0.0, INNER_Y - 0.05, z),
        (0.0, -(INNER_Y - 0.05), z),
        (INNER_X - 0.05, 0.0, z),
        (-(INNER_X - 0.05), 0.0, z),
    ]
    for p in join_pts:
        if not _point_in_solid(f, p):
            issues.append(f"前壳止口与内壁未拼上 @({p[0]:.1f},{p[1]:.1f})")
            break
    wall_pts = [
        (0.0, INNER_Y + embed / 2, z),
        (INNER_X + embed / 2, 0.0, z),
    ]
    for p in wall_pts:
        if not _point_in_solid(f, p):
            issues.append(f"前壳止口未嵌入外壁 @({p[0]:.1f},{p[1]:.1f})")
            break
    return issues


def audit_rim_corners(back: cq.Workplane) -> list[str]:
    """后壳止口槽四角留壁 ≥ MIN_RIM_WALL（等距圆角，不削穿）。"""
    issues: list[str] = []
    if MIN_RIM_WALL < 0.5:
        issues.append(f"MIN_RIM_WALL={MIN_RIM_WALL} < 0.5")
    if abs(_GROOVE_R - (CORNER_R - MIN_RIM_WALL)) > 0.05:
        issues.append("止口槽圆角未按外壁等距偏移")
    b = back.val()
    hb = OUTER_H - SPLIT_Z
    z = hb - LIP / 2
    s2 = math.sqrt(2.0) / 2.0
    # 四角：圆角圆心 → 对角线方向取样
    for sx, sy in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
        cx0 = sx * (OUTER_L / 2 - CORNER_R)
        cy0 = sy * (OUTER_W / 2 - CORNER_R)
        # 留壁中线应实心
        r_mid = CORNER_R - MIN_RIM_WALL / 2
        mid = (cx0 + sx * r_mid * s2, cy0 + sy * r_mid * s2, z)
        if not _point_in_solid(b, mid):
            issues.append(f"后壳止口四角壁厚缺失 @({mid[0]:.1f},{mid[1]:.1f})")
            break
        # 刚进槽内应为空（确认槽在，且未把外圈整段挖没）
        r_in = CORNER_R - MIN_RIM_WALL - 0.25
        inside = (cx0 + sx * r_in * s2, cy0 + sy * r_in * s2, z)
        if _point_in_solid(b, inside):
            issues.append(f"后壳止口槽四角未挖通 @({inside[0]:.1f},{inside[1]:.1f})")
            break
    return issues


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
    hole_issues = audit_through_holes(front, back)
    if hole_issues:
        print("AUDIT FAIL (through-holes):")
        for i in hole_issues:
            print("  !!", i)
        raise SystemExit(1)
    clip_issues = audit_clip_fit(front, back)
    if clip_issues:
        print("AUDIT FAIL (clips):")
        for i in clip_issues:
            print("  !!", i)
        raise SystemExit(1)
    ear_issues = audit_strap_ear(front, back)
    if ear_issues:
        print("AUDIT FAIL (strap ear):")
        for i in ear_issues:
            print("  !!", i)
        raise SystemExit(1)
    lip_issues = audit_lip_joined(front)
    if lip_issues:
        print("AUDIT FAIL (lip):")
        for i in lip_issues:
            print("  !!", i)
        raise SystemExit(1)
    rim_issues = audit_rim_corners(back)
    if rim_issues:
        print("AUDIT FAIL (rim corners):")
        for i in rim_issues:
            print("  !!", i)
        raise SystemExit(1)

    preview = front.union(mate_back(back))
    cq.exporters.export(front, str(out / "saoti_front.stl"))
    cq.exporters.export(back, str(out / "saoti_back.stl"))
    cq.exporters.export(preview, str(out / "saoti_assembled_preview.stl"))

    print("OK v7.36 ->", out)
    print(f"  外廓 {OUTER_L}×{OUTER_W}×{OUTER_H}  前{SPLIT_Z}/后{OUTER_H - SPLIT_Z}")
    print(f"  容错 TOL={TOL} POCKET={POCKET} WALL_GAP={WALL_GAP} HOOK={HOOK}")
    print(
        f"  止口四角留壁 MIN_RIM_WALL={MIN_RIM_WALL}"
        f" 槽R={_GROOVE_R:.1f} 唇R={_LIP_OUT_R:.1f} 合盖间隙{LIP_GROOVE_CLEAR}"
    )
    print(f"  屏前/摄后 中心距Y {a['gap_lcd_cam']:.1f}  ESP-PWR缝 {a['gap_esp_pwr']:.1f}")
    print(f"  电源→底内壁 {a['pwr_to_bottom']:.1f}mm  ESP→右内壁 {a['esp_to_right']:.1f}mm")
    print(
        f"  卡扣 {len(CLIPS)}枚 贴壁(_yw={_yw:.2f}/_xw={_xw:.2f})"
        f" EMBED={CLIP_EMBED} HOOK={HOOK} CLIP_H={CLIP_H} LIP={LIP}"
    )
    print("  外孔: 前屏窗 | 后摄像窗25×10 | 底充电USB | 右烧录USB 12×5 | 背开关 | 顶天线 | 挂绳")
    print("  开发板按键: 不开壳孔")
    print(
        f"  ESP舱 支撑板厚{ESP_DECK_H} 围边{ESP_RIM_H}"
        f"  烧录口{ESP_USB_W}×{ESP_USB_H} @Z={ESP_USB_Z:.1f}"
    )
    print(
        f"  摄像(后壳) PCB{CAM_PCB_L}×{CAM_PCB_W} 占位{CAM_BOARD_W}×{CAM_BOARD_H}"
        f" @({CAM_CX:.0f},{CAM_CY:.0f}) 镜头({a['lens'][0]:.1f},{a['lens'][1]:.1f})"
        f" 外窗{CAM_WIN_H}×{CAM_WIN_W}@({a['cam_win'][0]:.1f},{a['cam_win'][1]:.1f})"
    )
    print(f"  屏 AA={LCD_AA} 开窗={LCD_VIEW:.1f} 板{LCD_BOARD_W}×{LCD_BOARD_H} 外表面留壁{FACE_LIP}")
    print(
        f"  开关槽 @ ({a['switch'][0]:.1f},{a['switch'][1]:.1f})"
        f"  距电源底{SW_FROM_PWR_BOTTOM:.0f} 拨钮{SW_BTN_LEN:.0f}"
        f"  槽{SW_SLOT_W:.0f}×{SW_SLOT_H:.1f}"
    )
    print(f"  电源占位 {PWR_LEN_X:.0f}×{PWR_WID_Y:.0f}×{PWR_H:.0f}")
    print(
        f"  后仓净高 {a['back_clear']:.1f}mm（目标≥{BACK_STACK_H:.0f}）"
        f"  杜邦侧向 {DUPONT_SIDE_CLEAR} 线舱Z {WIRE_LOFT_Z}"
    )
    for name, length in a["cables"].items():
        print(f"  线径 {name}: {length:.0f}mm / {CABLE_LEN:.0f}mm")
    print("AUDIT PASS")


if __name__ == "__main__":
    main()
