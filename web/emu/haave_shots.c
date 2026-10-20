/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dominik Siemon */
/* HAAVE's screen for the website: the browser build (felucca_web.c) compiled natively, played through short scenes
 * (each world playing, the knobs turning, layers joining, the chords, the instruments, the worlds changing, the
 * behaviours, TIDE, the punch-ins, WARP, the sprinkles), its 240 x 240 screen taken 10 times a second. Each scene is written as
 * OUT_DIR/NAME.rgb: its frames one after another, 240 x 240 RGB bytes each (tools/make_haave_shots.py makes them
 * animated WebP).
 *   cc -O2 -ffp-contract=off -w -Ibuild/gen -Ifirmware/src -o build/haave_shots web/emu/haave_shots.c -lm
 *   build/haave_shots OUT_DIR */
#include "felucca_web.c"

#define SHOT_MS 100u                     /* a frame every 100 ms: 10 a second */
static FILE *shot;
static uint32_t shot_left;               /* ms to the next frame */
static char shot_dir[512];

static void take(void)
{
    static uint8_t rgb[240 * 240 * 3];
    uint32_t i;
    for (i = 0; i < 240u * 240u; i++) {
        uint32_t v = web_fb[i], p = ((v >> 8) | (v << 8)) & 0xFFFFu;   /* the panel's byte order -> RGB565 */
        rgb[3u * i] = (uint8_t)(((p >> 11) & 31u) * 255u / 31u);
        rgb[3u * i + 1u] = (uint8_t)(((p >> 5) & 63u) * 255u / 63u);
        rgb[3u * i + 2u] = (uint8_t)((p & 31u) * 255u / 31u);
    }
    fwrite(rgb, sizeof rgb, 1, shot);
}

/* ms of the device's time (in 1 ms steps: a frame on time), a frame each SHOT_MS while a scene is open */
static void run(uint32_t ms)
{
    uint32_t k;
    for (k = 0; k < ms; k++) {
        web_render(44);                                  /* (~1 ms) */
        if (shot && !--shot_left) {
            take();
            shot_left = SHOT_MS;
        }
    }
}

static void open_scene(const char *name)
{
    char path[600];
    snprintf(path, sizeof path, "%s/%s.rgb", shot_dir, name);
    shot = fopen(path, "wb");
    if (!shot) {
        perror(path);
        exit(1);
    }
    shot_left = 1;
}
static void close_scene(void)
{
    fclose(shot);
    shot = 0;
}

static void tap(uint32_t b) { web_buttons(1u << b); run(60); web_buttons(0); run(40); }
static void key(uint32_t k) { web_keys(1u << k); run(60); web_keys(0); run(40); }

/* a fresh start in world w: stopped, the knobs at their start, CALM, OCT 0, the layers given */
static void reset(uint32_t w, uint32_t layers)
{
    static const int16_t KNOB0[DK_N] = {90, 60, 56, 50};
    uint32_t i;
    web_buttons(0);
    web_keys(0);
    if (haave_run)
        tap(B_PLAY);
    run(1500);
    haave_world_next = (uint8_t)w;
    haave_load(w);
    for (i = 0; i < DK_N; i++)
        haave_knob[i] = KNOB0[i];
    haave_apply();
    haave_tide = DT_CALM;
    haave_oct = 0;
    haave_layers = (uint8_t)layers;
    haave_redraw = 1;                                    /* (the whole screen: the knobs' values too) */
    run(3000);                                           /* (the tails of the scene before ring out) */
}

/* white key places (from the left, 0..15) and black key places (0..10) -> key indices from F3 */
static const uint8_t WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
#define L_ALIVE (1u << DL_PAD | 1u << DL_BASS | 1u << DL_SPARKLE | 1u << DL_MELODY | 1u << DL_CHOIR)

int main(int argc, char **argv)
{
    uint32_t w, i;
    if (argc != 2) {
        fprintf(stderr, "usage: haave_shots OUT_DIR\n");
        return 2;
    }
    snprintf(shot_dir, sizeof shot_dir, "%s", argv[1]);
    web_nor_erase();
    web_boot();
    run(2500);                                           /* (the splash) */

    for (w = 0; w < HAAVE_NWORLDS; w++) {                /* each world playing */
        char name[32];
        reset(w, L_ALIVE);
        tap(B_PLAY);
        run(7000);
        snprintf(name, sizeof name, "world%02u", w);
        open_scene(name);
        run(6000);
        close_scene();
    }

    reset(0, L_ALIVE);                                   /* the hero: GLASS */
    tap(B_PLAY);
    run(7000);
    open_scene("hero");
    run(10000);
    close_scene();

    reset(0, 1u << DL_PAD | 1u << DL_MELODY);            /* PLAY: stopped, then going */
    open_scene("play");
    run(2500);
    tap(B_PLAY);
    run(4500);
    close_scene();

    reset(0, L_ALIVE);                                   /* the knobs: SPACE, GLOW, MOTION up and back */
    tap(B_PLAY);
    run(5000);
    open_scene("knobs");
    for (i = 0; i < 4u; i++) {
        uint32_t k, r = i == 1u ? DK_ECHO : i == 2u ? DK_GLOW : i == 3u ? DK_MOTION : DK_SPACE;
        for (k = 0; k < 12u; k++) {
            web_enc(EN_K1 + r, 2);
            run(60);
        }
        run(500);
        for (k = 0; k < 12u; k++) {
            web_enc(EN_K1 + r, -2);
            run(60);
        }
        run(300);
    }
    close_scene();

    reset(0, 1u << DL_PAD);                              /* the layers: joining one by one, then going */
    tap(B_PLAY);
    run(4000);
    open_scene("layers");
    run(800);
    for (i = 1; i < DL_N; i++) {
        key(WHITE[i]);
        run(900);
    }
    for (i = DL_N - 1u; i > 3u; i--) {
        key(WHITE[i]);
        run(600);
    }
    close_scene();

    reset(0, L_ALIVE);                                   /* the black keys: chords, plain then coloured */
    tap(B_PLAY);
    run(4000);
    haave_tide = DT_STILL;
    open_scene("chords");
    run(600);
    {   /* (the third again: coloured) */
        static const uint8_t SEQ[6] = {0, 2, 2, 3, 1, 4};
        for (i = 0; i < 6u; i++) {
            key(BLACK[SEQ[i]]);
            run(1500);
        }
    }
    close_scene();

    reset(0, 1u << DL_PAD | 1u << DL_MELODY);            /* the instruments: SYNTH held notes, BELLS, BASS */
    tap(B_PLAY);
    run(4000);
    open_scene("playkeys");
    tap(B_SAVE);
    run(600);
    web_keys(1u << WHITE[9]);
    run(900);
    web_keys(1u << WHITE[9] | 1u << WHITE[12]);
    run(1200);
    web_keys(0);
    run(600);
    tap(B_ARP);
    run(500);
    key(WHITE[7]);
    run(500);
    for (i = 8; i < 16u; i++) {
        key(WHITE[i]);
        run(160);
    }
    run(600);
    tap(B_SEQ);
    run(500);
    for (i = 0; i < 4u; i++) {
        web_keys(1u << WHITE[7u + (i * 3u) % 9u]);
        run(450);
        web_keys(0);
        run(150);
    }
    run(800);
    tap(B_ARP);
    close_scene();

    reset(0, L_ALIVE);                                   /* the worlds: PRESETS turning */
    tap(B_PLAY);
    run(4000);
    open_scene("presets");
    run(800);
    for (i = 0; i < 3u; i++) {
        web_enc(EN_PRESET, 1);
        run(4800);
    }
    close_scene();

    reset(0, L_ALIVE);                                   /* ALGORITHM: each behaviour's picture */
    tap(B_PLAY);
    run(4000);
    open_scene("algo");
    for (i = 0; i < DS_N; i++) {
        web_enc(EN_ALGO, 1);
        run(1800);
    }
    close_scene();

    reset(0, L_ALIVE);                                   /* SELECT: TIDE from STILL to WANDERING */
    tap(B_PLAY);
    run(4000);
    haave_tide = DT_STILL;
    open_scene("tide");
    web_enc(EN_SELECT, -1);
    run(2000);
    for (i = 1; i < DT_N; i++) {
        web_enc(EN_SELECT, 1);
        run(2000);
    }
    close_scene();

    reset(0, L_ALIVE);                                   /* the punch-ins: PUMP held, then BREATHE */
    tap(B_PLAY);
    run(4000);
    open_scene("punch");
    run(700);
    web_buttons(1u << B_SCL);
    run(5000);
    web_buttons(0);
    run(1800);
    web_buttons(1u << B_GLO);
    run(4500);
    web_buttons(0);
    run(2500);
    close_scene();

    reset(0, 1u << DL_PAD | 1u << DL_ARP | 1u << DL_MELODY);   /* WARP: HOME held, the music at half speed */
    tap(B_PLAY);
    run(4000);
    open_scene("warp");
    run(1500);
    web_buttons(1u << B_HOME);
    run(4500);
    web_buttons(0);
    run(2500);
    close_scene();

    reset(8, 1u << DL_PAD | 1u << DL_MELODY);            /* the sprinkles: FOREST's birds, leaves, owl, crickets */
    tap(B_PLAY);
    run(4000);
    open_scene("sprinkles");
    run(500);
    for (i = 0; i < 4u; i++) {
        key(BLACK[5u + i]);
        run(2600);
    }
    close_scene();

    printf("shots: %s\n", shot_dir);
    return 0;
}
