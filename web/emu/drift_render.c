/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dominik Siemon */
/* DRIFT, heard without a browser: the browser build (felucca_web.c) compiled natively, playing a short progression on
 * the keys (I - vi - IV - Vsus4), then the player in each style (DRIFT RIPPLE SWELL CHIME PULSE, 8 s each), written as a 16-bit stereo WAV, with its peak and RMS printed.
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

/* key k of the 27 (0 = F3) held for ms, with black key b (-1 none) */
static void chord(uint32_t k, int32_t b, double ms)
{
    web_keys(1u << k | (b >= 0 ? 1u << b : 0u));
    render(ms);
}

int main(int argc, char **argv)
{
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
    chord(12, -1, 3000);                           /* I   (white place 7: keys from F3, F3 = 0) */
    chord(21, -1, 3000);                           /* vi  (place 12) */
    chord(18, -1, 3000);                           /* IV  (place 10) */
    chord(19, 20, 3000);                           /* V   (place 11) with black place 8: SUS4 */
    web_keys(0);
    render(2000);
    {   /* the player: each style for 8 s over a chord tapped at its start (I, vi, IV, ii, V) */
        static const uint8_t KEY[DS_N] = {12, 21, 18, 14, 19};
        uint32_t st;
        web_buttons(1u << B_PLAY);
        render(20);
        web_buttons(0);
        for (st = 0; st < DS_N; st++) {
            drift_style = (uint8_t)st;
            chord(KEY[st], -1, 300);
            web_keys(0);
            render(7700);
        }
        web_buttons(1u << B_PLAY);
        render(20);
        web_buttons(0);
    }
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
