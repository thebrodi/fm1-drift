/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dominik Siemon */
/* DRIFT, heard without a browser: the browser build (felucca_web.c) compiled natively, pressing PLAY, adding the layers
 * one by one, then each behaviour, a progression and the black-key chords (it prints when each starts), written as a 16-bit
 * stereo WAV, with its peak and RMS printed.
 *   cc -O2 -ffp-contract=off -w -Ibuild/gen -Ifirmware/src -o build/drift_render web/emu/drift_render.c -lm
 *   build/drift_render OUT.wav */
#include "felucca_web.c"

static FILE *out;
static uint32_t frames;
static double sum2, peak;

static void render(double ms)
{
    uint32_t n = (uint32_t)(ms * 44.1 + 0.5), k, i;
    for (k = 0; k < n; k += 128u) {
        uint32_t m = n - k < 128u ? n - k : 128u;
        web_render(m);
        for (i = 0; i < m; i++) {
            float s[2] = {out_l[i], out_r[i]};
            int16_t q[2];
            uint32_t c;
            for (c = 0; c < 2u; c++) {
                double a = s[c] < 0 ? -s[c] : s[c];
                peak = a > peak ? a : peak;
                sum2 += (double)s[c] * s[c];
                q[c] = (int16_t)(s[c] > 1.0f ? 32767 : s[c] < -1.0f ? -32767 : s[c] * 32767.0f);
            }
            fwrite(q, sizeof q, 1, out);
            frames++;
        }
    }
}

static void put32(uint32_t v) { fputc(v & 255, out); fputc(v >> 8 & 255, out); fputc(v >> 16 & 255, out); fputc(v >> 24, out); }
static void put16(uint32_t v) { fputc(v & 255, out); fputc(v >> 8 & 255, out); }

/* the white key of layer l and the black key at place j (keys from F3 = 0): places 0..4 vi ii IV V I, 5..9 the same
 * chords add9 */
static const uint8_t WHITE[7] = {0, 2, 4, 6, 7, 9, 11}, BLACK[10] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22};
static void tap(uint32_t k)
{
    web_keys(1u << k);
    render(60);
    web_keys(0);
    render(40);
}
static void play(void)
{
    web_buttons(1u << B_PLAY);
    render(20);
    web_buttons(0);
}
static void mark(const char *what)
{
    printf("%6.1f s  %s\n", frames / (double)FS, what);
}

int main(int argc, char **argv)
{
    uint32_t i;
    if (argc < 2 || !(out = fopen(argv[1], "wb")))
        return 2;
    fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, out);
    put32(16); put16(1); put16(2); put32(FS); put32(FS * 4u); put16(4); put16(16);
    fwrite("data\0\0\0\0", 1, 8, out);
    web_nor_erase();
    web_boot();
    render(600);                                   /* the splash */
    frames = 0;
    sum2 = peak = 0;
    mark("PLAY: PAD + MELODY, GLASS, HOME, DRIFT");
    play();
    render(14000);
    for (i = 1; i < 7u; i++) {                     /* the other layers, one by one */
        static const char *const N[7] = {"", "+ BASS", "+ ARP", "+ SPARKLE", "- MELODY", "+ CHOIR", "+ SHIMMER"};
        mark(N[i]);
        tap(WHITE[i == 4u ? 4u : i]);
        render(7000);
    }
    for (i = 1; i < DS_N; i++) {                   /* every behaviour */
        drift_style = (uint8_t)i;
        mark(DRIFT_STYLE[i]);
        render(10000);
    }
    drift_style = DS_RIPPLE;
    drift_prog = 3;
    mark("RIPPLE, LONGING; then V held 6 s on a black key, then vi add9 tapped");
    render(8000);
    web_keys(1u << BLACK[3]);
    render(6000);
    web_keys(0);
    render(3000);
    tap(BLACK[5]);
    render(9000);
    mark("stop");
    play();
    render(5000);                                  /* the tails */
    fseek(out, 4, SEEK_SET);
    put32(36u + frames * 4u);
    fseek(out, 40, SEEK_SET);
    put32(frames * 4u);
    fclose(out);
    printf("%u frames, peak %.3f (%.1f dBFS), rms %.1f dBFS\n", frames, peak, 20.0 * log10(peak + 1e-9),
           10.0 * log10(sum2 / (2.0 * frames) + 1e-12));
    return 0;
}
