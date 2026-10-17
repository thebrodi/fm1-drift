/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dominik Siemon */
/* DRIFT: the panel and the screen (the main loop). With FELUCCA_DRIFT, felucca.c (and the browser build) call these in
 * place of Felucca's ui_input / ui_leds / ui_draw; Felucca's UI is still built (main.c's crash, update and UBOOT
 * screens use its drawing) but never runs. One page, no menus: every control does one thing (DRIFT.md).
 *   KNOB 1..4  SPACE (reverb), ECHO (delay), GLOW (FM brightness), MOTION (chorus and drift)
 *   OCT- OCT+  the register, one octave down / up
 *   PRESETS    the sound (one for now)
 * The keys play in the audio ISR (drift_play.c). */

/* -------------------------------------------------------------- sound --- */
typedef struct {
    const char *name;
    uint8_t pad_preset, bell_preset;     /* FM6 factory presets (eng_fm6.c FM6_PRESETS): PAD, BELL */
} drift_sound_t;
static const drift_sound_t DRIFT_SOUNDS[] = {
    {"GLASS PAD", 4, 1},
};
#define DRIFT_NSOUNDS (sizeof DRIFT_SOUNDS / sizeof DRIFT_SOUNDS[0])

enum { DK_SPACE, DK_ECHO, DK_GLOW, DK_MOTION, DK_N };
static const char *const DRIFT_KNOB[DK_N] = {"SPACE", "ECHO", "GLOW", "MOTION"};
static int16_t drift_knob[DK_N] = {90, 60, 56, 50};   /* 0..127 */
static uint8_t drift_sound, drift_ready, drift_redraw = 1;

/* the knobs -> the two parts and the effect buses */
static void drift_apply(void)
{
    track_t *pad = &trk[DRIFT_PAD], *bell = &trk[DRIFT_BELL];
    int32_t sp = drift_knob[DK_SPACE], ec = drift_knob[DK_ECHO], gl = drift_knob[DK_GLOW], mo = drift_knob[DK_MOTION];
    pad->p[P_REV] = (int16_t)sp;
    bell->p[P_REV] = (int16_t)(sp + (127 - sp) / 3);
    song.g[G_RSIZE] = (int16_t)(60 + sp / 2);
    pad->p[P_DLY] = (int16_t)(ec / 2);
    bell->p[P_DLY] = (int16_t)ec;
    song.g[G_DFDBK] = (int16_t)(30 + ec * 3 / 5);
    pad->p[P_E0 + 2] = (int16_t)(gl - 64);               /* FM6 MLVL: the modulators' level, -64..63 */
    bell->p[P_E0 + 2] = (int16_t)(gl - 64);
    pad->p[P_CHOR] = (int16_t)mo;
    pad->p[P_E0 + 6] = (int16_t)(mo / 2);                /* FM6 DTUN: the operators drift apart */
    pad->p[P_SPRD] = (int16_t)(40 + mo / 2);
}

static void drift_load(uint32_t s)
{
    const drift_sound_t *d = &DRIFT_SOUNDS[s % DRIFT_NSOUNDS];
    track_t *pad = &trk[DRIFT_PAD], *bell = &trk[DRIFT_BELL];
    uint32_t i;
    drift_sound = (uint8_t)(s % DRIFT_NSOUNDS);
    set_engine_of(pad, ENGI_FM6);
    apply_preset_to(pad, d->pad_preset);
    set_engine_of(bell, ENGI_FM6);
    apply_preset_to(bell, d->bell_preset);
    pad->p[P_VOICE] = V_POLY;
    pad->p[P_LEVEL] = 100;
    pad->p[P_ATK] = 72;                                  /* slow swell in, long tail out */
    pad->p[P_DEC] = 90;
    pad->p[P_SUS] = 112;
    pad->p[P_REL] = 104;
    bell->p[P_VOICE] = V_POLY;
    bell->p[P_LEVEL] = 78;
    bell->p[P_REL] = 100;
    bell->p[P_PAN] = 20;
    for (i = 2; i < NTRK; i++)                           /* the other parts are silent */
        trk[i].p[P_MUTE] = 1;
    song.g[G_RTYPE] = 2;                                 /* HALL */
    song.g[G_RDAMP] = 72;
    song.g[G_BPM] = 72;                                  /* (the delay's 1/4: 0.83 s) */
    song.g[G_DTIME] = 0;
    song.g[G_DCOLOR] = 44;                               /* darker repeats */
    song.g[G_DMIX] = 90;
    drift_apply();
    drift_redraw = 1;
}

/* ------------------------------------------------------------- input --- */
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
    if ((s = panel_enc(EN_PRESET)) != 0)
        drift_load((uint32_t)((int32_t)drift_sound + s + (int32_t)DRIFT_NSOUNDS * 8));
    panel_enc(EN_ALGO);                                  /* (styles and patterns: not yet) */
    panel_enc(EN_SELECT);
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
/* the held keys lit, every white key glowing; OCT- / OCT+ lit while shifted */
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
        led_put(nl, 14u + k, (int)((fm1_in.notes >> k) & 1u));
        led_put(nd, 14u + k, !key_black(k));
    }
    led_put(nl, panel.btn[B_OCTDN], drift_oct < 0);
    led_put(nl, panel.btn[B_OCTUP], drift_oct > 0);
    fm1_led_dim_level(1);
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led_dim[c] = nd[c];
        fm1_led_breath[c] = 0;
        fm1_led_mid[c] = 0;
        fm1_led[c] = nl[c];
    }
}

/* ------------------------------------------------------------ screen --- */
#define D_BG RGB(12, 14, 30)
#define D_TEXT RGB(226, 220, 255)
#define D_MID RGB(126, 126, 178)
#define D_DIM RGB(44, 46, 82)
#define D_WARM RGB(255, 196, 150)
#define D_COOL RGB(150, 190, 255)
#define VIZ_Y 92u
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

/* the sounding voices as drifting lights: across by pitch, the height a slow wave, the size their level */
static void drift_viz(void)
{
    static uint32_t ph;
    uint32_t p, i;
    int32_t x;
    ph += 1u;
    cv_begin(240, VIZ_H, D_BG);
    for (x = 0; x < 240; x += 3) {                       /* the horizon: a slow, faint wave */
        int32_t y = (int32_t)VIZ_H / 2 + (SINE[((uint32_t)x * 6u + ph * 2u) & 1023u] * 6) / 32768;
        cv_pset(x, y, D_DIM);
    }
    for (p = 0; p < 2u; p++)
        for (i = 0; i < NVOICE; i++) {
            const voice_t *v = &trk[p].v[i];
            int32_t lvl, cx, cy;
            if (!v->active)
                continue;
            lvl = v->env_out >> 10;                      /* Q15 -> 0..32 */
            if (lvl < 1)
                continue;
            cx = ((int32_t)v->note - 30) * 240 / 72;
            cy = (int32_t)VIZ_H / 2 + (SINE[(v->note * 97u + ph * (3u + p)) & 1023u] * 22) / 32768;
            d_glow(cx, cy, 3 + lvl / 3 + (p ? 0 : 3), p ? D_WARM : D_COOL, (uint32_t)(8 + lvl * 3 / 4));
        }
    cv_blit(0, VIZ_Y);
}

static void drift_draw_knobs(void)
{
    uint32_t i;
    for (i = 0; i < DK_N; i++) {
        uint32_t x = 8u + i * 58u, w = 50u, f = (uint32_t)drift_knob[i] * w / 127u;
        draw_text_line(x, 190, w + 4u, &AF_S, DRIFT_KNOB[i], D_MID, D_BG, 0);
        lcd_fill(x, 212, f, 4, D_TEXT);
        lcd_fill(x + f, 212, w - f, 4, D_DIM);
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
        draw_text_line(12, 10, 100, &AF_S, "DRIFT", D_MID, D_BG, 0);
        draw_text_line(120, 10, 108, &AF_S, DRIFT_SOUNDS[drift_sound].name, D_TEXT, D_BG, 2);
        draw_text_line(12, 226, 216, &AF_S, drift_oct < 0 ? "OCT -1" : drift_oct > 0 ? "OCT +1" : "", D_DIM, D_BG, 1);
        drift_draw_knobs();
        drift_redraw = 0;
        seen = (uint8_t)(drift_seen + 1u);
    }
    if (seen != drift_seen) {                            /* the chord: its degree and colour */
        seen = drift_seen;
        if (drift_deg < 0) {
            s = "PLAY A WHITE KEY";
            draw_text_line(0, 44, 240, &AF_S, s, D_MID, D_BG, 1);
            lcd_fill(0, 62, 240, 18, D_BG);
        } else {
            for (s = DRIFT_ROMAN[drift_deg]; *s && n < 8u; s++)
                b[n++] = *s;
            if (drift_col) {
                b[n++] = ' ';
                for (s = DRIFT_COLOUR[drift_col]; *s && n < 20u; s++)
                    b[n++] = *s;
            }
            b[n] = 0;
            draw_text_line(0, 40, 240, &AF_M, b, D_TEXT, D_BG, 1);
            lcd_fill(0, 62, 240, 18, D_BG);
        }
    }
    if (++frame & 1u)                                    /* the lights at ~30 frames a second */
        drift_viz();
}
