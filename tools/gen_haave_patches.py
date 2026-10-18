#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dominik Siemon
"""HAAVE's own FM6 patches -> a C header of packed 128-byte voices (firmware/src/haave_ui.c loads them into a part as
its own patch, the way a project would; Felucca's factory patches F1..F8 stay as they are).

  tools/gen_haave_patches.py OUT.h

Written in the same readable form as tools/gen_fm6_patches.py and packed by it. Operators are OP1..OP6; frequency
ratio = fc (0 = 0.5) x (1 + ff / 100); algorithm 5 = three pairs (1<-2, 3<-4, 5<-6), 32 = six carriers.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_fm6_patches import op, voice, pack, c_rows  # noqa: E402

PATCHES = [
    # A struck singing bowl: inharmonic partials (1, 2.72, 5.15, 8.9), the lower ones pairs a few cents apart that
    # beat slowly; it rings out over ~15 s, the high partials first.
    ("BOWL", voice("BOWL", 32, [
        op(r=(99, 40, 34, 17), l=(99, 82, 0, 0), ol=96, det=6),
        op(r=(99, 40, 34, 17), l=(99, 82, 0, 0), ol=92, det=9),
        op(r=(99, 46, 38, 21), l=(99, 74, 0, 0), ol=80, fc=2, ff=36, det=5),
        op(r=(99, 46, 38, 21), l=(99, 74, 0, 0), ol=74, fc=2, ff=36, det=10),
        op(r=(99, 54, 44, 26), l=(99, 64, 0, 0), ol=66, fc=5, ff=3, det=7),
        op(r=(99, 62, 50, 31), l=(99, 54, 0, 0), ol=52, fc=8, ff=93, det=8)],
        lpms=0)),
    # A warm drone: soft sines an octave apart, slow in and out.
    ("DRONE", voice("WARM DRONE", 32, [
        op(r=(28, 30, 40, 30), l=(99, 96, 94, 0), ol=94, det=5),
        op(r=(26, 30, 40, 30), l=(99, 96, 94, 0), ol=90, det=9),
        op(r=(30, 30, 40, 30), l=(99, 94, 92, 0), ol=78, fc=0, det=7),
        op(r=(32, 30, 40, 30), l=(99, 92, 90, 0), ol=70, fc=2, det=6),
        op(r=(34, 30, 40, 30), l=(99, 90, 86, 0), ol=56, fc=3, det=8),
        op(ol=0)],
        lfs=28, lfd=40, lpmd=3, lfw=0, lpms=2)),
    # A dark pad: round carriers, gentle modulators, a sub-octave underneath.
    ("DARK PAD", voice("DARK PAD", 5, [
        op(r=(34, 28, 40, 34), l=(99, 95, 92, 0), ol=94, det=5),
        op(r=(30, 28, 40, 34), l=(80, 82, 78, 0), ol=48, bp=48, rd=45, rc=0),
        op(r=(34, 28, 40, 34), l=(99, 95, 92, 0), ol=92, det=10),
        op(r=(30, 28, 40, 34), l=(78, 80, 76, 0), ol=44, fc=2, bp=48, rd=45, rc=0),
        op(r=(36, 28, 40, 34), l=(99, 95, 92, 0), ol=82, fc=0, det=7),
        op(r=(30, 28, 40, 34), l=(70, 76, 72, 0), ol=36)],
        fb=1, lfs=26, lfd=50, lpmd=4, lfw=0, lpms=2)),
    # An 80s crystal bell: a bright tine on top of a glassy body, a little chorus of detune.
    ("CRYSTAL", voice("CRYSTAL", 5, [
        op(r=(99, 38, 25, 40), l=(99, 70, 0, 0), ol=96, kvs=2, det=5),
        op(r=(99, 55, 35, 50), l=(99, 40, 0, 0), ol=74, fc=14, kvs=5, bp=48, rd=60, rc=0),   # quieter up high (no fizz)
        op(r=(99, 35, 25, 40), l=(99, 75, 0, 0), ol=92, det=10),
        op(r=(99, 45, 30, 45), l=(99, 60, 0, 0), ol=72, fc=3, ff=50, kvs=3, bp=48, rd=40, rc=0),
        op(r=(99, 32, 22, 38), l=(99, 80, 0, 0), ol=78, fc=2, det=7),
        op(r=(99, 50, 30, 45), l=(99, 50, 0, 0), ol=58, fc=1)],
        fb=2, lfs=34, lfd=30, lpmd=3, lfw=0, lpms=2)),
    # A cloud pad: soft pairs a few cents apart, gentle second and third harmonics, slow in, slow out: hazy, never
    # sharp.
    ("CLOUD", voice("CLOUD PAD", 5, [
        op(r=(30, 26, 40, 30), l=(99, 95, 92, 0), ol=92, det=4),
        op(r=(26, 26, 40, 30), l=(80, 84, 80, 0), ol=52, fc=2, bp=48, rd=45, rc=0),
        op(r=(30, 26, 40, 30), l=(99, 95, 92, 0), ol=90, det=10),
        op(r=(24, 26, 40, 30), l=(76, 82, 78, 0), ol=46, fc=3, bp=48, rd=45, rc=0),
        op(r=(32, 26, 40, 30), l=(99, 94, 90, 0), ol=80, fc=2, det=7),
        op(r=(24, 26, 40, 30), l=(70, 78, 74, 0), ol=40, fc=1)],
        fb=2, lfs=24, lfd=40, lpmd=3, lfw=0, lpms=2)),
    # A bloom pad: bright and shimmering, an octave and a twelfth glinting over soft pairs, a slow light vibrato (in
    # place of a granular shimmer, whose pitch wanders).
    ("BLOOM", voice("BLOOM PAD", 5, [
        op(r=(36, 28, 40, 32), l=(99, 95, 92, 0), ol=92, det=5),
        op(r=(32, 28, 40, 32), l=(84, 86, 82, 0), ol=56, fc=2, bp=48, rd=45, rc=0),
        op(r=(36, 28, 40, 32), l=(99, 95, 92, 0), ol=82, fc=2, det=9),
        op(r=(30, 28, 40, 32), l=(80, 84, 80, 0), ol=50, fc=4, bp=48, rd=40, rc=0),
        op(r=(40, 28, 40, 32), l=(99, 92, 88, 0), ol=64, fc=3, det=7),
        op(r=(30, 28, 40, 32), l=(78, 82, 78, 0), ol=44, fc=1)],
        fb=1, lfs=30, lfd=40, lpmd=5, lfw=0, lpms=2)),
    # An air pad: round sines with a breath of noise (a modulator in feedback) on top, like wind through a reed.
    ("AIR", voice("AIR PAD", 5, [
        op(r=(34, 28, 40, 34), l=(99, 95, 92, 0), ol=94, det=5),
        op(r=(30, 28, 40, 34), l=(82, 84, 80, 0), ol=40, bp=48, rd=45, rc=0),
        op(r=(34, 28, 40, 34), l=(99, 95, 92, 0), ol=86, fc=2, det=9),
        op(r=(30, 28, 40, 34), l=(80, 82, 78, 0), ol=36, fc=1, bp=48, rd=45, rc=0),
        op(r=(36, 28, 40, 34), l=(99, 94, 90, 0), ol=62, fc=4),
        op(r=(30, 28, 40, 34), l=(84, 86, 82, 0), ol=58, fc=7, ff=30)],
        fb=7, lfs=22, lfd=50, lpmd=4, lfw=0, lpms=2)),
    # A clear glass bell: Felucca's GLASS BELL (tools/gen_fm6_patches.py), its modulators softened above middle C, so
    # high notes stay clear instead of fizzing.
    ("CLEAR BELL", voice("CLEAR BELL", 5, [
        op(r=(99, 40, 25, 35), l=(99, 80, 0, 0), ol=96, kvs=1),
        op(r=(99, 35, 25, 35), l=(99, 70, 0, 0), ol=78, fc=3, ff=50, kvs=3, bp=48, rd=55, rc=0),
        op(r=(99, 45, 30, 40), l=(99, 70, 0, 0), ol=84, fc=2, ff=38, det=9, kvs=1),
        op(r=(99, 50, 30, 40), l=(99, 60, 0, 0), ol=70, fc=5, kvs=3, bp=48, rd=60, rc=0),
        op(r=(99, 60, 40, 45), l=(99, 50, 0, 0), ol=78, fc=5, ff=40, det=5),
        op(r=(99, 60, 40, 45), l=(99, 50, 0, 0), ol=60, bp=48, rd=50, rc=0)],
        fb=0, lpms=0)),
    # Wooden mallets: Felucca's WOOD BARS (tools/gen_fm6_patches.py), the modulators softened above middle C, so high
    # notes stay round instead of fizzing.
    ("SOFT WOOD", voice("SOFT WOOD", 5, [
        op(r=(99, 52, 40, 55), l=(99, 0, 0, 0), ol=99, kvs=2, rs=2),
        op(r=(99, 75, 50, 60), l=(99, 0, 0, 0), ol=70, fc=4, kvs=5, rs=3, bp=48, rd=60, rc=0),
        op(r=(99, 70, 50, 60), l=(99, 0, 0, 0), ol=72, fc=4, kvs=3, rs=3),
        op(r=(99, 80, 50, 60), l=(99, 0, 0, 0), ol=45, kvs=3, rs=3, bp=48, rd=50, rc=0),
        op(ol=0),
        op(ol=0)],
        fb=0, lpms=0)),
    # A kalimba: a plucked metal tine, a quick bright overtone that falls away, a round body that rings on.
    ("KALIMBA", voice("KALIMBA", 5, [
        op(r=(99, 36, 24, 34), l=(99, 50, 0, 0), ol=98, kvs=2, rs=2),
        op(r=(99, 80, 50, 60), l=(99, 0, 0, 0), ol=62, fc=6, ff=20, kvs=4, rs=3, bp=48, rd=70, rc=0),   # softer up high
        op(r=(99, 32, 22, 32), l=(99, 60, 0, 0), ol=82, det=9, rs=2),
        op(r=(99, 70, 45, 55), l=(99, 10, 0, 0), ol=52, fc=3, kvs=3, bp=48, rd=50, rc=0),
        op(ol=0),
        op(ol=0)],
        fb=0, lpms=0)),
    # A handpan: a round fundamental, its octave and the fifth above it (the tuned partials of a hang drum), a soft
    # thump as the hand strikes, a ring of a few seconds.
    ("HANDPAN", voice("HANDPAN", 5, [
        op(r=(99, 38, 26, 36), l=(99, 62, 0, 0), ol=97, kvs=2, rs=1),
        op(r=(99, 82, 50, 60), l=(99, 0, 0, 0), ol=58, fc=1, kvs=4, bp=48, rd=50, rc=0),   # the thump
        op(r=(99, 42, 28, 38), l=(99, 55, 0, 0), ol=80, fc=2, det=8, rs=1),                # the octave
        op(r=(99, 85, 55, 60), l=(99, 0, 0, 0), ol=40, fc=1),
        op(r=(99, 46, 30, 40), l=(99, 50, 0, 0), ol=70, fc=3, det=6, rs=1),                # the fifth above
        op(ol=0)],
        fb=0, lpms=0)),
    # A crystal singing bowl: a pure sine that swells in a little and rings very long, a pair a few cents apart for
    # its slow beating, a faint octave.
    ("CRYSTAL BOWL", voice("CRYS BOWL", 32, [
        op(r=(72, 34, 30, 18), l=(99, 90, 0, 0), ol=96, det=6),
        op(r=(72, 34, 30, 18), l=(99, 90, 0, 0), ol=92, det=9),
        op(r=(70, 40, 34, 22), l=(99, 80, 0, 0), ol=64, fc=2, det=7),
        op(ol=0),
        op(ol=0),
        op(ol=0)],
        fb=0, lpms=0)),
    # Soft lo-fi keys: a mellow electric piano, its tine dulled, a slow wobble.
    ("SOFT KEYS", voice("SOFT KEYS", 5, [
        op(r=(95, 30, 25, 55), l=(99, 70, 0, 0), ol=96, kvs=2, rs=2),
        op(r=(95, 50, 35, 70), l=(99, 50, 0, 0), ol=50, kvs=6, rs=2),
        op(r=(95, 25, 22, 50), l=(99, 85, 0, 0), ol=86, det=8, rs=2),
        op(r=(95, 60, 40, 60), l=(99, 40, 0, 0), ol=46, fc=1, kvs=4),
        op(ol=0),
        op(ol=0)],
        fb=0, lfs=22, lfd=0, lpmd=6, lfw=0, lpms=3)),
]


def main(path):
    L = ["/* generated by tools/gen_haave_patches.py: HAAVE's own FM6 patches, packed (128 bytes) */",
         f"#define HAAVE_NFM6 {len(PATCHES)}u",
         "enum { " + ", ".join("DF_" + n.replace(" ", "_") for n, _ in PATCHES) + " };",
         "static const uint8_t HAAVE_FM6[HAAVE_NFM6][128] = {"]
    for name, v in PATCHES:
        L += [f"    {{/* {name} */", c_rows(pack(v)) + "},"]
    L += ["};", ""]
    Path(path).write_text("\n".join(L))
    print(f"haave patches: {len(PATCHES)} -> {path}")


if __name__ == "__main__":
    main(sys.argv[1])
