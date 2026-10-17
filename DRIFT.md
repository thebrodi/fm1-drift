# Drift — design

Drift is a firmware for the M-VAVE FM-1, forked from Felucca. It is a small, simple instrument for
dreamy, ambient music that always sounds good: it keeps playing by itself, and you steer it live.

Inspiration: the HiChord (every key is a chord that fits) and the OP-1's generative playing (press
play, it plays on, you shape it with effects).

## Rules

These hold for every feature. If an idea breaks one, the idea changes, not the rule.

1. **No menus.** No button opens a page, a layer or a sub-menu. Every control does one thing, the
   same thing all the time.
2. **No wrong notes.** Everything is locked to a key and scale. Any key, at any time, sounds good
   with what is already playing.
3. **Always sounds good.** Soft envelopes, curated presets, a limiter on the output. No setting
   makes it harsh, silent or clipped.
4. **Few functions.** Things that are not listed below are not in the instrument.

## Controls

The FM-1 has 7 encoders (knobs), 14 buttons and 27 keys (see `firmware/src/panel.c`).

| Control | Does |
|---|---|
| **PRESETS** knob | Picks the sound: about 8–12 hand-tuned FM patches (glass pad, choir, music box, warm drone, bowed bell, …) |
| **ALGORITHM** knob | Picks the style, i.e. how the player behaves: Drift (sparse notes), Ripple (soft arpeggios), Swell (slow chords), Chime (high sparkle), Pulse (gentle rhythm) |
| **SELECT** knob | Steps through patterns: short chord progressions or motifs the player loops or wanders through |
| **Knob 1 — Space** | Reverb size and mix |
| **Knob 2 — Echo** | Delay time and feedback |
| **Knob 3 — Glow** | Brightness: how much the FM tone rings |
| **Knob 4 — Motion** | How busy the player is, from sparse to dense; also chorus and drift on the pad |
| **PLAY** | Starts / stops the generative player |
| **OCT− / OCT+** | Register down / up |
| **White keys** | The 7 chords of the current key (I, ii, iii, IV, V, vi, vii°), voiced as soft open pads; the player moves to that chord |
| **Black keys** | Colour the held chord: sus2, add9, 7, sus4, open (repeating across the keyboard) |
| Other buttons | Unused for now |

Status: the chord keys, PLAY, ALGORITHM (all five styles), the four knobs and OCT work. PRESETS has one sound so
far; SELECT (patterns) is not built yet.

## The player

PLAY starts it. It plays around the last chord the keys gave (I until a key is pressed), so changing chords while
it runs steers it. Its notes are mostly the chord's own tones, sometimes the scale's 2nd or 6th, in the register of
the key that set the chord. Each note is a short bell strike that rings out on its release.

| Style | What it plays |
|---|---|
| DRIFT | Single notes, 1.4–4.6 s apart (×2 … ×0.5 with Motion) |
| RIPPLE | An eighth-note arpeggio up and down the chord at 72 BPM; Motion sets how many notes are left out |
| SWELL | The chord itself, re-voiced every 5–10 s and held after the keys are let go; now and then a high note |
| CHIME | Bursts of 2–4 quick high notes, then a 2–6 s rest |
| PULSE | A soft quarter beat at 72 BPM, the first of each bar always played and a little louder |

## Screen

One page: current sound, style, pattern and chord, and a calm animated visual. Nothing to navigate.

## Sound

- FM voices (Felucca's open 6-operator engine, `fm6_core.c`, or a lighter 2–4 operator voice if the
  CPU needs it), tuned for pads and bells.
- One shared reverb and one shared delay.
- A limiter at the end of the chain.

## Where it lives

- `firmware/src/drift_play.c`: the keys → chords, and the player (runs in the audio interrupt, in place of
  Felucca's keyboard).
- `firmware/src/drift_ui.c`: the knobs, buttons, LEDs and the one screen (in place of Felucca's UI).
- `FELUCCA_DRIFT` (`core.h`, on by default) switches between them; `0` builds plain Felucca.
- Felucca's sound engine (FM6, reverb, delay, chorus), USB, update loader and storage are used unchanged.

## Try it in the browser

```
python3 -c 'import sys; sys.path.insert(0,"tools"); import build; build.generate()'   # once: build/gen
sh web/emu/build.sh                                                                     # needs emcc
python3 -m http.server -d build/emu 8790                                               # http://localhost:8790/
```

To hear it without a browser, `web/emu/drift_render.c` plays a short progression, then each player style, into a
WAV (the build command is at the top of the file; add `-DDRIFT_TRACE` to print every note the player plays).

## How we build it

1. Get Felucca's browser emulator (`web/emu/`) running unchanged.
2. Replace Felucca's instrument and UI with Drift, keeping its hardware layer, USB, update loader,
   storage and installer (as X0X and JIANT did).
3. Build and tune the player and the sounds in the browser emulator.
4. Only then: the JieLi toolchain (Docker), back up the stock firmware (V15), and flash the device.
