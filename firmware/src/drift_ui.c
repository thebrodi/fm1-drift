/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dominik Siemon */
/* DRIFT: the panel and the screen (the main loop). With FELUCCA_DRIFT, felucca.c (and the browser build) call these in
 * place of Felucca's ui_input / ui_leds / ui_draw; Felucca's UI is still built (main.c's crash, update and UBOOT
 * screens use its drawing) but never runs. One page, no menus: every control does one thing (DRIFT.md).
 *   PLAY       the music on / off
 *   KNOB 1..4  SPACE (reverb), ECHO (delay), GLOW (FM brightness), MOTION (how busy; chorus and drift)
 *   PRESETS    the world: the sounds, the key, the tempo
 *   ALGORITHM  the behaviour of every layer
 *   SELECT     the progression
 *   OCT- OCT+  everything an octave down / up
 * The keys (layers, nudges) and the music run in the audio ISR (drift_play.c). */

/* ------------------------------------------------------------ worlds --- */
typedef struct {
    const char *name;
    uint8_t root, bpm;                   /* the key (0 = C), the tempo */
    uint8_t pad, bell;                   /* FM6 factory presets (eng_fm6.c FM6_PRESETS) of parts 1 and 2 */
} drift_world_t;
static const drift_world_t DRIFT_WORLDS[] = {
    {"GLASS", 2, 72, 4, 1},              /* D: PAD, BELL */
    {"EMBER", 9, 66, 4, 0},              /* A: PAD, TINE EP */
    {"DUSK", 5, 80, 4, 5},               /* F: PAD, MARIMBA */
};
#define DRIFT_NWORLDS (sizeof DRIFT_WORLDS / sizeof DRIFT_WORLDS[0])
#define ENGI_ANALOG 0u                   /* engines.c ENGINES[] */
#define ENGI_FORMANT 5u
#define ANALOG_SUB_BASS 7u               /* eng_analog.c's presets */
#define FORMANT_CHOIR 0u                 /* eng_formant.c's: CHOIR AAH */

enum { DK_SPACE, DK_ECHO, DK_GLOW, DK_MOTION, DK_N };
static const char *const DRIFT_KNOB[DK_N] = {"SPACE", "ECHO", "GLOW", "MOTION"};
static int16_t drift_knob[DK_N] = {90, 60, 56, 50};   /* 0..127 */
static uint8_t drift_world, drift_ready, drift_redraw = 1;

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
    pad->p[P_E0 + 2] = (int16_t)(gl - 64);               /* FM6 MLVL: the modulators' level, -64..63 */
    bell->p[P_E0 + 2] = (int16_t)(gl - 64);
    pad->p[P_CHOR] = (int16_t)mo;
    choir->p[P_CHOR] = (int16_t)(mo / 2);
    pad->p[P_E0 + 6] = (int16_t)(mo / 2);                /* FM6 DTUN: the operators drift apart */
    pad->p[P_SPRD] = (int16_t)(40 + mo / 2);
    drift_motion = (uint8_t)mo;
}

static void drift_part(track_t *t, uint32_t engine, uint32_t preset, int16_t level, int16_t atk, int16_t sus,
                       int16_t rel)
{
    set_engine_of(t, engine);
    apply_preset_to(t, preset);
    t->p[P_VOICE] = V_POLY;
    t->p[P_MUTE] = 0;
    t->p[P_LEVEL] = level;
    t->p[P_ATK] = atk;
    t->p[P_SUS] = sus;
    t->p[P_REL] = rel;
}

static void drift_load(uint32_t w)
{
    const drift_world_t *d = &DRIFT_WORLDS[w % DRIFT_NWORLDS];
    drift_world = (uint8_t)(w % DRIFT_NWORLDS);
    drift_part(&trk[DRIFT_PAD], ENGI_FM6, d->pad, 102, 72, 112, 104);       /* slow swell in, long tail out */
    trk[DRIFT_PAD].p[P_DEC] = 90;
    drift_part(&trk[DRIFT_BELL], ENGI_FM6, d->bell, 112, 0, 127, 100);    /* struck: its own decay, a long release */
    trk[DRIFT_BELL].p[P_PAN] = 20;
    drift_part(&trk[DRIFT_BASS], ENGI_ANALOG, ANALOG_SUB_BASS, 88, 40, 120, 90);
    drift_part(&trk[DRIFT_CHOIR], ENGI_FORMANT, FORMANT_CHOIR, 86, 84, 120, 100);
    trk[DRIFT_CHOIR].p[P_GLIDE] = 40;
    trk[DRIFT_CHOIR].p[P_PAN] = -18;
    drift_root = d->root;
    drift_bpm = d->bpm;
    song.g[G_BPM] = d->bpm;                              /* (the delay's 1/4 follows it) */
    song.g[G_RTYPE] = 2;                                 /* HALL */
    song.g[G_RDAMP] = 72;
    song.g[G_DTIME] = 0;
    song.g[G_DCOLOR] = 44;                               /* darker repeats */
    song.g[G_DMIX] = 90;
    drift_apply();
    drift_resync = 1;                                    /* (the ISR starts the held notes on the new sounds) */
    drift_redraw = 1;
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
            drift_redraw = 1;
        }
    if ((s = drift_turn(EN_PRESET)) != 0)
        drift_load((uint32_t)((int32_t)drift_world + s + (int32_t)DRIFT_NWORLDS));
    if ((s = drift_turn(EN_ALGO)) != 0) {
        drift_style = (uint8_t)(((int32_t)drift_style + s + DS_N) % DS_N);
        drift_redraw = 1;
    }
    if ((s = drift_turn(EN_SELECT)) != 0) {
        drift_prog = (uint8_t)(((int32_t)drift_prog + s + (int32_t)DP_N) % (int32_t)DP_N);
        drift_redraw = 1;
    }
    if ((pressed >> panel.btn[B_PLAY]) & 1u) {
        drift_run = !drift_run;
        drift_redraw = 1;
    }
    if ((pressed >> panel.btn[B_OCTDN]) & 1u && drift_oct > -1) {
        drift_oct--;
        drift_redraw = 1;
    }
    if ((pressed >> panel.btn[B_OCTUP]) & 1u && drift_oct < 1) {
        drift_oct++;
        drift_redraw = 1;
    }
}

/* -------------------------------------------------------------- LEDs --- */
/* white keys: lit while their layer plays; black keys: HOLD BRIGHT DARK lit while on, NEXT and HOME flash when
 * pressed; every key glows dimly; PLAY green while the music plays; OCT- / OCT+ lit while shifted */
static void drift_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0};
    static uint8_t ready, flash;
    static uint32_t flash_ms;
    uint32_t k, c;
    if (drift_flash) {
        flash = drift_flash;
        drift_flash = 0;
        flash_ms = fm1_ms;
    }
    if (flash && fm1_ms - flash_ms > 180u)
        flash = 0;
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
        else {
            p %= DN_N;
            on = (p == DN_HOLD && drift_hold) || (p == DN_BRIGHT && drift_bright) || (p == DN_DARK && drift_dark) ||
                 (flash && p == flash - 1u);
        }
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
/* top: the world, the progression, the behaviour; the chord now; the seven layers; the lights; the four knobs */
#define D_BG RGB(12, 14, 30)
#define D_TEXT RGB(226, 220, 255)
#define D_MID RGB(126, 126, 178)
#define D_DIM RGB(44, 46, 82)
#define D_WARM RGB(255, 196, 150)
#define D_COOL RGB(150, 190, 255)
#define VIZ_Y 84u
#define VIZ_H 84u

static const char *const DRIFT_ROMAN[7] = {"I", "ii", "iii", "IV", "V", "vi", "vii"};

/* an RGB565 colour scaled by a / 32 */
static uint16_t d_shade(uint16_t c, uint32_t a)
{
    uint32_t r = (c >> 11) & 31u, g = (c >> 5) & 63u, b = c & 31u;
    a = a > 32u ? 32u : a;
    return (uint16_t)(((r * a / 32u) << 11) | ((g * a / 32u) << 5) | (b * a / 32u));
}

/* one soft glow: brightness falls off from the centre */
static void d_glow(int32_t cx, int32_t cy, int32_t r, uint16_t c, uint32_t a)
{
    int32_t x, y;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++) {
            int32_t d = x * x + y * y, rr = r * r;
            if (d <= rr)
                cv_pset(cx + x, cy + y, d_shade(c, a * (uint32_t)(rr - d) / (uint32_t)rr));
        }
}

/* the sounding voices as drifting lights: across by pitch, the height a slow wave, the size their level; each part
 * its own colour */
static void drift_viz(void)
{
    static const uint16_t COL[NPART] = {D_COOL, D_WARM, RGB(120, 110, 220), RGB(210, 170, 255)};
    static uint32_t ph;
    uint32_t p, i;
    int32_t x;
    ph += 1u;
    cv_begin(240, VIZ_H, D_BG);
    for (x = 0; x < 240; x += 3) {                       /* the horizon: a slow, faint wave */
        int32_t y = (int32_t)VIZ_H / 2 + (SINE[((uint32_t)x * 6u + ph * 2u) & 1023u] * 6) / 32768;
        cv_pset(x, y, D_DIM);
    }
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++) {
            const voice_t *v = &trk[p].v[i];
            int32_t lvl, cx, cy;
            if (!v->active)
                continue;
            lvl = v->env_out >> 10;                      /* Q15 -> 0..32 */
            if (lvl < 1)
                continue;
            cx = ((int32_t)v->note - 28) * 240 / 76;
            cy = (int32_t)VIZ_H / 2 + (SINE[(v->note * 97u + ph * (3u + p)) & 1023u] * 26) / 32768;
            d_glow(cx, cy, 3 + lvl / 3 + (p == DRIFT_PAD ? 3 : 0), COL[p], (uint32_t)(8 + lvl * 3 / 4));
        }
    cv_blit(0, VIZ_Y);
}

static void drift_draw_layers(void)
{
    uint32_t i;
    for (i = 0; i < DL_N; i++) {
        uint32_t on = (drift_layers >> i) & 1u, x = 2u + i * 34u;
        draw_text_line(x, 60, 34, &AF_S, DRIFT_LAYER[i], on ? D_TEXT : D_DIM, D_BG, 1);
        lcd_fill(x + 6u, 76, 22, 2, on ? D_WARM : D_BG);
    }
}

static void drift_draw_knobs(void)
{
    uint32_t i;
    for (i = 0; i < DK_N; i++) {
        uint32_t x = 6u + i * 58u, w = 54u, f = (uint32_t)drift_knob[i] * w / 127u;
        draw_text_line(x, 182, w, &AF_S, DRIFT_KNOB[i], D_TEXT, D_BG, 1);
        lcd_fill(x, 202, f, 10, D_WARM);
        lcd_fill(x + f, 202, w - f, 10, D_DIM);
    }
}

static void drift_draw(void)
{
    static uint8_t seen = 0xFF, frame;
    char b[24];
    uint32_t n = 0;
    const char *s;
    if (drift_redraw) {
        lcd_fill(0, 0, 240, 240, D_BG);
        draw_text_line(8, 8, 76, &AF_S, DRIFT_WORLDS[drift_world].name, D_TEXT, D_BG, 0);
        draw_text_line(84, 8, 72, &AF_S, DRIFT_PROG[drift_prog % DP_N], D_MID, D_BG, 1);
        draw_text_line(156, 8, 76, &AF_S, DRIFT_STYLE[drift_style % DS_N], D_MID, D_BG, 2);
        draw_text_line(8, 222, 224, &AF_S, drift_oct < 0 ? "OCT -1" : drift_oct > 0 ? "OCT +1" : "", D_DIM, D_BG, 1);
        drift_draw_knobs();
        drift_redraw = 0;
        seen = (uint8_t)(drift_seen + 1u);
    }
    if (seen != drift_seen) {                            /* the chord and the toggles */
        seen = drift_seen;
        if (!drift_run || drift_deg < 0) {
            draw_text_line(0, 30, 240, &AF_M, "PRESS PLAY", D_MID, D_BG, 1);
        } else {
            for (s = DRIFT_ROMAN[drift_deg % 7]; *s && n < 8u; s++)
                b[n++] = *s;
            for (s = drift_bright ? "  BRIGHT" : drift_dark ? "  DARK" : ""; *s && n < 18u; s++)
                b[n++] = *s;
            for (s = drift_hold ? "  HOLD" : ""; *s && n < 23u; s++)
                b[n++] = *s;
            b[n] = 0;
            draw_text_line(0, 30, 240, &AF_M, b, D_TEXT, D_BG, 1);
        }
        drift_draw_layers();
    }
    if (++frame & 1u)                                    /* the lights at ~30 frames a second */
        drift_viz();
}
