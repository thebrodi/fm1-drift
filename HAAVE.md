# Haave — design

Haave (Finnish: a daydream, a reverie) is a firmware for the M-VAVE FM-1, forked from Felucca. It is a small, simple instrument for
dreamy, ambient, meditative music that always sounds good: it keeps playing by itself, and you steer
it live. It is made for live meditation, alongside real singing bowls: slow, soft, never busy.

Inspiration: the HiChord (everything you press fits) and the OP-1's generative playing (press play,
it plays on, you shape it live).

**How it plays:** press PLAY and a gentle chord progression starts and keeps going. The white keys
add and remove layers (lit = playing). The four knobs shape it live. The black keys are chords to
jump to. PRESETS, ALGORITHM and SELECT pick the world, how it plays, and how much it moves by itself (TIDE). No
drums.

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
| **PLAY** | Starts / stops the music; stopped, everything rings out and PLAY breathes (fade by hand with the MASTER knob) |
| **KNOB 1 — Space** | Reverb: dry and close → a vast hall; the whole a little quieter as it grows (about 4 dB over the turn), so it moves away rather than getting louder |
| **KNOB 2 — Echo** | Delay: none → long repeating trails (1.5 dB quieter over the turn, the same idea) |
| **KNOB 3 — Glow** | Dark and warm → bright and glassy (FM brightness, the analog pads' filter; the bells half as much; the reverb's damping and the echo's colour around the world's own) |
| **KNOB 4 — Motion** | Still and sparse → busy and moving (how often the layers play: ×0.2 … ×2.2; chorus and drift; the pad's tone sways with its breath, and it breathes deeper) |
| **White keys 1–7** | Toggle the 7 layers, lit while on |
| **White keys 8–9** | GONG and BASS (below), a little brighter than the rest |
| **White keys 10–16** | PLAY keys: the chord's tones going up (below) |
| **Black keys** | Chords to move to: press = go there; the chord lasts as long as TIDE's chords do (STILL: until the next press) |
| **PRESETS** knob | The world: the sounds, the key and the tempo. Playing, a change dips out (1.5 s) and back in (2.5 s): no cut |
| **ALGORITHM** knob | The behaviour: how every layer plays (DRIFT, RIPPLE, SWELL, CHIME, PULSE); its rhythm shown a moment |
| **SELECT** knob | TIDE: how much the music moves by itself (STILL, CALM, FLOWING, WANDERING); shown a moment |
| **OCT− / OCT+** | Everything an octave down / up |
| **FX SCL ENV LFO EDIT GLO** | Punch-in effects on the whole sound while held (below) |
| **HOME SAVE ARP SEQ** | Moments: one press, it plays itself out (below) |
| REC | Unused |

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

### Playing along (white keys 8–16)

The right part of the keyboard plays over the music, with the world's own sounds, and every note fits:

| Key | Plays |
|---|---|
| 8 | GONG: the chord's root low on the world's bell, its octave softly above |
| 9 | BASS: the chord's root on the bass, held while the key is (the BASS layer's note struck again if it plays) |
| 10–16 | The chord's three tones from about C4 up, then again an octave higher, and the lowest once more two octaves up: seven notes going up. They follow the chord: press a black key and the same keys play the new chord |

They light while held, and they play when the music is stopped too.

### Moments (HOME SAVE ARP SEQ)

One press, and the moment starts on the next beat, plays itself out and gives the music back as it was (a layer
toggled meanwhile is kept). The button lights and the name shows on the screen while it plays; a press during
another moment ends that one. TIDE leaves the layers alone meanwhile. Stopped, they do nothing.

| Button | Moment |
|---|---|
| HOME | HOME: back to the path's first chord, landing with the gong and the bass (a bar) |
| SAVE | BREATHE: only the pad, a little darker, then everything returns (2 bars) |
| ARP | CASCADE: twelve bell notes falling down the chord from two octaves up, softer as they go (a bar) |
| SEQ | SWELL: the layers join one a beat and it opens up (GLOW +32 at the top), then they go again (4 bars) |

### Punch-in effects (FX SCL ENV LFO EDIT GLO)

Hold a button and its effect plays on the whole sound; let go and it returns, like a DJ in a live
set (after the Pocket Operator KO-33). While the music plays they snap to the beat: a press starts
the effect on the next beat, a release ends it at the end of the beat it is in, and a quick tap plays
one beat. Stopped, they act at once. The button lights while held and the effect's name shows in the
middle of the screen. They are Felucca's own performance effects (`perform.c`) on the master.

| Button | Effect |
|---|---|
| FX | ECHOES: everything thrown into the delay, the repeats lifted |
| SCL | STUTTER: the last sixteenth repeats |
| ENV | FREEZE: the moment holds |
| LFO | SHIMMER: an octave above, beside itself |
| EDIT | UNDERWATER: a low-pass sweeping down over a bar to a clear muffle (not to the bottom) |
| GLO | WASH: everything thrown into the reverb |

### Chords (black keys)

Every black key is a chord of the key you are in, so any of them fits at any time. Press one and the
music moves to that chord. It lasts as long as TIDE's chords do (CALM 4 bars, FLOWING 2, WANDERING 1),
then the chord path carries on from there (from its place in the path, when the path has it). In
STILL it stays until the next press: there the black keys are your chord changes. Letting go does
nothing.

| Black keys, from the left | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| First five | vi | ii | IV | V | I |
| Next five | vi add9 | ii add9 | IV add9 | V add9 | I add9 |

The 11th black key starts the pattern again. While the music plays, the black key of the chord
sounding now is lit, so you can see where you are.

### Behaviours (ALGORITHM)

A behaviour is the character of the whole piece: its pace, how the pad breathes, how the melody
moves and how often the other layers play. (The pad's attack only changes on the analog and granular pads: an FM6
sound's own envelopes shape it.) Each world starts in its own; ALGORITHM turns from there.

| Behaviour | Pace | Pad | Melody |
|---|---|---|---|
| DRIFT | half time | a slow sway over two bars, softer | single notes far apart, now and then an octave up |
| RIPPLE | normal | a gentle half-bar movement | a flowing line, a step of the scale each eighth |
| SWELL | half time | breathing in and out every bar | very rare notes |
| CHIME | normal | still, quicker and brighter | quick figures of three high notes, then a rest |
| PULSE | normal | a soft pulse on every beat | a four-note motif on the beats, new with each chord; the bass on the quarters |

### TIDE (SELECT)

How much the music moves by itself. The black keys stay your own hands on the chords: TIDE is what
happens when you leave it alone, which matters most in a long session. Turning it changes the pace
from the next chord on (no jump).

| TIDE | Chords | Layers |
|---|---|---|
| STILL | One chord; nothing moves on its own (you lead with the black keys) | Only yours |
| CALM | The world's chord path, a new chord every 4 bars | Only yours |
| FLOWING | The world's chord path, every 2 bars | Now and then (every 8–16 bars) one layer gently joins or leaves |
| WANDERING | Any chord that fits, every bar | Layers come and go more often (every 4–8 bars) |

TIDE only moves the optional layers (ARP, SPARKLE, MELODY, VOX, SHIMMER), keeps one to three of them
on, and never touches the pad or the bass. It starts at CALM and stays as you set it across worlds.

Each world has its own chord path: DRONE (one chord), HOME (I vi IV V), FLOAT (I IV), LONGING
(vi iii IV I) or RISE (I iii IV V vi).

In a minor world the same paths turn minor (HOME: i VI iv v), and the black keys play
VI iv v VII i (no diminished chords, so nothing sounds harsh).

### Worlds (PRESETS)

A world is a whole mood: the key and its mode, the tempo, and the sound of each part. Haave's own FM
patches (`tools/gen_haave_patches.py`: bowl, drone, dark pad, crystal, kalimba, soft keys) are loaded
into a part as its own patch; Felucca's factory sounds stay as they are.

| World | Key | BPM | Pad | Bells (ARP, SPARKLE, MELODY) | VOX | Behaviour, chord path |
|---|---|---|---|---|---|---|
| GLASS | D major | 72 | FM pad | glass bell | airy, high "aah" | DRIFT, HOME |
| EMBER | A major | 66 | analog strings | electric piano | warm "ooh" | RIPPLE, FLOAT |
| DUSK | F major | 80 | FM cloud pad | soft wooden mallets | low, soft hum | PULSE, RISE |
| NIGHT | E minor | 60 | dark FM pad | glass bell | dark, low whisper | SWELL, LONGING |
| NEON | G major | 84 | 80s PWM strings | 80s crystal bell | 80s machine voice | PULSE, HOME |
| LOFI | E♭ major | 70 | soft analog pad | soft lo-fi keys | muffled tape hum | RIPPLE, LONGING |
| BOWLS | C minor | 56 | warm drone | singing bowl (rings ~20 s) | overtone drone | DRIFT, DRONE |
| GARDEN | A major | 64 | FM bloom pad | kalimba | bright, open "ah" | CHIME, FLOAT |
| FOREST | E minor | 58 | FM air pad (breathy) | soft flute | breathy "hoo", like wind | DRIFT, WANDER |
| OCEAN | D major | 54 | brown-noise waves (swelling over two bars) | clear glass bell, low | low "ooh" | DRIFT, FLOAT |
| RAIN | E minor | 62 | soft rain drops (noise) | kalimba | hum | RIPPLE, LONGING |
| CAVE | A minor | 50 | dark FM pad, the whole world 5 semitones lower | clear glass bell | deep choir, "aah" to "ooh" | SWELL, DRONE |
| HANDPAN | D minor | 66 | warm drone | handpan | warm "ooh" | PULSE, FLOAT |
| CRYSTAL | C major | 52 | FM air pad | crystal singing bowl | airy, high "aah" | DRIFT, HOME |

Each world also has its own room: reverb type and damping, delay time and colour. NEON has a square
bass; the others a sub bass. The VOX is the voice engine with the world's own vowel, its glide to a
second vowel, formant shift, buzz, breath and resonance. Levels are measured so the pad, the bells and the voice sit
within a few dB of each other in every world.

## Screen

One page, nothing to navigate, in one style: solid. Crisp text, thin lines and dots, rounded bars and
solid discs on deep navy; nothing glows.

| Where | Shows |
|---|---|
| Top | The three choices: world (PRESETS), TIDE (SELECT), behaviour (ALGORITHM), over a thin line |
| Below | The chord now (`vi`, `V add9`), or PRESS PLAY |
| Layers | The seven layers, underlined while on |
| Middle | The world's quiet picture, the sounding notes in it as solid discs that dim as they fade; a punch-in's name on a pill while it plays. For 2.5 s after ALGORITHM turns: its rhythm (sixteen steps for ARP, MEL and BASS, lit where they play; HALF TIME; the pad's breathing as a curve). After SELECT turns: the TIDE as water (STILL a flat line, CALM long swells, FLOWING rolling waves, WANDERING choppy ones), the chords of its path riding on it (the one now lit, its numeral under it), and over FLOWING and WANDERING a few small lights rising and sinking: the layers that come and go |
| Knobs | SPACE, ECHO, GLOW, MOTION: a rounded bar in each knob's colour, and the value |
| Bottom | Key, mode and tempo; the octave when shifted |

The worlds' pictures: GLASS an orbit, EMBER a low warm line with rising embers, DUSK a slow horizon
wave, NIGHT stars, NEON an 80s grid, LOFI slow rain, BOWLS rings opening with each bowl, GARDEN
fireflies, FOREST bands of mist with lights drifting down, OCEAN three rolling waves with the notes bobbing
on them like buoys, RAIN drops falling onto a still surface (each bell a ripple ring where it lands), CAVE
stalactites with each note trailing smaller, fainter echoes, HANDPAN the instrument from above (shell, dome and
tone fields; each note lights its field, the pad the dome), CRYSTAL a cluster of crystals whose facets flash with
sparkles.

At power-on a splash shows a planet with two moons, HAAVE and the version. Colours: deep navy, with
pastel sky, lilac, rose, mint and peach.

## Sound

- Felucca's engines: FM6 (6-operator FM) for pads and bells, ANALOG for the sub bass and some pads,
  VOICE (formant) for the choir and whisper, WHEEL for the flute.
- One shared reverb and one shared delay.
- No limiter: the levels are set so that everything on at once peaks around −11 dBFS.

## Where it lives

- `firmware/src/haave_play.c`: the music: the clock, progressions, layers and black-key chords (runs in the audio
  interrupt, in place of Felucca's keyboard).
- `firmware/src/haave_ui.c`: the knobs, buttons, LEDs and the one screen (in place of Felucca's UI).
- `FELUCCA_HAAVE` (`core.h`, on by default) switches between them; `0` builds plain Felucca.
- Felucca's sound engine (FM6, reverb, delay, chorus), USB, update loader and storage are used unchanged.

## Try it in the browser

```
python3 -c 'import sys; sys.path.insert(0,"tools"); import build; build.generate()'   # once: build/gen
sh web/emu/build.sh                                                                     # needs emcc
python3 -m http.server -d build/emu 8790                                               # http://localhost:8790/
```

The website (<https://thebrodi.github.io/fm1-haave/>) is the `gh-pages` branch: `tools/make_haave_site.py
build/site` puts the landing page (`web/site/index.html`), the emulator (`try/`) and the installer (`install/`, once
there is a package) together, and that folder is pushed as the branch.

`tests/haave_test.c` plays the firmware natively: every world × behaviour with every layer on (clipping, clicks, DC,
level band), then random playing for MINUTES (default 30): output finite and below clipping, notes in range, never
more than 8 voices, never silent while the pad plays, silent 30–40 s after a stop. The build command is at the top of
the file. Known rough edge: with GLOW at its maximum, OCT+ and a high ECHO, a bright chord of bells can fizz for a few
seconds (two of six random seeds catch it).

`tests/haave_harmony.c` checks that everything harmonises: every world × behaviour × TIDE with every layer
on, the notes heard together sampled continuously; a semitone between two of them (a minor 2nd, a major 7th, a minor
9th) is a clash. It passes with none. What keeps it so: every melody note sits at least a whole tone from every note of
the chord (its root too), the flowing line also never steps a semitone from its own last, still ringing note, and a
minor chord's colour (the upper black keys) is its 7th, not the 9th that rubs against its minor 3rd. The scan also holds
a black-key chord every 3 s (plain and coloured in turn); what remains (0.1 % of the time on average) is a bell still
ringing from the chord before as a black key changes it, like a sustain pedal held across the change. Every part of
every world is in tune within 5 cents (the analog pads' two oscillators 8 cents apart; the granular pads whose pitch
wanders were replaced by Haave's FM CLOUD, AIR and BLOOM pads). Every pad, bell, bass and voice is levelled to within
about 3 dB of its kind in the other worlds, and the bright FM sounds (glass bell, crystal, kalimba, soft wood, the
pads) soften their overtones above middle C, so nothing fizzes when played high and bright.

Felucca's own suite (`tests/run_tests.sh`) passes with `FELUCCA_HAAVE=0`; with Haave its tests of Felucca's keyboard,
MIDI-out-of-keys, SONG grid and splash keys fail, as Haave replaces those by design.

To hear it without a browser, `web/emu/haave_render.c` presses PLAY, adds the layers one by one, then goes through
the behaviours, every TIDE, every world and the black-key chords, into a WAV (the build command is at the top of the file; add
`-DHAAVE_TRACE` to print every note struck).

## How we build it

1. Get Felucca's browser emulator (`web/emu/`) running unchanged.
2. Replace Felucca's instrument and UI with Haave, keeping its hardware layer, USB, update loader,
   storage and installer (as X0X and JIANT did).
3. Build and tune the player and the sounds in the browser emulator.
4. Only then: the JieLi toolchain (Docker), back up the stock firmware (V15), and flash the device.
