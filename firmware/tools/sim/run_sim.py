#!/usr/bin/env python3
"""Build the PC simulator with MSVC, run it, and turn its dumps into preview PNGs.

  python firmware/tools/sim/run_sim.py      -> firmware/tools/out/*.png

The simulator compiles the real ui.cpp / blade.cpp / settings.cpp, so what you see
is what the saber's OLED and LED strip render.
"""
import glob
import os
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
FW = HERE.parent.parent / "lightsaber"
OUT = HERE.parent / "out"
RAW = OUT / "raw"
SOURCES = ["sim.cpp"] + [str(FW / f) for f in ("ui.cpp", "blade.cpp", "settings.cpp", "mathx.cpp", "color.cpp")]


def build():
    exe = RAW / "sim.exe"
    gxx = r"C:\msys64\ucrt64\bin\g++.exe"
    if os.path.exists(gxx):
        subprocess.check_call([gxx, "-std=c++17", "-O2", "-static", "-Wall", "-Wno-unused-function",
                               f"-I{HERE}", f"-I{FW}", "-o", str(exe)] + SOURCES, cwd=HERE,
                              env=dict(os.environ, PATH=os.path.dirname(gxx) + os.pathsep + os.environ["PATH"]))
        return exe
    # fall back to MSVC (needs the "Desktop development with C++" workload)
    vswhere = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    vs = subprocess.check_output([vswhere, "-latest", "-property", "installationPath"], text=True).strip()
    vcvars = os.path.join(vs, r"VC\Auxiliary\Build\vcvars64.bat")
    srcs = " ".join(f'"{s}"' for s in SOURCES)
    flags = "/nologo /std:c++17 /O2 /EHsc /W3 /wd4244 /wd4267 /wd4996"
    lines = [
        f'@call "{vcvars}" >nul',
        f'cl {flags} /I"{HERE}" /I"{FW}" /Fo"{RAW}\\\\" /Fe"{exe}" {srcs}',
    ]
    bat = RAW / "build.bat"
    bat.write_text("\r\n".join(lines) + "\r\n")
    subprocess.check_call(["cmd", "/c", str(bat)], cwd=HERE)
    return exe


def oled_png(pgm, scale=5):
    from PIL import Image, ImageDraw
    src = Image.open(pgm)
    w, h = src.size
    img = Image.new("RGB", (w * scale, h * scale), (6, 8, 12))
    d = ImageDraw.Draw(img)
    px = src.load()
    for y in range(h):
        for x in range(w):
            if px[x, y]:
                d.rectangle([x * scale, y * scale, x * scale + scale - 2, y * scale + scale - 2], fill=(170, 225, 255))
    return img


def sheet(files, cols, out):
    from PIL import Image, ImageDraw
    tiles = [(Path(f).stem.replace("ui_", ""), oled_png(f)) for f in files]
    tw, th = tiles[0][1].size
    pad, lab = 14, 18
    rows = (len(tiles) + cols - 1) // cols
    img = Image.new("RGB", (cols * (tw + pad) + pad, rows * (th + pad + lab) + pad), (24, 26, 32))
    d = ImageDraw.Draw(img)
    for i, (name, t) in enumerate(tiles):
        x = pad + (i % cols) * (tw + pad)
        y = pad + (i // cols) * (th + pad + lab)
        img.paste(t, (x, y + lab))
        d.text((x, y + 2), name, fill=(200, 200, 210))
    img.save(out)
    print("wrote", out)


def blade_png(ppm, out):
    from PIL import Image
    img = Image.open(ppm)
    img.resize((img.width * 2, img.height * 3), Image.NEAREST).save(out)


def main():
    RAW.mkdir(parents=True, exist_ok=True)
    exe = build()
    subprocess.check_call([str(exe), str(RAW)])
    ui = sorted(glob.glob(str(RAW / "ui_*.pgm")))
    sheet([f for f in ui if "menu_" not in f], 9, OUT / "sim_screens.png")
    sheet([f for f in ui if "menu_" in f], 8, OUT / "sim_menu.png")
    for f in glob.glob(str(RAW / "blade_*.ppm")):
        if "tmp" not in f:
            blade_png(f, OUT / (Path(f).stem + ".png"))


if __name__ == "__main__":
    main()
