#!/usr/bin/env python3
"""Generate 100x100x100 mm cube enclosure for scan-answer pendant (Plan A)."""

from pathlib import Path

import cadquery as cq

OUT_DIR = Path(__file__).resolve().parent

# Outer dimensions (mm)
SIZE = 100.0
BODY_H = 92.0
LID_H = 8.0
WALL = 2.0

# Front openings (Y=0 face): X right, Z up
CAM_W, CAM_H = 36.0, 24.0
CAM_CX, CAM_CZ = 30.0, 70.0

LCD_W, LCD_H = 34.0, 34.0
LCD_CX, LCD_CZ = 79.0, 52.0

# Side openings
BTN_R = 6.0
BTN_CY, BTN_CZ = 50.0, 28.0  # right face (+X)

SIM_W, SIM_H = 16.0, 3.0
SIM_CY, SIM_CZ = 18.0, 50.0  # left face (X=0)

# Bottom USB-C slot (Z=0)
USBC_W, USBC_D = 12.0, 5.0
USBC_CX, USBC_CY = 50.0, 10.0

# Top lanyard hole
LANYARD_R = 2.2


def make_body() -> cq.Workplane:
    body = cq.Workplane("XY").box(SIZE, SIZE, BODY_H, centered=(False, False, False))

    # Hollow shell, open at top
    body = body.faces(">Z").shell(-WALL)

    # Camera window (front)
    body = (
        body.faces("<Y")
        .workplane(origin=(CAM_CX, 0, CAM_CZ))
        .rect(CAM_W, CAM_H)
        .cutThruAll()
    )

    # Display window (front)
    body = (
        body.faces("<Y")
        .workplane(origin=(LCD_CX, 0, LCD_CZ))
        .rect(LCD_W, LCD_H)
        .cutThruAll()
    )

    # Photo button (right side +X)
    body = (
        body.faces(">X")
        .workplane(origin=(SIZE, BTN_CY, BTN_CZ))
        .circle(BTN_R)
        .cutThruAll()
    )

    # Nano SIM tray slot (left side X=0)
    body = (
        body.faces("<X")
        .workplane(origin=(0, SIM_CY, SIM_CZ))
        .rect(SIM_W, SIM_H)
        .cutThruAll()
    )

    # USB-C opening on bottom, near front edge
    body = (
        body.faces("<Z")
        .workplane(origin=(USBC_CX, USBC_CY, 0))
        .rect(USBC_W, USBC_D)
        .cutThruAll()
    )

    # Internal standoffs for T-SIMCam area (optional screw posts)
    posts = []
    for px, py in ((8, 8), (8, 42), (74, 8), (74, 42)):
        posts.append(
            cq.Workplane("XY")
            .center(px + 4, py + 4)
            .circle(1.6)
            .extrude(6.0)
        )
    for p in posts[1:]:
        posts[0] = posts[0].union(p)
    body = body.union(posts[0])

    return body


def make_lid() -> cq.Workplane:
    lid = cq.Workplane("XY").box(SIZE, SIZE, LID_H, centered=(False, False, False))

    # Inner lip for friction fit
    lip = (
        cq.Workplane("XY")
        .workplane(offset=LID_H)
        .box(SIZE - 2 * WALL, SIZE - 2 * WALL, 6.0, centered=(False, False, False))
        .translate((WALL, WALL, 0))
    )
    lid = lid.union(lip)

    # Lanyard hole near front-top edge
    lid = (
        lid.faces(">Z")
        .workplane(origin=(50, 8, LID_H))
        .circle(LANYARD_R)
        .cutThruAll()
    )

    # Thin top area for 4G antenna (mark with shallow pocket, non-functional)
    lid = (
        lid.faces(">Z")
        .workplane(origin=(50, 72, LID_H))
        .rect(70, 24)
        .cutBlind(-0.6)
    )

    return lid


def export_part(part: cq.Workplane, name: str) -> None:
    stl_path = OUT_DIR / f"{name}.stl"
    step_path = OUT_DIR / f"{name}.step"

    cq.exporters.export(part, str(stl_path))
    cq.exporters.export(part, str(step_path))
    print(f"Exported: {stl_path.name}, {step_path.name}")


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    export_part(make_body(), "saoti_guajian_body")
    export_part(make_lid(), "saoti_guajian_lid")
    print(f"\nDone. Files are in: {OUT_DIR}")


if __name__ == "__main__":
    main()
