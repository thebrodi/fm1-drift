/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dominik Siemon */
/* HAAVE's checks: the browser build (web/emu/felucca_web.c) compiled natively and played by a restless player.
 *   0. PLAY at once, a world change's dip, a stop ringing out: rising, falling, no click, silent at the end
 *   1. every world x behaviour with every layer on: no clipping, no clicks, no DC, never silent, levels in a band
 *   2. every punch-in in every world: no clipping, no click, no level jump
 *   4. TIDE: FLOWING and WANDERING move the optional layers now and then, STILL and CALM never; the pad and bass stay
 *   5. the instruments and the sprinkles: each instrument's notes in the chord and rising in every world, OCT and
 *      chord; every instrument sound and every sprinkle heard, below clipping, no click; a chord's key again colours it;
 *      a crowd of notes takes no held note for longer than a sixteenth; nothing held ends by itself
 *   6. WARP halves the speed and comes back, PUMP dips on the beat without a click, SYNTH's held keys follow a chord
 *      and leave nothing held, BREATHE rests the layers but the pad and gives them back
 *   3. half an hour (simulated) of random playing: PLAY, layers, black-key chords (tapped and held), the seven knobs,
 *      OCT, the punch-ins, the instruments and their keys, the sprinkles, WARP; the output always finite, below clipping, no
 *      clicks; every note in range (the bass from E1, the voice E2..C5, the bells up to C7); never more voices than the
 *      8; while PLAY and the pad are on, never 3 s of silence; 30..40 s after a stop, the output below -60 dBFS (no
 *      stuck note; an FM6 voice is judged by what it sounds: its own envelopes end it, a bowl's tail runs long)
 *   cc -O2 -ffp-contract=off -w -Ibuild/gen -Ifirmware/src -Iweb/emu -o build/haave_test tests/haave_test.c -lm
 *   build/haave_test [MINUTES]          exit 0: all passed */
#include "felucca_web.c"

static uint32_t fails, t_seed = 12345u;
static uint32_t rnd(uint32_t n) { t_seed = t_seed * 1664525u + 1013904223u; return n ? (t_seed >> 8) % n : 0u; }
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static double pk, s2, s1, jump;          /* the last run: peak, sum of squares, sum, largest sample step */
static uint64_t nf;
static float last_l, last_r;
static uint32_t bad_note, too_many, lost, lost_run, lost_long;

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
                        act += trk[p].v[v].stage != 4u;  /* (a voice the budget took fades out over this block: the
                                                          * one that took it starts beside it, by design) */
                        if (trk[p].engine != trk[p].eng_req || trk[p].xf_on)
                            continue;                    /* (a sound changing: the last one's notes fade out) */
                        if (trk[p].v[v].note < (p == HAAVE_BASS ? 28u : p == HAAVE_CHOIR && haave_p4 == 0u ? 40u : 21u) ||
                            trk[p].v[v].note > (p == HAAVE_CHOIR && haave_p4 == 0u ? 72u : p == HAAVE_BELL ? 96u : 108u))
                            bad_note++;
                    }
            if (act > NVOICE)
                too_many++;
            {   /* a held note whose voice the budget took: back within about a sixteenth (haave_heal) */
                const haave_held_t *H[3] = {&haave_pad, &haave_bass, &haave_choir};
                uint32_t h, j, gone = 0;
                for (h = 0; h < 3u; h++)
                    for (j = 0; j < H[h]->n; j++) {
                        const track_t *tt = &trk[H[h]->part];
                        gone += !(tt->xf_on || tt->eng_req != tt->engine) && !haave_gated(H[h]->part, H[h]->note[j]);
                    }
                lost += gone != 0;
                lost_run = gone ? lost_run + 1u : 0u;
                lost_long += lost_run == 160u;           /* (~0.46 s: a half-time sixteenth and a bit) */
            }
        }
    }
}
static void reset(void) { pk = s2 = s1 = jump = 0; nf = 0; }
static double db(double x) { return 20.0 * log10(x + 1e-12); }
static double rms(void) { return nf ? sqrt(s2 / nf) : 0; }
static const uint8_t WHITE[7] = {0, 2, 4, 6, 7, 9, 11}, BLACK[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
static const uint8_t PLAYK[9] = {12, 14, 16, 18, 19, 21, 23, 24, 26};   /* the instrument's nine (white keys 8..16) */
#define CHORDK (1u << 1 | 1u << 3 | 1u << 5 | 1u << 8 | 1u << 10)   /* the chords' black keys (the first five) */
#define INSTK (1u << 12 | 1u << 14 | 1u << 16 | 1u << 18 | 1u << 19 | 1u << 21 | 1u << 23 | 1u << 24 | 1u << 26)
#define HELD (CHORDK | INSTK)                    /* the keys a press holds: the chords, the instrument's */
static const uint8_t INST_BTN[3] = {B_SAVE, B_ARP, B_SEQ};   /* SYNTH BELLS BASS */
static void press(uint32_t mask) { web_keys(mask); run(30); }
static void button(uint32_t b) { web_buttons(1u << b); run(20); web_buttons(0); run(20); }

/* 1: every world and behaviour, every layer on, the progression it starts with */
static void matrix(void)
{
    uint32_t w, st;
    printf("-- every world x behaviour, all layers (rms / peak dBFS, largest step, DC)\n");
    for (w = 0; w < HAAVE_NWORLDS; w++)
        for (st = 0; st < DS_N; st++) {
            haave_load(w);
            haave_style = (uint8_t)st;
            haave_apply();
            haave_layers = (1u << DL_N) - 1u;
            if (!haave_run)
                button(B_PLAY);
            run(3000);                                   /* (the change settles) */
            reset();
            run(16000);
            {
                double r = db(rms()), p = db(pk), dc = fabs(s1 / (double)nf);
                printf("  %-6s %-6s %6.1f / %6.1f  step %.3f  dc %.4f\n", HAAVE_WORLDS[w].name, HAAVE_STYLE[st], r, p,
                       jump, dc);
                CHECK(pk < 0.7, "%s %s: peak %.3f (headroom below clipping)", HAAVE_WORLDS[w].name, HAAVE_STYLE[st], pk);
                CHECK(r > -40.0 && r < -14.0, "%s %s: level %.1f dBFS out of the band", HAAVE_WORLDS[w].name,
                      HAAVE_STYLE[st], r);
                CHECK(jump < 0.25, "%s %s: a sample step of %.3f (a click)", HAAVE_WORLDS[w].name, HAAVE_STYLE[st], jump);
                CHECK(dc < 0.01, "%s %s: DC %.4f", HAAVE_WORLDS[w].name, HAAVE_STYLE[st], dc);
            }
        }
    button(B_PLAY);
    run(30000);
    reset();
    run(10000);
    CHECK(db(rms()) < -60.0, "after the matrix and a stop: still %.1f dBFS 30..40 s later", db(rms()));
}

/* 3: every punch-in in every world, all layers on: held 4 s, then let go: no clipping, no click, and the level within
 * a few dB of the sound without it (each fades in over a beat and out over a beat: none may jump) */
static const uint8_t PUNCH_BTN[DP_NPUNCH] = {B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO};
static void punches(void)
{
    uint32_t w, k;
    printf("-- every punch-in in every world (level change while held, dB)\n");
    for (w = 0; w < HAAVE_NWORLDS; w++) {
        printf("  %-6s", HAAVE_WORLDS[w].name);
        haave_load(w);
        haave_layers = (1u << DL_N) - 1u;
        if (!haave_run)
            button(B_PLAY);
        run(4000);
        for (k = 0; k < DP_NPUNCH; k++) {
            double r0, r1;
            reset();
            run(3000);
            r0 = db(rms());
            reset();
            web_buttons(1u << PUNCH_BTN[k]);
            run(4000);
            r1 = db(rms());
            CHECK(haave_punch == 1u << k, "%s: %s not shown as held", HAAVE_WORLDS[w].name, HAAVE_PUNCH_NAME[k]);
            web_buttons(0);
            run(2000);
            printf("  %s %+5.1f", HAAVE_PUNCH_NAME[k], r1 - r0);
            CHECK(pk < 0.8, "%s %s: peak %.3f", HAAVE_WORLDS[w].name, HAAVE_PUNCH_NAME[k], pk);
            CHECK(jump < 0.35, "%s %s: a sample step of %.3f (a click)", HAAVE_WORLDS[w].name, HAAVE_PUNCH_NAME[k], jump);
            CHECK(r1 - r0 < 8.0 && r1 - r0 > -24.0, "%s %s: the level jumps %+.1f dB", HAAVE_WORLDS[w].name,
                  HAAVE_PUNCH_NAME[k], r1 - r0);
        }
        printf("\n");
    }
    button(B_PLAY);
    run(3000);
}

/* 2: random playing */
static void restless(uint32_t minutes)
{
    uint32_t t, silent = 0, keys = 0, stops = 0, stuck = 0, end = minutes * 60000u, heals0 = haave_heals;
    printf("-- %u minutes of random playing\n", minutes);
    reset();
    for (t = 0; t < end;) {
        uint32_t a = rnd(100), dt = 50u + rnd(2500);
        if (a < 6)
            button(B_PLAY);
        else if (a < 22)
            keys ^= 1u << WHITE[rnd(7)];                 /* a layer: press and let go */
        else if (a < 30)
            keys ^= 1u << PLAYK[rnd(9)];                 /* the instrument: a key held, or let go */
        else if (a < 42)
            keys = (keys & ~CHORDK) | 1u << BLACK[rnd(5)];   /* a chord: held for a while (again: coloured) */
        else if (a < 45)
            keys |= 1u << BLACK[5u + rnd(6)];            /* a sprinkle: a tap */
        else if (a < 55)
            keys &= ~CHORDK;                             /* .. let go */
        else if (a < 80)
            web_enc(EN_K1 + rnd(4), (int32_t)rnd(21) - 10);
        else if (a < 85)
            web_enc(EN_PRESET, rnd(2) ? 1 : -1);
        else if (a < 90)
            web_enc(EN_ALGO, rnd(2) ? 1 : -1);
        else if (a < 95)
            web_enc(EN_SELECT, rnd(2) ? 1 : -1);
        else if (a < 97)
            button(rnd(2) ? B_OCTUP : B_OCTDN);
        else if (a < 98)
            button(INST_BTN[rnd(3)]);                    /* an instrument (SAVE again: SYNTH's next sound) */
        else if (a < 99 && rnd(2)) {                     /* WARP: HOME held a while */
            web_buttons(1u << B_HOME);
            run(500.0 + rnd(5000));
            web_buttons(0);
        }
        else {                                           /* a punch-in, held for a while */
            web_buttons(1u << PUNCH_BTN[rnd(DP_NPUNCH)]);
            run(300.0 + rnd(3000));
            web_buttons(0);
        }
        press(keys);
        keys &= HELD;                                    /* (the layers' keys and the sprinkles': a tap) */
        press(keys);
        {
            double before = s2;
            uint64_t nb = nf;
            run(dt);
            if (haave_run && ((haave_layers >> DL_PAD) & 1u) && dt > 1500u) {
                double r = sqrt((s2 - before) / (double)(nf - nb));
                if (db(r) < -70.0)
                    silent++;
            }
        }
        t += dt + 100u;
        if (!haave_run && rnd(30) == 0u) {               /* now and then, stopped, wait out the tails */
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
    printf("  output peak %.3f (%.1f dBFS), largest step %.3f, %u waits after a stop, %u held notes played again\n", pk,
           db(pk), jump, stops, haave_heals - heals0);
    CHECK(pk < 0.9, "random playing: peak %.3f", pk);
    CHECK(jump < 0.3, "random playing: a sample step of %.3f (a click)", jump);
    CHECK(silent == 0, "random playing: %u stretches of silence while PLAY and the pad were on", silent);
    CHECK(stuck == 0, "random playing: stuck notes after %u of %u stops", stuck, stops);
}

/* 4: TIDE moves the layers by itself: FLOWING and WANDERING change them over a few minutes, STILL and CALM never; it
 * keeps one to three optional layers on and never touches the pad or the bass */
static void tide(void)
{
    static const char *const NAME[DT_N] = {"STILL", "CALM", "FLOWING", "WANDERING"};
    uint32_t t, k;
    printf("-- TIDE: layer changes in 4 minutes\n");
    for (t = 0; t < DT_N; t++) {
        uint32_t changes = 0, was, bad = 0;
        haave_load(0);
        haave_tide = (uint8_t)t;
        haave_layers = 1u << DL_PAD | 1u << DL_BASS | 1u << DL_MELODY;
        if (!haave_run)
            button(B_PLAY);
        was = haave_layers;
        for (k = 0; k < 240u * 10u; k++) {               /* 4 minutes, a look every 0.1 s */
            uint32_t opt = haave_layers & (1u << DL_ARP | 1u << DL_SPARKLE | 1u << DL_MELODY | 1u << DL_CHOIR |
                                           1u << DL_SHIMMER), n = 0, b;
            run(100);
            if (haave_layers != was)
                changes++;
            for (b = opt; b; b &= b - 1u)
                n++;
            bad += (haave_layers & (1u << DL_PAD | 1u << DL_BASS)) != (1u << DL_PAD | 1u << DL_BASS) || n > 3u ||
                   (t >= DT_FLOW && changes && n < 1u);
            was = haave_layers;
        }
        printf("  %-9s %u changes\n", NAME[t], changes);
        CHECK(t < DT_FLOW ? changes == 0u : changes >= 3u, "TIDE %s: %u layer changes in 4 minutes", NAME[t], changes);
        CHECK(!bad, "TIDE %s: the pad or bass touched, or more than three optional layers on (%u looks)", NAME[t], bad);
    }
    button(B_PLAY);
    run(3000);
}

/* 0: PLAY starts at once (no fade: the MASTER knob is the hand's own), a world change dips out and back in without a
 * click, and a stop lets the tails ring out to silence */
static void fades(void)
{
    double r[8];
    uint32_t i;
    printf("-- PLAY at once, a world change's dip (1.5 s out, 2.5 s in), a stop ringing out\n");
    haave_load(0);
    haave_layers = (1u << DL_N) - 1u;
    button(B_PLAY);
    for (i = 0; i < 4u; i++) {
        reset();
        run(1000);
        r[i] = db(rms());
    }
    printf("  PLAY: %.0f %.0f %.0f %.0f dBFS\n", r[0], r[1], r[2], r[3]);
    CHECK(r[0] > r[3] - 6.0, "PLAY: the first second (%.1f) far below the fourth (%.1f): a fade?", r[0], r[3]);
    web_enc(EN_PRESET, 1);                               /* a world change, playing */
    for (i = 0; i < 6u; i++) {
        reset();
        run(1000);
        r[i] = db(rms());
        CHECK(jump < 0.25, "world change: a click (%.3f) in second %u", jump, i + 1);
    }
    printf("  world change:");
    for (i = 0; i < 6u; i++) printf(" %.0f", r[i]);
    printf(" dBFS (now %s)\n", HAAVE_WORLDS[haave_world].name);
    CHECK(haave_world == 1u, "world change: still %s", HAAVE_WORLDS[haave_world].name);
    CHECK(r[1] < r[5] - 10.0, "world change: no dip (%.1f vs %.1f)", r[1], r[5]);
    button(B_PLAY);                                      /* stop: the tails ring out */
    reset();
    run(1000);
    r[0] = db(rms());
    run(29000);
    reset();
    run(10000);
    r[1] = db(rms());
    printf("  stop: %.0f dBFS the first second, %.0f dBFS 30..40 s later\n", r[0], r[1]);
    CHECK(!haave_on && r[0] > -45.0, "stop: no tail (%.1f dBFS)", r[0]);
    CHECK(r[1] < -60.0, "stop: still %.1f dBFS 30..40 s later", r[1]);
}

/* 5: the instruments and the sprinkles, in every world: each instrument's nine notes fit the chord whatever it is (every
 * chord, coloured or not, OCT) and rise, in their ranges (SYNTH and BELLS C2..C7, BASS E1..E4); each instrument and
 * each SYNTH sound plays a run (heard, no clipping, no click); each world's six sprinkles play out (heard, no clipping,
 * no click, their voices free again); a chord's key pressed again colours it, and again makes it plain; a crowd of
 * notes takes no held note for long; nothing held ends by itself */
static void playing(void)
{
    uint32_t w, m, i, d, in, k, bad = 0, cbad = 0, quiet = 0, loud = 0, spr = 0;
    printf("-- the instruments and the sprinkles\n");
    for (w = 0; w < HAAVE_NWORLDS; w++) {
        haave_load(w);
        for (in = 0; in < DI_N; in++)
            for (m = 0; m < 3u; m++)
                for (d = 0; d < 7u; d++)
                    for (i = 0; i < 2u; i++) {
                        uint32_t last = 0;
                        haave_inst = (uint8_t)in;
                        haave_oct = (int8_t)((int32_t)m - 1);
                        haave_cur = (int8_t)d;
                        haave_bright = (uint8_t)i;
                        for (k = in == DI_BELLS ? 1u : 0u; k < 9u; k++) {   /* (BELLS' first: the GONG) */
                            uint32_t n = haave_key_note(k), lo = in == DI_BASS ? 28u : 36u, hi = in == DI_BASS ? 64u : 96u;
                            bad += !haave_fits((int32_t)n) || n <= last || n < lo || n > hi;
                            last = n;
                        }
                    }
    }
    haave_oct = 0;
    haave_inst = DI_BELLS;
    CHECK(!bad, "the instruments: %u notes out of the chord, not rising, or out of range", bad);
    haave_tide = DT_STILL;
    button(B_PLAY);
    for (w = 0; w < HAAVE_NWORLDS; w++) {
        haave_load(w);
        run(3000);
        haave_layers = 1u << DL_PAD;
        haave_voice_held();
        for (in = 0; in < 3u + DY_N - 1u; in++) {        /* SYNTH (each of its sounds), BELLS, BASS */
            button(INST_BTN[in < DY_N ? 0u : in - DY_N + 1u]);
            run(400);
            reset();
            for (i = 0; i < 9u; i++) {
                web_keys(1u << PLAYK[i]);
                run(260);
                web_keys(0);
                run(40);
            }
            run(800);
            quiet += pk < 0.01;
            loud += pk > 0.9 || jump > 0.3;
            if (pk < 0.01 || pk > 0.9 || jump > 0.3)
                printf("  %s, %s: peak %.3f, step %.3f\n", HAAVE_WORLDS[w].name,
                       in < DY_N ? HAAVE_SYNTH_NAME[haave_synth % DY_N] : in == DY_N ? "BELLS" : "BASS", pk, jump);
        }
        for (k = 0; k < 6u; k++) {                       /* the six sprinkles */
            reset();
            press(1u << BLACK[5u + k]);
            press(0);
            for (i = 0; i < 80u && haave_spr_lit; i++)
                run(100);
            spr++;
            quiet += pk < 0.005;
            loud += pk > 0.9 || jump > 0.3 || haave_spr_lit;
            if (pk < 0.005 || pk > 0.9 || jump > 0.3 || haave_spr_lit)
                printf("  %s, %s: peak %.3f, step %.3f, %s\n", HAAVE_WORLDS[w].name,
                       HAAVE_SPR_NAME[HAAVE_SPR[w][k]], pk, jump, haave_spr_lit ? "still sounding" : "done");
        }
        {   /* a chord's key: there; again: coloured; again: plain */
            int32_t dg = HAAVE_BLACK[haave_minor & 1u][1];
            press(1u << BLACK[1]);
            press(0);
            cbad += haave_cur != dg || haave_bright;
            press(1u << BLACK[1]);
            press(0);
            cbad += haave_cur != dg || !haave_bright;
            press(1u << BLACK[1]);
            press(0);
            cbad += haave_cur != dg || haave_bright;
        }
    }
    button(INST_BTN[1]);
    printf("  14 worlds x 6 instrument sounds and %u sprinkles: %u not heard, %u too loud or clicking; colour %s\n", spr,
           quiet, loud, cbad ? "WRONG" : "right");
    CHECK(!quiet && !loud, "the instruments and sprinkles: %u not heard, %u too loud or clicking", quiet, loud);
    CHECK(!cbad, "a chord's key again: not coloured, or not plain again (%u)", cbad);
    for (w = 0; w < HAAVE_NWORLDS; w++) {                 /* a crowd: every layer, the busiest behaviours, the gong and
                                                          * quick runs up the bells (the voice budget) */
        haave_load(w);
        haave_style = (uint8_t)(w % 2u ? DS_RIPPLE : DS_CHIME);
        haave_apply();
        run(3000);
        haave_layers = (1u << DL_N) - 1u;
        haave_voice_held();
        run(1000);
        for (m = 0; m < 4u; m++) {
            for (i = 0; i < 9u; i++) {
                press(1u << PLAYK[i]);
                press(0);
            }
            run(600);
        }
    }
    printf("  a crowd of notes: held notes away %u blocks in all, %u for longer than a sixteenth\n", lost, lost_long);
    for (w = 0; w < HAAVE_NWORLDS; w++) {                 /* held a minute, nothing else: no held sound ends by itself
                                                          * (haave_heal would strike it again: a repeat you hear) */
        uint32_t h0;
        haave_load(w);
        haave_tide = DT_STILL;
        run(3000);
        haave_layers = 1u << DL_PAD | 1u << DL_BASS | 1u << DL_CHOIR | 1u << DL_SHIMMER;
        haave_voice_held();
        run(2000);
        h0 = haave_heals;
        run(60000);
        CHECK(haave_heals == h0, "%s: %u held notes ended by themselves in a minute", HAAVE_WORLDS[w].name, haave_heals - h0);
    }
    haave_tide = DT_CALM;
    button(B_PLAY);
    run(3000);
}

/* 6: WARP (HOME held) halves the speed and comes back; PUMP dips on every beat (6 dB or more between the dip and the
 * swell) and leaves no click; SYNTH's keys held follow a chord change and leave nothing held when let go; BREATHE rests
 * the layers but the pad while held and gives them back */
static void hands(void)
{
    uint32_t i, k, bad = 0;
    printf("-- WARP, PUMP, SYNTH held, BREATHE\n");
    haave_load(0);
    haave_tide = DT_STILL;
    haave_layers = 1u << DL_PAD | 1u << DL_MELODY | 1u << DL_ARP;
    button(B_PLAY);
    run(4000);
    {   /* WARP */
        uint32_t t0 = haave_tick, t1, t2;
        run(4000);
        t1 = haave_tick - t0;
        web_buttons(1u << B_HOME);
        run(1500);
        t0 = haave_tick;
        run(4000);
        t2 = haave_tick - t0;
        web_buttons(0);
        run(1500);
        printf("  WARP: %u sixteenths in 4 s, held %u (%.2f), let go speed %.2f\n", t1, t2, (double)t2 / t1,
               haave_warp / 65536.0);
        CHECK(t2 * 10u >= t1 * 4u && t2 * 10u <= t1 * 6u, "WARP: %u sixteenths held against %u (not a half)", t2, t1);
        CHECK(haave_warp == 65536, "WARP let go: the speed %d, not back", haave_warp);
    }
    {   /* PUMP: the level 10 ms at a time over 8 beats (after its fade in), its dips against its swells */
        double lo = 1e9, hi = 0;
        web_buttons(1u << B_SCL);
        run(4000);
        reset();
        for (k = 0; k < 8u * 4u * 10u; k++) {        /* (a beat at 72, half time: ~1.67 s; 10 ms windows) */
            double before = s2, r;
            uint64_t nb = nf;
            run(10);
            r = (s2 - before) / (double)(nf - nb);
            if (r > 0) {
                lo = r < lo ? r : lo;
                hi = r > hi ? r : hi;
            }
        }
        web_buttons(0);
        run(4000);
        printf("  PUMP: %.1f dB between its dips and its swells, largest step %.3f\n", 10 * log10(hi / lo), jump);
        CHECK(10 * log10(hi / lo) > 6.0, "PUMP: only %.1f dB of dip", 10 * log10(hi / lo));
        CHECK(jump < 0.3, "PUMP: a step of %.3f (a click)", jump);
    }
    {   /* SYNTH: three keys held across a chord change, then let go */
        uint32_t held = 1u << PLAYK[0] | 1u << PLAYK[3] | 1u << PLAYK[6], n0;
        button(B_SAVE);
        run(500);
        press(held);
        run(800);
        n0 = haave_kpad.n;
        for (i = 0; i < haave_kpad.n; i++)
            bad += !haave_gated(HAAVE_CHOIR, haave_kpad.note[i]) || !haave_fits(haave_kpad.note[i]);
        press(held | 1u << BLACK[3]);
        press(held);
        run(800);
        for (i = 0; i < haave_kpad.n; i++)
            bad += !haave_gated(HAAVE_CHOIR, haave_kpad.note[i]) || !haave_fits(haave_kpad.note[i]);
        press(0);
        run(300);
        for (k = 0; k < 128u; k++)                       /* (the voice's part: nothing held, its layer resting) */
            bad += haave_nref[HAAVE_CHOIR][k] != 0;
        printf("  SYNTH: %u notes held of three pressed, %u wrong\n", n0, bad);
        CHECK(n0 == 3u, "SYNTH: %u notes held, not 3", n0);
        CHECK(haave_kpad.n == 0u && !bad, "SYNTH: %u left held, %u wrong", haave_kpad.n, bad);
        button(B_ARP);
        run(500);
    }
    {   /* BREATHE held 3 bars: the layers but the pad rest; let go: back */
        uint32_t resting, back;
        haave_layers = 1u << DL_PAD | 1u << DL_BASS | 1u << DL_CHOIR;
        haave_voice_held();
        run(1000);
        web_buttons(1u << B_GLO);
        run(3 * 4 * 1667);
        resting = haave_breathe && haave_pad.n && !haave_bass.n && !haave_choir.n;
        web_buttons(0);
        run(4000);
        back = !haave_breathe && haave_pad.n && haave_bass.n && haave_choir.n;
        printf("  BREATHE: %s, let go %s\n", resting ? "the pad alone" : "NOT resting", back ? "all back" : "NOT back");
        CHECK(resting && back, "BREATHE: resting %u, back %u", resting, back);
    }
    button(B_PLAY);
    run(3000);
    haave_tide = DT_CALM;
}

int main(int argc, char **argv)
{
    uint32_t minutes = argc > 1 ? (uint32_t)atoi(argv[1]) : 30u;
    web_nor_erase();
    web_boot();
    run(1000);
    haave_input();
    fades();
    matrix();
    tide();
    punches();
    playing();
    hands();
    restless(minutes);
    CHECK(bad_note == 0, "%u voice blocks with a note out of range (bass from 28, voice 40..72, bells to 96, all to 108)", bad_note);
    CHECK(too_many == 0, "%u blocks with more than %u voices", too_many, NVOICE);
    CHECK(lost_long == 0, "%u held notes taken by the voice budget and not back within a sixteenth (%u blocks in all)",
          lost_long, lost);
    printf(fails ? "haave test: %u FAILED\n" : "haave test passed\n", fails);
    return fails != 0;
}
