/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dominik Siemon */
/* DRIFT: the keys, in the audio ISR (seq.c events_block calls drift_block where Felucca's keyboard_block was).
 * Included by seq.c with FELUCCA_DRIFT.
 *   White keys: the seven chords of the key (I ii iii IV V vi vii), left to right, rising an octave every seven.
 *   Black keys: colour the chord (SUS2 ADD9 7TH SUS4 OPEN, repeating across the keyboard).
 * The pad (track 1) holds the chord while a white key is down; a chord change moves only the notes that change, so
 * the long tails overlap. Each new white key also rings one bell (track 2) above the chord. Every note comes from the
 * major scale of drift_root: there is no wrong key. */
#define DRIFT_PAD 0u
#define DRIFT_BELL 1u
#define DRIFT_PAD_MAX 4u                 /* pad voices per chord: the 8 voices are shared by all parts (core.h NVOICE) */
enum { DC_NONE, DC_SUS2, DC_ADD9, DC_7TH, DC_SUS4, DC_OPEN, DC_N };
static const char *const DRIFT_COLOUR[DC_N] = {"", "SUS2", "ADD9", "7", "SUS4", "OPEN"};
static const uint8_t DRIFT_SCALE[7] = {0, 2, 4, 5, 7, 9, 11};

static volatile int8_t drift_oct;        /* OCT- / OCT+ (the UI): -1 .. +1 */
static volatile uint8_t drift_root = 2;  /* the key: D */
static volatile int8_t drift_deg = -1;   /* the chord sounding (0..6), -1 none: the screen shows it */
static volatile uint8_t drift_col;       /* its colour (DC_*) */
static volatile uint8_t drift_seen;      /* bumped on each chord change (the screen redraws) */
static uint8_t drift_pad[DRIFT_PAD_MAX], drift_pad_n;
static uint32_t drift_keys;              /* the keys as the last block saw them */
static uint8_t drift_white = 0xFF, drift_black = 0xFF;   /* the place of the white / black key that leads, 0xFF none */
static int8_t drift_oct_was;

/* a scale step of the key: semitones above drift_root (any step, negative too) */
static int32_t drift_step(int32_t s)
{
    int32_t o = s >= 0 ? s / 7 : -((6 - s) / 7);
    return 12 * o + DRIFT_SCALE[s - 7 * o];
}

/* note n moved by octaves into [lo, lo + 12) */
static int32_t drift_fold(int32_t n, int32_t lo)
{
    while (n < lo)
        n += 12;
    while (n >= lo + 12)
        n -= 12;
    return n;
}

/* the chord of white place w with colour c -> out (ascending), the number of notes */
static uint32_t drift_chord(uint32_t w, uint32_t c, uint8_t *out)
{
    int32_t deg = (int32_t)(w % 7u), lo = 50 + 12 * (int32_t)(w / 7u) + 12 * drift_oct, root = drift_root;
    int32_t steps[4] = {0, 2, 4, -1}, n = 0, i, j, x;
    int32_t note[DRIFT_PAD_MAX + 1u];
    if (c == DC_SUS2)
        steps[1] = 1;
    else if (c == DC_SUS4)
        steps[1] = 3;
    else if (c == DC_ADD9)
        steps[3] = 8;
    else if (c == DC_7TH)
        steps[3] = 6;
    for (i = 0; i < 4 && steps[i] >= 0; i++)
        note[n++] = drift_fold(root + drift_step(deg + steps[i]), lo);
    if (c == DC_OPEN) {                                  /* the fifth an octave up, the third two: wide and airy */
        note[1] = drift_fold(root + drift_step(deg + 4), lo) + 12;
        note[2] = drift_fold(root + drift_step(deg + 2), lo) + 24;
        note[0] = drift_fold(root + drift_step(deg), lo);
        note[n++] = note[0] + 12;
    }
    if (n < (int32_t)DRIFT_PAD_MAX)                      /* room left: the chord's root below, a soft bass */
        note[n++] = drift_fold(root + drift_step(deg), lo - 14);
    for (i = 1; i < n; i++)                              /* ascending, no doubles */
        for (j = i; j > 0 && note[j - 1] > note[j]; j--) {
            x = note[j];
            note[j] = note[j - 1];
            note[j - 1] = x;
        }
    for (i = 0, j = 0; i < n && j < (int32_t)DRIFT_PAD_MAX; i++)
        if ((j == 0 || note[i] != out[j - 1]) && note[i] > 0 && note[i] < 128)
            out[j++] = (uint8_t)note[i];
    return (uint32_t)j;
}

/* the place among keys of its colour of the highest held key of that colour, 0xFF none */
static uint8_t drift_top(uint32_t keys, int black)
{
    int32_t k;
    for (k = 26; k >= 0; k--)
        if (((keys >> k) & 1u) && key_black((uint32_t)k) == black)
            return (uint8_t)key_place((uint32_t)k);
    return 0xFF;
}

/* 1 = the key of that colour at place p is held */
static int drift_held(uint32_t keys, int black, uint8_t p)
{
    uint32_t k;
    for (k = 0; p != 0xFF && k < 27u; k++)
        if (((keys >> k) & 1u) && key_black(k) == black && key_place(k) == p)
            return 1;
    return 0;
}

static void drift_set_pad(const uint8_t *want, uint32_t n)
{
    track_t *t = &trk[DRIFT_PAD];
    uint32_t i, j;
    for (i = 0; i < drift_pad_n; i++) {                  /* what the new chord does not have ends */
        for (j = 0; j < n && want[j] != drift_pad[i]; j++)
            ;
        if (j == n)
            trk_note_off(t, drift_pad[i]);
    }
    for (j = 0; j < n; j++) {                            /* what it adds starts */
        for (i = 0; i < drift_pad_n && drift_pad[i] != want[j]; i++)
            ;
        if (i == drift_pad_n)
            trk_note_on(t, want[j], 84);
    }
    for (j = 0; j < n; j++)
        drift_pad[j] = want[j];
    drift_pad_n = (uint8_t)n;
}

/* each audio block, before the voices: the keys -> the chord */
static void drift_block(void)
{
    uint32_t cur = fm1_in.notes, down, k;
    uint8_t chord[DRIFT_PAD_MAX], w, b;
    uint32_t n;
    if (kb_boot_hold) {                                  /* (main.c: under the splash the keys are silent) */
        drift_keys = cur;
        return;
    }
    if (cur == drift_keys && drift_oct == drift_oct_was)
        return;
    down = cur & ~drift_keys;
    drift_keys = cur;
    drift_oct_was = drift_oct;
    w = 0xFF;
    b = 0xFF;
    for (k = 0; k < 27u; k++)                            /* a key pressed now leads its colour */
        if ((down >> k) & 1u) {
            if (key_black(k))
                b = (uint8_t)key_place(k);
            else
                w = (uint8_t)key_place(k);
        }
    /* none pressed now: the one that led stays while held, else the highest held */
    drift_white = w != 0xFF ? w : drift_held(cur, 0, drift_white) ? drift_white : drift_top(cur, 0);
    drift_black = b != 0xFF ? b : drift_held(cur, 1, drift_black) ? drift_black : drift_top(cur, 1);
    if (drift_white == 0xFF) {                           /* no white key: the pad lets go (its long release) */
        drift_set_pad(chord, 0);
        drift_deg = -1;
        drift_seen++;
        return;
    }
    n = drift_chord(drift_white, drift_black == 0xFF ? DC_NONE : 1u + drift_black % (DC_N - 1u), chord);
    drift_set_pad(chord, n);
    if (w != 0xFF && n) {                                /* a new white key: one bell above the chord */
        trk_note_on(&trk[DRIFT_BELL], chord[n - 1u] + 12u, 70);
        trk_note_off(&trk[DRIFT_BELL], chord[n - 1u] + 12u);
    }
    drift_deg = (int8_t)(drift_white % 7u);
    drift_col = drift_black == 0xFF ? DC_NONE : (uint8_t)(1u + drift_black % (DC_N - 1u));
    drift_seen++;
}
