#!/usr/bin/env python3
"""tools/make_widget_icons.py

Generates the main_interface widget icons committed under assets/widget/:
five 48x48 PNGs, one per main-interface widget (SET TARGET, CONFIRM, ARM,
RELEASE, ABORT — see src/hw/widget_map.h). Re-run after changing the design:

    python tools/make_widget_icons.py

Requires Pillow (pip install pillow). The output is intentionally simple
flat geometry — the icons render at a few dozen pixels in DJI Pilot 2 and
only need to be distinguishable and readable at that size.
"""

import os
import sys

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("Pillow is required: pip install pillow")

SIZE = 48
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "assets", "widget")

# (file name, background colour, glyph painter)
ICONS = [
    ("icon_set_target.png", (30, 105, 210), "crosshair"),   # blue
    ("icon_confirm.png", (40, 145, 60), "check"),           # green
    ("icon_arm.png", (215, 140, 20), "triangle"),           # amber
    ("icon_release.png", (190, 60, 160), "down_arrow"),     # magenta
    ("icon_abort.png", (200, 40, 40), "x"),                 # red
]


def draw_crosshair(d: ImageDraw.ImageDraw, s: int) -> None:
    c, r = s // 2, s // 4
    d.ellipse([c - r, c - r, c + r, c + r], outline="white", width=4)
    d.line([c, 4, c, s // 3], fill="white", width=4)
    d.line([c, s - s // 3, c, s - 4], fill="white", width=4)
    d.line([4, c, s // 3, c], fill="white", width=4)
    d.line([s - s // 3, c, s - 4, c], fill="white", width=4)


def draw_check(d: ImageDraw.ImageDraw, s: int) -> None:
    d.line([s // 5, s // 2, s * 5 // 12, s * 3 // 4, s * 4 // 5, s // 4],
           fill="white", width=6, joint="curve")


def draw_triangle(d: ImageDraw.ImageDraw, s: int) -> None:
    d.polygon([(s // 4, s // 5), (s * 4 // 5, s // 2), (s // 4, s * 4 // 5)],
              fill="white")


def draw_down_arrow(d: ImageDraw.ImageDraw, s: int) -> None:
    d.line([s // 2, s // 6, s // 2, s * 3 // 5], fill="white", width=7)
    d.polygon([(s // 5, s // 2), (s * 4 // 5, s // 2), (s // 2, s * 5 // 6)],
              fill="white")


def draw_x(d: ImageDraw.ImageDraw, s: int) -> None:
    d.line([s // 4, s // 4, s * 3 // 4, s * 3 // 4], fill="white", width=7)
    d.line([s * 3 // 4, s // 4, s // 4, s * 3 // 4], fill="white", width=7)


PAINTERS = {
    "crosshair": draw_crosshair,
    "check": draw_check,
    "triangle": draw_triangle,
    "down_arrow": draw_down_arrow,
    "x": draw_x,
}


def main() -> None:
    os.makedirs(OUT_DIR, exist_ok=True)
    for name, colour, glyph in ICONS:
        img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        d.rounded_rectangle([1, 1, SIZE - 2, SIZE - 2], radius=10, fill=colour)
        PAINTERS[glyph](d, SIZE)
        path = os.path.join(OUT_DIR, name)
        img.save(path)
        print(f"wrote {path} ({os.path.getsize(path)} bytes)")


if __name__ == "__main__":
    main()
