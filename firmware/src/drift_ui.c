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
 * The keys (layers, chords) and the music run in the audio ISR (drift_play.c). */

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
static uint8_t drift_world, drift_ready;
static uint8_t drift_redraw = 1;         /* the whole screen */
static uint8_t drift_head, drift_cards = 0xF, drift_strip;   /* the header, the knob cards (bit per knob), the strip */
static const char *drift_toast;          /* a name shown over the scene a moment (PRESETS ALGORITHM SELECT turned) */
static uint32_t drift_toast_ms;

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
    drift_toast = d->name;
    drift_toast_ms = fm1_ms;
}

/* ------------------------------------------------------------- input --- */
static int32_t drift_turn(uint32_t role)                 /* a list knob: one step per turn, whatever its speed */
{
    int32_t s = panel_enc(role);
    return s > 0 ? 1 : s < 0 ? -1 : 0;
}

static void drift_say(const char *s)
{
    drift_toast = s;
    drift_toast_ms = fm1_ms;
}

static void drift_input(void)
{
    uint32_t pressed = fm1_input_edges(0), i;
    int32_t s;
    fm1_input_note_edges();                              /* (the keys are drift_play.c's) */
    if (!drift_ready) {                                  /* first pass: after the boot and any autosave restore */
        drift_ready = 1;
        drift_load(0);
        drift_toast = 0;
    }
    for (i = 0; i < DK_N; i++)
        if ((s = panel_enc(EN_K1 + i)) != 0) {
            int32_t v = drift_knob[i] + 2 * s;
            drift_knob[i] = (int16_t)(v < 0 ? 0 : v > 127 ? 127 : v);
            drift_apply();
            drift_cards |= (uint8_t)(1u << i);
        }
    if ((s = drift_turn(EN_PRESET)) != 0)
        drift_load((uint32_t)((int32_t)drift_world + s + (int32_t)DRIFT_NWORLDS));
    if ((s = drift_turn(EN_ALGO)) != 0) {
        drift_style = (uint8_t)(((int32_t)drift_style + s + DS_N) % DS_N);
        drift_say(DRIFT_STYLE[drift_style]);
        drift_strip = 1;
    }
    if ((s = drift_turn(EN_SELECT)) != 0) {
        drift_prog = (uint8_t)(((int32_t)drift_prog + s + (int32_t)DP_N) % (int32_t)DP_N);
        drift_say(DRIFT_PROG[drift_prog]);
        drift_strip = 1;
    }
    if ((pressed >> panel.btn[B_PLAY]) & 1u) {
        drift_run = !drift_run;
        drift_head = drift_strip = 1;
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
                 (drift_run && drift_deg == DRIFT_BLACK[p % 5u] && drift_bright == (p >= 5u));
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
/* One page (DRIFT.md):
 *     0  header: play state, world, key and tempo, octave
 *    26  the four knob cards: name, value, bar (the biggest thing on the screen: the knobs come first)
 *    76  the world's scene, the notes in it as lights | the layers, each with its activity
 *   194  the progression: its chords, the one now, the bars it has left; the behaviour
 * Each part redraws only when it changes, the scene and the layers as they move. */
#define D_BG RGB(14, 17, 36)
#define D_PANEL RGB(21, 25, 54)
#define D_LINE RGB(42, 45, 85)
#define D_TEXT RGB(230, 226, 255)
#define D_MID RGB(154, 154, 208)
#define D_DIM RGB(74, 78, 134)
#define D_SKY RGB(133, 183, 235)
#define D_LILAC RGB(175, 169, 236)
#define D_ROSE RGB(237, 147, 177)
#define D_MINT RGB(159, 225, 203)
#define D_PEACH RGB(245, 196, 179)
#define D_VIOLET RGB(127, 119, 221)
#define SCN_X 6u
#define SCN_Y 76u
#define SCN_W 158u
#define SCN_H 112u
#define LAY_X 168u
#define LAY_W 66u

static const char *const DRIFT_ROMAN[7] = {"I", "ii", "iii", "IV", "V", "vi", "vii"};
static const char *const DRIFT_KEYNAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const uint16_t DRIFT_KCOL[DK_N] = {D_SKY, D_LILAC, D_ROSE, D_MINT};
static const uint16_t DRIFT_LCOL[DL_N] = {D_SKY, D_VIOLET, D_PEACH, RGB(244, 192, 209), D_ROSE, D_LILAC, D_MINT};
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

static int32_t d_sin(uint32_t ph, int32_t amp) { return SINE[ph & 1023u] * amp / 32768; }

/* a rounded panel with a hairline border, filling the canvas */
static void d_panel(uint32_t w, uint32_t h)
{
    cv_begin(w, h, D_BG);
    cv_rrect(0, 0, (int32_t)w, (int32_t)h, -4, D_LINE, D_BG);
    cv_rrect(1, 1, (int32_t)w - 2, (int32_t)h - 2, -3, D_PANEL, D_LINE);
}

static void drift_draw_head(void)
{
    char b[16];
    uint32_t n = 0;
    const char *s;
    const drift_world_t *w = &DRIFT_WORLDS[drift_world];
    cv_begin(240, 22, D_BG);
    if (drift_run) {                                     /* the play mark: solid while playing */
        int32_t y;
        for (y = 0; y < 9; y++)
            cv_rect(8, 6 + y, 1 + (y < 5 ? y : 8 - y), 1, D_MINT);
    } else
        cv_frame(8, 6, 6, 9, D_DIM);
    cv_text(20, 4, &AF_S, w->name, D_TEXT);
    for (s = DRIFT_KEYNAME[w->root % 12u]; *s; s++)
        b[n++] = *s;
    b[n++] = ' ';
    b[n++] = (char)('0' + w->bpm / 100u % 10u);
    if (w->bpm < 100u)
        n--;
    b[n++] = (char)('0' + w->bpm / 10u % 10u);
    b[n++] = (char)('0' + w->bpm % 10u);
    for (s = " BPM"; *s; s++)
        b[n++] = *s;
    b[n] = 0;
    cv_text(120 - text_w(&AF_S, b) / 2, 4, &AF_S, b, D_MID);
    cv_text_r(232, 4, &AF_S, drift_oct < 0 ? "OCT -1" : drift_oct > 0 ? "OCT +1" : "OCT 0", D_MID, D_BG);
    cv_rect(6, 21, 228, 1, D_LINE);
    cv_blit(0, 0);
}

static void drift_draw_card(uint32_t i)
{
    char b[4];
    uint32_t v = (uint32_t)drift_knob[i] * 100u / 127u, x;
    d_panel(54, 44);
    cv_text(6, 3, &AF_S, DRIFT_KNOB[i], D_MID);
    b[0] = (char)(v >= 100u ? '1' : '0' + v / 10u);
    b[1] = (char)(v >= 100u ? '0' : '0' + v % 10u);
    b[2] = v >= 100u ? '0' : 0;
    b[3] = 0;
    x = (uint32_t)cv_text_on(6, 17, &AF_M, v < 10u ? b + 1 : b, DRIFT_KCOL[i], D_PANEL);
    cv_text_on((int32_t)x + 1, 20, &AF_S, "%", DRIFT_KCOL[i], D_PANEL);
    cv_rrect(6, 36, 42, 3, -1, D_LINE, D_PANEL);
    if (v)
        cv_rrect(6, 36, (int32_t)(42u * v / 100u) ? (int32_t)(42u * v / 100u) : 1, 3, -1, DRIFT_KCOL[i], D_LINE);
    cv_blit(6u + i * 58u, 26);
}

/* ---- the scenes: GLASS orbits, EMBER a drifting cloud, DUSK a dune under the moon ---- */
static uint32_t drift_ph;                                /* the scenes' clock: one step a drawn frame */

/* where a voice of part p, note n, sits in the scene (canvas pixels) */
static void scene_pos(uint32_t p, uint32_t n, int32_t *x, int32_t *y)
{
    int32_t w = (int32_t)SCN_W, h = (int32_t)SCN_H;
    uint32_t a;
    switch (drift_world) {
    case 0:                                              /* GLASS: on two tilted orbits, the high notes inside */
        a = n * 53u + drift_ph * (2u + p);
        *x = w / 2 + d_sin(a + 256u, n > 66u ? 38 : 62);
        *y = h / 2 + d_sin(a, n > 66u ? 14 : 24) + d_sin(a + 256u, 6);
        break;
    case 1:                                              /* EMBER: rising from the cloud, left to right by pitch */
        *x = ((int32_t)n - 28) * w / 76 + d_sin(drift_ph * 3u + n * 41u, 5);
        *y = h - 22 - (int32_t)((drift_ph / 2u + n * 37u) % 70u);
        break;
    default:                                             /* DUSK: stars in the sky, the low notes on the dune */
        *x = ((int32_t)n - 28) * w / 76;
        *y = n < 50u ? h - 26 + d_sin(n * 90u, 4) : 16 + (int32_t)((n * 29u) % 48u);
        break;
    }
}

static void scene_back(void)
{
    int32_t x, w = (int32_t)SCN_W, h = (int32_t)SCN_H;
    uint32_t i, r = 0x9E3779B9u;
    switch (drift_world) {
    case 0:                                              /* GLASS: the orbits, dotted */
        for (i = 0; i < 1024u; i += 8u) {
            d_add(w / 2 + d_sin(i + 256u, 62), h / 2 + d_sin(i, 24) + d_sin(i + 256u, 6), D_LILAC, 10);
            d_add(w / 2 + d_sin(i + 256u, 38), h / 2 + d_sin(i, 14) + d_sin(i + 256u, 6), D_LILAC, 8);
        }
        d_glow(w / 2 + d_sin(drift_ph, 4), h / 2, 15, D_SKY, 14);          /* the planet */
        break;
    case 1:                                              /* EMBER: three soft clouds drifting */
        for (i = 0; i < 3u; i++)
            d_glow(w / 2 + d_sin(drift_ph + i * 340u, 44), h - 24 + d_sin(drift_ph * 2u + i * 200u, 6), 28,
                   i == 1u ? D_ROSE : D_LILAC, 9);
        break;
    default:                                             /* DUSK: stars, the moon, the dune */
        for (i = 0; i < 40u; i++) {
            r = r * 1664525u + 1013904223u;
            d_add((int32_t)(r >> 8) % w, (int32_t)(r >> 20) % (h - 40), D_TEXT,
                  6u + ((r >> 4) + drift_ph / 4u) % 9u);
        }
        d_glow(w - 30, 22, 13, D_TEXT, 18);
        for (x = 0; x < w; x += 2) {
            int32_t top = h - 34 + d_sin((uint32_t)x * 5u + drift_ph / 2u, 7) + d_sin((uint32_t)x * 13u, 3), y;
            for (y = top; y < h; y += 3)
                d_add(x + (y & 2), y, D_SKY, (uint32_t)(8 + (y - top) / 3));
        }
        break;
    }
}

static void drift_draw_scene(void)
{
    uint32_t p, i;
    drift_ph++;
    d_panel(SCN_W, SCN_H);
    scene_back();
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++) {
            const voice_t *v = &trk[p].v[i];
            int32_t lvl, x, y;
            if (!v->active || (lvl = v->env_out >> 10) < 1)       /* (Q15 -> 0..32) */
                continue;
            scene_pos(p, v->note, &x, &y);
            d_glow(x, y, 4 + lvl / 3 + (p == DRIFT_PAD ? 4 : 0), DRIFT_PCOL[p], (uint32_t)(10 + lvl / 2));
        }
    if (drift_toast && fm1_ms - drift_toast_ms < 1300u) {   /* a name just turned to */
        int32_t tw = text_w(&AF_M, drift_toast) + 16;
        cv_rrect(((int32_t)SCN_W - tw) / 2, (int32_t)SCN_H / 2 - 13, tw, 26, -4, D_BG, D_PANEL);
        cv_text_on(((int32_t)SCN_W - tw) / 2 + 8, (int32_t)SCN_H / 2 - 9, &AF_M, drift_toast, D_TEXT, D_BG);
    } else
        drift_toast = 0;
    cv_blit(SCN_X, SCN_Y);
}

static void drift_draw_layers(void)
{
    static uint8_t act[DL_N];
    uint32_t i;
    d_panel(LAY_W, SCN_H);
    cv_text_on(6, 3, &AF_S, "LAYERS", D_MID, D_PANEL);
    for (i = 0; i < DL_N; i++) {
        uint32_t on = (drift_layers >> i) & 1u, y = 19u + i * 13u, a;
        if (drift_act[i] > act[i])                       /* what the ISR just played, then a slow fade */
            act[i] = drift_act[i];
        drift_act[i] = 0;
        act[i] = (uint8_t)(act[i] * 7u / 8u);
        if (on)
            cv_rrect(5, (int32_t)y + 5, 5, 5, -2, DRIFT_LCOL[i], D_PANEL);
        else
            cv_frame(5, (int32_t)y + 5, 5, 5, D_DIM);
        cv_text_on(13, (int32_t)y, &AF_S, DRIFT_LAYER[i], on ? D_TEXT : D_DIM, D_PANEL);
        a = on && drift_run ? 2u + act[i] * 14u / 255u : 0u;
        if (a)
            cv_rrect(48, (int32_t)y + 6, (int32_t)a, 3, -1, DRIFT_LCOL[i], D_PANEL);
    }
    cv_blit(LAY_X, SCN_Y);
}

static void drift_draw_strip(void)
{
    const drift_beh_t *bh = &DRIFT_BEH[drift_style % DS_N];
    const int8_t *pr = PROG[drift_prog % DP_N];
    uint32_t i, x = 64, bars = bh->bars, bar;
    cv_begin(240, 46, D_BG);
    cv_rect(6, 0, 228, 1, D_LINE);
    cv_text(8, 6, &AF_S, DRIFT_PROG[drift_prog % DP_N], D_MID);
    cv_text_r(232, 6, &AF_S, DRIFT_STYLE[drift_style % DS_N], D_MID, D_BG);
    if (!drift_run) {
        cv_text(120 - text_w(&AF_M, "PRESS PLAY") / 2, 22, &AF_M, "PRESS PLAY", D_TEXT);
        cv_blit(0, 194);
        return;
    }
    if (drift_prog % DP_N == DP_WANDER || drift_pin != 0xFF || drift_bright) {   /* the chord now, and why */
        char b[16];
        uint32_t n = 0;
        const char *s;
        for (s = DRIFT_ROMAN[drift_deg < 0 ? 0 : drift_deg % 7]; *s; s++)
            b[n++] = *s;
        for (s = drift_bright ? " add9" : ""; *s; s++)
            b[n++] = *s;
        b[n] = 0;
        x = (uint32_t)cv_text(64, 21, &AF_M, b, D_TEXT);
        cv_rect(64, 39, (int32_t)x - 64, 2, D_MINT);
        cv_text((int32_t)x + 8, 24, &AF_S, drift_pin != 0xFF ? "HELD" : drift_prog % DP_N == DP_WANDER ?
                                       "WANDERING" : "", D_DIM);
    } else
        for (i = 0; i < 5u && pr[i] >= 0; i++) {         /* the progression, the chord now lit */
            uint32_t now = i == drift_pos, x1 = (uint32_t)cv_text((int32_t)x, 21, &AF_M, DRIFT_ROMAN[pr[i]],
                                                                  now ? D_TEXT : D_DIM);
            if (now)
                cv_rect((int32_t)x - 2, 39, (int32_t)(x1 - x) + 4, 2, D_MINT);
            x = x1 + 14u;
        }
    bar = (drift_tick - drift_since) / 16u;              /* the chord's bars: done, now, to come */
    for (i = 0; i < bars && i < 8u; i++)
        cv_rrect(8 + (int32_t)i * 12, 30, 9, 3, -1, drift_pin != 0xFF ? D_DIM : i <= bar ? D_MINT : D_LINE, D_BG);
    cv_blit(0, 194);
}

/* the splash (main.c, in place of Felucca's): a planet, DRIFT, a line */
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
    static uint8_t seen = 0xFF, frame, bar_was = 0xFF;
    uint32_t i, bar = drift_run ? (drift_tick - drift_since) / 16u : 0xFFu;
    if (drift_redraw) {
        lcd_fill(0, 0, 240, 240, D_BG);
        drift_redraw = 0;
        drift_head = drift_strip = 1;
        drift_cards = 0xF;
    }
    if (drift_head) {
        drift_head = 0;
        drift_draw_head();
    }
    for (i = 0; i < DK_N; i++)
        if ((drift_cards >> i) & 1u) {
            drift_cards &= (uint8_t)~(1u << i);
            drift_draw_card(i);
        }
    if (seen != drift_seen || bar != bar_was || drift_strip) {
        seen = drift_seen;
        bar_was = (uint8_t)bar;
        drift_strip = 0;
        drift_draw_strip();
    }
    frame++;
    if (frame & 1u)                                      /* the scene at ~30 frames a second, the layers at ~15 */
        drift_draw_scene();
    else if (frame & 2u)
        drift_draw_layers();
}
