#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dominik Siemon
"""Put Drift's website together (GitHub Pages):

  index.html                  web/site/index.html: what Drift is, how to play, the worlds
  try/                        the browser emulator (build/emu: web/emu/build.sh first)
  install/                    the web installer, or a "coming soon" page without a package
  webapp/installer/, firmware/  Felucca's installer and the package, as web/make_site.py makes them

  tools/make_drift_site.py OUT_DIR [PACKAGE.fwsc VERSION]
"""
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "web"))

SOON = """<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Drift: install</title>
<body style="margin:0;background:#0e1124;color:#e6e2ff;font:17px/1.6 system-ui,sans-serif">
<main style="max-width:640px;margin:0 auto;padding:56px 20px">
<h1 style="letter-spacing:.06em">Install: coming soon</h1>
<p>Drift has not been released for the FM-1 yet. Meanwhile, <a style="color:#85b7eb" href="../try/">try it in your
browser</a>.</p><p><a style="color:#85b7eb" href="../">Back to Drift</a></p></main>
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
        shutil.rmtree(out / "webapp" / "editor", ignore_errors=True)   # (Felucca's sound editor: not Drift's)
        (out / "install").mkdir()
        (out / "install" / "index.html").write_text(
            '<!doctype html><meta charset="utf-8"><title>Drift: install</title>'
            '<meta http-equiv="refresh" content="0; url=../webapp/installer/">'
            '<a href="../webapp/installer/">Drift installer</a>\n', encoding="utf-8")
    else:
        (out / "install").mkdir()
        (out / "install" / "index.html").write_text(SOON, encoding="utf-8")
    shutil.copytree(emu, out / "try")
    shutil.copy(ROOT / "web" / "site" / "index.html", out / "index.html")
    (out / ".nojekyll").write_text("")
    print(f"site: {out}: index.html, try/, install/" + (f" (package {Path(pkg).name}, {version})" if pkg else
                                                         " (no package yet: coming soon)"))


if __name__ == "__main__":
    if len(sys.argv) not in (2, 4):
        sys.exit(__doc__)
    main(*sys.argv[1:])
