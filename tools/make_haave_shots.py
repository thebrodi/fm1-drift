#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dominik Siemon
"""HAAVE's screen for the website: build and run web/emu/haave_shots.c (the firmware, natively, through short scenes),
then write its frames as animated WebP (lossless, 10 frames a second) into OUT_DIR:

  hero.webp, play.webp, knobs.webp, ...   the whole 240 x 240 screen, one per scene
  world00.webp .. world13.webp            each world's picture (the screen's middle, 240 x 88), playing
  NAMEs.webp                              a still frame of each (world00s.webp ..)
  fonts/InterTight.woff                   the screen's font (assets/fonts, SIL OFL 1.1), its Latin letters only

Needs cc, Pillow (with WebP) and fontTools; run after one ./build.sh (build/gen).
  tools/make_haave_shots.py OUT_DIR
"""
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FRAME = 240 * 240 * 3
VIZ = (0, 84, 240, 172)                                 # haave_ui.c VIZ_Y, VIZ_H
TEXT = (" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"
        "äöüÄÖÜéèàçñ·–—‘’“”…→←↑↓−×♭♯°")


def frames(path):
    from PIL import Image
    raw = path.read_bytes()
    return [Image.frombytes("RGB", (240, 240), raw[i * FRAME:(i + 1) * FRAME]) for i in range(len(raw) // FRAME)]


def save(images, path):
    images[0].save(path, save_all=True, append_images=images[1:], duration=100, loop=0, lossless=True, method=6)


def font(out):
    from fontTools import subset
    src = ROOT / "assets" / "fonts" / "InterTight[wght].ttf"
    (out / "fonts").mkdir(parents=True, exist_ok=True)
    opts = subset.Options()
    opts.flavor = "woff"
    opts.layout_features = ["kern", "liga", "calt", "tnum"]
    f = subset.load_font(str(src), opts)
    s = subset.Subsetter(opts)
    s.populate(text=TEXT)
    s.subset(f)
    subset.save_font(f, str(out / "fonts" / "InterTight.woff"), opts)
    shutil.copy(ROOT / "assets" / "fonts" / "OFL.txt", out / "fonts" / "OFL.txt")


def main(out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    raw = ROOT / "build" / "shots"
    raw.mkdir(parents=True, exist_ok=True)
    for old in raw.glob("*.rgb"):                       # (a scene gone stays gone)
        old.unlink()
    exe = ROOT / "build" / "haave_shots"
    subprocess.run(["cc", "-O2", "-ffp-contract=off", "-w", "-Ibuild/gen", "-Ifirmware/src", "-o", str(exe),
                    "web/emu/haave_shots.c", "-lm"], cwd=ROOT, check=True)
    subprocess.run([str(exe), str(raw)], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    n = 0
    for p in sorted(raw.glob("*.rgb")):
        fs = frames(p)
        if p.stem.startswith("world"):
            fs = [f.crop(VIZ) for f in fs]
        fs[len(fs) // 2].save(out / f"{p.stem}s.webp", lossless=True, method=6)   # (a still: before a hover, or
                                                                                   # with reduced motion)
        save(fs, out / f"{p.stem}.webp")
        n += 1
    font(out)
    size = sum(f.stat().st_size for f in out.rglob("*") if f.is_file())
    print(f"shots: {out}: {n} animations and the font, {size // 1024} KB")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
