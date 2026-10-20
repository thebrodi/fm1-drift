/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dominik Siemon */
/* HAAVE's sprinkles: little sounds of the world's own on the six black keys on the right (haave_play.c
 * haave_keys_block), a press each, playing when the music is stopped too. Most are made here, by four voices of their
 * own beside the parts' eight (birds, an owl, crickets, leaves, drops, bubbles, a laser, crackle, a wave, wind,
 * thunder, a gull): a sine with a glide, or noise through a band-pass, in short events with an attack and a fall, the
 * pitched ones on the chord's tones. CHIMES, CASCADE and GONG strike the world's own bells. They are mixed into the
 * dry sound and the echo's and reverb's sends (fx.c, before SPACE's dry level): in the world's room.
 * Included at the end of haave_play.c (its notes, its random numbers, its bells). */

enum { HS_BIRD, HS_SONG, HS_OWL, HS_CRICKET, HS_LEAVES, HS_DROPS, HS_BUBBLES, HS_LASER, HS_CRACKLE, HS_WAVE, HS_WIND,
       HS_THUNDER, HS_GULL, HS_CHIMES, HS_CASCADE, HS_GONG, HS_N };
static const char *const HAAVE_SPR_NAME[HS_N] = {"BIRD", "SONGBIRD", "OWL", "CRICKETS", "LEAVES", "DROPS", "BUBBLES",
                                                 "LASER", "CRACKLE", "WAVE", "WIND", "THUNDER", "GULL", "CHIMES",
                                                 "CASCADE", "GONG"};
/* each world's six, left to right (haave_ui.c HAAVE_WORLDS' order) */
static const uint8_t HAAVE_SPR[14][6] = {
    {HS_CHIMES, HS_CASCADE, HS_DROPS, HS_BIRD, HS_WIND, HS_GONG},          /* GLASS */
    {HS_CRACKLE, HS_BIRD, HS_CHIMES, HS_WIND, HS_CASCADE, HS_GONG},        /* EMBER: a fire */
    {HS_BIRD, HS_CRICKET, HS_WIND, HS_CHIMES, HS_CASCADE, HS_GONG},        /* DUSK */
    {HS_OWL, HS_CRICKET, HS_WIND, HS_DROPS, HS_CHIMES, HS_GONG},           /* NIGHT */
    {HS_LASER, HS_BUBBLES, HS_CRACKLE, HS_CASCADE, HS_CHIMES, HS_GONG},    /* NEON */
    {HS_CRACKLE, HS_LASER, HS_DROPS, HS_SONG, HS_CASCADE, HS_GONG},        /* LOFI: vinyl, space */
    {HS_GONG, HS_CHIMES, HS_CASCADE, HS_DROPS, HS_WIND, HS_BIRD},          /* BOWLS */
    {HS_BIRD, HS_SONG, HS_LEAVES, HS_CHIMES, HS_DROPS, HS_CASCADE},        /* GARDEN */
    {HS_BIRD, HS_LEAVES, HS_OWL, HS_CRICKET, HS_DROPS, HS_WIND},           /* FOREST */
    {HS_GULL, HS_WAVE, HS_BUBBLES, HS_WIND, HS_CHIMES, HS_GONG},           /* OCEAN */
    {HS_THUNDER, HS_DROPS, HS_BUBBLES, HS_WIND, HS_CHIMES, HS_SONG},       /* RAIN */
    {HS_DROPS, HS_BUBBLES, HS_WIND, HS_THUNDER, HS_CHIMES, HS_GONG},       /* CAVE */
    {HS_CHIMES, HS_DROPS, HS_BIRD, HS_WIND, HS_CASCADE, HS_GONG},          /* HANDPAN */
    {HS_CHIMES, HS_CASCADE, HS_DROPS, HS_BUBBLES, HS_WIND, HS_GONG},       /* CRYSTAL */
};
static volatile uint8_t haave_spr_world; /* the world whose sprinkles the keys play (haave_ui.c haave_load) */

#define SPR_NV 4u                        /* voices of their own */
#define SPR_MS(ms) ((uint32_t)(ms) * (FS / 1000u))
enum { K_SINE, K_NOISE, K_DUST, K_STRIKE };
typedef struct {
    uint8_t type, slot, ev, k;           /* SP_* (0xFF: free), its key (0..5), events still to come, this one's kind */
    uint32_t t, len, gap, atk;           /* samples into the event, its length, the silence after it, its attack */
    int32_t p0, p1;                      /* a sine's pitch, 1/16 semitones, from .. to; a band-pass's centre, Hz */
    int32_t amp, pan, pan_to;            /* its level (Q15); the place, 0 left .. 32768 right, gliding to pan_to */
    int32_t e, de, r, dr;                /* the envelope: now, its step in the attack; the fall's 1 .. 0 and its step */
    uint32_t ph, inc, rng;               /* the sine: phase, step (a block at a time); the noise */
    int32_t f, q, lo, bp;                /* the band-pass (a state-variable filter): frequency, damping (Q15), state */
    int32_t ca, c1, cb, c2;              /* the noise darkened before it (a one-pole low-pass, Q15; 32768: none) and
                                          * after it, their states */
    uint8_t lpo;                         /* 1: the filter's low-pass out (a rumble, a surf: 12 dB an octave above) */
    uint32_t vph;                        /* GULL: its vibrato's phase */
    int32_t note;                        /* the event's tone (the bells, BUBBLES' climb) */
} haave_spr_t;
static haave_spr_t haave_spr[SPR_NV] = {{.type = 0xFF}, {.type = 0xFF}, {.type = 0xFF}, {.type = 0xFF}};
static volatile uint8_t haave_spr_lit;   /* the keys whose sprinkle sounds (their lights), a bit each */
static volatile uint8_t haave_spr_last = 0xFF;   /* the last one started (its name on the screen) */
static volatile uint8_t haave_spr_seen;  /* .. bumped each time */
static uint32_t haave_spr_age[SPR_NV], haave_spr_seq;

/* a tone of the chord now from lo (an octave of it) */
static int32_t haave_spr_tone(int32_t lo)
{
    int32_t st[3];
    haave_tones(st);
    return (int32_t)haave_note(st[haave_rand(3u)], lo);
}
static uint32_t haave_spr_r(uint32_t lo, uint32_t hi) { return lo + haave_rand(hi - lo + 1u); }

/* voice v's next event, as its kind has them: 0 = it has played out */
static int haave_spr_event(haave_spr_t *v, int first)
{
    int32_t n;
    if (!first && !v->ev)
        return 0;
    v->ev--;
    v->t = 0;
    v->k = K_SINE;
    switch (v->type) {
    case HS_BIRD:                                        /* quick chirps, up or down, high on the chord */
        if (first)
            v->ev = (uint8_t)haave_spr_r(2, 5);
        n = haave_spr_tone(84) * 16;
        v->p0 = n;
        v->p1 = n + (haave_rand(3u) ? 1 : -1) * (int32_t)haave_spr_r(48, 112);
        v->len = SPR_MS(haave_spr_r(40, 90));
        v->atk = SPR_MS(4);
        v->amp = (int32_t)haave_spr_r(9000, 14000);
        v->gap = SPR_MS(haave_spr_r(25, 110));
        break;
    case HS_SONG:                                        /* a whistle of two notes, each falling a little */
        if (first) {
            v->ev = (uint8_t)haave_spr_r(2, 4);
            v->note = haave_spr_tone(79);
        }
        n = (v->note + (v->ev & 1u ? 0 : 4)) * 16;
        v->p0 = n;
        v->p1 = n - 24;
        v->len = SPR_MS(haave_spr_r(180, 260));
        v->atk = SPR_MS(18);
        v->amp = 10000;
        v->gap = SPR_MS(haave_spr_r(60, 140));
        break;
    case HS_OWL:                                         /* soft low hoots */
        if (first) {
            v->ev = (uint8_t)haave_spr_r(1, 2);
            v->note = haave_spr_tone(58);
        }
        v->p0 = v->note * 16;
        v->p1 = v->p0 - 20;
        v->len = SPR_MS(400);
        v->atk = SPR_MS(90);
        v->amp = 13000;
        v->gap = SPR_MS(haave_spr_r(200, 300));
        break;
    case HS_CRICKET:                                     /* trains of tiny high pulses */
        if (first)
            v->ev = 3u * 6u - 1u;
        v->p0 = v->p1 = 106 * 16;
        v->len = SPR_MS(16);
        v->atk = SPR_MS(3);
        v->amp = 4200;
        v->gap = SPR_MS(v->ev % 6u ? 13 : haave_spr_r(250, 380));
        break;
    case HS_LEAVES:                                      /* rustles: grains of bright noise, drifting */
        if (first)
            v->ev = (uint8_t)haave_spr_r(8, 13);
        v->k = K_NOISE;
        v->p0 = v->p1 = (int32_t)haave_spr_r(2500, 5500);
        v->q = 20000;
        v->len = SPR_MS(haave_spr_r(30, 140));
        v->atk = SPR_MS(6);
        v->amp = (int32_t)haave_spr_r(9000, 18000);
        v->gap = SPR_MS(haave_spr_r(10, 120));
        v->pan_to = (int32_t)haave_spr_r(6000, 26000);
        break;
    case HS_DROPS:                                       /* drops: a quick sine leaping up */
        if (first)
            v->ev = (uint8_t)haave_spr_r(0, 2);
        n = haave_spr_tone(76) * 16;
        v->p0 = n;
        v->p1 = n + 9 * 16;
        v->len = SPR_MS(70);
        v->atk = SPR_MS(2);
        v->amp = 14000;
        v->gap = SPR_MS(haave_spr_r(250, 700));
        v->pan_to = (int32_t)haave_spr_r(6000, 26000);
        break;
    case HS_BUBBLES:                                     /* little drops climbing the chord */
        if (first) {
            v->ev = (uint8_t)haave_spr_r(5, 9);
            v->note = 0;
        }
        n = (int32_t)haave_chord_up((uint32_t)v->note++ % 9u, 64, 8u, 96) * 16;
        v->p0 = n;
        v->p1 = n + 7 * 16;
        v->len = SPR_MS(45);
        v->atk = SPR_MS(1);
        v->amp = 9000;
        v->gap = SPR_MS(haave_spr_r(35, 90));
        break;
    case HS_LASER:                                       /* pew: a fast fall from high, landing on the chord */
        if (first)
            v->ev = (uint8_t)haave_spr_r(1, 2);
        v->p0 = 100 * 16;
        v->p1 = haave_spr_tone(55) * 16;
        v->len = SPR_MS(260);
        v->atk = SPR_MS(1);
        v->amp = 9000;
        v->gap = SPR_MS(haave_spr_r(90, 180));
        v->pan_to = 32768 - v->pan;
        break;
    case HS_CRACKLE:                                     /* crackle: specks through a band-pass, swelling and fading */
        v->ev = 0;
        v->k = K_DUST;
        v->p0 = v->p1 = 2200;
        v->q = 16000;
        v->cb = 14000;                                   /* (each speck's edge rounded: a soft tick, not a scratch) */
        v->len = SPR_MS(haave_spr_r(1800, 2600));
        v->atk = SPR_MS(300);
        v->amp = 26000;
        v->gap = 0;
        break;
    case HS_WAVE:                                        /* a wave: low noise rising and falling, rolling across */
        v->ev = 0;
        v->k = K_NOISE;
        v->p0 = 450;
        v->p1 = 900;
        v->q = 36000;
        v->ca = 5000;                                    /* (pink-dark noise, the low-pass out: a surf, no hiss) */
        v->lpo = 1;
        v->len = SPR_MS(3200);
        v->atk = SPR_MS(1300);
        v->amp = 17000;
        v->gap = 0;
        v->pan = 7000;
        v->pan_to = 26000;
        break;
    case HS_WIND:                                        /* a gust: a band of noise sweeping up, whistling a little */
        v->ev = 0;
        v->k = K_NOISE;
        v->p0 = 700;
        v->p1 = 1500;
        v->q = 12000;
        v->ca = 4000;                                    /* (darker noise in, softened after: no hiss over it) */
        v->cb = 6000;
        v->len = SPR_MS(2800);
        v->atk = SPR_MS(1100);
        v->amp = 11000;
        v->gap = 0;
        v->pan_to = 32768 - v->pan;
        break;
    case HS_THUNDER:                                     /* far thunder: rumbles of low noise */
        if (first)
            v->ev = 2;
        v->k = K_NOISE;
        v->p0 = v->p1 = (int32_t)haave_spr_r(110, 200);
        v->q = 30000;
        v->ca = 1500;                                    /* (brown noise, the low-pass out: a rumble, nothing above) */
        v->lpo = 1;
        v->len = SPR_MS(haave_spr_r(900, 1600));
        v->atk = SPR_MS(150);
        v->amp = 24000 - 5000 * (2 - v->ev);
        v->gap = 0;
        break;
    case HS_GULL:                                        /* a gull: falling cries with a quiver */
        if (first)
            v->ev = (uint8_t)haave_spr_r(1, 2);
        n = haave_spr_tone(84) * 16;
        v->p0 = n;
        v->p1 = n - 6 * 16;
        v->len = SPR_MS(320);
        v->atk = SPR_MS(25);
        v->amp = 8500;
        v->gap = SPR_MS(haave_spr_r(120, 220));
        break;
    case HS_CHIMES:                                      /* the bells, high, a few at random: a wind chime */
        if (first)
            v->ev = (uint8_t)haave_spr_r(2, 4);
        v->k = K_STRIKE;
        v->note = (int32_t)haave_chord_up(haave_spr_r(3, 8), haave_lo() + 5, 8u, 96);
        v->amp = (int32_t)haave_spr_r(14, 26);
        v->len = 0;
        v->gap = SPR_MS(haave_spr_r(70, 260));
        break;
    case HS_CASCADE:                                     /* seven bells falling down the chord, softer */
        if (first)
            v->ev = 6;
        v->k = K_STRIKE;
        v->note = (int32_t)haave_chord_up(v->ev, haave_lo() + 5, 6u, 96);
        v->amp = 26 - 2 * (6 - v->ev);
        v->len = 0;
        v->gap = SPR_MS(140);
        break;
    default:                                             /* GONG */
        v->ev = 0;
        v->k = K_STRIKE;
        v->note = -1;
        v->len = 0;
        v->gap = SPR_MS(1500);
        break;
    }
    if (v->k == K_STRIKE) {                              /* (the bells: struck now) */
        if (v->note < 0)
            haave_gong(36);
        else
            haave_strike(DL_SPARKLE | DL_USER, HAAVE_BELL, (uint32_t)v->note, (uint32_t)v->amp);
        return 1;
    }
    if (v->atk < 1u)
        v->atk = 1;
    if (v->len <= v->atk)
        v->len = v->atk + 1u;
    v->e = 0;
    v->de = v->amp / (int32_t)v->atk + 1;
    v->r = 32768;
    v->dr = (int32_t)(32768u / (v->len - v->atk)) + 1;
    if (v->k == K_SINE)                                  /* (its start now: an event may begin inside a block) */
        v->inc = pitch_inc((uint32_t)(v->p0 < 0 ? 0 : v->p0 > 2047 ? 2047 : v->p0));
    else
        v->f = v->p0 * 4668 / 1000;
    return 1;
}

/* black key 6 + k pressed: its sprinkle (a voice free, else the oldest; the same key's again from its start) */
static void haave_spr_key(uint32_t k)
{
    uint32_t i, best = SPR_NV;
    uint8_t type = HAAVE_SPR[haave_spr_world % 14u][k % 6u];
    haave_spr_t *v;
    for (i = 0; i < SPR_NV && best == SPR_NV; i++)       /* the same key's, playing */
        if (haave_spr[i].type != 0xFF && haave_spr[i].slot == k)
            best = i;
    for (i = 0; i < SPR_NV && best == SPR_NV; i++)       /* else a free one */
        if (haave_spr[i].type == 0xFF)
            best = i;
    if (best == SPR_NV)                                  /* else the oldest */
        for (i = best = 0; i < SPR_NV; i++)
            if (haave_spr_age[i] < haave_spr_age[best])
                best = i;
    v = &haave_spr[best];
    memset(v, 0, sizeof *v);
    v->type = type;
    v->slot = (uint8_t)k;
    v->rng = 0x9E3779B9u ^ (haave_spr_seq * 2654435761u);
    v->pan = v->pan_to = (int32_t)haave_spr_r(8000, 24000);
    haave_spr_age[best] = ++haave_spr_seq;
    haave_spr_event(v, 1);
    haave_spr_last = type;
    haave_spr_seen++;
}

/* fx.c: a block of the sprinkles into the dry mix (l, r) and the echo's and reverb's sends (sd, sv); each kind at its
 * level beside the music (Q8: noise through a narrow band, sparse specks and low rumbles need more than a sine) */
#define SPR_GAIN 22700                   /* (about 4 dB under the instruments' level beside the pad) */
static const uint16_t SPR_KIND_GAIN[HS_N] = {256, 256, 230, 720, 640, 256, 256, 320, 3100, 1020, 750, 2040, 510,
                                             256, 256, 256};
static void haave_spr_mix(int32_t *l, int32_t *r, int32_t *sd, int32_t *sv, uint32_t n)
{
    uint32_t i, j, lit = 0;
    int32_t rv = trk[HAAVE_BELL].p[P_REV] * 258, dl = trk[HAAVE_BELL].p[P_DLY] * 258;
    for (j = 0; j < SPR_NV; j++) {
        haave_spr_t *v = &haave_spr[j];
        int32_t p, gl, gr;
        if (v->type == 0xFF)
            continue;
        lit |= 1u << v->slot;
        if (v->k != K_STRIKE && v->t < v->len) {         /* (a block at a time: the pitch, the band, the place) */
            p = v->p0 + (int32_t)(((int64_t)(v->p1 - v->p0) * v->t) / (int32_t)v->len);
            if (v->k == K_SINE) {
                if (v->type == HS_GULL) {
                    v->vph += n * 900000u;               /* (~9 Hz) */
                    p += (osc_sine(v->vph) * 6) >> 15;
                }
                v->inc = pitch_inc((uint32_t)(p < 0 ? 0 : p > 2047 ? 2047 : p));
            } else
                v->f = p * 4668 / 1000;                  /* (2 pi fc / FS, Q15) */
        }
        v->pan += (v->pan_to - v->pan) / 64;
        gl = 32768 - v->pan;
        gr = v->pan;
        for (i = 0; i < n; i++) {
            int32_t x = 0;
            if (v->t < v->len) {
                if (v->t < v->atk)
                    v->e += v->de;
                else {
                    v->r -= v->dr;
                    if (v->r < 0)
                        v->r = 0;
                    v->e = (int32_t)(((int64_t)v->amp * ((v->r * v->r) >> 15)) >> 15);
                }
                if (v->e > v->amp)
                    v->e = v->amp;
                if (v->k == K_SINE) {
                    x = osc_sine(v->ph);
                    v->ph += v->inc;
                } else {                                 /* noise (DUST: a speck now and then) through the band-pass */
                    int32_t in, hp;
                    v->rng ^= v->rng << 13;
                    v->rng ^= v->rng >> 17;
                    v->rng ^= v->rng << 5;
                    in = v->k == K_DUST ? ((v->rng & 1023u) < 1u ? (int32_t)(v->rng >> 17) - 16384 : 0)
                                        : (int32_t)(v->rng >> 17) - 16384;
                    if (v->ca) {                         /* (darker noise in) */
                        v->c1 += ((in - v->c1) * v->ca) >> 15;
                        in = v->c1;
                    }
                    v->lo += (v->f * v->bp) >> 15;
                    hp = in - v->lo - ((v->q * v->bp) >> 15);
                    v->bp += (v->f * hp) >> 15;
                    if (v->bp > 98304)
                        v->bp = 98304;
                    else if (v->bp < -98304)
                        v->bp = -98304;
                    if (v->lo > 98304)
                        v->lo = 98304;
                    else if (v->lo < -98304)
                        v->lo = -98304;
                    x = v->lpo ? v->lo : v->k == K_DUST ? v->bp << 1 : v->bp;
                    if (v->cb) {                         /* (softened after) */
                        v->c2 += ((x - v->c2) * v->cb) >> 15;
                        x = v->c2;
                    }
                }
                x = (int32_t)(((int64_t)x * v->e) >> 15);
                x = (int32_t)(((int64_t)x * SPR_GAIN * SPR_KIND_GAIN[v->type]) >> 23);
                l[i] += (int32_t)(((int64_t)x * gl) >> 15);
                r[i] += (int32_t)(((int64_t)x * gr) >> 15);
                sv[i] += mulq15(x, rv);
                sd[i] += mulq15(x, dl);
            }
            if (++v->t >= v->len + v->gap && !haave_spr_event(v, 0)) {
                v->type = 0xFF;
                break;
            }
        }
    }
    haave_spr_lit = (uint8_t)lit;
}
