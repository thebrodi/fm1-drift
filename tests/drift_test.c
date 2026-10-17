/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dominik Siemon */
/* DRIFT's checks: the browser build (web/emu/felucca_web.c) compiled natively and played by a restless player.
 *   1. every world x behaviour with every layer on: no clipping, no clicks, no DC, never silent, levels in a band
 *   2. half an hour (simulated) of random playing: PLAY, layers, black-key chords (tapped and held), the seven knobs,
 *      OCT; the output always finite, below clipping, no clicks; every note in range (the bass from E1, the voice
 *      E2..C5, the bells up to C7); never more voices than the 8;
 *      while PLAY and the pad are on, never 3 s of silence; 30..40 s after a stop, the output below -60 dBFS (no
 *      stuck note; an FM6 voice is judged by what it sounds: its own envelopes end it, a bowl's tail runs long)
 *   cc -O2 -ffp-contract=off -w -Ibuild/gen -Ifirmware/src -Iweb/emu -o build/drift_test tests/drift_test.c -lm
 *   build/drift_test [MINUTES]          exit 0: all passed */
#include "felucca_web.c"

static uint32_t fails, t_seed = 12345u;
static uint32_t rnd(uint32_t n) { t_seed = t_seed * 1664525u + 1013904223u; return n ? (t_seed >> 8) % n : 0u; }
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static double pk, s2, s1, jump;          /* the last run: peak, sum of squares, sum, largest sample step */
static uint64_t nf;
static float last_l, last_r;
static uint32_t bad_note, too_many;

/* render ms of audio, checking every sample and the voices after each block */
static void run(double ms)
{
    uint32_t n = (uint32_t)(ms * 44.1 + 0.5), k, i, p, v;
    for (k = 0; k < n; k += 128u) {
        web_render(128);
        for (i = 0; i < 128u; i++) {
            float l = out_l[i], r = out_r[i];
            double a = fabs(l) > fabs(r) ? fabs(l) : fabs(r), d1 = fabs(l - last_l), d2 = fabs(r - last_r);
            if (!isfinite(l) || !isfinite(r))
                a = 99.0;
            pk = a > pk ? a : pk;
            jump = d1 > jump ? d1 : d2 > jump ? d2 : jump;
            s2 += (double)l * l + (double)r * r;
            s1 += l + r;
            nf += 2;
            last_l = l;
            last_r = r;
        }
        {
            uint32_t act = 0;
            for (p = 0; p < NPART; p++)
                for (v = 0; v < NVOICE; v++)
                    if (trk[p].v[v].active) {
                        act++;
                        if (trk[p].v[v].note < (p == DRIFT_BASS ? 28u : p == DRIFT_CHOIR ? 40u : 21u) ||
                            trk[p].v[v].note > (p == DRIFT_CHOIR ? 72u : p == DRIFT_BELL ? 96u : 108u))
                            bad_note++;
                    }
            if (act > NVOICE)
                too_many++;
        }
    }
}
static void reset(void) { pk = s2 = s1 = jump = 0; nf = 0; }
static double db(double x) { return 20.0 * log10(x + 1e-12); }
static double rms(void) { return nf ? sqrt(s2 / nf) : 0; }
static const uint8_t WHITE[7] = {0, 2, 4, 6, 7, 9, 11}, BLACK[10] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22};
static void press(uint32_t mask) { web_keys(mask); run(30); }
static void button(uint32_t b) { web_buttons(1u << b); run(20); web_buttons(0); run(20); }

/* 1: every world and behaviour, every layer on, the progression it starts with */
static void matrix(void)
{
    uint32_t w, st;
    printf("-- every world x behaviour, all layers (rms / peak dBFS, largest step, DC)\n");
    for (w = 0; w < DRIFT_NWORLDS; w++)
        for (st = 0; st < DS_N; st++) {
            drift_load(w);
            drift_style = (uint8_t)st;
            drift_apply();
            drift_layers = (1u << DL_N) - 1u;
            if (!drift_run)
                button(B_PLAY);
            run(3000);                                   /* (the change settles) */
            reset();
            run(16000);
            {
                double r = db(rms()), p = db(pk), dc = fabs(s1 / (double)nf);
                printf("  %-6s %-6s %6.1f / %6.1f  step %.3f  dc %.4f\n", DRIFT_WORLDS[w].name, DRIFT_STYLE[st], r, p,
                       jump, dc);
                CHECK(pk < 0.7, "%s %s: peak %.3f (headroom below clipping)", DRIFT_WORLDS[w].name, DRIFT_STYLE[st], pk);
                CHECK(r > -40.0 && r < -14.0, "%s %s: level %.1f dBFS out of the band", DRIFT_WORLDS[w].name,
                      DRIFT_STYLE[st], r);
                CHECK(jump < 0.25, "%s %s: a sample step of %.3f (a click)", DRIFT_WORLDS[w].name, DRIFT_STYLE[st], jump);
                CHECK(dc < 0.01, "%s %s: DC %.4f", DRIFT_WORLDS[w].name, DRIFT_STYLE[st], dc);
            }
        }
    button(B_PLAY);
    run(30000);
    reset();
    run(10000);
    CHECK(db(rms()) < -60.0, "after the matrix and a stop: still %.1f dBFS 30..40 s later", db(rms()));
}

/* 2: random playing */
static void restless(uint32_t minutes)
{
    uint32_t t, silent = 0, keys = 0, stops = 0, stuck = 0, end = minutes * 60000u;
    printf("-- %u minutes of random playing\n", minutes);
    reset();
    for (t = 0; t < end;) {
        uint32_t a = rnd(100), dt = 50u + rnd(2500);
        if (a < 6)
            button(B_PLAY);
        else if (a < 30)
            keys ^= 1u << WHITE[rnd(7)];                 /* a layer: press and let go */
        else if (a < 45)
            keys = (keys & ~0x252A52Au) | 1u << BLACK[rnd(10)];   /* a chord: held for a while */
        else if (a < 55)
            keys &= ~0x252A52Au;                         /* .. let go */
        else if (a < 80)
            web_enc(EN_K1 + rnd(4), (int32_t)rnd(21) - 10);
        else if (a < 85)
            web_enc(EN_PRESET, rnd(2) ? 1 : -1);
        else if (a < 90)
            web_enc(EN_ALGO, rnd(2) ? 1 : -1);
        else if (a < 95)
            web_enc(EN_SELECT, rnd(2) ? 1 : -1);
        else
            button(rnd(2) ? B_OCTUP : B_OCTDN);
        press(keys);
        keys &= 0x252A52Au;                              /* (white keys: a tap) */
        press(keys);
        {
            double before = s2;
            uint64_t nb = nf;
            run(dt);
            if (drift_run && ((drift_layers >> DL_PAD) & 1u) && dt > 1500u) {
                double r = sqrt((s2 - before) / (double)(nf - nb));
                if (db(r) < -70.0)
                    silent++;
            }
        }
        t += dt + 100u;
        if (!drift_run && rnd(30) == 0u) {               /* now and then, stopped, wait out the tails */
            double before;
            uint64_t nb;
            press(0);
            keys = 0;
            run(30000);
            before = s2;
            nb = nf;
            run(10000);                                  /* 30 .. 40 s after the stop: nothing left to hear */
            t += 40000u;
            stops++;
            if (db(sqrt((s2 - before) / (double)(nf - nb))) > -60.0) {
                stuck++;
                printf("FAIL 30..40 s after a stop the output is still %.1f dBFS\n",
                       db(sqrt((s2 - before) / (double)(nf - nb))));
            }
        }
    }
    printf("  output peak %.3f (%.1f dBFS), largest step %.3f, %u waits after a stop\n", pk, db(pk), jump, stops);
    CHECK(pk < 0.9, "random playing: peak %.3f", pk);
    CHECK(jump < 0.3, "random playing: a sample step of %.3f (a click)", jump);
    CHECK(silent == 0, "random playing: %u stretches of silence while PLAY and the pad were on", silent);
    CHECK(stuck == 0, "random playing: stuck notes after %u of %u stops", stuck, stops);
}

int main(int argc, char **argv)
{
    uint32_t minutes = argc > 1 ? (uint32_t)atoi(argv[1]) : 30u;
    web_nor_erase();
    web_boot();
    run(1000);
    matrix();
    restless(minutes);
    CHECK(bad_note == 0, "%u voice blocks with a note out of range (bass from 28, voice 40..72, bells to 96, all to 108)", bad_note);
    CHECK(too_many == 0, "%u blocks with more than %u voices", too_many, NVOICE);
    printf(fails ? "drift test: %u FAILED\n" : "drift test passed\n", fails);
    return fails != 0;
}
