/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dominik Siemon */
/* DRIFT: the panel and the screen (the main loop). With FELUCCA_DRIFT, felucca.c (and the browser build) call these in
 * place of Felucca's ui_input / ui_leds / ui_draw; Felucca's UI is still built (main.c's crash, update and UBOOT
 * screens use its drawing) but never runs. One page, no menus: every control does one thing (DRIFT.md).
 *   PLAY       the music on / off
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
#include "drift_fm6.h"                   /* DRIFT_FM6[]: BOWL, DRONE, DARK PAD, CRYSTAL, KALIMBA, SOFT KEYS */
#define ENGI_ANALOG 0u                   /* engines.c ENGINES[] */
#define ENGI_FORMANT 5u
#define ENGI_WHEEL 7u
enum { FM6_TINE = 0, FM6_BELL = 1, FM6_PAD = 4, FM6_MARIMBA = 5 };          /* eng_fm6.c FM6_PRESETS */
enum { AN_SOFT_PAD = 1, AN_PWM_STR = 3, AN_SUB_BASS = 7 };                    /* eng_analog.c's presets */
enum { FO_CHOIR = 0, FO_WHISPER = 3 };                                        /* eng_formant.c's */
enum { WH_SOFT_FLUTE = 3 };                                                   /* eng_wheel.c's */
#define NO_PATCH 0xFFu
typedef struct { uint8_t engine, preset, patch, level; } drift_sound_t;
typedef struct {
    const char *name;
    uint8_t root, minor, bpm;            /* the key (0 = C) and its mode, the tempo */
    drift_sound_t part[NPART];           /* pad, bells, bass, choir */
} drift_world_t;
#define SUB {ENGI_ANALOG, AN_SUB_BASS, NO_PATCH, 88}
#define AAH {ENGI_FORMANT, FO_CHOIR, NO_PATCH, 86}
#define BREATH {ENGI_FORMANT, FO_WHISPER, NO_PATCH, 100}
static const drift_world_t DRIFT_WORLDS[] = {
    {"GLASS", 2, 0, 72, {{ENGI_FM6, FM6_PAD, NO_PATCH, 102}, {ENGI_FM6, FM6_BELL, NO_PATCH, 112}, SUB, AAH}},
    {"EMBER", 9, 0, 66, {{ENGI_FM6, FM6_PAD, NO_PATCH, 102}, {ENGI_FM6, FM6_TINE, NO_PATCH, 108}, SUB, AAH}},
    {"DUSK", 5, 0, 80, {{ENGI_FM6, FM6_PAD, NO_PATCH, 102}, {ENGI_FM6, FM6_MARIMBA, NO_PATCH, 127}, SUB, AAH}},
    {"NIGHT", 4, 1, 60, {{ENGI_FM6, FM6_PAD, DF_DARK_PAD, 102}, {ENGI_FM6, FM6_BELL, NO_PATCH, 112}, SUB, BREATH}},
    {"NEON", 7, 0, 84, {{ENGI_ANALOG, AN_PWM_STR, NO_PATCH, 87}, {ENGI_FM6, FM6_BELL, DF_CRYSTAL, 108}, SUB, AAH}},
    {"LOFI", 3, 0, 70, {{ENGI_ANALOG, AN_SOFT_PAD, NO_PATCH, 87}, {ENGI_FM6, FM6_TINE, DF_SOFT_KEYS, 110}, SUB,
                         BREATH}},
    {"BOWLS", 0, 1, 56, {{ENGI_FM6, FM6_PAD, DF_DRONE, 100}, {ENGI_FM6, FM6_BELL, DF_BOWL, 98}, SUB, AAH}},
    {"GARDEN", 9, 0, 64, {{ENGI_FM6, FM6_PAD, DF_DRONE, 100}, {ENGI_FM6, FM6_MARIMBA, DF_KALIMBA, 112}, SUB, AAH}},
    {"FOREST", 4, 1, 58, {{ENGI_FM6, FM6_PAD, NO_PATCH, 100}, {ENGI_WHEEL, WH_SOFT_FLUTE, NO_PATCH, 122}, SUB,
                          BREATH}},
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

/* GLOW on one part: FM6 the modulators' level (MLVL), ANALOG the filter's cutoff around the preset's own */
static void drift_glow(track_t *t, int32_t gl)
{
    int32_t c;
    if (t->eng_req == ENGI_FM6)
        t->p[P_E0 + 2] = (int16_t)(gl - 64);
    else if (t->eng_req == ENGI_ANALOG) {
        c = drift_cut[t - trk] + (gl - 56) / 2;
        t->p[P_E0 + 4] = (int16_t)(c < 0 ? 0 : c > 127 ? 127 : c);
    }
}

/* the knobs -> the parts and the effect buses */
static void drift_apply(void)
{
    track_t *pad = &trk[DRIFT_PAD], *bell = &trk[DRIFT_BELL], *bass = &trk[DRIFT_BASS], *choir = &trk[DRIFT_CHOIR];
    int32_t sp = drift_knob[DK_SPACE], ec = drift_knob[DK_ECHO], gl = drift_knob[DK_GLOW], mo = drift_knob[DK_MOTION];
    pad->p[P_REV] = (int16_t)sp;
    bell->p[P_REV] = (int16_t)(sp + (127 - sp) / 3);
    choir->p[P_REV] = (int16_t)(sp + (127 - sp) / 2);
    bass->p[P_REV] = (int16_t)(sp / 4);
    song.g[G_RSIZE] = (int16_t)(60 + sp / 2);
    pad->p[P_DLY] = (int16_t)(ec / 2);
    bell->p[P_DLY] = (int16_t)ec;
    choir->p[P_DLY] = (int16_t)(ec / 3);
    song.g[G_DFDBK] = (int16_t)(30 + ec * 3 / 5);
    drift_glow(pad, gl);
    drift_glow(bell, gl);
    pad->p[P_CHOR] = (int16_t)mo;
    choir->p[P_CHOR] = (int16_t)(mo / 2);
    if (pad->eng_req == ENGI_FM6)
        pad->p[P_E0 + 6] = (int16_t)(mo / 2);            /* FM6 DTUN: the operators drift apart */
    pad->p[P_SPRD] = (int16_t)(40 + mo / 2);
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
    trk[DRIFT_PAD].p[P_DEC] = 90;
    drift_part(&trk[DRIFT_BELL], &d->part[1], 0, 127, 100);     /* struck: its own decay, a long release */
    trk[DRIFT_BELL].p[P_PAN] = 20;
    drift_part(&trk[DRIFT_BASS], &d->part[2], 40, 120, 90);
    drift_part(&trk[DRIFT_CHOIR], &d->part[3], 84, 120, 100);
    trk[DRIFT_CHOIR].p[P_GLIDE] = 40;
    trk[DRIFT_CHOIR].p[P_PAN] = -18;
    drift_root = d->root;
    drift_minor = d->minor;
    drift_bpm = d->bpm;
    song.g[G_BPM] = d->bpm;                              /* (the delay's 1/4 follows it) */
    song.g[G_RTYPE] = 2;                                 /* HALL */
    song.g[G_RDAMP] = 72;
    song.g[G_DTIME] = 0;
    song.g[G_DCOLOR] = 44;                               /* darker repeats */
    song.g[G_DMIX] = 90;
    drift_apply();
    drift_resync = 1;                                    /* (the ISR starts the held notes on the new sounds) */
    drift_head = 1;
}

/* ------------------------------------------------------------- input --- */
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
 * glows dimly; PLAY green while the music plays; OCT- / OCT+ lit while shifted */
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
 *    84  the sounding notes as soft lights over a slow horizon
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

/* a light added onto the canvas: colour c at a / 32, the channels saturating */
static void d_add(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    uint32_t i, o, r, g, b;
    if ((uint32_t)x >= cv_w || (uint32_t)y >= cv_h || !a)
        return;
    i = (uint32_t)y * cv_w + (uint32_t)x;
    o = swap16(cv_px[i]);
    r = ((o >> 11) & 31u) + (((c >> 11) & 31u) * a >> 5);
    g = ((o >> 5) & 63u) + (((c >> 5) & 63u) * a >> 5);
    b = (o & 31u) + ((c & 31u) * a >> 5);
    cv_px[i] = swap16((r > 31u ? 31u : r) << 11 | (g > 63u ? 63u : g) << 5 | (b > 31u ? 31u : b));
}

/* a soft round light: a / 32 at the centre, falling off to nothing at radius r */
static void d_glow(int32_t cx, int32_t cy, int32_t r, uint16_t c, uint32_t a)
{
    int32_t x, y, rr = r * r;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++) {
            int32_t d = x * x + y * y;
            if (d < rr)
                d_add(cx + x, cy + y, c, a * (uint32_t)(rr - d) * (uint32_t)(rr - d) / (uint32_t)(rr * rr));
        }
}

/* the sounding voices as soft lights: across by pitch, the height a slow wave, the size their level */
static void drift_viz(void)
{
    static uint32_t ph;
    uint32_t p, i;
    int32_t x;
    ph++;
    cv_begin(240, VIZ_H, D_BG);
    for (x = 0; x < 240; x += 3)                         /* the horizon: a slow, faint wave */
        cv_pset(x, (int32_t)VIZ_H / 2 + SINE[((uint32_t)x * 6u + ph * 2u) & 1023u] * 6 / 32768, D_DIM);
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++) {
            const voice_t *v = &trk[p].v[i];
            int32_t lvl, cx, cy;
            if (!v->active || (lvl = v->env_out >> 10) < 1)       /* (Q15 -> 0..32) */
                continue;
            cx = ((int32_t)v->note - 28) * 240 / 76;
            cy = (int32_t)VIZ_H / 2 + SINE[(v->note * 97u + ph * (3u + p)) & 1023u] * 28 / 32768;
            d_glow(cx, cy, 5 + lvl / 3 + (p == DRIFT_PAD ? 4 : 0), DRIFT_PCOL[p], (uint32_t)(10 + lvl / 2));
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
    cv_begin(240, 20, D_BG);                             /* the layers, underlined while on */
    for (i = 0; i < DL_N; i++) {
        uint32_t on = (drift_layers >> i) & 1u, x = 2u + i * 34u;
        cv_text((int32_t)(x + 17u) - text_w(&AF_S, DRIFT_LAYER[i]) / 2, 0, &AF_S, DRIFT_LAYER[i],
                on ? D_TEXT : D_DIM);
        if (on)
            cv_rect((int32_t)x + 7, 17, 20, 2, D_PEACH);
    }
    cv_blit(0, 60);
}

static void drift_draw_bar(uint32_t i)
{
    uint32_t x = 6u + i * 58u, w = 54u, f = (uint32_t)drift_knob[i] * w / 127u;
    cv_begin(w, 34, D_BG);
    cv_text((int32_t)w / 2 - text_w(&AF_S, DRIFT_KNOB[i]) / 2, 0, &AF_S, DRIFT_KNOB[i], D_MID);
    cv_rect(0, 22, (int32_t)w, 6, D_DIM);
    cv_rect(0, 22, (int32_t)f, 6, DRIFT_KCOL[i]);
    cv_blit(x, 182);
}

/* the splash (main.c, in place of Felucca's): a soft planet, DRIFT, a line */
static void drift_splash(void)
{
    lcd_fill(0, 0, 240, 240, D_BG);
    cv_begin(240, 120, D_BG);
    d_glow(120, 40, 28, D_SKY, 14);
    d_glow(150, 28, 6, D_ROSE, 12);
    d_glow(84, 54, 4, D_MINT, 12);
    cv_text(120 - text_w(&AF_L, "DRIFT") / 2, 72, &AF_L, "DRIFT", D_TEXT);
    cv_blit(0, 46);
    draw_text_line(0, 176, 240, &AF_S, "DREAM FIRMWARE FOR THE FM-1", D_MID, D_BG, 1);
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
