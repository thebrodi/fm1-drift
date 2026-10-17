/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dominik Siemon */
/* DRIFT: the music, in the audio ISR (seq.c events_block calls drift_block where Felucca's keyboard_block was).
 * Included by seq.c with FELUCCA_DRIFT. DRIFT.md has the design.
 *   PLAY: a chord progression (SELECT) walks on by itself on a sixteenth-note clock at the world's tempo.
 *   White keys: toggle the seven layers (PAD BASS ARP SPARKLE MELODY CHOIR SHIMMER), repeating across the keyboard.
 *   Black keys: chords to jump to (vi ii IV V I from the left; the upper ones the same chords, add9): pressed, the music
 *   moves there; held, it stays; let go, the progression carries on from there.
 *   ALGORITHM: how every layer behaves (DS_*: how long a chord lasts, how often each layer plays).
 *   FX SCL ENV LFO EDIT GLO: punch-in effects on the whole sound while held (Felucca's perform.c effects, on the master):
 *   TAPE STOP, STUTTER (REPEAT 1/16), FREEZE, SHIMMER (OCT UP), UNDERWATER (the LPF sweeping down over a bar), WASH
 *   (REV THROW).
 * Every note comes from the scale of the world (major or natural minor, from drift_root) and mostly from the chord
 * sounding now: nothing can clash.
 * Parts: PAD and SHIMMER on part 1, ARP SPARKLE MELODY on part 2 (struck bells), BASS on 3, CHOIR on 4. The FM-1
 * sounds 8 voices across the parts (core.h NVOICE), so the held layers use at most 6 (pad 3, shimmer 1, bass 1,
 * choir 1) and leave the bells room. */
#define DRIFT_PAD 0u
#define DRIFT_BELL 1u
#define DRIFT_BASS 2u
#define DRIFT_CHOIR 3u
enum { DL_PAD, DL_BASS, DL_ARP, DL_SPARKLE, DL_MELODY, DL_CHOIR, DL_SHIMMER, DL_N };
static const char *const DRIFT_LAYER[DL_N] = {"PAD", "BASS", "ARP", "SPARK", "MEL", "VOX", "SHIM"};
/* the chord of each black key of a group: major vi ii IV V I, minor VI iv v VII i (no diminished chord in either) */
static const int8_t DRIFT_BLACK[2][5] = {{5, 1, 3, 4, 0}, {5, 3, 4, 6, 0}};
static const int8_t DRIFT_WANDER[2][5] = {{0, 1, 3, 4, 5}, {0, 2, 3, 5, 6}};   /* the chords WANDER visits */
enum { DS_DRIFT, DS_RIPPLE, DS_SWELL, DS_CHIME, DS_PULSE, DS_N };
static const char *const DRIFT_STYLE[DS_N] = {"DRIFT", "RIPPLE", "SWELL", "CHIME", "PULSE"};
/* the punch-in effects of the six buttons (perform.c PF_*), and their names on the screen */
#define DP_NPUNCH 6u
static volatile uint32_t drift_punch_bit[DP_NPUNCH];   /* the buttons' bits in fm1_in.buttons (drift_ui.c sets them:
                                                         * FX SCL ENV LFO EDIT GLO; panel.c comes after this file) */
static const uint8_t DRIFT_PUNCH[DP_NPUNCH] = {PF_TAPE, PF_R16, PF_FRZ, PF_OUP, PF_LPF, PF_RTHR};
static const char *const DRIFT_PUNCH_NAME[DP_NPUNCH] = {"TAPE STOP", "STUTTER", "FREEZE", "SHIMMER", "UNDERWATER", "WASH"};
static const uint8_t DRIFT_SCALE[2][7] = {{0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}};   /* major, natural minor */

/* ------------------------------------------------------- progressions --- */
#define DP_WANDER 5u
static const char *const DRIFT_PROG[] = {"DRONE", "HOME", "FLOAT", "LONGING", "RISE", "WANDER"};
#define DP_N (sizeof DRIFT_PROG / sizeof DRIFT_PROG[0])
/* scale degrees 0..6 (I..vii), -1 ends (WANDER picks from DRIFT_WANDER); and how many bars each chord lasts (DRONE: it
 * never moves). Each its own shape: DRONE stays, HOME goes round, FLOAT rocks between two, LONGING leans sad and slow,
 * RISE climbs a step a bar, WANDER moves every bar to anywhere that fits */
static const int8_t PROG[DP_N][6] = {
    {0, -1}, {0, 5, 3, 4, -1}, {0, 3, -1}, {5, 2, 3, 0, -1}, {0, 2, 3, 4, 5, -1}, {0, -1},
};
static const uint8_t PROG_BARS[DP_N] = {0, 2, 2, 4, 1, 1};

/* -------------------------------------------------------- behaviours --- */
/* A behaviour (ALGORITHM) is the character of the whole piece: its pace (DRIFT and SWELL at half time), how the pad
 * breathes (drift_ui.c drift_apply: its LFO on the level, its attack, its brightness), how the melody moves, and how often
 * the other layers play. The melody, by behaviour:
 *   MM_FLOAT  single notes far apart, now and then an octave leap
 *   MM_LINE   a flowing line, a step of the scale up or down each eighth
 *   MM_BURST  a quick figure of three high notes, then a rest
 *   MM_MOTIF  a four-note motif drawn at each chord, repeated on the beats */
enum { MM_FLOAT, MM_LINE, MM_BURST, MM_MOTIF };
typedef struct {
    uint8_t half;                        /* 1: half time (a sixteenth twice as long) */
    uint8_t arp_div, arp_p;              /* ARP: a note every arp_div sixteenths, played with chance arp_p / 128 */
    uint8_t spk_p, spk_burst;            /* SPARKLE: chance per sixteenth (/ 1024); notes per burst */
    uint8_t mel, mel_min, mel_max;       /* MELODY: MM_*, sixteenths between notes (FLOAT, BURST: between figures) */
    uint8_t choir_every;                 /* VOX: moves to another tone every this many sixteenths */
    uint8_t bass_pulse;                  /* BASS: 0 held, 1 struck again on each quarter */
} drift_beh_t;
static const drift_beh_t DRIFT_BEH[DS_N] = {
    {1, 4, 40, 14, 1, MM_FLOAT, 12, 28, 16, 0},     /* DRIFT: slow and floating */
    {0, 2, 110, 20, 1, MM_LINE, 0, 0, 8, 0},        /* RIPPLE: flowing */
    {1, 8, 40, 8, 1, MM_FLOAT, 24, 48, 32, 0},      /* SWELL: breathing, very few notes */
    {0, 1, 60, 34, 3, MM_BURST, 10, 24, 8, 0},      /* CHIME: bright figures */
    {0, 2, 80, 16, 1, MM_MOTIF, 0, 0, 8, 1},        /* PULSE: a steady soft beat */
};

/* --------------------------------------------- shared with the UI ---- */
static volatile uint8_t drift_run;       /* PLAY */
static volatile uint8_t drift_style;     /* ALGORITHM: DS_* */
static volatile uint8_t drift_prog = 1;  /* SELECT: the progression */
static volatile uint8_t drift_motion = 50;   /* KNOB 4: 0..127 */
static volatile int8_t drift_oct;        /* OCT- / OCT+: -1 .. +1 */
static volatile uint8_t drift_root = 2;  /* the world's key (D) */
static volatile uint8_t drift_minor;     /* the world's mode: 0 major, 1 natural minor */
static volatile int8_t drift_vox_reg;    /* the world's VOX register: semitones from the middle (-12, 0, +12) */
static volatile uint8_t drift_bpm = 72;  /* the world's tempo */
static volatile uint8_t drift_resync;    /* the UI reloaded the parts' sounds: start the held notes again */
static volatile uint8_t drift_layers = 1u << DL_PAD | 1u << DL_MELODY;   /* the layers on (bit DL_*) */
static volatile uint8_t drift_bright;    /* the chord now is the add9 one (an upper black key chose it) */
static volatile uint8_t drift_pin = 0xFF;   /* the black key (its place) holding the chord, 0xFF none */
static volatile uint8_t drift_act[DL_N]; /* each layer's activity: 255 as it plays a note, the screen lets it fade */
static volatile int8_t drift_deg = -1;   /* the chord sounding (0..6), -1 none: the screen shows it */
static volatile uint8_t drift_seen;      /* bumped when the chord or a toggle changes (the screen redraws) */
static volatile uint8_t drift_punch;     /* the punch-in effects held (bit per button of DRIFT_PUNCH_BTN) */

/* ------------------------------------------------------------- notes --- */
static int32_t drift_step(int32_t s)     /* a scale step of the key: semitones above drift_root (negative too) */
{
    int32_t o = s >= 0 ? s / 7 : -((6 - s) / 7);
    return 12 * o + DRIFT_SCALE[drift_minor & 1u][s - 7 * o];
}
static int32_t drift_fold(int32_t n, int32_t lo)   /* n moved by octaves into [lo, lo + 12) */
{
    while (n < lo)
        n += 12;
    while (n >= lo + 12)
        n -= 12;
    return n;
}
static uint32_t drift_rng = 0x2545F491u;
static uint32_t drift_rand(uint32_t n)   /* 0 .. n - 1 */
{
    drift_rng ^= drift_rng << 13;
    drift_rng ^= drift_rng >> 17;
    drift_rng ^= drift_rng << 5;
    return n ? drift_rng % n : 0u;
}
/* 1 with chance p / 128, more often when MOTION is high (x0.2 .. x2.2) */
static int drift_chance(uint32_t p, uint32_t scale)
{
    return drift_rand(scale) < p * (24u + drift_motion * 2u) / 128u;
}

/* a held voice of one part: the notes it sounds now -> want (n of them): only what changes moves */
typedef struct { uint8_t part, n, note[4]; } drift_held_t;
static drift_held_t drift_pad = {DRIFT_PAD, 0, {0}}, drift_bass = {DRIFT_BASS, 0, {0}},
                    drift_choir = {DRIFT_CHOIR, 0, {0}};
static void drift_hold_set(drift_held_t *h, const uint8_t *want, uint32_t n, uint32_t vel)
{
    track_t *t = &trk[h->part];
    uint32_t i, j;
    for (i = 0; i < h->n; i++) {
        for (j = 0; j < n && want[j] != h->note[i]; j++)
            ;
        if (j == n)
            trk_note_off(t, h->note[i]);
    }
    for (j = 0; j < n; j++) {
        for (i = 0; i < h->n && h->note[i] != want[j]; i++)
            ;
        if (i == h->n)
            trk_note_on(t, want[j], vel);
    }
    for (j = 0; j < n; j++)
        h->note[j] = want[j];
    h->n = (uint8_t)n;
}

/* a bell (part 2): struck, held DRIFT_STRIKE, then let go to ring out on its release (a note-off in the block of its
 * note-on would end it before its attack: no sound) */
#define DRIFT_NBELL 6u
#define DRIFT_STRIKE (FS / 8u)
static uint8_t drift_bn[DRIFT_NBELL];
static int32_t drift_bt[DRIFT_NBELL];
static void drift_strike(uint32_t layer, uint32_t part, uint32_t note, uint32_t vel)
{
    uint32_t i, k = 0;
    drift_act[layer] = 255;
#ifdef DRIFT_TRACE
    printf("%7.2f s  %-6s  part %u  note %3u  vel %3u\n", fm1_ms / 1000.0, DRIFT_STYLE[drift_style], part + 1u, note,
           vel);
#endif
    if (note < 24u)
        return;
    if (part == DRIFT_BELL)
        while (note > 96u)                               /* (C7: higher, the bright patches fold back as fizz) */
            note -= 12u;
    if (part != DRIFT_BELL) {                        /* (the bass's pulse: its own held note, struck again) */
        trk_note_off(&trk[part], note);
        trk_note_on(&trk[part], note, vel);
        return;
    }
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

/* ------------------------------------------------------------- chord --- */
static volatile uint8_t drift_pos;       /* the place in the progression (the screen shows it) */
static int8_t drift_cur;                 /* the chord's degree now */
static uint8_t drift_arp, drift_burst, drift_last;
static uint8_t drift_choir_tone = 1;     /* CHOIR: which of the chord's tones it sings */
static uint16_t drift_mel_wait, drift_choir_wait;
static int8_t drift_mel_step = 2;        /* MM_LINE: where the line is, a scale step above the chord's root */
static int8_t drift_mel_dir = 1;         /* .. and which way it goes */
static uint8_t drift_mel_burst;          /* MM_BURST: notes of the figure still to come */
static int8_t drift_motif[4];            /* MM_MOTIF: the motif, scale steps above the chord's root */
static uint8_t drift_motif_ok;           /* 0: draw a new one at the next beat (a new chord) */
static uint8_t drift_prog_was;           /* the progression the music walks (SELECT turned: the new one at the next beat) */
static volatile uint32_t drift_tick;     /* sixteenths since PLAY */
static volatile uint32_t drift_since;    /* the sixteenth the chord began on (the screen counts its bars) */
static int32_t drift_clock;              /* samples to the next sixteenth */

/* the register's low edge: middle, moved by OCT (and a little higher for an add9 chord) */
static int32_t drift_lo(void) { return 55 + 12 * drift_oct + (drift_bright ? 3 : 0); }

/* the chord's tones as scale steps above its degree: a triad, or its add9 (3rd 5th 9th; the bass has the root) */
static uint32_t drift_tones(int32_t *st)
{
    st[0] = drift_bright ? 2 : 0;
    st[1] = drift_bright ? 4 : 2;
    st[2] = drift_bright ? 8 : 4;
    return 3;
}
static uint32_t drift_note(int32_t steps_above, int32_t lo)   /* a tone of the chord now, folded into [lo, lo+12) */
{
    return (uint32_t)drift_fold(drift_root + drift_step(drift_cur + steps_above), lo);
}

/* note n, raised by octaves until it is at least lo */
static uint32_t drift_above(uint32_t n, uint32_t lo)
{
    while (n < lo)
        n += 12u;
    return n;
}

/* the held layers to the chord and the toggles now */
static void drift_voice_held(void)
{
    uint8_t want[4];
    int32_t st[3], lo = drift_lo();
    uint32_t n = 0, i, j, x;
    drift_tones(st);
    if (drift_run && (drift_layers >> DL_PAD) & 1u) {
        for (i = 0; i < 3u; i++)
            want[n++] = (uint8_t)drift_note(st[i], lo - 3);
        for (i = 1; i < n; i++)                          /* ascending (the shimmer goes above the top) */
            for (j = i; j > 0 && want[j - 1] > want[j]; j--) {
                x = want[j];
                want[j] = want[j - 1];
                want[j - 1] = (uint8_t)x;
            }
    }
    if (drift_run && (drift_layers >> DL_SHIMMER) & 1u) {
        want[n] = n ? (uint8_t)(want[n - 1u] + 12u) : (uint8_t)drift_note(4, lo + 12);
        while (want[n] > 84u)                            /* (C6: higher, a bright pad turns thin and fizzy) */
            want[n] = (uint8_t)(want[n] - 12u);
        n++;
    }
    if (n)
        drift_act[DL_PAD] = drift_act[DL_SHIMMER] = 255;
    drift_hold_set(&drift_pad, want, n, 80);
    n = 0;
    if (drift_run && (drift_layers >> DL_BASS) & 1u)
        want[n++] = (uint8_t)drift_above(drift_note(0, lo - 22), 28u);   /* (E1, 41 Hz: lower, a small speaker loses it) */
    if (n)
        drift_act[DL_BASS] = 255;
    drift_hold_set(&drift_bass, want, n, 90);
    n = 0;
    if (drift_run && (drift_layers >> DL_CHOIR) & 1u) {
        uint32_t v = drift_above(drift_note(st[drift_choir_tone % 3u], lo + drift_vox_reg), 40u);   /* (E2 ..) */
        while (v > 72u)                                  /* (.. C5: higher, the voice turns shrill) */
            v -= 12u;
        want[n++] = (uint8_t)v;
    }
    if (n)
        drift_act[DL_CHOIR] = 255;
    drift_hold_set(&drift_choir, want, n, 76);
}

/* the next chord of the progression (WANDER: another of its chords, at random) */
static void drift_advance(void)
{
    const int8_t *p = PROG[drift_prog % DP_N];
    if (drift_prog % DP_N == DP_WANDER) {
        int8_t d;
        do
            d = DRIFT_WANDER[drift_minor & 1u][drift_rand(5u)];
        while (d == drift_cur);
        drift_cur = d;
    } else {
        drift_pos++;
        if (drift_pos >= 5u || p[drift_pos] < 0)
            drift_pos = 0;
        drift_cur = p[drift_pos];
    }
    drift_bright = 0;
    drift_since = drift_tick;
    drift_motif_ok = 0;
    drift_deg = drift_cur;
    drift_seen++;
}

/* a black key: to chord d now (bright: its add9), the progression's place moved to it when it has it */
static void drift_jump(int32_t d, uint32_t bright)
{
    const int8_t *p = PROG[drift_prog % DP_N];
    uint32_t i;
    for (i = 0; i < 5u && p[i] >= 0; i++)
        if (p[i] == d) {
            drift_pos = (uint8_t)i;
            break;
        }
    drift_cur = (int8_t)d;
    drift_bright = (uint8_t)bright;
    drift_since = drift_tick;
    drift_motif_ok = 0;
    drift_deg = drift_cur;
    drift_seen++;
    drift_voice_held();
}

/* --------------------------------------------------------------- keys --- */
static uint32_t drift_keys;              /* the keys as the last block saw them */

/* 1 = the black key at place p is held */
static int drift_black_held(uint32_t keys, uint32_t p)
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if (((keys >> k) & 1u) && key_black(k) && key_place(k) == p)
            return 1;
    return 0;
}

static void drift_keys_block(void)
{
    uint32_t cur = fm1_in.notes, down = cur & ~drift_keys, k;
    drift_keys = cur;
    if (drift_pin != 0xFF && !drift_black_held(cur, drift_pin)) {   /* let go: the chord lasts its length from now */
        drift_pin = 0xFF;
        drift_since = drift_tick;
        drift_seen++;
    }
    for (k = 0; down && k < 27u; k++) {
        uint32_t p;
        if (!((down >> k) & 1u))
            continue;
        p = key_place(k);
        if (!key_black(k)) {                             /* a layer on / off */
            drift_layers ^= (uint8_t)(1u << (p % DL_N));
            drift_voice_held();
        } else if (drift_run) {                          /* a chord: there now, and held while the key is */
            drift_pin = (uint8_t)p;
            drift_jump(DRIFT_BLACK[drift_minor & 1u][p % 5u], p >= 5u);
        }
        drift_seen++;
    }
}

/* ------------------------------------------------------------ player --- */
static uint8_t drift_was_run;

/* the progression from its first chord (WANDER: any of its chords) */
static void drift_prog_start(void)
{
    drift_prog_was = drift_prog;
    drift_pos = 0;
    drift_cur = drift_prog % DP_N == DP_WANDER ? DRIFT_WANDER[drift_minor & 1u][drift_rand(5u)] : PROG[drift_prog % DP_N][0];
    drift_bright = 0;
    drift_since = drift_tick;
    drift_motif_ok = 0;
    drift_deg = drift_run ? drift_cur : -1;
    drift_seen++;
}

static const int8_t MEL_STEP[10] = {0, 0, 0, 2, 2, 4, 4, 4, 1, 5};   /* the chord's tones, sometimes the 2nd or the 6th */

/* MELODY, one sixteenth (q: its place in the bar), as the behaviour has it */
static void drift_melody(const drift_beh_t *b, uint32_t q, int32_t lo, const int32_t *st)
{
    uint32_t n = 0, i;
    switch (b->mel) {
    case MM_LINE:                                        /* a step of the scale up or down each eighth, turning at the edges */
        if (q & 1u)
            return;
        if (drift_rand(4u) == 0u)
            drift_mel_dir = (int8_t)-drift_mel_dir;
        drift_mel_step = (int8_t)(drift_mel_step + drift_mel_dir);
        if (drift_mel_step > 9 || drift_mel_step < 0) {
            drift_mel_dir = (int8_t)-drift_mel_dir;
            drift_mel_step = (int8_t)(drift_mel_step + 2 * drift_mel_dir);
        }
        n = (uint32_t)(drift_fold(drift_root + drift_step(drift_cur), lo + 5) + drift_step(drift_cur + drift_mel_step) -
                       drift_step(drift_cur));
        if (q % 4u == 0u || drift_chance(90u, 128u))
            drift_strike(DL_MELODY, DRIFT_BELL, n, q % 8u ? 42u + drift_rand(14u) : 60u);
        return;
    case MM_BURST:                                       /* three quick high notes up the chord, then a rest */
        if (drift_mel_burst) {
            if (q & 1u)
                return;
            drift_mel_burst--;
            drift_strike(DL_MELODY, DRIFT_BELL, drift_note(st[2u - drift_mel_burst % 3u], lo + 17) +
                         (drift_mel_burst ? 0u : 12u), 46u + drift_rand(18u));
            if (!drift_mel_burst)
                drift_mel_wait = (uint16_t)(b->mel_min + drift_rand(b->mel_max - b->mel_min + 1u));
            return;
        }
        if (!drift_mel_wait || !--drift_mel_wait)
            drift_mel_burst = 3;
        return;
    case MM_MOTIF:                                       /* four notes drawn at each chord, one on each beat */
        if (q % 4u)
            return;
        if (!drift_motif_ok) {
            for (i = 0; i < 4u; i++)
                drift_motif[i] = MEL_STEP[drift_rand(10u)];
            drift_motif_ok = 1;
        }
        if (q == 0u || drift_chance(96u, 128u))
            drift_strike(DL_MELODY, DRIFT_BELL, drift_note(drift_motif[q / 4u], lo + 9), q ? 46u + drift_rand(12u) : 64u);
        return;
    default:                                             /* MM_FLOAT: a note, then a long rest; now and then an octave up */
        if (drift_mel_wait && --drift_mel_wait)
            return;
        for (i = 0; i < 3u; i++) {                       /* not the note just played, if it can help it */
            n = drift_note(MEL_STEP[drift_rand(10u)], lo + 9);
            if (n != drift_last)
                break;
        }
        drift_last = (uint8_t)n;
        drift_strike(DL_MELODY, DRIFT_BELL, n + (drift_rand(5u) ? 0u : 12u), 50u + drift_rand(22u));
        drift_mel_wait = (uint16_t)(b->mel_min + drift_rand(b->mel_max - b->mel_min + 1u));
        return;
    }
}

/* one sixteenth: the layers that strike */
static void drift_sixteenth(void)
{
    const drift_beh_t *b = &DRIFT_BEH[drift_style % DS_N];
    int32_t lo = drift_lo(), st[3];
    uint32_t q = drift_tick % 16u, L = drift_layers, bars = PROG_BARS[drift_prog % DP_N];
    if (drift_prog != drift_prog_was && q % 4u == 0u && drift_pin == 0xFF) {   /* SELECT turned: the new one, on the beat */
        drift_prog_start();
        drift_voice_held();
    } else if (bars && q == 0u && drift_pin == 0xFF && drift_tick - drift_since >= 16u * bars) {   /* the next chord */
        drift_advance();
        drift_voice_held();
    }
    drift_tones(st);
    if (((L >> DL_ARP) & 1u) && drift_tick % b->arp_div == 0u) {   /* ARP: up and back down the chord, two octaves */
        uint32_t a = drift_arp % 10u, k = a < 6u ? a : 10u - a;      /* 0 1 2 3 4 5 4 3 2 1: tone k % 3, octave k / 3 */
        if (q == 0u || drift_chance(b->arp_p, 128u))
            drift_strike(DL_ARP, DRIFT_BELL, drift_note(st[k % 3u], lo + 2) + 12u * (k / 3u),
                         q % 4u ? 40u + drift_rand(14u) : 58u);
        drift_arp++;
    }
    if ((L >> DL_SPARKLE) & 1u) {                        /* SPARKLE: high and quiet, now and then (in bursts) */
        if (drift_burst) {
            drift_burst--;
            drift_strike(DL_SPARKLE, DRIFT_BELL, drift_note(st[drift_rand(3u)], lo + 24), 34u + drift_rand(20u));
        } else if (drift_chance(b->spk_p, 1024u)) {
            drift_burst = (uint8_t)(b->spk_burst - 1u);
            drift_strike(DL_SPARKLE, DRIFT_BELL, drift_note(st[drift_rand(3u)], lo + 24), 38u + drift_rand(24u));
        }
    }
    if ((L >> DL_MELODY) & 1u)
        drift_melody(b, q, lo, st);
    if ((L >> DL_CHOIR) & 1u && (!drift_choir_wait || !--drift_choir_wait)) {  /* VOX: glides to another tone */
        drift_choir_tone = (uint8_t)(drift_choir_tone + 1u + drift_rand(2u));
        drift_voice_held();
        drift_choir_wait = b->choir_every;
    }
    if (b->bass_pulse && (L >> DL_BASS) & 1u && q % 4u == 0u && drift_bass.n)   /* PULSE: the bass on the quarters */
        drift_strike(DL_BASS, DRIFT_BASS, drift_bass.note[0], q ? 70u : 92u);
    drift_tick++;
}

static void drift_player(uint32_t n)
{
    if (drift_resync) {                                  /* new sounds: their notes were cut (ui.c apply_preset_to) */
        drift_resync = 0;
        drift_pad.n = drift_bass.n = drift_choir.n = 0;
        memset(drift_bn, 0, sizeof drift_bn);
        drift_prog_start();                              /* a new world: its progression from the start */
        drift_voice_held();
    }
    if (drift_was_run != drift_run) {                    /* started (the progression from its first chord) or stopped */
        drift_was_run = drift_run;
        drift_tick = 0;
        drift_clock = 0;
        drift_pin = 0xFF;
        drift_mel_wait = 2;
        drift_mel_burst = 0;
        drift_choir_wait = 0;
        drift_prog_start();
        drift_voice_held();                              /* (stopped: everything held lets go, the tails ring) */
    }
    if (!drift_run)
        return;
    drift_clock -= (int32_t)n;
    while (drift_clock <= 0) {
        drift_clock += (int32_t)(FS * 15u / drift_bpm << DRIFT_BEH[drift_style % DS_N].half);   /* a sixteenth: 60 / bpm
                                                                                                 * / 4 s (half time x2) */
        drift_sixteenth();
    }
}

/* each audio block, before the voices */
static void drift_block(uint32_t n)
{
    static int8_t oct;
    if (kb_boot_hold) {                                  /* (main.c: under the splash the keys are silent) */
        drift_keys = fm1_in.notes;
        return;
    }
    drift_bells_block(n);
    drift_keys_block();
    {   /* the punch-in buttons: each holds its effect while it is down */
        uint32_t i, b, down = 0;
        for (i = 0; i < DP_NPUNCH; i++)
            if (fm1_in.buttons & drift_punch_bit[i])
                down |= 1u << i;
        for (i = 0; i < DP_NPUNCH; i++)
            if ((b = 1u << i) & (down ^ drift_punch))
                perf_press(DRIFT_PUNCH[i], (down & b) != 0u);
        if (down != drift_punch) {
            drift_punch = (uint8_t)down;
            drift_seen++;
        }
    }
    if (oct != drift_oct) {                              /* the register moved: the held notes */
        oct = drift_oct;
        drift_voice_held();
    }
    drift_player(n);
}
