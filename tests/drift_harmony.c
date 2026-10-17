/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dominik Siemon */
/* DRIFT's harmony check: every world x behaviour x TIDE with every layer on, the notes that sound together
 * sampled every block. A clash is two notes a semitone apart (in pitch class: a minor 2nd, a major 7th, a minor 9th)
 * both heard: held, or struck within the last 0.6 s, and since the chord now began (a bell of the chord before still
 * ringing over the next is a crossfade, not a wrong note). Prints the share of time with a clash, worst first, and which
 * layers clash most.
 *   cc -O2 -ffp-contract=off -w -Ibuild/gen -Ifirmware/src -Iweb/emu -o build/drift_harmony tests/drift_harmony.c -lm
 *   build/drift_harmony [SECONDS per combination]          exit 0: no combination above 3 % */
#include "felucca_web.c"

static uint32_t seen_ms[NPART][NVOICE];  /* when each voice last started (its age changed) */
static uint32_t age_was[NPART][NVOICE];
static uint32_t pair[NPART][NPART];      /* clashing blocks by the parts of the two notes */
static uint32_t chord_ms;                /* when the chord now began */

/* the notes heard now -> n[], their parts -> pt[] */
static uint32_t heard(uint8_t *n, uint8_t *pt)
{
    uint32_t p, v, k = 0;
    for (p = 0; p < NPART; p++)
        for (v = 0; v < NVOICE; v++) {
            const voice_t *x = &trk[p].v[v];
            if (!x->active)
                continue;
            if (x->age != age_was[p][v]) {
                age_was[p][v] = x->age;
                seen_ms[p][v] = fm1_ms;
            }
            if (p == DRIFT_PAD && drift_pad_noise)       /* (a noise pad has no pitch to clash) */
                continue;
            if (x->gate || (fm1_ms - seen_ms[p][v] < 600u && seen_ms[p][v] >= chord_ms)) {
                n[k] = x->note;
                pt[k++] = (uint8_t)p;
            }
        }
    return k;
}

int main(int argc, char **argv)
{
    uint32_t secs = argc > 1 ? (uint32_t)atoi(argv[1]) : 20u, w, st, pg, worst_fail = 0;
    struct { double share; uint32_t w, st, pg; } r[DRIFT_NWORLDS * DS_N * DT_N];
    uint32_t nr = 0, i, j;
    web_nor_erase();
    web_boot();
    for (i = 0; i < 44100u; i += 128u) web_render(128);
    drift_input();
    drift_fade_in_ms = drift_fade_out_ms = 0;            /* (no fades: the music from its first block) */
    for (w = 0; w < DRIFT_NWORLDS; w++)
        for (st = 0; st < DS_N; st++)
            for (pg = 0; pg < DT_N; pg++) {
                uint32_t blocks = 0, bad = 0, k;
                drift_load(w);
                drift_style = (uint8_t)st;
                drift_tide = (uint8_t)pg;
                drift_apply();
                drift_layers = (1u << DL_N) - 1u;
                drift_run = 1;
                for (k = 0; k < 2u * 44100u; k += 128u) web_render(128);
                for (k = 0; k < secs * 44100u; k += 128u) {
                    uint8_t n[NPART * NVOICE], pt[NPART * NVOICE];
                    uint32_t c, a, b, hit = 0;
                    static uint8_t seen_was;
                    web_render(128);
                    if (drift_seen != seen_was) {
                        seen_was = drift_seen;
                        chord_ms = fm1_ms;
                    }
                    c = heard(n, pt);
                    for (a = 0; a < c; a++)
                        for (b = a + 1; b < c; b++) {
                            uint32_t d = (uint32_t)(n[a] > n[b] ? n[a] - n[b] : n[b] - n[a]) % 12u;
                            if (d == 1u || d == 11u) {
                                hit = 1;
                                pair[pt[a]][pt[b]]++;
                            }
                        }
                    blocks++;
                    bad += hit;
                }
                r[nr].share = 100.0 * bad / blocks;
                r[nr].w = w; r[nr].st = st; r[nr].pg = pg;
                nr++;
                drift_run = 0;
                for (k = 0; k < 8u * 44100u; k += 128u) web_render(128);
            }
    for (i = 0; i < nr; i++)                         /* worst first */
        for (j = i + 1; j < nr; j++)
            if (r[j].share > r[i].share) { __typeof__(r[0]) t = r[i]; r[i] = r[j]; r[j] = t; }
    printf("time with a semitone clash, worst 15 of %u:\n", nr);
    for (i = 0; i < nr && i < 15u; i++)
        printf("  %5.1f %%  %-6s %-6s %s\n", r[i].share, DRIFT_WORLDS[r[i].w].name, DRIFT_STYLE[r[i].st], DRIFT_TIDE[r[i].pg]);
    {
        double sum = 0; for (i = 0; i < nr; i++) sum += r[i].share, worst_fail += r[i].share > 3.0;
        printf("average %.1f %%, %u of %u above 3 %%\n", sum / nr, worst_fail, nr);
    }
    printf("clashing blocks by parts (1 pad+shimmer, 2 bells, 3 bass, 4 voice):\n");
    for (i = 0; i < NPART; i++)
        for (j = i; j < NPART; j++)
            if (pair[i][j] + (i != j ? pair[j][i] : 0u))
                printf("  %u-%u: %u\n", i + 1, j + 1, pair[i][j] + (i != j ? pair[j][i] : 0u));
    return worst_fail != 0;
}
