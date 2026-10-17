/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dominik Siemon */
/* DRIFT: the keys and the player, in the audio ISR (seq.c events_block calls drift_block where Felucca's
 * keyboard_block was). Included by seq.c with FELUCCA_DRIFT.
 *   White keys: the seven chords of the key (I ii iii IV V vi vii), left to right, rising an octave every seven.
 *   Black keys: colour the chord (SUS2 ADD9 7TH SUS4 OPEN, repeating across the keyboard).
 * The pad (track 1) holds the chord while a white key is down; a chord change moves only the notes that change, so
 * the long tails overlap. Each new white key also rings one bell (track 2) above the chord.
 * The player (PLAY): plays on by itself around the last chord the keys gave, in one of five styles (ALGORITHM), as
 * busy as MOTION says. Every note comes from the major scale of drift_root, mostly the chord's own tones: there is
 * no wrong note, whatever the keys and the knobs do. */
#define DRIFT_PAD 0u
#define DRIFT_BELL 1u
#define DRIFT_PAD_MAX 4u                 /* pad voices per chord: the 8 voices are shared by all parts (core.h NVOICE) */
enum { DC_NONE, DC_SUS2, DC_ADD9, DC_7TH, DC_SUS4, DC_OPEN, DC_N };
static const char *const DRIFT_COLOUR[DC_N] = {"", "SUS2", "ADD9", "7", "SUS4", "OPEN"};
static const uint8_t DRIFT_SCALE[7] = {0, 2, 4, 5, 7, 9, 11};
enum { DS_DRIFT, DS_RIPPLE, DS_SWELL, DS_CHIME, DS_PULSE, DS_N };
static const char *const DRIFT_STYLE[DS_N] = {"DRIFT", "RIPPLE", "SWELL", "CHIME", "PULSE"};

static volatile int8_t drift_oct;        /* OCT- / OCT+ (the UI): -1 .. +1 */
static volatile uint8_t drift_root = 2;  /* the key: D */
static volatile int8_t drift_deg = -1;   /* the chord the keys hold (0..6), -1 none: the screen shows it */
static volatile uint8_t drift_col;       /* its colour (DC_*) */
static volatile uint8_t drift_seen;      /* bumped on each chord change (the screen redraws) */
static volatile uint8_t drift_run;       /* the player is on (PLAY, the UI) */
static volatile uint8_t drift_style;     /* DS_* (ALGORITHM, the UI) */
static volatile uint8_t drift_motion = 50;   /* 0..127 (KNOB 4, the UI): how busy the player is */
static uint8_t drift_pad[DRIFT_PAD_MAX], drift_pad_n;
static uint32_t drift_keys;              /* the keys as the last block saw them */
static uint8_t drift_white = 0xFF, drift_black = 0xFF;   /* the place of the white / black key that leads, 0xFF none */
static uint8_t drift_ctx_w = 7, drift_ctx_c;   /* the player's chord: the last white place and colour (I to begin) */
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

/* the low edge of white place w's register */
static int32_t drift_lo(uint32_t w) { return 50 + 12 * (int32_t)(w / 7u) + 12 * drift_oct; }

/* the chord of white place w with colour c, its register moved up by lift semitones -> out (ascending), the number
 * of notes */
static uint32_t drift_chord(uint32_t w, uint32_t c, int32_t lift, uint8_t *out)
{
    int32_t deg = (int32_t)(w % 7u), lo = drift_lo(w) + lift, root = drift_root;
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
        note[n++] = drift_fold(root + drift_step(deg), drift_lo(w) - 14);
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

/* one bell: struck, held DRIFT_STRIKE, then let go to ring out on its own release (a note-off in the block of its
 * note-on would end it before its attack: no sound) */
#define DRIFT_NBELL 8u
#define DRIFT_STRIKE (FS / 8u)                       /* 125 ms */
static uint8_t drift_bn[DRIFT_NBELL];                /* the bells held, 0 = free */
static int32_t drift_bt[DRIFT_NBELL];                /* samples each is still held */
static void drift_bell(uint32_t note, uint32_t vel)
{
    uint32_t i, k = 0;
#ifdef DRIFT_TRACE
    printf("%7.2f s  style %u  bell %3u  vel %3u\n", fm1_ms / 1000.0, drift_style, note, vel);
#endif
    if (note < 24u || note > 108u)
        return;
    for (i = 0; i < DRIFT_NBELL; i++) {              /* a free slot, else the one closest to its end */
        if (!drift_bn[i] || drift_bn[i] == note) {
            k = i;
            break;
        }
        if (drift_bt[i] < drift_bt[k])
            k = i;
    }
    if (drift_bn[k])
        trk_note_off(&trk[DRIFT_BELL], drift_bn[k]);
    trk_note_on(&trk[DRIFT_BELL], note, vel);
    drift_bn[k] = (uint8_t)note;
    drift_bt[k] = (int32_t)DRIFT_STRIKE;
}

static void drift_bells_block(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < DRIFT_NBELL; i++)
        if (drift_bn[i] && (drift_bt[i] -= (int32_t)n) <= 0) {
            trk_note_off(&trk[DRIFT_BELL], drift_bn[i]);
            drift_bn[i] = 0;
        }
}

/* --------------------------------------------------------------- keys --- */
static void drift_keys_block(void)
{
    uint32_t cur = fm1_in.notes, down, k;
    uint8_t chord[DRIFT_PAD_MAX], w, b;
    uint32_t n;
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
    if (drift_white == 0xFF) {                           /* no white key: the pad lets go (its long release); */
        if (!drift_run || drift_style != DS_SWELL)       /* SWELL playing keeps it (drift_player) */
            drift_set_pad(chord, 0);
        drift_deg = -1;
        drift_seen++;
        return;
    }
    drift_ctx_w = drift_white;
    drift_ctx_c = drift_black == 0xFF ? DC_NONE : (uint8_t)(1u + drift_black % (DC_N - 1u));
    n = drift_chord(drift_ctx_w, drift_ctx_c, 0, chord);
    drift_set_pad(chord, n);
    if (w != 0xFF && n)                                  /* a new white key: one bell above the chord */
        drift_bell(chord[n - 1u] + 12u, 70);
    drift_deg = (int8_t)(drift_ctx_w % 7u);
    drift_col = drift_ctx_c;
    drift_seen++;
}

/* ------------------------------------------------------------- player --- */
static uint32_t drift_rng = 0x2545F491u;
static uint32_t drift_rand(uint32_t n)                   /* 0 .. n - 1 */
{
    drift_rng ^= drift_rng << 13;
    drift_rng ^= drift_rng >> 17;
    drift_rng ^= drift_rng << 5;
    return n ? drift_rng % n : 0u;
}
static int32_t drift_wait;               /* samples to the player's next move */
static uint8_t drift_beat, drift_burst, drift_last, drift_was_run, drift_was_style;

/* samples of ms, longer when MOTION is low and shorter when it is high (x2 .. x0.5) */
static int32_t drift_ms(uint32_t ms)
{
    return (int32_t)((uint64_t)ms * FS / 1000u * (512u - 3u * drift_motion) / 256u);
}

/* 1 = this beat plays: MOTION low leaves more of them out */
static int drift_plays(void) { return drift_rand(127u) < 40u + drift_motion * 2u / 3u; }

/* a note of the player's chord in the octave from lo: mostly its tones, sometimes the 2nd or the 6th of the scale */
static uint32_t drift_pick(int32_t lo)
{
    static const int8_t STEP[10] = {0, 0, 0, 2, 2, 4, 4, 4, 1, 5};
    int32_t deg = (int32_t)(drift_ctx_w % 7u), n = 0, tries;
    for (tries = 0; tries < 3; tries++) {                /* not the note just played, if it can help it */
        int32_t s = STEP[drift_rand(10u)];
        if (drift_ctx_c == DC_SUS2 && s == 2)
            s = 1;
        else if (drift_ctx_c == DC_SUS4 && s == 2)
            s = 3;
        n = drift_fold(drift_root + drift_step(deg + s), lo);
        if (n != drift_last)
            break;
    }
    drift_last = (uint8_t)n;
    return (uint32_t)n;
}

/* the RIPPLE figure: the chord's tones up and back down, an octave's span */
static const int8_t RIPPLE[8] = {0, 2, 4, 7, 9, 7, 4, 2};

static void drift_player(uint32_t n)
{
    int32_t lo = drift_lo(drift_ctx_w);
    if (drift_was_run != drift_run || drift_was_style != drift_style) {   /* started, stopped or a new style */
        if (drift_was_style == DS_SWELL && drift_white == 0xFF)          /* SWELL's own pad ends */
            drift_set_pad(0, 0);
        drift_was_run = drift_run;
        drift_was_style = drift_style;
        drift_wait = 0;
        drift_beat = 0;
        drift_burst = 0;
    }
    if (!drift_run)
        return;
    drift_wait -= (int32_t)n;
    if (drift_wait > 0)
        return;
    switch (drift_style) {
    case DS_DRIFT:                                       /* single notes, far apart, floating above the pad */
        drift_bell(drift_pick(lo + 12) + 12u * drift_rand(2u), 46u + drift_rand(28u));
        drift_wait = drift_ms(1400u + drift_rand(3200u));
        break;
    case DS_RIPPLE: {                                    /* a soft arpeggio, an eighth at 72 BPM, a few left out */
        int32_t s = RIPPLE[drift_beat & 7u], deg = (int32_t)(drift_ctx_w % 7u);
        if (drift_plays())
            drift_bell((uint32_t)(drift_fold(drift_root + drift_step(deg), lo + 7) + drift_step(deg + s) -
                                  drift_step(deg)), drift_beat % 4u ? 44u + drift_rand(14u) : 62u);
        drift_beat++;
        drift_wait = (int32_t)(FS * 60u / 72u / 2u);
        break;
    }
    case DS_SWELL: {                                     /* the chord itself, slowly re-voiced; a note now and then */
        uint8_t chord[DRIFT_PAD_MAX];
        if (drift_white == 0xFF)
            drift_set_pad(chord, drift_chord(drift_ctx_w, drift_ctx_c, (int32_t)drift_rand(9u) - 4, chord));
        if (drift_rand(3u) == 0u)
            drift_bell(drift_pick(lo + 19), 40u + drift_rand(20u));
        drift_wait = drift_ms(5000u + drift_rand(5000u));
        break;
    }
    case DS_CHIME:                                       /* little bursts of high, quick notes, then a long rest */
        if (!drift_burst)
            drift_burst = (uint8_t)(2u + drift_rand(3u));
        drift_bell(drift_pick(lo + 24), 40u + drift_rand(30u));
        if (--drift_burst)
            drift_wait = (int32_t)(FS / 1000u * (110u + drift_rand(140u)));
        else
            drift_wait = drift_ms(2200u + drift_rand(4000u));
        break;
    default:                                             /* PULSE: a gentle quarter beat, the bar's first leaning in */
        if (drift_beat % 4u == 0u || drift_plays())
            drift_bell(drift_pick(lo + 5), drift_beat % 4u ? 42u + drift_rand(12u) : 66u);
        drift_beat++;
        drift_wait = (int32_t)(FS * 60u / 72u);
        break;
    }
}

/* each audio block, before the voices */
static void drift_block(uint32_t n)
{
    if (kb_boot_hold) {                                  /* (main.c: under the splash the keys are silent) */
        drift_keys = fm1_in.notes;
        return;
    }
    drift_bells_block(n);
    drift_keys_block();
    drift_player(n);
}
