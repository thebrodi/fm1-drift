# Drift — design

Drift is a firmware for the M-VAVE FM-1, forked from Felucca. It is a small, simple instrument for
dreamy, ambient music that always sounds good: it keeps playing by itself, and you steer it live.

Inspiration: the HiChord (everything you press fits) and the OP-1's generative playing (press play,
it plays on, you shape it live).

**How it plays:** press PLAY and a gentle chord progression starts and keeps going. The white keys
add and remove layers (lit = playing). The four knobs shape it live. The black keys are chords to
jump to. PRESETS, ALGORITHM and SELECT pick the world, the behaviour and the progression. No drums.

## Rules

These hold for every feature. If an idea breaks one, the idea changes, not the rule.

1. **No menus.** No button opens a page, a layer or a sub-menu. Every control does one thing, the
   same thing all the time.
2. **No wrong notes.** Everything is locked to a key and scale. Nothing you press can clash with
   what is already playing.
3. **Always sounds good.** Soft envelopes, curated sounds, balanced levels. No setting makes it
   harsh, silent or clipped.
4. **Few functions.** Things that are not listed below are not in the instrument.
5. **The knobs come first.** KNOB 1–4 are the main way to play it live.

## Controls

The FM-1 has 7 encoders (knobs), 14 buttons and 27 keys (see `firmware/src/panel.c`).

| Control | Does |
|---|---|
| **PLAY** | Starts / stops the music |
| **KNOB 1 — Space** | Reverb, from a small room to a huge hall |
| **KNOB 2 — Echo** | Delay repeats |
| **KNOB 3 — Glow** | Dark and warm → bright and glassy (FM brightness) |
| **KNOB 4 — Motion** | Still and sparse → busy and moving (how often the layers play; chorus and drift) |
| **White keys** | Toggle the 7 layers, lit while on (the pattern repeats across the keyboard) |
| **Black keys** | Chords to jump to: press = go there, hold = stay, let go = the progression carries on |
| **PRESETS** knob | The world: the sounds, the key and the tempo (a few, each complete) |
| **ALGORITHM** knob | The behaviour: how every layer plays (DRIFT, RIPPLE, SWELL, CHIME, PULSE) |
| **SELECT** knob | The progression: which chords the music walks through |
| **OCT− / OCT+** | Everything an octave down / up |
| Other buttons | Unused |

### Layers (white keys, from the left)

| Key | Layer | Sound |
|---|---|---|
| 1 | PAD | The chord, soft and slow (FM pad) |
| 2 | BASS | The chord's root, low and round (sub bass) |
| 3 | ARP | The chord's tones rippling up and down (FM bell) |
| 4 | SPARKLE | High, quiet bell notes |
| 5 | MELODY | Slow single notes floating in the middle |
| 6 | CHOIR | One voice gliding between the chord's tones ("aah") |
| 7 | SHIMMER | A high note above the pad, an octave up |

PAD and MELODY are on at power-on. The FM-1 sounds 8 voices at once across 4 parts, so the layers
share them: PAD and SHIMMER (part 1), ARP, SPARKLE and MELODY (part 2), BASS (part 3), CHOIR (part 4).

### Chords (black keys)

Every black key is a chord of the key you are in, so any of them fits at any time. Press one and the
music moves to that chord. Hold it and the music stays there. Let go and the progression carries on
from that chord (from its place in the progression, when the progression has it).

| Black keys, from the left | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| First five | vi | ii | IV | V | I |
| Next five | vi add9 | ii add9 | IV add9 | V add9 | I add9 |

The 11th black key starts the pattern again. While the music plays, the black key of the chord
sounding now is lit, so you can see where you are.

### Behaviours (ALGORITHM)

| Behaviour | Feel |
|---|---|
| DRIFT | Sparse and floating; chords change every 4 bars |
| RIPPLE | Flowing eighth-note arpeggios; every 2 bars |
| SWELL | Slow and wide, few notes; every 4 bars |
| CHIME | Quick sixteenth figures and bursts of sparkle; every 2 bars |
| PULSE | A soft steady beat, the bass on the quarters; every bar |

### Progressions (SELECT)

DRONE (one chord), HOME (I vi IV V), FLOAT (I IV), LONGING (vi IV I V), RISE (IV V iii vi),
WANDER (moves at random between chords that fit).

### Worlds (PRESETS)

GLASS (D, 72 BPM, glass pad and bells), EMBER (A, 66 BPM, warm pad and electric piano), DUSK
(F, 80 BPM, pad and wooden mallets).

## Screen

One page, nothing to navigate:

| Where | Shows |
|---|---|
| Top | Play mark, world, key and tempo, octave |
| Knob cards | SPACE, ECHO, GLOW, MOTION: each value large in its own colour, with a bar (the knobs come first) |
| Scene | The world's picture with the sounding notes as soft lights: GLASS orbits, EMBER a drifting cloud, DUSK a dune under the moon. Turning PRESETS, ALGORITHM or SELECT shows its new name here for a moment |
| Layers | The seven layers, a dot when on, a bar that moves as each one plays |
| Bottom | The progression with the chord now underlined (or the chord and HELD while a black key holds it), the bars it has left, the behaviour |

At power-on a splash shows a small planet and DRIFT. Colours: deep navy, with pastel sky, lilac,
rose, mint and peach.

## Sound

- FM voices (Felucca's open 6-operator engine, `fm6_core.c`, or a lighter 2–4 operator voice if the
  CPU needs it), tuned for pads and bells.
- One shared reverb and one shared delay.
- A limiter at the end of the chain.

## Where it lives

- `firmware/src/drift_play.c`: the music: the clock, progressions, layers and black-key chords (runs in the audio
  interrupt, in place of Felucca's keyboard).
- `firmware/src/drift_ui.c`: the knobs, buttons, LEDs and the one screen (in place of Felucca's UI).
- `FELUCCA_DRIFT` (`core.h`, on by default) switches between them; `0` builds plain Felucca.
- Felucca's sound engine (FM6, reverb, delay, chorus), USB, update loader and storage are used unchanged.

## Try it in the browser

```
python3 -c 'import sys; sys.path.insert(0,"tools"); import build; build.generate()'   # once: build/gen
sh web/emu/build.sh                                                                     # needs emcc
python3 -m http.server -d build/emu 8790                                               # http://localhost:8790/
```

To hear it without a browser, `web/emu/drift_render.c` presses PLAY, adds the layers one by one, then goes through
the behaviours, a progression and the black-key chords, into a WAV (the build command is at the top of the file; add
`-DDRIFT_TRACE` to print every note struck).

## How we build it

1. Get Felucca's browser emulator (`web/emu/`) running unchanged.
2. Replace Felucca's instrument and UI with Drift, keeping its hardware layer, USB, update loader,
   storage and installer (as X0X and JIANT did).
3. Build and tune the player and the sounds in the browser emulator.
4. Only then: the JieLi toolchain (Docker), back up the stock firmware (V15), and flash the device.
