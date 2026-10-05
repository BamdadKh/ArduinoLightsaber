#!/usr/bin/env python3
"""Build docs/images/screens.png and blade.png from the simulator output.

  python firmware/tools/sim/run_sim.py && python firmware/tools/make_readme_images.py
"""
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
RAW = HERE / "out" / "raw"
DOCS = HERE.parent.parent / "docs" / "images"
BG, FG = (24, 26, 32), (200, 200, 210)
PAD, LAB = 14, 20


def oled(name, scale=4):
    src = Image.open(RAW / f"ui_{name}.pgm")
    w, h = src.size
    img = Image.new("RGB", (w * scale, h * scale), (6, 8, 12))
    d = ImageDraw.Draw(img)
    px = src.load()
    for y in range(h):
        for x in range(w):
            if px[x, y]:
                d.rectangle([x * scale, y * scale, x * scale + scale - 2, y * scale + scale - 2], fill=(170, 225, 255))
    return img


def sheet(tiles, cols):
    tw = max(t.width for _, t in tiles)
    th = max(t.height for _, t in tiles)
    rows = (len(tiles) + cols - 1) // cols
    img = Image.new("RGB", (cols * (tw + PAD) + PAD, rows * (th + PAD + LAB) + PAD), BG)
    d = ImageDraw.Draw(img)
    for i, (label, t) in enumerate(tiles):
        x = PAD + (i % cols) * (tw + PAD)
        y = PAD + (i // cols) * (th + PAD + LAB)
        d.text((x, y + 3), label, fill=FG)
        img.paste(t, (x, y + LAB))
    return img


def blade(name, width=520, height=150):
    # time runs left to right, hilt at the bottom, tip at the top
    return Image.open(RAW / f"blade_{name}.ppm").resize((width, height), Image.LANCZOS)


def main():
    screens = [("boot", "boot_1850"), ("ready", "idle"), ("low battery", "idle_lowbatt"), ("lit", "on_still"),
               ("swing", "on_swing"), ("blaster", "on_blaster"), ("lockup", "on_lockup"), ("menu: sound", "menu_00"),
               ("menu: battery", "menu_06")]
    sheet([(n, oled(f)) for n, f in screens], 9).save(DOCS / "screens.png")

    tiles = [("ignite and retract", blade("ignite")), ("flash, blaster block, lockup", blade("effects"))]
    names = ["azure", "jade", "ruby", "amber", "iris", "ice"]
    tiles += [(f"{n}, with a swing in the middle", blade(f"look_{i}")) for i, n in enumerate(names)]
    sheet(tiles, 2).save(DOCS / "blade.png")


if __name__ == "__main__":
    main()
