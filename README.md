# Drift

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

A simple, dreamy ambient instrument: custom firmware for the M-VAVE FM-1 pocket synth.

Press PLAY and a gentle chord progression starts and keeps going. Add and remove layers with the white keys, jump
to other chords with the black keys, and shape everything live with the four knobs. Every note fits, so it always sounds
good. No menus, no drums.

**Website:** <https://thebrodi.github.io/fm1-drift/> · **[Try it in your browser](https://thebrodi.github.io/fm1-drift/try/)**, no FM-1 needed.

> **Work in progress.** Drift runs in the browser emulator. It has not been flashed to an FM-1 yet.

## Why Drift

I'm not a music expert, and Drift isn't meant to be a full synthesizer. It deliberately limits what the FM-1 can do:
fewer functions and fewer options, so all that's left is playing melodic, dreamy, drifting sounds. I use it for live
meditation, together with my singing bowls: slow, soft and relaxing, with nothing to get lost in.

If you want everything the FM-1 can do, use [Felucca](https://github.com/hugelton/Felucca), which Drift is built on.

## How to play

| Control | Does |
|---|---|
| **PLAY** | Starts / stops the music (fade by hand with the MASTER knob) |
| **KNOB 1 — Space** | Reverb, from a small room to a huge hall |
| **KNOB 2 — Echo** | Delay repeats |
| **KNOB 3 — Glow** | Dark and warm → bright and glassy |
| **KNOB 4 — Motion** | Still and sparse → busy and moving |
| **White keys** | Layers on / off (lit = playing): PAD, BASS, ARP, SPARKLE, MELODY, VOX, SHIMMER |
| **Black keys** | Chords to jump to (vi ii IV V I, then the same with add9): press to go, hold to stay, let go to carry on |
| **PRESETS** | The world: GLASS, EMBER, DUSK, NIGHT, NEON, LOFI, BOWLS, GARDEN, FOREST, OCEAN, RAIN, CAVE, HANDPAN, CRYSTAL |
| **ALGORITHM** | The behaviour: DRIFT, RIPPLE, SWELL, CHIME, PULSE |
| **SELECT** | TIDE, how much it moves by itself: STILL, CALM, FLOWING (layers come and go now and then), WANDERING |
| **OCT− / OCT+** | Everything an octave down / up |
| **FX SCL ENV LFO EDIT GLO** | Punch-in effects while held, on the beat: echoes, stutter, freeze, shimmer, underwater, wash |

The worlds range from glassy bells and warm electric piano to a dark minor pad, 80s strings, lo-fi keys, a singing
bowl over a drone, a kalimba, a soft flute, ocean waves and rain (brown noise), a deep cave, a handpan and crystal
singing bowls. The full design and its rules are in [DRIFT.md](DRIFT.md).

## Try it in your browser

You need Python 3 with Pillow and fonttools, and [Emscripten](https://emscripten.org) (`brew install emscripten`).

```
python3 -c 'import sys; sys.path.insert(0,"tools"); import build; build.generate()'
sh web/emu/build.sh
python3 -m http.server -d build/emu 8790
```

Then open <http://localhost:8790/> in Chrome or Edge and press **Switch on**.

## Building the firmware

Drift builds like Felucca: see [BUILDING.md](BUILDING.md). That needs Docker Desktop on macOS, the JieLi toolchain and
the AC79 SDK. Installing custom firmware on an FM-1 is at your own risk. Back up first, and keep M-VAVE's updater
(official V15) ready to go back.

## Based on Felucca

Drift is a fork of [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (Hügelton Instruments). It replaces
Felucca's keyboard, UI and screen with Drift's (`firmware/src/drift_play.c`, `firmware/src/drift_ui.c`) and keeps
everything else as Felucca made it: the sound engines, effects, USB, update loader and storage. Building with
`FELUCCA_DRIFT=0` gives plain Felucca. Felucca's own README is kept in [docs/FELUCCA.md](docs/FELUCCA.md).

## Credits and licence

Free software: [GPL-3.0-only](LICENSE), as Felucca. The engines in `firmware/engines` are MIT
([Felucca-Engines](https://github.com/hugelton/Felucca-Engines)), the Hügelton Sample Pack is CC0 1.0, and the bundled
fonts and ported DSP keep their own licences ([LICENSES/](LICENSES/), [LICENSING.md](LICENSING.md),
[CREDITS.md](CREDITS.md)).

Drift is developed with the help of AI coding agents.

M-VAVE and FM-1 are trademarks of their respective owners. Drift is not affiliated with or endorsed by them, or by
Hügelton Instruments.

Felucca: Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments.
Drift's changes: Copyright (C) 2026 Dominik Siemon.
