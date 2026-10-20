/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dominik Siemon */
/* HAAVE: the panel and the screen (the main loop). With FELUCCA_HAAVE, felucca.c (and the browser build) call these in
 * place of Felucca's ui_input / ui_leds / ui_draw; Felucca's UI is still built (main.c's crash, update and UBOOT
 * screens use its drawing) but never runs. One page, no menus: every control does one thing (HAAVE.md).
 *   PLAY       the music on / off
 *   FX SCL ENV LFO EDIT GLO   punch-in effects while held (haave_play.c)
 *   KNOB 1..4  SPACE (reverb), ECHO (delay), GLOW (brightness), MOTION (how busy; chorus and drift)
 *   PRESETS    the world: the sounds, the key and its mode, the tempo
 *   ALGORITHM  the behaviour of every layer
 *   SELECT     the progression
 *   OCT- OCT+  everything an octave down / up
 * The keys (layers, chords) and the music run in the audio ISR (haave_play.c). */

/* ------------------------------------------------------------ worlds --- */
/* A world (PRESETS) is a whole mood: the key and its mode, the tempo, and the sound of each of the four parts. A part
 * is an engine and one of its presets, then (FM6) one of HAAVE's own patches put in over it (tools/gen_haave_patches.py:
 * loaded as the part's own patch, Felucca's factory patches untouched), at a level that sits it in the mix. */
#include "haave_fm6.h"                   /* HAAVE_FM6[]: Haave's own FM patches (tools/gen_haave_patches.py) */
#define ENGI_ANALOG 0u                   /* engines.c ENGINES[] */
#define ENGI_FORMANT 5u
#define ENGI_WHEEL 7u
#define ENGI_NOISE 11u
enum { FM6_TINE = 0, FM6_BELL = 1, FM6_PAD = 4, FM6_MARIMBA = 5 };   /* eng_fm6.c FM6_PRESETS */
enum { AN_SAW_LEAD = 0, AN_SOFT_PAD = 1, AN_SQR_BASS = 2, AN_PWM_STR = 3, AN_SINE_KEY = 5, AN_SUB_BASS = 7,
       AN_STRINGS = 11 };                                                    /* eng_analog.c's */
enum { FO_CHOIR = 0, FO_LEAD = 1, FO_WHISPER = 3 };                                        /* eng_formant.c's */
enum { WH_SOFT_FLUTE = 3 };                                                   /* eng_wheel.c's */
enum { NZ_WIND = 0, NZ_RAIN = 1 };                                            /* eng_noise.c's */
enum { RV_ROOM, RV_SPRING, RV_HALL };                                         /* G_RTYPE */
enum { DT_4 = 0, DT_8 = 1, DT_8T = 4, DT_2 = 6, DT_1 = 7, DT_8D = 10, DT_4D = 12 };   /* G_DTIME (params.c N_DTIME) */
#define NO_PATCH 0xFFu
typedef struct { uint8_t engine, preset, patch, level; } haave_sound_t;
/* the world's VOX: a preset of the voice engine, then its eight values (VOWL VOWL2 TALK SHIFT BUZZ BRTH Q RAND: the vowel
 * and the one it moves to over TALK, the formants' shift, buzz, breath, resonance, a random vowel per note), its
 * register and level */
typedef struct { uint8_t preset; int8_t e[8]; int8_t reg; uint8_t level; } haave_vox_t;
typedef struct {
    const char *name;
    uint8_t root, minor, bpm;            /* the key (0 = C) and its mode, the tempo */
    haave_sound_t part[3];               /* pad, bells, bass */
    haave_vox_t vox;                     /* the voice */
    uint8_t rtype, rdamp, dtime, dcolor; /* the room: reverb type and damping; the echo: its time and colour */
    uint8_t style, prog;                 /* the behaviour and progression it starts with (ALGORITHM, SELECT) */
    int8_t reg;                          /* semitones the whole world sits from the middle (CAVE: lower) */
} haave_world_t;
#define SUB {ENGI_ANALOG, AN_SUB_BASS, NO_PATCH, 88}
static const haave_world_t HAAVE_WORLDS[] = {
    {"GLASS", 2, 0, 72, {{ENGI_FM6, FM6_PAD, NO_PATCH, 102}, {ENGI_FM6, FM6_BELL, DF_CLEAR_BELL, 112}, SUB},
     {FO_CHOIR, {0, 20, 70, 3, 30, 40, 72, 0}, 12, 82},             /* airy, high: "aah" opening to "eh" */
     RV_HALL, 60, DT_4, 64, DS_DRIFT, 1},
    {"EMBER", 9, 0, 66, {{ENGI_ANALOG, AN_STRINGS, NO_PATCH, 94}, {ENGI_FM6, FM6_TINE, NO_PATCH, 116}, SUB},
     {FO_CHOIR, {100, 95, 0, -3, 46, 16, 60, 0}, 0, 91},            /* warm "ooh" */
     RV_SPRING, 80, DT_8T, 40, DS_RIPPLE, 2},
    {"DUSK", 5, 0, 80, {{ENGI_FM6, FM6_PAD, DF_CLOUD, 110}, {ENGI_FM6, FM6_MARIMBA, DF_SOFT_WOOD, 127}, SUB},
     {FO_CHOIR, {127, 127, 0, -5, 18, 28, 38, 0}, -12, 93},         /* a low, soft hum */
     RV_ROOM, 70, DT_8D, 50, DS_PULSE, 4},
    {"NIGHT", 4, 1, 60, {{ENGI_FM6, FM6_PAD, DF_DARK_PAD, 102}, {ENGI_FM6, FM6_BELL, DF_CLEAR_BELL, 112}, SUB},
     {FO_WHISPER, {95, 127, 100, -6, 40, 112, 44, 0}, -12, 104},    /* a dark, low whisper */
     RV_HALL, 88, DT_2, 30, DS_SWELL, 3},
    {"NEON", 7, 0, 84, {{ENGI_ANALOG, AN_PWM_STR, NO_PATCH, 87}, {ENGI_FM6, FM6_BELL, DF_CRYSTAL, 102},
                        {ENGI_ANALOG, AN_SQR_BASS, NO_PATCH, 96}},
     {FO_LEAD, {32, 64, 60, 2, 92, 6, 84, 0}, 0, 92},               /* an 80s machine voice */
     RV_HALL, 50, DT_8D, 72, DS_PULSE, 1},
    {"LOFI", 3, 0, 70, {{ENGI_ANALOG, AN_SOFT_PAD, NO_PATCH, 91}, {ENGI_FM6, FM6_TINE, DF_SOFT_KEYS, 113}, SUB},
     {FO_CHOIR, {110, 110, 0, -4, 28, 52, 34, 0}, -12, 93},         /* a muffled hum, like old tape */
     RV_ROOM, 90, DT_8, 24, DS_RIPPLE, 3},
    {"BOWLS", 0, 1, 56, {{ENGI_FM6, FM6_PAD, DF_DRONE, 100}, {ENGI_FM6, FM6_BELL, DF_BOWL, 98}, SUB},
     {FO_CHOIR, {127, 64, 127, -2, 112, 8, 122, 0}, -12, 82},       /* an overtone drone, slowly sweeping */
     RV_HALL, 66, DT_1, 40, DS_DRIFT, 0},
    {"GARDEN", 9, 0, 64, {{ENGI_FM6, FM6_PAD, DF_BLOOM, 118}, {ENGI_FM6, FM6_MARIMBA, DF_KALIMBA, 119}, SUB},
     {FO_CHOIR, {10, 40, 50, 2, 56, 14, 70, 40}, 0, 96},            /* a bright, open "ah", its vowel wandering */
     RV_SPRING, 60, DT_8T, 60, DS_CHIME, 2},
    {"FOREST", 4, 1, 58, {{ENGI_FM6, FM6_PAD, DF_AIR, 104}, {ENGI_WHEEL, WH_SOFT_FLUTE, NO_PATCH, 122}, SUB},
     {FO_WHISPER, {120, 100, 90, 0, 24, 92, 54, 0}, 0, 99},        /* a breathy "hoo", like wind */
     RV_HALL, 76, DT_4D, 44, DS_DRIFT, 5},
    {"OCEAN", 2, 0, 54, {{ENGI_NOISE, NZ_WIND, NO_PATCH, 123}, {ENGI_FM6, FM6_BELL, DF_CLEAR_BELL, 112}, SUB},
     {FO_CHOIR, {100, 95, 0, -3, 40, 20, 56, 0}, -12, 86},          /* a low "ooh" under the waves */
     RV_HALL, 80, DT_2, 36, DS_DRIFT, 2},
    {"RAIN", 4, 1, 62, {{ENGI_NOISE, NZ_RAIN, NO_PATCH, 111}, {ENGI_FM6, FM6_MARIMBA, DF_KALIMBA, 118}, SUB},
     {FO_CHOIR, {127, 127, 0, -5, 18, 30, 38, 0}, -12, 93},         /* a hum through the rain */
     RV_ROOM, 82, DT_8D, 40, DS_RIPPLE, 3},
    {"CAVE", 9, 1, 50, {{ENGI_FM6, FM6_PAD, DF_DARK_PAD, 107}, {ENGI_FM6, FM6_BELL, DF_CLEAR_BELL, 112}, SUB},
     {FO_CHOIR, {0, 100, 120, -6, 50, 26, 66, 0}, -12, 86},         /* a deep choir, "aah" closing to "ooh" */
     RV_HALL, 96, DT_1, 30, DS_SWELL, 0, -5},
    {"HANDPAN", 2, 1, 66, {{ENGI_FM6, FM6_PAD, DF_DRONE, 102}, {ENGI_FM6, FM6_MARIMBA, DF_HANDPAN, 112}, SUB},
     {FO_CHOIR, {100, 100, 0, -2, 44, 18, 58, 0}, 0, 86},           /* a warm "ooh" */
     RV_HALL, 70, DT_8T, 50, DS_PULSE, 2},
    {"CRYSTAL", 0, 0, 52, {{ENGI_FM6, FM6_PAD, DF_AIR, 107}, {ENGI_FM6, FM6_BELL, DF_CRYSTAL_BOWL, 96}, SUB},
     {FO_CHOIR, {0, 20, 70, 4, 26, 44, 74, 0}, 12, 94},             /* an airy, high "aah" */
     RV_HALL, 50, DT_2, 56, DS_DRIFT, 1},
};
#define HAAVE_NWORLDS (sizeof HAAVE_WORLDS / sizeof HAAVE_WORLDS[0])

enum { DK_SPACE, DK_ECHO, DK_GLOW, DK_MOTION, DK_N };
static const char *const HAAVE_KNOB[DK_N] = {"SPACE", "ECHO", "GLOW", "MOTION"};
static int16_t haave_knob[DK_N] = {90, 60, 56, 50};   /* 0..127 */
static uint8_t haave_world, haave_ready;
static uint8_t haave_world_next;
static uint8_t haave_show;               /* a picture over the scene for a moment: 1 the behaviour's rhythm, 2 the chord path */
static uint32_t haave_show_ms;         /* the world PRESETS has turned to (shown at once; loaded at the fade's silence) */
static uint8_t haave_redraw = 1;         /* the whole screen */
static uint8_t haave_head = 1;           /* the top and bottom lines */
static uint8_t haave_bars = 0xF;         /* the knobs' bars (bit per knob) */
static int16_t haave_cut[NPART];         /* an ANALOG part's own cutoff (GLOW moves it from there) */

/* GLOW on one part: FM6 the modulators' level (MLVL), ANALOG the filter's cutoff around the preset's own. The bells take
 * half its range: their bright overtones, all the way up on high notes, fold back as fizz */
static void haave_glow(track_t *t, int32_t gl)
{
    int32_t c;
    if (t->eng_req == ENGI_FM6)
        t->p[P_E0 + 2] = (int16_t)(t == &trk[HAAVE_BELL] ? (gl - 64) / 2 : gl > 112 ? 48 : gl - 64);   /* (the pad's top: no
                                                                                                    * fizz) */
    else if (t->eng_req == ENGI_ANALOG || t->eng_req == ENGI_NOISE) {   /* (the noise's FREQ is its E2) */
        c = haave_cut[t - trk] + (t->eng_req == ENGI_NOISE ? (gl - 56) / 3 : (gl - 56) * 3 / 4);
        if (t->eng_req == ENGI_NOISE && c > 80)          /* (noise: brighter, never to a crackle) */
            c = 80;
        t->p[P_E0 + (t->eng_req == ENGI_NOISE ? 2 : 4)] = (int16_t)(c < 0 ? 0 : c > 127 ? 127 : c);
    }
}

/* how the pad breathes in each behaviour: its LFO on the level (a note value of the tempo, its wave, its depth), its
 * attack and a brightness offset to GLOW */
static const struct { uint8_t sync, wave, amp, atk; int8_t glow; } PAD_BEH[DS_N] = {
    {2, 0, 22, 84, -6},                  /* DRIFT: a slow sway over two bars, softer */
    {4, 0, 14, 68, 0},                   /* RIPPLE: a gentle half-bar movement */
    {3, 0, 84, 92, -10},                 /* SWELL: breathing in and out every bar */
    {0, 0, 0, 48, 14},                   /* CHIME: still, quicker and brighter */
    {5, 1, 64, 36, 4},                   /* PULSE: a soft pulse on every beat */
};

/* the knobs and the behaviour -> the parts and the effect buses */
static void haave_apply(void)
{
    track_t *pad = &trk[HAAVE_PAD], *bell = &trk[HAAVE_BELL], *choir = &trk[HAAVE_CHOIR];
    int32_t sp = haave_knob[DK_SPACE], ec = haave_knob[DK_ECHO], mo = haave_knob[DK_MOTION];
    int32_t gl = haave_knob[DK_GLOW] + haave_fx_glow;    /* (BREATHE's GLOW on top) */
    int32_t pg, sync = PAD_BEH[haave_style % DS_N].sync, wave = PAD_BEH[haave_style % DS_N].wave,
            amp = PAD_BEH[haave_style % DS_N].amp;
    gl = gl < 0 ? 0 : gl > 127 ? 127 : gl;
    pg = gl + PAD_BEH[haave_style % DS_N].glow;
    if (pad->eng_req == ENGI_NOISE) {                    /* noise: waves, swelling and falling over two bars (rain:
                                                          * steadier, gently) */
        sync = 2;
        wave = 0;
        amp = HAAVE_WORLDS[haave_world % HAAVE_NWORLDS].part[0].preset == NZ_RAIN ? 45 : 110;
    }
    /* (each parameter written once, its value worked out first: the audio, cutting in between two writes, would
     * play a block of the first; the pad's breath at its base depth for 3 ms is a tick) */
    pad->p[P_LSYNC] = (int16_t)sync;
    pad->p[P_LWAVE] = (int16_t)wave;
    pad->p[P_LTRIG] = 0;
    pad->p[P_ATK] = PAD_BEH[haave_style % DS_N].atk;
    /* SPACE: dry and close .. a vast hall, the sound itself falling back above the middle (fx.c fx_dry: -10 dB); the
     * sends and the dry level are written by the audio (haave_play.c haave_punch_block: WASH lifts them) */
    haave_rev_base[HAAVE_PAD] = (int16_t)sp;
    haave_rev_base[HAAVE_BELL] = (int16_t)sp;
    haave_rev_base[HAAVE_CHOIR] = (int16_t)sp;
    haave_rev_base[HAAVE_BASS] = (int16_t)(sp / 4);
    song.g[G_RSIZE] = (int16_t)(16 + sp * 111 / 127);
    haave_dry_space = sp <= 64 ? 32768 : 32768 - (sp - 64) * (sp - 64) * 5645 / 1000;
    pad->p[P_DLY] = (int16_t)(ec * 3 / 4);               /* ECHO: none .. long trails; above the middle they circle on
                                                          * (feedback to 0.8) and darken each time */
    bell->p[P_DLY] = (int16_t)ec;
    choir->p[P_DLY] = (int16_t)(ec / 2);
    song.g[G_DFDBK] = (int16_t)(ec <= 64 ? 10 + ec * 70 / 64 : 80 + (ec - 64) * 36 / 63);
    haave_glow(pad, pg < 0 ? 0 : pg > 127 ? 127 : pg);
    haave_glow(bell, gl);
    pad->p[P_CHOR] = (int16_t)mo;
    choir->p[P_CHOR] = (int16_t)(mo / 2);
    /* GLOW below 48: the whole sound through a low-pass closing, muffled at the bottom (perform.c's FILTER macro, the
     * same filter as its LPF; it glides ~45 ms); above, the sounds' own brightness as before */
    perf_k[0] = (int8_t)(gl < 48 ? -(48 - gl) * 72 / 48 : 0);
    /* MOTION above 56: the PHASER swirling (up to 80 % of its depth: haave_play.c haave_punch_block) and the sound
     * drifting left and right */
    haave_motion_depth = mo > 56 ? (mo - 56) * 26214 / 71 : 0;
    haave_pan_to = mo > 56 ? (mo - 56) * 16384 / 71 : 0;
    {   /* MOTION: the pad's tone sways with its breath (CHIME: slowly by itself, ~0.13 Hz) and it breathes deeper */
        int32_t lf = mo / (pad->eng_req == ENGI_FM6 ? 2 : pad->eng_req == ENGI_NOISE ? 8 : 6);
        if (pad->eng_req == ENGI_FM6 || pad->eng_req == ENGI_NOISE) {   /* (its tops no brighter than GLOW's own top:
                                                                         * FM6 no fizz, the noise no crackle) */
            int32_t room = pad->eng_req == ENGI_FM6 ? (1152 - pad->p[P_E0 + 2] * 24) / 16 : 84 - pad->p[P_E0 + 2];
            lf = lf < room ? lf : room < 0 ? 0 : room;
        }
        pad->p[P_LD_FLT] = (int16_t)lf;
        amp += mo / 5;
        pad->p[P_LD_AMP] = (int16_t)(amp > 120 ? 120 : amp);   /* (0..127: above, its trough would turn the sound over) */
        if (!sync)
            pad->p[P_LRATE] = 18;
    }
    {   /* GLOW: the tails too, darker or airier (the world's room as the middle); far away (SPACE) the room darker,
         * long echoes (ECHO) darker each time round */
        const haave_world_t *w = &HAAVE_WORLDS[haave_world % HAAVE_NWORLDS];
        int32_t rd = w->rdamp + (64 - gl) / 2 + (sp > 64 ? (sp - 64) / 4 : 0);
        int32_t dc = w->dcolor + (gl - 64) / 2 - (ec > 64 ? (ec - 64) / 2 : 0);
        song.g[G_RDAMP] = (int16_t)(rd < 0 ? 0 : rd > 127 ? 127 : rd);
        song.g[G_DCOLOR] = (int16_t)(dc < 0 ? 0 : dc > 127 ? 127 : dc);
        int32_t le = ec * ec * 159 / 100 - 5724;          /* ECHO: -1.5 dB over its turn (as loud at its start, 60) */
        int32_t bm = mo > 56 ? (mo - 56) * 330 / 71 : 0;  /* MOTION: +2.5 dB at its top (the PHASER's notches, the
                                                          * drift); SPACE: as loud (the dry falls as the room grows) */
        haave_trim_to = DG_ONE - (int32_t)((int64_t)DG_ONE * le / 161290) + (int32_t)((int64_t)DG_ONE * bm / 1000);
    }
    if (pad->eng_req == ENGI_FM6)
        pad->p[P_E0 + 6] = (int16_t)(mo * 3 / 4);        /* FM6 DTUN: the operators drift apart */
    pad->p[P_SPRD] = (int16_t)(20 + mo * 3 / 4);
    haave_motion = (uint8_t)mo;
}

/* part t to sound d, with its envelope (atk, sus, rel) */
static void haave_part(track_t *t, const haave_sound_t *d, int16_t atk, int16_t sus, int16_t rel)
{
    uint32_t tr = (uint32_t)(t - trk);
    set_engine_of(t, d->engine);
    apply_preset_to(t, d->preset);
    if (d->engine == ENGI_FM6 && d->patch < HAAVE_NFM6) {   /* HAAVE's own patch, as the part's own (SLOT OWN) */
        uint8_t v[FP_SIZE + 1u];
        fm6_unpack(HAAVE_FM6[d->patch], v);
        fm6_set_patch(tr, v);
        fm6_own_ok &= (uint8_t)~(1u << tr);
        fm6_slot[tr] = FM6_OWN;
        t->p[P_E7] = FM6_OWN;
    }
    haave_cut[tr] = t->p[P_E0 + (d->engine == ENGI_NOISE ? 2 : 4)];
    t->p[P_VOICE] = V_POLY;
    t->p[P_MUTE] = 0;
    t->p[P_LEVEL] = d->level;
    t->p[P_ATK] = atk;
    t->p[P_SUS] = sus;
    t->p[P_REL] = rel;
}

/* SYNTH's sounds (SAVE again: the next), on the voice's part while SYNTH is chosen; each world starts on its own */
enum { DY_WARM, DY_KEYS, DY_GLASS, DY_AIR, DY_N };
static const char *const HAAVE_SYNTH_NAME[DY_N] = {"SYNTH WARM", "SYNTH KEYS", "SYNTH GLASS", "SYNTH AIR"};
static const haave_sound_t HAAVE_SYNTH[DY_N] = {
    {ENGI_ANALOG, AN_SAW_LEAD, NO_PATCH, 96},            /* a soft saw lead (its filter down: haave_inst_load) */
    {ENGI_FM6, FM6_TINE, DF_SOFT_KEYS, 105},             /* soft electric keys */
    {ENGI_ANALOG, AN_SINE_KEY, NO_PATCH, 91},            /* a glassy sine */
    {ENGI_WHEEL, WH_SOFT_FLUTE, NO_PATCH, 106},          /* a breathy flute */
};
static const uint8_t HAAVE_SYNTH0[14] = {DY_KEYS, DY_WARM, DY_KEYS, DY_WARM, DY_WARM, DY_WARM, DY_GLASS, DY_AIR,
                                         DY_GLASS, DY_GLASS, DY_KEYS, DY_WARM, DY_GLASS, DY_KEYS};
static const haave_sound_t HAAVE_BASS_PUNCH = {ENGI_ANALOG, AN_SQR_BASS, NO_PATCH, 100};   /* BASS's: plucked */
static volatile uint8_t haave_synth;     /* SYNTH's sound now (DY_*) */
static uint8_t haave_p4 = 0xFF, haave_p3 = 0xFF;   /* what parts 4 and 3 hold: 0 the world's, else SYNTH's 1 + DY_* /
                                                    * BASS's 1 */
static uint32_t haave_pill_ms;           /* a name in the middle of the screen (a sprinkle, an instrument) since */
static const char *haave_pill;

/* the world's voice on part 4 */
static void haave_vox_load(const haave_world_t *d)
{
    const haave_sound_t v = {ENGI_FORMANT, d->vox.preset, NO_PATCH, d->vox.level};
    uint32_t i;
    haave_part(&trk[HAAVE_CHOIR], &v, 84, 120, 100);
    for (i = 0; i < 8u; i++)
        trk[HAAVE_CHOIR].p[P_E0 + i] = d->vox.e[i];
    haave_vox_reg = d->vox.reg;
    trk[HAAVE_CHOIR].p[P_GLIDE] = 40;
    trk[HAAVE_CHOIR].p[P_PAN] = -18;
}

/* the sounds the instrument borrows: part 4 SYNTH's (else the world's voice), part 3 BASS's punchy one (else the
 * world's bass); force: both (a world loading), else what changed, its held notes started again (haave_resync_part) */
static void haave_inst_load(int force)
{
    const haave_world_t *d = &HAAVE_WORLDS[haave_world % HAAVE_NWORLDS];
    uint8_t p4 = haave_inst == DI_SYNTH ? (uint8_t)(1u + haave_synth % DY_N) : 0u, p3 = haave_inst == DI_BASS, m = 0;
    if (force || p4 != haave_p4) {
        if (p4) {
            track_t *t = &trk[HAAVE_CHOIR];
            haave_part(t, &HAAVE_SYNTH[p4 - 1u], 24, 112, 84);   /* (a soft start, a long tail) */
            t->p[P_GLIDE] = 0;
            t->p[P_PAN] = -12;
            if (p4 - 1u == DY_WARM)
                t->p[P_E0 + 4] = 58;                     /* (the saw's filter down: warm, not buzzy) */
        } else
            haave_vox_load(d);
        haave_p4 = p4;
        m |= 1u << HAAVE_CHOIR;
    }
    if (force || p3 != haave_p3) {
        if (p3)
            haave_part(&trk[HAAVE_BASS], &HAAVE_BASS_PUNCH, 0, 70, 40);   /* (a pluck: at once, falling, short) */
        else
            haave_part(&trk[HAAVE_BASS], &d->part[2], 40, 120, 90);
        haave_p3 = p3;
        m |= 1u << HAAVE_BASS;
    }
    if (m && !force) {
        haave_apply();
        haave_resync_part |= m;
    }
}

static void haave_load(uint32_t w)
{
    const haave_world_t *d = &HAAVE_WORLDS[w % HAAVE_NWORLDS];
    haave_world = haave_world_next = (uint8_t)(w % HAAVE_NWORLDS);
    haave_part(&trk[HAAVE_PAD], &d->part[0], 72, 112, 104);     /* slow swell in, long tail out */
    if (trk[HAAVE_PAD].eng_req == ENGI_ANALOG)           /* its two oscillators 8 cents apart: a chorus, still in tune */
        trk[HAAVE_PAD].p[P_E0 + 1] = 8;
    haave_pad_noise = trk[HAAVE_PAD].eng_req == ENGI_NOISE;
    if (haave_pad_noise) {                               /* noise: no pitch (TRK 0); the wind brown and slowly wandering */
        track_t *t = &trk[HAAVE_PAD];
        t->p[P_E0 + 4] = 0;
        if (d->part[0].preset == NZ_WIND) {
            t->p[P_E0 + 1] = 120;                        /* COLR: brown */
            t->p[P_E0 + 2] = haave_cut[HAAVE_PAD] = 70;  /* FREQ */
            t->p[P_E0 + 3] = 10;                         /* RES */
            t->p[P_E0 + 6] = 60;                         /* DRFT */
        }
        if (d->part[0].preset == NZ_RAIN) {             /* soft rain: a steady hiss, no drops (each drop's impulse was a
                                                         * click: a scratch), dark and slowly moving (the drops are a
                                                         * sprinkle) */
            t->p[P_E0 + 0] = 0;                          /* MODE: ANLG */
            t->p[P_E0 + 1] = 100;                        /* COLR: towards brown */
            t->p[P_E0 + 2] = haave_cut[HAAVE_PAD] = 62;  /* FREQ */
            t->p[P_E0 + 3] = 10;                         /* RES */
            t->p[P_E0 + 5] = 0;                          /* DENS: no impulses */
            t->p[P_E0 + 6] = 45;                         /* DRFT */
        }
    }
    haave_reg = d->reg;
    trk[HAAVE_PAD].p[P_DEC] = 90;
    haave_part(&trk[HAAVE_BELL], &d->part[1], 0, 127, 100);     /* struck: its own decay, a long release */
    trk[HAAVE_BELL].p[P_PAN] = 20;
    haave_synth = HAAVE_SYNTH0[haave_world % 14u];       /* (each world's own SYNTH to start with) */
    haave_inst_load(1);                                  /* (the bass and the voice, or the instrument's) */
    haave_spr_world = haave_world;
    haave_root = d->root;
    haave_minor = d->minor;
    haave_bpm = d->bpm;
    song.g[G_BPM] = d->bpm;                              /* (the delay's 1/4 follows it) */
    song.g[G_RTYPE] = d->rtype;
    song.g[G_RDAMP] = d->rdamp;
    song.g[G_DTIME] = d->dtime;
    song.g[G_DCOLOR] = d->dcolor;
    song.g[G_DMIX] = 90;
    haave_style = d->style;                              /* where the world starts; ALGORITHM and SELECT turn from there */
    haave_prog = d->prog;
    haave_apply();
    haave_resync = 1;                                    /* (the ISR starts the held notes on the new sounds) */
    haave_head = 1;
}

/* ------------------------------------------------------------- input --- */
static const uint8_t HAAVE_PUNCH_BTN[DP_NPUNCH] = {B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO};   /* (haave_play.c) */
static int32_t haave_turn(uint32_t role)                 /* a list knob: one step per turn, whatever its speed */
{
    int32_t s = panel_enc(role);
    return s > 0 ? 1 : s < 0 ? -1 : 0;
}

static void haave_input(void)
{
    uint32_t pressed = fm1_input_edges(0), i;
    int32_t s;
    fm1_input_note_edges();                              /* (the keys are haave_play.c's) */
    if (!haave_ready) {                                  /* first pass: after the boot and any autosave restore */
        haave_ready = 1;
        perf_latch_on = 0;                               /* (the punch-ins hold while held, whatever was saved) */
        perf_mask = 0;
        for (i = 0; i < DP_NPUNCH; i++)
            haave_punch_bit[i] = 1u << panel.btn[HAAVE_PUNCH_BTN[i]];
        haave_warp_bit = 1u << panel.btn[B_HOME];
        haave_load(0);
    }
    for (i = 0; i < DK_N; i++)
        if ((s = panel_enc(EN_K1 + i)) != 0) {
            int32_t v = haave_knob[i] + 2 * s;
            haave_knob[i] = (int16_t)(v < 0 ? 0 : v > 127 ? 127 : v);
            haave_apply();
            haave_bars |= (uint8_t)(1u << i);
        }
    {   /* BREATHE's GLOW moved */
        static int8_t g;
        if (g != haave_fx_glow) {
            g = haave_fx_glow;
            haave_apply();
        }
    }
    {   /* the instrument: SAVE SYNTH (again: its next sound), ARP BELLS, SEQ BASS; its name a moment */
        const char *nm = 0;
        uint32_t b = (pressed >> panel.btn[B_SAVE]) & 1u ? 1u : (pressed >> panel.btn[B_ARP]) & 1u ? 2u :
                     (pressed >> panel.btn[B_SEQ]) & 1u ? 3u : 0u;
        if (b == 1u) {
            if (haave_inst == DI_SYNTH)
                haave_synth = (uint8_t)((haave_synth + 1u) % DY_N);
            haave_inst = DI_SYNTH;
            nm = HAAVE_SYNTH_NAME[haave_synth % DY_N];
        } else if (b == 2u) {
            haave_inst = DI_BELLS;
            nm = "BELLS";
        } else if (b == 3u) {
            haave_inst = DI_BASS;
            nm = "BASS";
        }
        if (b) {
            haave_inst_load(0);
            haave_pill = nm;
            haave_pill_ms = fm1_ms;
            haave_head = 1;
        }
    }
    {   /* a sprinkle started: its name a moment */
        static uint8_t seen;
        if (seen != haave_spr_seen) {
            seen = haave_spr_seen;
            if (haave_spr_last < HS_N) {
                haave_pill = HAAVE_SPR_NAME[haave_spr_last];
                haave_pill_ms = fm1_ms;
            }
        }
    }
    if ((s = haave_turn(EN_PRESET)) != 0) {              /* playing: fade out, load at silence, fade in (haave_play.c) */
        haave_world_next = (uint8_t)(((int32_t)haave_world_next + s + (int32_t)HAAVE_NWORLDS) % (int32_t)HAAVE_NWORLDS);
        if (haave_on)
            haave_xf_req = 1;
        else
            haave_load(haave_world_next);
        haave_head = 1;
    }
    if (haave_xf_req && !haave_on && !haave_xf_ready) {  /* stopped during the dip: the new world at once */
        haave_load(haave_world_next);
        haave_xf_req = 0;
    }
    if (haave_xf_ready) {                                /* silent now: the new world, then the fade back in */
        haave_load(haave_world_next);
        haave_xf_in = 1;
        haave_xf_ready = 0;
        haave_xf_req = 0;
    }
    if ((s = haave_turn(EN_ALGO)) != 0) {
        haave_style = (uint8_t)(((int32_t)haave_style + s + DS_N) % DS_N);
        haave_apply();
        haave_show = 1;
        haave_show_ms = fm1_ms;
        haave_head = 1;
    }
    if ((s = haave_turn(EN_SELECT)) != 0) {
        int32_t v = (int32_t)haave_tide + s;             /* TIDE: from STILL to WANDERING (not round) */
        haave_tide = (uint8_t)(v < 0 ? 0 : v >= DT_N ? DT_N - 1 : v);
        haave_show = 2;
        haave_show_ms = fm1_ms;
        haave_head = 1;
    }
    if ((pressed >> panel.btn[B_PLAY]) & 1u) {
        haave_run = !haave_run;
        haave_head = 1;
        haave_seen++;
    }
    if ((pressed >> panel.btn[B_OCTDN]) & 1u && haave_oct > -1) {
        haave_oct--;
        haave_head = 1;
    }
    if ((pressed >> panel.btn[B_OCTUP]) & 1u && haave_oct < 1) {
        haave_oct++;
        haave_head = 1;
    }
}

/* -------------------------------------------------------------- LEDs --- */
/* white keys: the seven layers lit while they play, GONG BASS and the PLAY keys while held (GONG and BASS a little
 * brighter than the glow); black keys: lit while held, and the one whose chord sounds now; every key is dimly lit; PLAY
 * green while the music plays, breathing while it is stopped; the moment and punch-in buttons lit while theirs plays;
 * OCT- / OCT+ lit while shifted */
static void haave_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0}, nm[FM1_NCOL] = {0}, nb[FM1_NCOL] = {0};
    static uint8_t ready;
    uint32_t k, c;
    if (fm1_led_anim_on())
        return;
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    for (k = 0; k < 27u; k++) {
        uint32_t p = key_place(k), on, rest = 0;
        if (!key_black(k)) {                             /* a layer: lit while on (breathing while its part plays the
                                                          * instrument); the instrument's keys: as held */
            on = p < DL_N ? (haave_layers >> p) & 1u : (fm1_in.notes >> k) & 1u;
            rest = p < DL_N && on && ((p == DL_CHOIR && haave_inst == DI_SYNTH) || (p == DL_BASS && haave_inst == DI_BASS));
        } else if (p < 5u)                               /* a chord: the one sounding lit */
            on = ((fm1_in.notes >> k) & 1u) || (haave_run && haave_deg == HAAVE_BLACK[haave_minor & 1u][p]);
        else                                             /* a sprinkle: lit while it sounds */
            on = ((fm1_in.notes >> k) & 1u) || ((haave_spr_lit >> (p - 5u)) & 1u);
        led_put(rest ? nb : nl, 14u + k, (int)on);
        led_put(nd, 14u + k, !rest);
    }
    led_put(nl, panel.btn[B_OCTDN], haave_oct < 0);
    led_put(nl, panel.btn[B_OCTUP], haave_oct > 0);
    if (haave_run)
        nl[LED_PLAY_GREEN >> 3] |= (uint8_t)(1u << (LED_PLAY_GREEN & 7u));
    else
        led_put(nb, panel.btn[B_PLAY], 1);               /* stopped: PLAY breathes, press me */
    led_put(nl, panel.btn[B_HOME], haave_warp < 65000 || (fm1_in.buttons & haave_warp_bit));   /* HOME: WARP */
    led_put(nd, panel.btn[B_HOME], 1);
    led_put(nd, panel.btn[B_REC], 1);                    /* (REC: nothing yet) */
    led_put(nl, panel.btn[B_SAVE], haave_inst == DI_SYNTH);   /* the instrument chosen */
    led_put(nl, panel.btn[B_ARP], haave_inst == DI_BELLS);
    led_put(nl, panel.btn[B_SEQ], haave_inst == DI_BASS);
    led_put(nd, panel.btn[B_SAVE], 1);
    led_put(nd, panel.btn[B_ARP], 1);
    led_put(nd, panel.btn[B_SEQ], 1);
    for (k = 0; k < DP_NPUNCH; k++) {                    /* the punch-in buttons: lit while they play, dim otherwise */
        led_put(nl, panel.btn[HAAVE_PUNCH_BTN[k]], ((haave_punch >> k) & 1u) || haave_plvl[k] > 0);
        led_put(nd, panel.btn[HAAVE_PUNCH_BTN[k]], 1);
    }
    fm1_led_dim_level(1);
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led_dim[c] = (uint8_t)(nd[c] & ~nl[c]);
        fm1_led_breath[c] = nb[c];
        fm1_led_mid[c] = (uint8_t)(nm[c] & ~nl[c]);
        fm1_led[c] = nl[c];
    }
}

/* ------------------------------------------------------------ screen --- */
/* One page, plain (HAAVE.md):
 *     8  the three choices: the world (PRESETS), the progression (SELECT), the behaviour (ALGORITHM)
 *    34  the chord now
 *    60  the seven layers, underlined while on
 *    84  the world's quiet picture, the sounding notes in it as solid lights
 *   182  the four knobs: name and bar, each in its colour
 *   222  key and tempo, octave
 * Each line redraws only when it changes; the lights at ~30 frames a second. */
#define D_BG RGB(14, 17, 36)
#define D_TEXT RGB(230, 226, 255)
#define D_MID RGB(154, 154, 208)
#define D_DIM RGB(52, 56, 98)
#define D_SKY RGB(133, 183, 235)
#define D_LILAC RGB(175, 169, 236)
#define D_ROSE RGB(237, 147, 177)
#define D_MINT RGB(159, 225, 203)
#define D_PEACH RGB(245, 196, 179)
#define D_VIOLET RGB(127, 119, 221)
#define VIZ_Y 84u
#define VIZ_H 88u

static const char *const HAAVE_ROMAN[2][7] = {{"I", "II", "III", "IV", "V", "VI", "VII"},
                                              {"i", "ii", "iii", "iv", "v", "vi", "vii"}};
/* chord d's numeral: upper case for a major triad (a major third above its root in the world's scale), else lower */
static const char *haave_roman(int32_t d)
{
    return HAAVE_ROMAN[haave_step(d + 2) - haave_step(d) != 4][d % 7];
}
static const char *const HAAVE_KEYNAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const uint16_t HAAVE_KCOL[DK_N] = {D_SKY, D_LILAC, D_ROSE, D_MINT};
static const uint16_t HAAVE_PCOL[NPART] = {D_SKY, D_PEACH, D_VIOLET, D_LILAC};   /* the parts' lights */

/* colour c at strength a / 32 over the background: solid, its tone dimmer for a smaller a (a >= 11: c itself) */
static uint16_t d_tone(uint16_t c, uint32_t a)
{
    uint32_t pct = a * 300u / 32u;
    return pct >= 100u ? c : ux_mix(D_BG, c, (int32_t)pct);
}

/* a solid dot */
static void d_dot(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    if (a)
        cv_pset(x, y, d_tone(c, a));
}

/* a solid disc of radius r, its edge smoothed over a pixel */
static void d_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c, uint32_t a)
{
    int32_t x, y, r2 = r * r, ri = (r - 1) * (r - 1);
    uint16_t t = d_tone(c, a);
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++) {
            int32_t d = x * x + y * y, px = cx + x, py = cy + y + cv_oy;
            if (d > r2 || (uint32_t)px >= cv_w || (uint32_t)py >= cv_h)
                continue;
            if (d <= ri)
                cv_px[(uint32_t)py * cv_w + (uint32_t)px] = swap16(t);
            else                                         /* the rim: half over what is there */
                cv_px[(uint32_t)py * cv_w + (uint32_t)px] = swap16(ux_mix(swap16(cv_px[(uint32_t)py * cv_w + (uint32_t)px]),
                                                                          t, 50));
        }
}

/* ---- the lights: the sounding voices as solid lights, each world with its own quiet picture behind them and its own
 * way of moving them (one sentence each in HAAVE.md). Lights across by pitch, their size and brightness their level.
 * Only thin dots and lines and solid discs: nothing busy ---- */
static uint32_t viz_ph;                  /* one step a drawn frame (~30 a second) */
static int32_t v_sin(uint32_t a, int32_t amp) { return SINE[a & 1023u] * amp / 32768; }

/* a solid line across, from x0 to x1 at row y */
static void d_hline(int32_t x0, int32_t x1, int32_t y, uint16_t c, uint32_t a)
{
    if (x1 > x0)
        cv_rect(x0, y, x1 - x0, 1, d_tone(c, a));
}

/* a solid line between two points */
static void d_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c, uint32_t a)
{
    cv_line(x0, y0, x1, y1, d_tone(c, a));
}

/* a dotted ellipse */
static void viz_ring(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint16_t c, uint32_t a)
{
    uint32_t k, n = rx < 8 ? 16u : 48u;
    for (k = 0; k < n; k++)
        d_dot(cx + v_sin(k * 1024u / n + 256u, rx), cy + v_sin(k * 1024u / n, ry), c, a);
}

/* OCEAN: the height of wave k (0 the front) at x */
static int32_t viz_wave(int32_t x, uint32_t k)
{
    return (int32_t)VIZ_H / 2 + 6 + (int32_t)k * 12 + v_sin((uint32_t)x * (5u + k * 2u) + viz_ph * (3u - k), 5 - (int32_t)k);
}

/* CRYSTAL: the facets of a cluster of three crystals (lines x0 y0 x1 y1), and the points where sparkles sit */
static const int16_t FACET[][4] = {
    {96, 86, 110, 22}, {110, 22, 124, 86}, {110, 22, 112, 86},                      /* the tall one */
    {70, 86, 82, 44}, {82, 44, 96, 86}, {82, 44, 86, 86},                           /* the left */
    {124, 86, 140, 38}, {140, 38, 154, 86}, {140, 38, 142, 86},                     /* the right */
    {150, 86, 162, 60}, {162, 60, 172, 86},                                         /* a small one */
};
static const int16_t SPARK[8][2] = {{110, 22}, {82, 44}, {140, 38}, {162, 60}, {104, 50}, {118, 60}, {88, 64}, {146, 66}};

/* HANDPAN: where tone field k (0 the centre dome, 1..8 the ring) sits */
static void viz_field(uint32_t k, int32_t *x, int32_t *y)
{
    if (!k) {
        *x = 120;
        *y = (int32_t)VIZ_H / 2;
        return;
    }
    *x = 120 + v_sin((k - 1u) * 128u + 256u, 64);
    *y = (int32_t)VIZ_H / 2 + v_sin((k - 1u) * 128u, 28);
}

/* the picture behind the lights */
static void viz_back(void)
{
    int32_t x, y, w = 240, h = (int32_t)VIZ_H, m = h / 2;
    uint32_t i, r = 0x9E3779B9u;
    switch (haave_world) {
    case 0:                                              /* GLASS: a thin orbit */
        viz_ring(w / 2, m, 100, 34, D_LILAC, 14);
        break;
    case 1:                                              /* EMBER: a warm line low down */
        for (x = 0; x < w; x += 4)                       /* a low warm line, slowly breathing */
            d_dot(x, h - 6 + v_sin((uint32_t)x * 5u + viz_ph * 2u, 3), D_PEACH, 4);
        break;
    case 3:                                              /* NIGHT: still stars, a few twinkling */
        for (i = 0; i < 34u; i++) {
            r = r * 1664525u + 1013904223u;
            d_dot((int32_t)(r >> 8) % w, (int32_t)(r >> 20) % h, D_TEXT, 3u + ((r >> 3) + viz_ph / 6u) % 5u);
        }
        break;
    case 4:                                              /* NEON: a horizon and a grid running toward it */
        d_hline(0, w, m + 6, D_ROSE, 14);
        for (x = -6; x <= 6; x++)
            d_line(w / 2 + x * 8, m + 8, w / 2 + x * 44, h, D_LILAC, 6);
        for (i = 0; i < 4u; i++) {
            y = m + 6 + (int32_t)(((viz_ph / 2u + i * 16u) % 64u) * ((viz_ph / 2u + i * 16u) % 64u) / 120u);
            if (y < h)
                d_hline(0, w, y, D_LILAC, 4);
        }
        break;
    case 5:                                              /* LOFI: slow soft rain */
        for (i = 0; i < 22u; i++) {
            r = r * 1664525u + 1013904223u;
            x = (int32_t)(r >> 8) % w;
            y = (int32_t)(((r >> 20) + viz_ph * (2u + (r >> 30))) % (uint32_t)(h + 10)) - 10;
            d_line(x, y, x - 1, y + 7, D_LILAC, 5);
        }
        break;
    case 6:                                              /* BOWLS: still rings around the centre */
        for (i = 1; i <= 3u; i++)
            viz_ring(w / 2, m, (int32_t)i * 26, (int32_t)i * 13, D_LILAC, 10);
        break;
    case 8:                                              /* FOREST: slow mist in bands */
        for (i = 0; i < 3u; i++)
            for (x = 0; x < w; x += 2)
                if (!(x & 3))
                    d_dot(x, m - 18 + (int32_t)i * 18 + v_sin((uint32_t)x * 4u + viz_ph + i * 300u, 4), D_MINT, 3);
        break;
    case 9:                                              /* OCEAN: three rolling waves, the far ones fainter */
        for (i = 0; i < 3u; i++)
            for (x = 0; x < w; x++)
                d_dot(x, viz_wave(x, i), D_SKY, 14u - i * 4u);
        break;
    case 10:                                             /* RAIN: drops falling straight onto a still surface */
        d_hline(0, w, h - 12, D_LILAC, 6);
        for (i = 0; i < 18u; i++) {
            r = r * 1664525u + 1013904223u;
            x = (int32_t)(r >> 8) % w;
            y = (int32_t)(((r >> 20) + viz_ph * (3u + (r >> 30))) % (uint32_t)(h - 12));
            d_line(x, y, x, y + 4, D_SKY, 9);
        }
        break;
    case 11:                                             /* CAVE: stalactites hanging from the dark */
        d_hline(0, w, 0, D_DIM, 32);
        for (x = 6, i = 0; x < w; x += 14, i++) {
            r = r * 1664525u + 1013904223u;
            y = 8 + (int32_t)((r >> 24) % 22u);
            d_line(x - 4, 0, x, y, D_LILAC, 6);
            d_line(x + 4, 0, x, y, D_LILAC, 6);
            if ((viz_ph / 4u + i * 7u) % 60u == 0u)      /* now and then a drop */
                d_dot(x, y + 2, D_SKY, 12);
        }
        break;
    case 12:                                             /* HANDPAN: from above, the shell, the dome, the tone fields */
        viz_ring(120, m, 92, 40, D_DIM, 32);
        viz_ring(120, m, 12, 7, D_LILAC, 9);
        for (i = 1; i <= 8u; i++) {
            viz_field(i, &x, &y);
            viz_ring(x, y, 9, 5, D_LILAC, 7);
        }
        break;
    case 13:                                             /* CRYSTAL: a cluster of crystals, their facets thin lines */
        for (i = 0; i < sizeof FACET / sizeof FACET[0]; i++)
            d_line(FACET[i][0], FACET[i][1], FACET[i][2], FACET[i][3], D_LILAC, i % 3u == 2u ? 4u : 8u);
        break;
    case 2:                                              /* DUSK: the horizon, a slow wave */
    default:                                             /* GARDEN: nothing but the fireflies */
        if (haave_world == 2u)
            for (x = 0; x < w; x += 3)
                d_dot(x, m + v_sin((uint32_t)x * 6u + viz_ph * 2u, 6), D_LILAC, 7);
        break;
    }
}

/* where voice i of part p (note n) shines, and how big; 0 = not drawn as a light (BOWLS' bells are rings) */
static int viz_place(uint32_t p, uint32_t i, uint32_t n, int32_t lvl, int32_t *x, int32_t *y, int32_t *rad)
{
    int32_t w = 240, h = (int32_t)VIZ_H, m = h / 2, px = ((int32_t)n - 28) * w / 76;
    uint32_t a = n * 97u + viz_ph * (3u + p);
    *rad = 5 + lvl / 3 + (p == HAAVE_PAD ? 4 : 0);
    switch (haave_world) {
    case 0:                                              /* GLASS: around the orbit */
        a = n * 53u + viz_ph * (2u + p);
        *x = w / 2 + v_sin(a + 256u, 100);
        *y = m + v_sin(a, 34);
        break;
    case 1:                                              /* EMBER: rising and fading */
        *x = px + v_sin(viz_ph * 3u + n * 41u, 6);
        *y = h - 6 - (int32_t)((viz_ph / 2u + n * 37u + i * 11u) % (uint32_t)(h + 10));
        break;
    case 3:                                              /* NIGHT: small and high, like stars */
        *x = px;
        *y = 10 + (int32_t)((n * 29u) % (uint32_t)(m + 10));
        *rad = 3 + lvl / 5;
        break;
    case 4:                                              /* NEON: sitting on the horizon */
        *x = px;
        *y = m + 4 - lvl / 4;
        break;
    case 6:                                              /* BOWLS: a bell is a ring opening out; the rest float */
        if (p == HAAVE_BELL) {
            int32_t rx = 6 + (32 - lvl) * 3;
            viz_ring(w / 2 + (px - w / 2) / 4, m, rx, rx / 2, HAAVE_PCOL[p], 10u + (uint32_t)lvl / 2u);
            return 0;
        }
        *x = px;
        *y = m + v_sin(a, 16);
        break;
    case 7:                                              /* GARDEN: fireflies wandering */
        *x = px + v_sin(viz_ph * (5u + i) + n * 71u, 22);
        *y = m + v_sin(viz_ph * (4u + p) + n * 113u + 256u, 30);
        *rad = 3 + lvl / 4;
        break;
    case 8:                                              /* FOREST: drifting slowly down through the mist */
        *x = px + v_sin(viz_ph * 2u + n * 61u, 14);
        *y = (int32_t)((viz_ph / 3u + n * 23u + i * 17u) % (uint32_t)(h + 10)) - 5;
        break;
    case 9:                                              /* OCEAN: bobbing on the front wave like buoys */
        *x = px;
        *y = viz_wave(px, 0) - 4 - lvl / 6;
        break;
    case 10:                                             /* RAIN: a drop landing makes a ring on the surface; the rest float */
        if (p == HAAVE_BELL) {
            int32_t rx = 3 + (32 - lvl) * 2;
            viz_ring(px, h - 12, rx, rx / 4 + 1, HAAVE_PCOL[p], 6u + (uint32_t)lvl / 3u);
            return 0;
        }
        *x = px;
        *y = m - 10 + v_sin(a, 16);
        break;
    case 11:                                             /* CAVE: a light, and its echoes trailing off smaller and fainter */
        *x = px;
        *y = m + 10 + v_sin(a, 12);
        d_disc(*x + 14, *y + 6, 2 + lvl / 10, HAAVE_PCOL[p], (uint32_t)lvl / 4u + 2u);
        d_disc(*x + 26, *y + 11, 1 + lvl / 16, HAAVE_PCOL[p], (uint32_t)lvl / 8u + 1u);
        break;
    case 12: {                                           /* HANDPAN: the note's tone field lights up (the pad: the dome) */
        uint32_t k = p == HAAVE_PAD ? 0u : 1u + (n * 5u) % 8u;
        viz_field(k, x, y);
        *rad = 4 + lvl / 3;
        break;
    }
    case 13: {                                           /* CRYSTAL: a sparkle on a point of the crystals */
        uint32_t k = (n * 3u + p) % 8u;
        int32_t s = 2 + lvl / 4;
        uint16_t c = d_tone(HAAVE_PCOL[p], 6u + (uint32_t)lvl / 2u);
        cv_line(SPARK[k][0] - s, SPARK[k][1], SPARK[k][0] + s, SPARK[k][1], c);
        cv_line(SPARK[k][0], SPARK[k][1] - s, SPARK[k][0], SPARK[k][1] + s, c);
        d_disc(SPARK[k][0], SPARK[k][1], 2, HAAVE_PCOL[p], 6u + (uint32_t)lvl / 2u);
        return 0;
    }
    default:                                             /* DUSK, LOFI: floating on a slow wave */
        *x = px;
        *y = m + v_sin(a, 28);
        break;
    }
    return 1;
}

/* the behaviour just turned to, as a picture: a bar of sixteen steps per moving layer, lit where it plays, and the pad's
 * breathing as a curve */
static void haave_show_rhythm(void)
{
    static const char *const ROW[3] = {"ARP", "MEL", "BASS"};
    static const uint8_t PER[6] = {0, 64, 32, 16, 8, 4};   /* the pad's LFO (PAD_BEH sync): sixteenths a cycle */
    const haave_beh_t *b = &HAAVE_BEH[haave_style % DS_N];
    uint32_t r, k;
    int32_t x0 = 56, dx = 11, y;
    cv_text(8, 2, &AF_M, HAAVE_STYLE[haave_style % DS_N], D_TEXT);
    cv_text_r(232, 6, &AF_S, b->half ? "HALF TIME" : "", D_MID, D_BG);
    for (r = 0; r < 3u; r++) {
        y = 30 + (int32_t)r * 14;
        cv_text(8, y - 6, &AF_S, ROW[r], D_MID);
        for (k = 0; k < 16u; k++) {
            uint32_t on = r == 0u ? k % b->arp_div == 0u
                        : r == 2u ? (b->bass_pulse ? k % 4u == 0u : 1u)
                        : b->mel == MM_LINE ? k % 2u == 0u
                        : b->mel == MM_MOTIF ? k % 4u == 0u
                        : b->mel == MM_BURST ? k == 0u || k == 2u || k == 4u
                        : k == 0u || k == 11u;          /* MM_FLOAT: far apart */
            if (r == 2u && !b->bass_pulse)
                cv_rect(x0 - 2, y, dx * 16, 2, HAAVE_KCOL[2]);   /* (a held bass: one line) */
            else
                d_disc(x0 + (int32_t)k * dx, y + 1, on ? 3 : 1, on ? HAAVE_KCOL[r] : D_DIM, 32);
        }
    }
    y = 72;
    cv_text(8, y - 6, &AF_S, "PAD", D_MID);
    for (k = 0; k < (uint32_t)(dx * 16); k++) {          /* the pad: still, or breathing at its pace */
        uint32_t per = PER[PAD_BEH[haave_style % DS_N].sync % 6u], a = PAD_BEH[haave_style % DS_N].amp;
        int32_t yy = per ? y + SINE[(k * 1024u / (per * (uint32_t)dx)) & 1023u] * (int32_t)(2u + a / 12u) / 32768 : y;
        cv_pset(x0 - 2 + (int32_t)k, yy, D_SKY);
    }
}

/* TIDE just turned to, as a picture: water. STILL a flat line, CALM long swells, FLOWING rolling waves, WANDERING
 * choppy ones; the chords of its path sit on the wave as discs (the one now lit, its numeral under it); over FLOWING and
 * WANDERING a few small lights rise and sink: the layers that come and go */
static void haave_show_path(void)
{
    static const uint8_t AMP[DT_N] = {0, 5, 10, 14};    /* the wave: its height .. */
    static const uint16_t LEN[DT_N] = {1, 160, 90, 46};   /* .. and its length (px) */
    static const uint8_t FLOAT[DT_N] = {0, 0, 2, 4};    /* the lights over it */
    const int8_t *pr = PROG[haave_path()];
    uint32_t t = haave_tide % DT_N, i, n = 0, ph = viz_ph * (2u + t * 2u);
    int32_t m = (int32_t)VIZ_H / 2 + 6, x;
    cv_text(8, 2, &AF_M, HAAVE_TIDE[t], D_TEXT);
    for (x = 0; x < 240; x++) {                          /* the water, two pixels thick */
        int32_t y = m + v_sin((uint32_t)x * 1024u / LEN[t] + ph, AMP[t]);
        cv_pset(x, y, D_SKY);
        cv_pset(x, y + 1, d_tone(D_SKY, 6));
    }
    if (t == DT_STILL || t == DT_WANDER)                 /* the chords: one (STILL), or any (WANDERING: five) */
        n = t == DT_STILL ? 1u : 5u;
    else
        while (n < 6u && pr[n] >= 0)
            n++;
    for (i = 0; i < n; i++) {
        int32_t cx = n == 1u ? 120 : 30 + (int32_t)(i * 180u / (n - 1u)), cy = m + v_sin((uint32_t)cx * 1024u / LEN[t] + ph,
                                                                                         AMP[t]) - 1;
        int deg = t == DT_STILL ? (haave_deg < 0 ? pr[0] : haave_deg) : t == DT_WANDER ? -1 : pr[i];
        int now = deg >= 0 && deg == haave_deg;
        d_disc(cx, cy, now ? 7 : 5, now ? D_PEACH : D_LILAC, now ? 32u : 18u);
        if (deg >= 0)
            cv_text(cx - text_w(&AF_S, haave_roman(deg)) / 2, cy + 10, &AF_S, haave_roman(deg), now ? D_TEXT : D_MID);
    }
    for (i = 0; i < FLOAT[t]; i++) {                     /* the layers that come and go: lights rising and sinking */
        int32_t lx = 40 + (int32_t)i * 52, ly = m - 18 + v_sin(viz_ph * (5u + i * 2u) + i * 300u, 10);
        d_disc(lx, ly, 3, HAAVE_KCOL[i % DK_N], 26);
    }
}

static void haave_viz(void)
{
    uint32_t p, i;
    viz_ph++;
    cv_begin(240, VIZ_H, D_BG);
    if (haave_show && fm1_ms - haave_show_ms < 2600u) {  /* ALGORITHM or SELECT just turned: their picture a moment */
        if (haave_show == 1u)
            haave_show_rhythm();
        else
            haave_show_path();
        cv_blit(0, VIZ_Y);
        return;
    }
    haave_show = 0;
    viz_back();
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++) {
            const voice_t *v = &trk[p].v[i];
            int32_t lvl, x, y, rad;
            if (!v->active || (lvl = v->env_out >> 10) < 1)       /* (Q15 -> 0..32) */
                continue;
            if (viz_place(p, i, v->note, lvl, &x, &y, &rad))
                d_disc(x, y, 2 + rad / 2, HAAVE_PCOL[p], (uint32_t)(4 + lvl / 4));   /* (dimmer as it fades) */
        }
    {   /* a punch-in playing, else WARP, else a sprinkle or an instrument just chosen: its name in the middle */
        const char *nm = haave_warp < 65000 ? "WARP" : haave_pill && fm1_ms - haave_pill_ms < 1600u ? haave_pill : 0;
        for (p = 0; p < DP_NPUNCH; p++)
            if ((haave_punch >> p) & 1u) {
                nm = HAAVE_PUNCH_NAME[p];
                break;
            }
        if (nm) {
            int32_t tw = text_w(&AF_M, nm);
            cv_rrect(120 - tw / 2 - 10, (int32_t)VIZ_H / 2 - 12, tw + 20, 24, -6, D_DIM, D_BG);
            cv_text_on(120 - tw / 2, (int32_t)VIZ_H / 2 - 9, &AF_M, nm, D_TEXT, D_DIM);
        }
    }
    cv_blit(0, VIZ_Y);
}

static void haave_draw_head(void)
{
    char b[16];
    uint32_t n = 0;
    const char *s;
    const haave_world_t *w = &HAAVE_WORLDS[haave_world_next];
    cv_begin(240, 24, D_BG);                             /* the three choices */
    cv_text(8, 4, &AF_S, w->name, D_TEXT);
    cv_text(120 - text_w(&AF_S, HAAVE_TIDE[haave_tide % DT_N]) / 2, 4, &AF_S, HAAVE_TIDE[haave_tide % DT_N], D_TEXT);
    cv_text_r(232, 4, &AF_S, HAAVE_STYLE[haave_style % DS_N], D_TEXT, D_BG);
    cv_rect(8, 22, 224, 1, D_DIM);
    cv_blit(0, 4);
    for (s = HAAVE_KEYNAME[w->root % 12u]; *s; s++)     /* key and tempo, octave */
        b[n++] = *s;
    for (s = w->minor ? " MINOR  " : " MAJOR  "; *s; s++)
        b[n++] = *s;
    if (w->bpm >= 100u)
        b[n++] = (char)('0' + w->bpm / 100u);
    b[n++] = (char)('0' + w->bpm / 10u % 10u);
    b[n++] = (char)('0' + w->bpm % 10u);
    b[n] = 0;
    cv_begin(240, 15, D_BG);
    cv_text(8, 0, &AF_S, b, D_MID);
    cv_text_r(232, 0, &AF_S, haave_oct < 0 ? "OCT -1" : haave_oct > 0 ? "OCT +1" : "", D_MID, D_BG);
    cv_blit(0, 222);
}

static void haave_draw_chord(void)
{
    char b[24];
    uint32_t n = 0, i;
    const char *s;
    if (!haave_run || haave_deg < 0)
        draw_text_line(0, 34, 240, &AF_M, "PRESS PLAY", D_MID, D_BG, 1);
    else {
        for (s = haave_roman(haave_deg); *s; s++)
            b[n++] = *s;
        for (s = haave_bright ? " add9" : ""; *s; s++)
            b[n++] = *s;
        b[n] = 0;
        draw_text_line(0, 34, 240, &AF_M, b, D_TEXT, D_BG, 1);
    }
    cv_begin(240, 24, D_BG);                             /* the layers, underlined while on */
    for (i = 0; i < DL_N; i++) {
        uint32_t on = (haave_layers >> i) & 1u, x = 2u + i * 34u;
        cv_text((int32_t)(x + 17u) - text_w(&AF_S, HAAVE_LAYER[i]) / 2, 0, &AF_S, HAAVE_LAYER[i],
                on ? D_TEXT : D_DIM);
        if (on)
            cv_rrect((int32_t)x + 7, 18, 20, 3, -1, D_PEACH, D_BG);
    }
    cv_blit(0, 60);
}

/* a knob: its name, a solid bar, its value */
static void haave_draw_bar(uint32_t i)
{
    int32_t w = 56, f = haave_knob[i] * (w - 8) / 127 + 4;
    char b[4];
    uint32_t v = (uint32_t)haave_knob[i] * 100u / 127u;
    cv_begin((uint32_t)w, 46, D_BG);
    cv_text(w / 2 - text_w(&AF_S, HAAVE_KNOB[i]) / 2, 0, &AF_S, HAAVE_KNOB[i], D_MID);
    cv_rrect(4, 20, w - 8, 6, -3, D_DIM, D_BG);          /* the track */
    cv_rrect(4, 20, f - 4, 6, -3, HAAVE_KCOL[i], D_DIM); /* the value */
    b[0] = (char)(v >= 100u ? '1' : '0' + v / 10u);
    b[1] = (char)(v >= 100u ? '0' : '0' + v % 10u);
    b[2] = v >= 100u ? '0' : 0;
    b[3] = 0;
    cv_text(w / 2 - text_w(&AF_S, v < 10u ? b + 1 : b) / 2, 31, &AF_S, v < 10u ? b + 1 : b, HAAVE_KCOL[i]);
    cv_blit(6u + i * 58u, 174);
}

/* the splash (main.c, in place of Felucca's): a planet, HAAVE, a line */
static void haave_splash(void)
{
    lcd_fill(0, 0, 240, 240, D_BG);
    cv_begin(240, 120, D_BG);
    d_disc(120, 40, 22, D_SKY, 32);                      /* a planet, two moons */
    d_disc(156, 24, 5, D_ROSE, 32);
    d_disc(82, 56, 3, D_MINT, 32);
    cv_text(120 - text_w(&AF_L, "HAAVE") / 2, 72, &AF_L, "HAAVE", D_TEXT);
    cv_blit(0, 46);
    draw_text_line(0, 176, 240, &AF_S, "DREAM FIRMWARE FOR THE FM-1", D_MID, D_BG, 1);
    draw_text_line(0, 196, 240, &AF_S, FELUCCA_VERSION, D_DIM, D_BG, 1);
}

static void haave_draw(void)
{
    static uint8_t seen = 0xFF, frame;
    uint32_t i;
    if (haave_redraw) {
        lcd_fill(0, 0, 240, 240, D_BG);
        haave_redraw = 0;
        haave_head = 1;
        haave_bars = 0xF;
        seen = (uint8_t)(haave_seen + 1u);
    }
    if (haave_head) {
        haave_head = 0;
        haave_draw_head();
    }
    for (i = 0; i < DK_N; i++)
        if ((haave_bars >> i) & 1u) {
            haave_bars &= (uint8_t)~(1u << i);
            haave_draw_bar(i);
        }
    if (seen != haave_seen) {
        seen = haave_seen;
        haave_draw_chord();
    }
    if (++frame & 1u)
        haave_viz();
}
