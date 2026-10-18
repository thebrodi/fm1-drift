#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dominik Siemon
"""Put Haave's website together (GitHub Pages):

  index.html                  web/site/index.html: what Haave is, how to play, the worlds
  img/, fonts/                the screen's pictures and font (tools/make_haave_shots.py: cc, Pillow, fontTools)
  try/                        the browser emulator (build/emu: web/emu/build.sh first)
  install/                    the web installer, or a "coming soon" page without a package
  webapp/installer/, firmware/  Felucca's installer and the package, as web/make_site.py makes them

  tools/make_haave_site.py OUT_DIR [PACKAGE.fwsc VERSION]
"""
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "web"))
sys.path.insert(0, str(ROOT / "tools"))

SOON = """<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Haave: install</title>
<style>@font-face{font-family:"Inter Tight";src:url("../fonts/InterTight.woff") format("woff");font-weight:100 900}
body{margin:0;background:#0e1124;color:#e6e2ff;font:17px/1.65 "Inter Tight",system-ui,sans-serif}
main{max-width:640px;margin:0 auto;padding:56px 20px}a{color:#85b7eb}
.hd{border-bottom:1px solid #2a2d55;padding-bottom:8px;font-size:14px;font-weight:600;letter-spacing:.16em;color:#afa9ec}
.pill{display:inline-block;background:#343862;border-radius:999px;padding:4px 14px;font-size:13px;font-weight:600;
letter-spacing:.14em;margin:28px 0 14px}</style>
<main><div class="hd">HAAVE · INSTALL</div><span class="pill">COMING SOON</span>
<p>Haave isn't released for everyone yet. It runs on my own FM-1, and in your <a href="../try/">browser</a>.</p>
<p>Once it's released, you'll install it here from Chrome or Edge, with nothing else needed.</p>
<p><a href="../">Back to Haave</a></p></main>
"""


def main(out, pkg=None, version=None):
    out = Path(out)
    emu = ROOT / "build" / "emu"
    if not (emu / "felucca.wasm").exists():
        raise SystemExit("no build/emu: run web/emu/build.sh first")
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    if pkg:
        import make_site                                  # web/make_site.py: the installer and the package
        make_site.main(pkg, version, out)
        shutil.rmtree(out / "webapp" / "editor", ignore_errors=True)   # (Felucca's sound editor: not Haave's)
        (out / "install").mkdir()
        (out / "install" / "index.html").write_text(
            '<!doctype html><meta charset="utf-8"><title>Haave: install</title>'
            '<meta http-equiv="refresh" content="0; url=../webapp/installer/">'
            '<a href="../webapp/installer/">Haave installer</a>\n', encoding="utf-8")
    else:
        (out / "install").mkdir()
        (out / "install" / "index.html").write_text(SOON, encoding="utf-8")
    shutil.copytree(emu, out / "try")
    shutil.copy(ROOT / "web" / "site" / "index.html", out / "index.html")
    import make_haave_shots                               # (tools/: the screen's pictures and font)
    make_haave_shots.main(out / "img")
    shutil.move(str(out / "img" / "fonts"), str(out / "fonts"))
    (out / ".nojekyll").write_text("")
    print(f"site: {out}: index.html, try/, install/" + (f" (package {Path(pkg).name}, {version})" if pkg else
                                                         " (no package yet: coming soon)"))


if __name__ == "__main__":
    if len(sys.argv) not in (2, 4):
        sys.exit(__doc__)
    main(*sys.argv[1:])
