# Drift — design

Drift is a firmware for the M-VAVE FM-1, forked from Felucca. It is a small, simple instrument for
dreamy, ambient, meditative music that always sounds good: it keeps playing by itself, and you steer
it live. It is made for live meditation, alongside real singing bowls: slow, soft, never busy.

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
| 6 | VOX | One voice gliding between the chord's tones (a choir "aah", or a breathy whisper) |
| 7 | SHIMMER | A high note above the pad, an octave up |

PAD and MELODY are on at power-on. The FM-1 sounds 8 voices at once across 4 parts, so the layers
share them: PAD and SHIMMER (part 1), ARP, SPARKLE and MELODY (part 2), BASS (part 3), VOX (part 4).

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

In a minor world the same progressions turn minor (HOME: i VI iv v), and the black keys play
VI iv v VII i (no diminished chords, so nothing sounds harsh).

### Worlds (PRESETS)

A world is a whole mood: the key and its mode, the tempo, and the sound of each part. Drift's own FM
patches (`tools/gen_drift_patches.py`: bowl, drone, dark pad, crystal, kalimba, soft keys) are loaded
into a part as its own patch; Felucca's factory sounds stay as they are.

| World | Key | BPM | Pad | Bells (ARP, SPARKLE, MELODY) | VOX |
|---|---|---|---|---|---|
| GLASS | D major | 72 | FM pad | glass bell | choir |
| EMBER | A major | 66 | FM pad | electric piano | choir |
| DUSK | F major | 80 | FM pad | wooden mallets | choir |
| NIGHT | E minor | 60 | dark FM pad | glass bell | whisper |
| NEON | G major | 84 | 80s PWM strings | 80s crystal bell | choir |
| LOFI | E♭ major | 70 | soft analog pad | soft lo-fi keys | whisper |
| BOWLS | C minor | 56 | warm drone | singing bowl (rings ~20 s) | choir |
| GARDEN | A major | 64 | warm drone | kalimba | choir |
| FOREST | E minor | 58 | FM pad | soft flute | whisper |

Every world uses the same sub bass. Levels are measured so the pad, the bells and the voice sit
within a few dB of each other in every world.

## Screen

One page, plain, nothing to navigate:

| Where | Shows |
|---|---|
| Top | The three choices: world (PRESETS), progression (SELECT), behaviour (ALGORITHM) |
| Below | The chord now (`vi`, `V add9  HELD`), or PRESS PLAY |
| Layers | The seven layers, underlined while on |
| Middle | The sounding notes as soft lights over a slow horizon, each part in its own colour |
| Knobs | SPACE, ECHO, GLOW, MOTION: name and bar, each in its own colour |
| Bottom | Key, mode and tempo; the octave when shifted |

At power-on a splash shows a soft planet and DRIFT. Colours: deep navy, with pastel sky, lilac,
rose, mint and peach.

## Sound

- Felucca's engines: FM6 (6-operator FM) for pads and bells, ANALOG for the sub bass and some pads,
  VOICE (formant) for the choir and whisper, WHEEL for the flute.
- One shared reverb and one shared delay.
- No limiter: the levels are set so that everything on at once peaks around −11 dBFS.

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

The website (<https://thebrodi.github.io/fm1-drift/>) is the `gh-pages` branch: `tools/make_drift_site.py
build/site` puts the landing page (`web/site/index.html`), the emulator (`try/`) and the installer (`install/`, once
there is a package) together, and that folder is pushed as the branch.

To hear it without a browser, `web/emu/drift_render.c` presses PLAY, adds the layers one by one, then goes through
the behaviours, a progression and the black-key chords, into a WAV (the build command is at the top of the file; add
`-DDRIFT_TRACE` to print every note struck).

## How we build it

1. Get Felucca's browser emulator (`web/emu/`) running unchanged.
2. Replace Felucca's instrument and UI with Drift, keeping its hardware layer, USB, update loader,
   storage and installer (as X0X and JIANT did).
3. Build and tune the player and the sounds in the browser emulator.
4. Only then: the JieLi toolchain (Docker), back up the stock firmware (V15), and flash the device.
