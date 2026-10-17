/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dominik Siemon */
/* DRIFT: the panel and the screen (the main loop). With FELUCCA_DRIFT, felucca.c (and the browser build) call these in
 * place of Felucca's ui_input / ui_leds / ui_draw; Felucca's UI is still built (main.c's crash, update and UBOOT
 * screens use its drawing) but never runs. One page, no menus: every control does one thing (DRIFT.md).
 *   PLAY       the music on / off
 *   FX SCL ENV LFO EDIT GLO   punch-in effects while held (drift_play.c)
 *   KNOB 1..4  SPACE (reverb), ECHO (delay), GLOW (brightness), MOTION (how busy; chorus and drift)
 *   PRESETS    the world: the sounds, the key and its mode, the tempo
 *   ALGORITHM  the behaviour of every layer
 *   SELECT     the progression
 *   OCT- OCT+  everything an octave down / up
 * The keys (layers, chords) and the music run in the audio ISR (drift_play.c). */

/* ------------------------------------------------------------ worlds --- */
/* A world (PRESETS) is a whole mood: the key and its mode, the tempo, and the sound of each of the four parts. A part
 * is an engine and one of its presets, then (FM6) one of DRIFT's own patches put in over it (tools/gen_drift_patches.py:
 * loaded as the part's own patch, Felucca's factory patches untouched), at a level that sits it in the mix. */
#include "drift_fm6.h"                   /* DRIFT_FM6[]: BOWL, DRONE, DARK PAD, CRYSTAL, CLOUD, AIR, CLEAR BELL, KALIMBA,
                                          * SOFT KEYS */
#define ENGI_ANALOG 0u                   /* engines.c ENGINES[] */
#define ENGI_FORMANT 5u
#define ENGI_WHEEL 7u
#define ENGI_GRAIN 8u
enum { FM6_TINE = 0, FM6_BELL = 1, FM6_PAD = 4, FM6_MARIMBA = 5 };   /* eng_fm6.c FM6_PRESETS */
enum { AN_SOFT_PAD = 1, AN_SQR_BASS = 2, AN_PWM_STR = 3, AN_SUB_BASS = 7, AN_STRINGS = 11 };   /* eng_analog.c's */
enum { FO_CHOIR = 0, FO_LEAD = 1, FO_WHISPER = 3 };                                        /* eng_formant.c's */
enum { WH_SOFT_FLUTE = 3 };                                                   /* eng_wheel.c's */
enum { GR_SHIMMER = 3 };                                                      /* eng_grain.c's */
enum { RV_ROOM, RV_SPRING, RV_HALL };                                         /* G_RTYPE */
enum { DT_4 = 0, DT_8 = 1, DT_8T = 4, DT_2 = 6, DT_1 = 7, DT_8D = 10, DT_4D = 12 };   /* G_DTIME (params.c N_DTIME) */
#define NO_PATCH 0xFFu
typedef struct { uint8_t engine, preset, patch, level; } drift_sound_t;
/* the world's VOX: a preset of the voice engine, then its eight values (VOWL VOWL2 TALK SHIFT BUZZ BRTH Q RAND: the vowel
 * and the one it moves to over TALK, the formants' shift, buzz, breath, resonance, a random vowel per note), its
 * register and level */
typedef struct { uint8_t preset; int8_t e[8]; int8_t reg; uint8_t level; } drift_vox_t;
typedef struct {
    const char *name;
    uint8_t root, minor, bpm;            /* the key (0 = C) and its mode, the tempo */
    drift_sound_t part[3];               /* pad, bells, bass */
    drift_vox_t vox;                     /* the voice */
    uint8_t rtype, rdamp, dtime, dcolor; /* the room: reverb type and damping; the echo: its time and colour */
    uint8_t style, prog;                 /* the behaviour and progression it starts with (ALGORITHM, SELECT) */
} drift_world_t;
#define SUB {ENGI_ANALOG, AN_SUB_BASS, NO_PATCH, 88}
static const drift_world_t DRIFT_WORLDS[] = {
    {"GLASS", 2, 0, 72, {{ENGI_FM6, FM6_PAD, NO_PATCH, 102}, {ENGI_FM6, FM6_BELL, DF_CLEAR_BELL, 112}, SUB},
     {FO_CHOIR, {0, 20, 70, 3, 30, 40, 72, 0}, 12, 82},             /* airy, high: "aah" opening to "eh" */
     RV_HALL, 60, DT_4, 64, DS_DRIFT, 1},
    {"EMBER", 9, 0, 66, {{ENGI_ANALOG, AN_STRINGS, NO_PATCH, 94}, {ENGI_FM6, FM6_TINE, NO_PATCH, 108}, SUB},
     {FO_CHOIR, {100, 95, 0, -3, 46, 16, 60, 0}, 0, 86},            /* warm "ooh" */
     RV_SPRING, 80, DT_8T, 40, DS_RIPPLE, 2},
    {"DUSK", 5, 0, 80, {{ENGI_FM6, FM6_PAD, DF_CLOUD, 110}, {ENGI_FM6, FM6_MARIMBA, NO_PATCH, 127}, SUB},
     {FO_CHOIR, {127, 127, 0, -5, 18, 28, 38, 0}, -12, 93},         /* a low, soft hum */
     RV_ROOM, 70, DT_8D, 50, DS_PULSE, 4},
    {"NIGHT", 4, 1, 60, {{ENGI_FM6, FM6_PAD, DF_DARK_PAD, 102}, {ENGI_FM6, FM6_BELL, DF_CLEAR_BELL, 112}, SUB},
     {FO_WHISPER, {95, 127, 100, -6, 40, 112, 44, 0}, -12, 108},    /* a dark, low whisper */
     RV_HALL, 88, DT_2, 30, DS_SWELL, 3},
    {"NEON", 7, 0, 84, {{ENGI_ANALOG, AN_PWM_STR, NO_PATCH, 87}, {ENGI_FM6, FM6_BELL, DF_CRYSTAL, 108},
                        {ENGI_ANALOG, AN_SQR_BASS, NO_PATCH, 96}},
     {FO_LEAD, {32, 64, 60, 2, 92, 6, 84, 0}, 0, 88},               /* an 80s machine voice */
     RV_HALL, 50, DT_8D, 72, DS_PULSE, 1},
    {"LOFI", 3, 0, 70, {{ENGI_ANALOG, AN_SOFT_PAD, NO_PATCH, 91}, {ENGI_FM6, FM6_TINE, DF_SOFT_KEYS, 110}, SUB},
     {FO_CHOIR, {110, 110, 0, -4, 28, 52, 34, 0}, -12, 93},         /* a muffled hum, like old tape */
     RV_ROOM, 90, DT_8, 24, DS_RIPPLE, 3},
    {"BOWLS", 0, 1, 56, {{ENGI_FM6, FM6_PAD, DF_DRONE, 100}, {ENGI_FM6, FM6_BELL, DF_BOWL, 98}, SUB},
     {FO_CHOIR, {127, 64, 127, -2, 112, 8, 122, 0}, -12, 88},       /* an overtone drone, slowly sweeping */
     RV_HALL, 66, DT_1, 40, DS_DRIFT, 0},
    {"GARDEN", 9, 0, 64, {{ENGI_GRAIN, GR_SHIMMER, NO_PATCH, 106}, {ENGI_FM6, FM6_MARIMBA, DF_KALIMBA, 112}, SUB},
     {FO_CHOIR, {10, 40, 50, 2, 56, 14, 70, 40}, 0, 90},            /* a bright, open "ah", its vowel wandering */
     RV_SPRING, 60, DT_8T, 60, DS_CHIME, 2},
    {"FOREST", 4, 1, 58, {{ENGI_FM6, FM6_PAD, DF_AIR, 104}, {ENGI_WHEEL, WH_SOFT_FLUTE, NO_PATCH, 122}, SUB},
     {FO_WHISPER, {120, 100, 90, 0, 24, 92, 54, 0}, 0, 94},        /* a breathy "hoo", like wind */
     RV_HALL, 76, DT_4D, 44, DS_DRIFT, 5},
};
#define DRIFT_NWORLDS (sizeof DRIFT_WORLDS / sizeof DRIFT_WORLDS[0])

enum { DK_SPACE, DK_ECHO, DK_GLOW, DK_MOTION, DK_N };
static const char *const DRIFT_KNOB[DK_N] = {"SPACE", "ECHO", "GLOW", "MOTION"};
static int16_t drift_knob[DK_N] = {90, 60, 56, 50};   /* 0..127 */
static uint8_t drift_world, drift_ready;
static uint8_t drift_redraw = 1;         /* the whole screen */
static uint8_t drift_head = 1;           /* the top and bottom lines */
static uint8_t drift_bars = 0xF;         /* the knobs' bars (bit per knob) */
static int16_t drift_cut[NPART];         /* an ANALOG part's own cutoff (GLOW moves it from there) */

/* GLOW on one part: FM6 the modulators' level (MLVL), ANALOG the filter's cutoff around the preset's own. The bells take
 * half its range: their bright overtones, all the way up on high notes, fold back as fizz */
static void drift_glow(track_t *t, int32_t gl)
{
    int32_t c;
    if (t->eng_req == ENGI_FM6)
        t->p[P_E0 + 2] = (int16_t)(t == &trk[DRIFT_BELL] ? (gl - 64) / 2 : gl - 64);
    else if (t->eng_req == ENGI_ANALOG) {
        c = drift_cut[t - trk] + (gl - 56) * 3 / 4;
        t->p[P_E0 + 4] = (int16_t)(c < 0 ? 0 : c > 127 ? 127 : c);
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
static void drift_apply(void)
{
    track_t *pad = &trk[DRIFT_PAD], *bell = &trk[DRIFT_BELL], *bass = &trk[DRIFT_BASS], *choir = &trk[DRIFT_CHOIR];
    int32_t sp = drift_knob[DK_SPACE], ec = drift_knob[DK_ECHO], gl = drift_knob[DK_GLOW], mo = drift_knob[DK_MOTION];
    int32_t pg = gl + PAD_BEH[drift_style % DS_N].glow;
    pad->p[P_LSYNC] = PAD_BEH[drift_style % DS_N].sync;
    pad->p[P_LWAVE] = PAD_BEH[drift_style % DS_N].wave;
    pad->p[P_LD_AMP] = PAD_BEH[drift_style % DS_N].amp;
    pad->p[P_LTRIG] = 0;
    pad->p[P_ATK] = PAD_BEH[drift_style % DS_N].atk;
    pad->p[P_REV] = (int16_t)sp;                         /* SPACE: dry and close .. a vast hall */
    bell->p[P_REV] = (int16_t)sp;
    choir->p[P_REV] = (int16_t)sp;
    bass->p[P_REV] = (int16_t)(sp / 4);
    song.g[G_RSIZE] = (int16_t)(16 + sp * 111 / 127);
    pad->p[P_DLY] = (int16_t)(ec * 3 / 4);               /* ECHO: none .. long trails (feedback up to 100 of 120) */
    bell->p[P_DLY] = (int16_t)ec;
    choir->p[P_DLY] = (int16_t)(ec / 2);
    song.g[G_DFDBK] = (int16_t)(10 + ec * 90 / 127);
    drift_glow(pad, pg < 0 ? 0 : pg > 127 ? 127 : pg);
    drift_glow(bell, gl);
    pad->p[P_CHOR] = (int16_t)mo;
    choir->p[P_CHOR] = (int16_t)(mo / 2);
    if (pad->eng_req == ENGI_FM6)
        pad->p[P_E0 + 6] = (int16_t)(mo * 3 / 4);        /* FM6 DTUN: the operators drift apart */
    pad->p[P_SPRD] = (int16_t)(20 + mo * 3 / 4);
    drift_motion = (uint8_t)mo;
}

/* part t to sound d, with its envelope (atk, sus, rel) */
static void drift_part(track_t *t, const drift_sound_t *d, int16_t atk, int16_t sus, int16_t rel)
{
    uint32_t tr = (uint32_t)(t - trk);
    set_engine_of(t, d->engine);
    apply_preset_to(t, d->preset);
    if (d->engine == ENGI_FM6 && d->patch < DRIFT_NFM6) {   /* DRIFT's own patch, as the part's own (SLOT OWN) */
        uint8_t v[FP_SIZE + 1u];
        fm6_unpack(DRIFT_FM6[d->patch], v);
        fm6_set_patch(tr, v);
        fm6_own_ok &= (uint8_t)~(1u << tr);
        fm6_slot[tr] = FM6_OWN;
        t->p[P_E7] = FM6_OWN;
    }
    drift_cut[tr] = t->p[P_E0 + 4];
    t->p[P_VOICE] = V_POLY;
    t->p[P_MUTE] = 0;
    t->p[P_LEVEL] = d->level;
    t->p[P_ATK] = atk;
    t->p[P_SUS] = sus;
    t->p[P_REL] = rel;
}

static void drift_load(uint32_t w)
{
    const drift_world_t *d = &DRIFT_WORLDS[w % DRIFT_NWORLDS];
    drift_world = (uint8_t)(w % DRIFT_NWORLDS);
    drift_part(&trk[DRIFT_PAD], &d->part[0], 72, 112, 104);     /* slow swell in, long tail out */
    if (trk[DRIFT_PAD].eng_req == ENGI_ANALOG)           /* its two oscillators 8 cents apart: a chorus, still in tune */
        trk[DRIFT_PAD].p[P_E0 + 1] = 8;
    trk[DRIFT_PAD].p[P_DEC] = 90;
    drift_part(&trk[DRIFT_BELL], &d->part[1], 0, 127, 100);     /* struck: its own decay, a long release */
    trk[DRIFT_BELL].p[P_PAN] = 20;
    drift_part(&trk[DRIFT_BASS], &d->part[2], 40, 120, 90);
    {   /* the voice: its preset, then the world's own vowel, register and level */
        const drift_sound_t v = {ENGI_FORMANT, d->vox.preset, NO_PATCH, d->vox.level};
        uint32_t i;
        drift_part(&trk[DRIFT_CHOIR], &v, 84, 120, 100);
        for (i = 0; i < 8u; i++)
            trk[DRIFT_CHOIR].p[P_E0 + i] = d->vox.e[i];
        drift_vox_reg = d->vox.reg;
    }
    trk[DRIFT_CHOIR].p[P_GLIDE] = 40;
    trk[DRIFT_CHOIR].p[P_PAN] = -18;
    drift_root = d->root;
    drift_minor = d->minor;
    drift_bpm = d->bpm;
    song.g[G_BPM] = d->bpm;                              /* (the delay's 1/4 follows it) */
    song.g[G_RTYPE] = d->rtype;
    song.g[G_RDAMP] = d->rdamp;
    song.g[G_DTIME] = d->dtime;
    song.g[G_DCOLOR] = d->dcolor;
    song.g[G_DMIX] = 90;
    drift_style = d->style;                              /* where the world starts; ALGORITHM and SELECT turn from there */
    drift_prog = d->prog;
    drift_apply();
    drift_resync = 1;                                    /* (the ISR starts the held notes on the new sounds) */
    drift_head = 1;
}

/* ------------------------------------------------------------- input --- */
static const uint8_t DRIFT_PUNCH_BTN[DP_NPUNCH] = {B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO};   /* (drift_play.c) */
static int32_t drift_turn(uint32_t role)                 /* a list knob: one step per turn, whatever its speed */
{
    int32_t s = panel_enc(role);
    return s > 0 ? 1 : s < 0 ? -1 : 0;
}

static void drift_input(void)
{
    uint32_t pressed = fm1_input_edges(0), i;
    int32_t s;
    fm1_input_note_edges();                              /* (the keys are drift_play.c's) */
    if (!drift_ready) {                                  /* first pass: after the boot and any autosave restore */
        drift_ready = 1;
        perf_latch_on = 0;                               /* (the punch-ins hold while held, whatever was saved) */
        perf_mask = 0;
        for (i = 0; i < DP_NPUNCH; i++)
            drift_punch_bit[i] = 1u << panel.btn[DRIFT_PUNCH_BTN[i]];
        drift_load(0);
    }
    for (i = 0; i < DK_N; i++)
        if ((s = panel_enc(EN_K1 + i)) != 0) {
            int32_t v = drift_knob[i] + 2 * s;
            drift_knob[i] = (int16_t)(v < 0 ? 0 : v > 127 ? 127 : v);
            drift_apply();
            drift_bars |= (uint8_t)(1u << i);
        }
    if ((s = drift_turn(EN_PRESET)) != 0)
        drift_load((uint32_t)((int32_t)drift_world + s + (int32_t)DRIFT_NWORLDS));
    if ((s = drift_turn(EN_ALGO)) != 0) {
        drift_style = (uint8_t)(((int32_t)drift_style + s + DS_N) % DS_N);
        drift_apply();
        drift_head = 1;
    }
    if ((s = drift_turn(EN_SELECT)) != 0) {
        drift_prog = (uint8_t)(((int32_t)drift_prog + s + (int32_t)DP_N) % (int32_t)DP_N);
        drift_head = 1;
    }
    if ((pressed >> panel.btn[B_PLAY]) & 1u) {
        drift_run = !drift_run;
        drift_head = 1;
        drift_seen++;
    }
    if ((pressed >> panel.btn[B_OCTDN]) & 1u && drift_oct > -1) {
        drift_oct--;
        drift_head = 1;
    }
    if ((pressed >> panel.btn[B_OCTUP]) & 1u && drift_oct < 1) {
        drift_oct++;
        drift_head = 1;
    }
}

/* -------------------------------------------------------------- LEDs --- */
/* white keys: lit while their layer plays; black keys: lit while held, and the one whose chord sounds now; every key
 * is dimly lit; PLAY green while the music plays; OCT- / OCT+ lit while shifted */
static void drift_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0};
    static uint8_t ready;
    uint32_t k, c;
    if (fm1_led_anim_on())
        return;
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    for (k = 0; k < 27u; k++) {
        uint32_t p = key_place(k), on;
        if (!key_black(k))
            on = (drift_layers >> (p % DL_N)) & 1u;
        else
            on = ((fm1_in.notes >> k) & 1u) ||
                 (drift_run && drift_deg == DRIFT_BLACK[drift_minor & 1u][p % 5u] && drift_bright == (p >= 5u));
        led_put(nl, 14u + k, (int)on);
        led_put(nd, 14u + k, 1);
    }
    led_put(nl, panel.btn[B_OCTDN], drift_oct < 0);
    led_put(nl, panel.btn[B_OCTUP], drift_oct > 0);
    if (drift_run)
        nl[LED_PLAY_GREEN >> 3] |= (uint8_t)(1u << (LED_PLAY_GREEN & 7u));
    for (k = 0; k < DP_NPUNCH; k++) {                    /* the punch-in buttons: lit while held, dim otherwise */
        led_put(nl, panel.btn[DRIFT_PUNCH_BTN[k]], (drift_punch >> k) & 1u);
        led_put(nd, panel.btn[DRIFT_PUNCH_BTN[k]], 1);
    }
    fm1_led_dim_level(1);
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led_dim[c] = (uint8_t)(nd[c] & ~nl[c]);
        fm1_led_breath[c] = 0;
        fm1_led_mid[c] = 0;
        fm1_led[c] = nl[c];
    }
}

/* ------------------------------------------------------------ screen --- */
/* One page, plain (DRIFT.md):
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

static const char *const DRIFT_ROMAN[2][7] = {{"I", "II", "III", "IV", "V", "VI", "VII"},
                                              {"i", "ii", "iii", "iv", "v", "vi", "vii"}};
/* chord d's numeral: upper case for a major triad (a major third above its root in the world's scale), else lower */
static const char *drift_roman(int32_t d)
{
    return DRIFT_ROMAN[drift_step(d + 2) - drift_step(d) != 4][d % 7];
}
static const char *const DRIFT_KEYNAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const uint16_t DRIFT_KCOL[DK_N] = {D_SKY, D_LILAC, D_ROSE, D_MINT};
static const uint16_t DRIFT_PCOL[NPART] = {D_SKY, D_PEACH, D_VIOLET, D_LILAC};   /* the parts' lights */

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
 * way of moving them (one sentence each in DRIFT.md). Lights across by pitch, their size and brightness their level.
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

/* the picture behind the lights */
static void viz_back(void)
{
    int32_t x, y, w = 240, h = (int32_t)VIZ_H, m = h / 2;
    uint32_t i, r = 0x9E3779B9u;
    switch (drift_world) {
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
    case 2:                                              /* DUSK: the horizon, a slow wave */
    default:                                             /* GARDEN: nothing but the fireflies */
        if (drift_world == 2u)
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
    *rad = 5 + lvl / 3 + (p == DRIFT_PAD ? 4 : 0);
    switch (drift_world) {
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
        if (p == DRIFT_BELL) {
            int32_t rx = 6 + (32 - lvl) * 3;
            viz_ring(w / 2 + (px - w / 2) / 4, m, rx, rx / 2, DRIFT_PCOL[p], 10u + (uint32_t)lvl / 2u);
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
    default:                                             /* DUSK, LOFI: floating on a slow wave */
        *x = px;
        *y = m + v_sin(a, 28);
        break;
    }
    return 1;
}

static void drift_viz(void)
{
    uint32_t p, i;
    viz_ph++;
    cv_begin(240, VIZ_H, D_BG);
    viz_back();
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++) {
            const voice_t *v = &trk[p].v[i];
            int32_t lvl, x, y, rad;
            if (!v->active || (lvl = v->env_out >> 10) < 1)       /* (Q15 -> 0..32) */
                continue;
            if (viz_place(p, i, v->note, lvl, &x, &y, &rad))
                d_disc(x, y, 2 + rad / 2, DRIFT_PCOL[p], (uint32_t)(4 + lvl / 4));   /* (dimmer as it fades) */
        }
    for (p = 0; p < DP_NPUNCH; p++)                      /* a punch-in playing: its name in the middle */
        if ((drift_punch >> p) & 1u) {
            int32_t tw = text_w(&AF_M, DRIFT_PUNCH_NAME[p]);
            cv_rrect(120 - tw / 2 - 10, (int32_t)VIZ_H / 2 - 12, tw + 20, 24, -6, D_DIM, D_BG);
            cv_text_on(120 - tw / 2, (int32_t)VIZ_H / 2 - 9, &AF_M, DRIFT_PUNCH_NAME[p], D_TEXT, D_DIM);
            break;
        }
    cv_blit(0, VIZ_Y);
}

static void drift_draw_head(void)
{
    char b[16];
    uint32_t n = 0;
    const char *s;
    const drift_world_t *w = &DRIFT_WORLDS[drift_world];
    cv_begin(240, 24, D_BG);                             /* the three choices */
    cv_text(8, 4, &AF_S, w->name, D_TEXT);
    cv_text(120 - text_w(&AF_S, DRIFT_PROG[drift_prog % DP_N]) / 2, 4, &AF_S, DRIFT_PROG[drift_prog % DP_N], D_TEXT);
    cv_text_r(232, 4, &AF_S, DRIFT_STYLE[drift_style % DS_N], D_TEXT, D_BG);
    cv_rect(8, 22, 224, 1, D_DIM);
    cv_blit(0, 4);
    for (s = DRIFT_KEYNAME[w->root % 12u]; *s; s++)     /* key and tempo, octave */
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
    cv_text_r(232, 0, &AF_S, drift_oct < 0 ? "OCT -1" : drift_oct > 0 ? "OCT +1" : "", D_MID, D_BG);
    cv_blit(0, 222);
}

static void drift_draw_chord(void)
{
    char b[24];
    uint32_t n = 0, i;
    const char *s;
    if (!drift_run || drift_deg < 0)
        draw_text_line(0, 34, 240, &AF_M, "PRESS PLAY", D_MID, D_BG, 1);
    else {
        for (s = drift_roman(drift_deg); *s; s++)
            b[n++] = *s;
        for (s = drift_bright ? " add9" : ""; *s; s++)
            b[n++] = *s;
        for (s = drift_pin != 0xFF ? "  HELD" : ""; *s; s++)
            b[n++] = *s;
        b[n] = 0;
        draw_text_line(0, 34, 240, &AF_M, b, D_TEXT, D_BG, 1);
    }
    cv_begin(240, 24, D_BG);                             /* the layers, underlined while on */
    for (i = 0; i < DL_N; i++) {
        uint32_t on = (drift_layers >> i) & 1u, x = 2u + i * 34u;
        cv_text((int32_t)(x + 17u) - text_w(&AF_S, DRIFT_LAYER[i]) / 2, 0, &AF_S, DRIFT_LAYER[i],
                on ? D_TEXT : D_DIM);
        if (on)
            cv_rrect((int32_t)x + 7, 18, 20, 3, -1, D_PEACH, D_BG);
    }
    cv_blit(0, 60);
}

/* a knob: its name, a solid bar, its value */
static void drift_draw_bar(uint32_t i)
{
    int32_t w = 56, f = drift_knob[i] * (w - 8) / 127 + 4;
    char b[4];
    uint32_t v = (uint32_t)drift_knob[i] * 100u / 127u;
    cv_begin((uint32_t)w, 46, D_BG);
    cv_text(w / 2 - text_w(&AF_S, DRIFT_KNOB[i]) / 2, 0, &AF_S, DRIFT_KNOB[i], D_MID);
    cv_rrect(4, 20, w - 8, 6, -3, D_DIM, D_BG);          /* the track */
    cv_rrect(4, 20, f - 4, 6, -3, DRIFT_KCOL[i], D_DIM); /* the value */
    b[0] = (char)(v >= 100u ? '1' : '0' + v / 10u);
    b[1] = (char)(v >= 100u ? '0' : '0' + v % 10u);
    b[2] = v >= 100u ? '0' : 0;
    b[3] = 0;
    cv_text(w / 2 - text_w(&AF_S, v < 10u ? b + 1 : b) / 2, 31, &AF_S, v < 10u ? b + 1 : b, DRIFT_KCOL[i]);
    cv_blit(6u + i * 58u, 174);
}

/* the splash (main.c, in place of Felucca's): a soft planet, DRIFT, a line */
static void drift_splash(void)
{
    lcd_fill(0, 0, 240, 240, D_BG);
    cv_begin(240, 120, D_BG);
    d_disc(120, 40, 22, D_SKY, 32);                      /* a planet, two moons */
    d_disc(156, 24, 5, D_ROSE, 32);
    d_disc(82, 56, 3, D_MINT, 32);
    cv_text(120 - text_w(&AF_L, "DRIFT") / 2, 72, &AF_L, "DRIFT", D_TEXT);
    cv_blit(0, 46);
    draw_text_line(0, 176, 240, &AF_S, "DREAM FIRMWARE FOR THE FM-1", D_MID, D_BG, 1);
    draw_text_line(0, 196, 240, &AF_S, FELUCCA_VERSION, D_DIM, D_BG, 1);
}

static void drift_draw(void)
{
    static uint8_t seen = 0xFF, frame;
    uint32_t i;
    if (drift_redraw) {
        lcd_fill(0, 0, 240, 240, D_BG);
        drift_redraw = 0;
        drift_head = 1;
        drift_bars = 0xF;
        seen = (uint8_t)(drift_seen + 1u);
    }
    if (drift_head) {
        drift_head = 0;
        drift_draw_head();
    }
    for (i = 0; i < DK_N; i++)
        if ((drift_bars >> i) & 1u) {
            drift_bars &= (uint8_t)~(1u << i);
            drift_draw_bar(i);
        }
    if (seen != drift_seen) {
        seen = drift_seen;
        drift_draw_chord();
    }
    if (++frame & 1u)
        drift_viz();
}
