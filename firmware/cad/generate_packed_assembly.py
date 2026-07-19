#!/usr/bin/env python3
"""
生成「3D 打印外壳合盖 + 全部配件就位」装配模型。

输出（cad/stl/）:
  saoti_packed_assembly.stl     — 合盖整机（壳半剖，能看见内部）
  saoti_packed_internals.stl    — 仅内部配件（无壳，便于看摆位）
  saoti_packed_assembly.step    — STEP（可用 FreeCAD/Fusion 打开）
  （不再生成 packed_closed）

用法:
  python3 cad/generate_packed_assembly.py
"""

from __future__ import annotations

from pathlib import Path

import cadquery as cq

# 与 generate_case.py v7.1 保持一致
OUTER_L = 106.0
OUTER_W = 140.0
OUTER_H = 70.0
WALL = 2.0
CORNER_R = 8.0
SPLIT_Z = 34.0
INNER_X = OUTER_L / 2 - WALL
INNER_Y = OUTER_W / 2 - WALL

PWR_LEN_X, PWR_WID_Y, PWR_H = 64.0, 35.8, 20.0
PWR_CX = -6.0
PWR_CY = -INNER_Y + PWR_WID_Y / 2 + 0.6

ESP_LEN_X, ESP_WID_Y = 64.0, 28.4
ESP_CX = INNER_X - ESP_LEN_X / 2 - 0.4
ESP_CY = (PWR_CY + PWR_WID_Y / 2) + 18.0 + ESP_WID_Y / 2
ESP_BOARD_T = 1.6
ESP_COMP_H = 10.0  # USB/模组朝后壳外侧
ESP_PIN_H = 12.0   # 针脚+杜邦朝分型面/前线舱

LCD_BOARD_W, LCD_BOARD_H, LCD_T = 45.0, 31.0, 1.6  # PCB
LCD_GLASS = 26.5  # 黑边玻璃外廓约
LCD_VIEW = 23.4  # 可视区
LCD_MODULE_T = 3.2  # 玻璃+背光相对 PCB 顶面
LCD_CX, LCD_CY = 0.0, 52.0

CAM_BOARD_W, CAM_BOARD_H, CAM_BOARD_T = 35.7, 23.9, 2.0
CAM_LENS_H, CAM_D, CAM_SQ = 10.0, 8.0, 10.5
CAM_CX, CAM_CY = 0.0, 8.0
CAM_LENS_DX = 8.5  # 镜头偏模组端（+X），排针在 -X

MODEM_W, MODEM_H, MODEM_T = 28.0, 26.0, 12.0
MODEM_CX, MODEM_CY = -22.0, 36.0

ANT_PAD_W, ANT_PAD_H, ANT_PAD_T = 48.0, 14.0, 0.8
ANT_PAD_CX, ANT_PAD_CY = 0.0, 52.0

DUPONT_SIDE = 17.0


def _box(x: float, y: float, z0: float, sx: float, sy: float, sz: float) -> cq.Workplane:
    """中心在 (x,y)，底面在 z0，向 +Z 长 sz。"""
    return (
        cq.Workplane("XY")
        .workplane(offset=z0)
        .center(x, y)
        .box(sx, sy, sz, centered=(True, True, False))
    )


def _import_shells():
    """复用外壳生成函数。"""
    import importlib.util

    path = Path(__file__).resolve().parent / "generate_case.py"
    spec = importlib.util.spec_from_file_location("case", path)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod.shell_front(), mod.shell_back()


def place_back_closed(back: cq.Workplane) -> cq.Workplane:
    """
    前壳: Z=0 外前脸，开口在 +Z=SPLIT_Z。
    后壳建模同为杯形(Z=0 外后脸，开口在 +Z=h)。
    合盖：对 XY 镜像再平移，XY 不翻转，开口对开口，外后脸在 Z=OUTER_H。
    """
    h = OUTER_H - SPLIT_Z
    # opening→0, outer→-h → mirror → opening0/outer+h → +SPLIT_Z
    return back.translate((0, 0, -h)).mirror("XY").translate((0, 0, SPLIT_Z))


def make_components() -> dict[str, cq.Workplane]:
    """世界坐标：Z=0 前壳外表面，Z=OUTER_H 后壳外表面。"""
    parts: dict[str, cq.Workplane] = {}

    # 屏（按微雪实物）：蓝板 45×31，方屏居中，接口在左右短边
    z_lcd0 = WALL - 0.3
    parts["lcd_board"] = _box(LCD_CX, LCD_CY, z_lcd0, LCD_BOARD_W, LCD_BOARD_H, LCD_T)
    # 黑边玻璃（居中）
    parts["lcd_bezel"] = _box(
        LCD_CX, LCD_CY, z_lcd0 + LCD_T, LCD_GLASS, LCD_GLASS, LCD_MODULE_T
    )
    # 可视区略凸出前脸内侧
    parts["lcd_glass"] = _box(LCD_CX, LCD_CY, 0.35, LCD_VIEW, LCD_VIEW, 1.0)
    # 左侧 PH2.0 座（矮）
    parts["lcd_ph20"] = _box(
        LCD_CX - LCD_BOARD_W / 2 + 3.5,
        LCD_CY,
        z_lcd0 + LCD_T,
        7.0,
        18.0,
        4.5,
    )
    # 右侧排针 + 杜邦（用户接线侧）
    parts["lcd_header"] = _box(
        LCD_CX + LCD_BOARD_W / 2 - 2.0,
        LCD_CY,
        z_lcd0 + LCD_T,
        2.54,
        20.5,
        8.0,
    )
    parts["dupont_lcd"] = _box(
        LCD_CX + LCD_BOARD_W / 2 + DUPONT_SIDE / 2 - 1.0,
        LCD_CY,
        z_lcd0 + 2.0,
        DUPONT_SIDE - 2.0,
        18.0,
        6.0,
    )

    # 摄像：矩形 PCB；方形模组偏 +X；排针在 -X
    z_cam_board = WALL + 0.5
    lens_x, lens_y = CAM_CX + CAM_LENS_DX, CAM_CY
    parts["cam_board"] = _box(
        CAM_CX, CAM_CY, z_cam_board, CAM_BOARD_W, CAM_BOARD_H, CAM_BOARD_T
    )
    parts["cam_square"] = _box(
        lens_x, lens_y, 0.4, CAM_SQ, CAM_SQ, CAM_LENS_H - 1.5
    )
    parts["cam_lens"] = (
        cq.Workplane("XY")
        .workplane(offset=0.15)
        .center(lens_x, lens_y)
        .circle(CAM_D / 2)
        .extrude(CAM_LENS_H)
    )
    # 排针侧杜邦（-X 短边，DVP 约 18 线）
    parts["dupont_cam"] = _box(
        CAM_CX - CAM_BOARD_W / 2 - DUPONT_SIDE / 2 + 1.0,
        CAM_CY,
        z_cam_board + 1.0,
        DUPONT_SIDE - 2.0,
        18.0,
        8.0,
    )

    # 中央线舱线束（屏+摄 → ESP）
    parts["wire_loft"] = _box(-4.0, 22.0, SPLIT_Z - 14.0, 50.0, 28.0, 12.0)

    # 后壳内底板世界 Z
    z_back_floor = OUTER_H - WALL  # 零件“坐”在此面向 -Z 生长时用 z0=floor-height

    # 电源：贴后壳内底
    parts["power"] = _box(
        PWR_CX, PWR_CY, z_back_floor - PWR_H, PWR_LEN_X, PWR_WID_Y, PWR_H
    )

    # ESP：器件朝后（+Z 向外后），针脚+杜邦朝前（-Z）
    z_esp_pcb = z_back_floor - ESP_COMP_H - ESP_BOARD_T
    parts["esp_pcb"] = _box(
        ESP_CX, ESP_CY, z_esp_pcb, ESP_LEN_X, ESP_WID_Y, ESP_BOARD_T
    )
    parts["esp_components"] = _box(
        ESP_CX, ESP_CY, z_esp_pcb + ESP_BOARD_T, ESP_LEN_X * 0.7, ESP_WID_Y * 0.7, ESP_COMP_H
    )
    parts["esp_dupont"] = _box(
        ESP_CX - 8.0,
        ESP_CY,
        z_esp_pcb - ESP_PIN_H,
        ESP_LEN_X * 0.55,
        ESP_WID_Y + 10.0,
        ESP_PIN_H,
    )

    # 4G
    parts["modem"] = _box(
        MODEM_CX, MODEM_CY, z_back_floor - MODEM_T, MODEM_W, MODEM_H, MODEM_T
    )
    parts["modem_dupont"] = _box(
        MODEM_CX + MODEM_W / 2 + 6.0,
        MODEM_CY,
        z_back_floor - 8.0,
        12.0,
        14.0,
        6.0,
    )

    # 天线外贴（后壳外侧 = Z≥OUTER_H）
    parts["antenna"] = _box(
        ANT_PAD_CX, ANT_PAD_CY, OUTER_H, ANT_PAD_W, ANT_PAD_H, ANT_PAD_T
    )

    return parts


def union_all(parts: list[cq.Workplane]) -> cq.Workplane:
    out = parts[0]
    for p in parts[1:]:
        try:
            out = out.union(p)
        except Exception:
            try:
                out = out.fuse(p)
            except Exception:
                pass
    return out


def make_cutaway_shell(front: cq.Workplane, back_closed: cq.Workplane) -> cq.Workplane:
    """去掉 -X 半边外壳，保留右侧（ESP/电源）便于看内部。"""
    cutter = (
        cq.Workplane("XY")
        .workplane(offset=-1)
        .center(-(OUTER_L / 4 + 5), 0)
        .box(OUTER_L / 2 + 20, OUTER_W + 40, OUTER_H + 20, centered=(True, True, False))
    )
    try:
        shell = front.union(back_closed)
    except Exception:
        shell = front.fuse(back_closed)
    return shell.cut(cutter)


def main() -> None:
    out = Path(__file__).resolve().parent / "stl"
    out.mkdir(parents=True, exist_ok=True)

    print("生成外壳…")
    front, back = _import_shells()
    back_closed = place_back_closed(back)

    print("生成配件占位…")
    comps = make_components()
    internal = union_all(list(comps.values()))

    print("组合半剖装配…")
    shell_cut = make_cutaway_shell(front, back_closed)
    try:
        packed = shell_cut.union(internal)
    except Exception:
        packed = shell_cut.fuse(internal)

    stl_packed = out / "saoti_packed_assembly.stl"
    stl_inside = out / "saoti_packed_internals.stl"
    step_packed = out / "saoti_packed_assembly.step"

    cq.exporters.export(packed, str(stl_packed))
    cq.exporters.export(internal, str(stl_inside))

    # STEP：壳半剖 + 内部（单实体，兼容性最好）
    try:
        cq.exporters.export(packed, str(step_packed))
    except Exception as e:
        print("STEP 导出失败，仅 STL:", e)

    desk = Path.home() / "Desktop" / "saoti-guajian-packed-3d"
    desk.mkdir(parents=True, exist_ok=True)
    for p in out.glob("saoti_packed_*"):
        target = desk / p.name
        target.write_bytes(p.read_bytes())

    readme = desk / "README.txt"
    readme.write_text(
        "扫题挂件 — 打印外壳合盖后配件就位预览（v7.1）\n"
        "\n"
        "文件说明：\n"
        "  saoti_packed_assembly.stl   推荐：半剖外壳 + 全部配件（能看清内部）\n"
        "  saoti_packed_internals.stl  仅配件占位（屏/摄/ESP/电源/4G/天线/线束）\n"
        "  saoti_packed_assembly.step  STEP，可用 FreeCAD / Fusion 360 打开\n"
        "\n"
        "占位尺寸按实物近似：\n"
        "  屏 45×31×5  摄像板+镜头  电源 64×35.8×20  ESP 64×28.4+杜邦  4G 28×26×12\n"
        "颜色区分需用 STEP 或分件 STL；单色 STL 靠形状区分。\n"
        "此文件仅供装配示意，不要直接当打印件。\n",
        encoding="utf-8",
    )

    # zip
    import shutil

    zip_base = Path.home() / "Desktop" / "saoti-guajian-packed-3d"
    shutil.make_archive(str(zip_base), "zip", str(desk))

    print("OK ->", out)
    print("桌面文件夹:", desk)
    print("桌面压缩包:", f"{zip_base}.zip")
    for p in sorted(desk.iterdir()):
        print(f"  {p.name:40s} {p.stat().st_size/1024:.1f} KB")


if __name__ == "__main__":
    main()
