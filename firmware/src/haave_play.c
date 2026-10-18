/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dominik Siemon */
/* HAAVE: the music, in the audio ISR (seq.c events_block calls haave_block where Felucca's keyboard_block was).
 * Included by seq.c with FELUCCA_HAAVE. HAAVE.md has the design.
 *   PLAY: a chord progression (SELECT) walks on by itself on a sixteenth-note clock at the world's tempo.
 *   White keys: toggle the seven layers (PAD BASS ARP SPARKLE MELODY CHOIR SHIMMER), repeating across the keyboard.
 *   Black keys: chords to jump to (vi ii IV V I from the left; the upper ones the same chords, coloured): pressed, the
 *   music moves there, for as long as TIDE's chords last (STILL: until the next), then its path carries on from there.
 *   ALGORITHM: how every layer behaves (DS_*: how long a chord lasts, how often each layer plays).
 *   FX SCL ENV LFO EDIT GLO: punch-in effects on the whole sound while held (Felucca's perform.c effects, on the master):
 *   ECHOES (ECHO THROW), STUTTER (REPEAT 1/16), FREEZE, SHIMMER (OCT UP), UNDERWATER (the LPF sweeping down over a bar,
 *   to a soft muffle: perform.c PF_LPF_END), WASH (REV THROW). While the music plays they snap to the beat: a press
 *   starts its effect on the next beat, a release ends it at the end of the beat it is in (a tap: one beat).
 * Every note comes from the scale of the world (major or natural minor, from haave_root) and mostly from the chord
 * sounding now: nothing can clash.
 * Parts: PAD and SHIMMER on part 1, ARP SPARKLE MELODY on part 2 (struck bells), BASS on 3, CHOIR on 4. The FM-1
 * sounds 8 voices across the parts (core.h NVOICE), so the held layers use at most 6 (pad 3, shimmer 1, bass 1,
 * choir 1) and leave the bells room. */
#define HAAVE_PAD 0u
#define HAAVE_BELL 1u
#define HAAVE_BASS 2u
#define HAAVE_CHOIR 3u
enum { DL_PAD, DL_BASS, DL_ARP, DL_SPARKLE, DL_MELODY, DL_CHOIR, DL_SHIMMER, DL_N };
static const char *const HAAVE_LAYER[DL_N] = {"PAD", "BASS", "ARP", "SPARK", "MEL", "VOX", "SHIM"};
/* the chord of each black key of a group: major vi ii IV V I, minor VI iv v VII i (no diminished chord in either) */
static const int8_t HAAVE_BLACK[2][5] = {{5, 1, 3, 4, 0}, {5, 3, 4, 6, 0}};
static const int8_t HAAVE_WANDER[2][5] = {{0, 1, 3, 4, 5}, {0, 2, 3, 5, 6}};   /* the chords WANDER visits */
enum { DS_DRIFT, DS_RIPPLE, DS_SWELL, DS_CHIME, DS_PULSE, DS_N };
static const char *const HAAVE_STYLE[DS_N] = {"DRIFT", "RIPPLE", "SWELL", "CHIME", "PULSE"};
/* the punch-in effects of the six buttons (perform.c PF_*), and their names on the screen */
#define DP_NPUNCH 6u
static volatile uint32_t haave_punch_bit[DP_NPUNCH];   /* the buttons' bits in fm1_in.buttons (haave_ui.c sets them:
                                                         * FX SCL ENV LFO EDIT GLO; panel.c comes after this file) */
static const uint8_t HAAVE_PUNCH[DP_NPUNCH] = {PF_ETHR, PF_R16, PF_FRZ, PF_OUP, PF_LPF, PF_RTHR};
static const char *const HAAVE_PUNCH_NAME[DP_NPUNCH] = {"ECHOES", "STUTTER", "FREEZE", "SHIMMER", "UNDERWATER", "WASH"};
static const uint8_t HAAVE_SCALE[2][7] = {{0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}};   /* major, natural minor */

/* ------------------------------------------------------- chord paths --- */
/* each world's own chord path (haave_ui.c HAAVE_WORLDS .prog): scale degrees 0..6 (I..vii), -1 ends. DRONE stays, HOME
 * goes round, FLOAT rocks between two, LONGING leans sad, RISE climbs; WANDER (TIDE WANDERING) goes anywhere that fits
 * (HAAVE_WANDER) */
#define DP_WANDER 5u
static const char *const HAAVE_PROG[] = {"DRONE", "HOME", "FLOAT", "LONGING", "RISE", "WANDER"};
#define DP_N (sizeof HAAVE_PROG / sizeof HAAVE_PROG[0])
static const int8_t PROG[DP_N][6] = {
    {0, -1}, {0, 5, 3, 4, -1}, {0, 3, -1}, {5, 2, 3, 0, -1}, {0, 2, 3, 4, 5, -1}, {0, -1},
};

/* ---------------------------------------------------------------- tide --- */
/* TIDE (SELECT): how much the music moves by itself; the black keys stay the hands' own chords.
 *   STILL      one chord; nothing moves on its own
 *   CALM       the world's chord path, a chord every 4 bars
 *   FLOWING    .. every 2 bars, and now and then (every 8..16 bars) one layer gently joins or leaves
 *   WANDERING  any chord that fits, every bar; layers come and go more often (every 4..8 bars)
 * The layers it moves are the optional ones (ARP SPARKLE MELODY VOX SHIMMER), never the pad or the bass, and it keeps
 * one to three of them on. */
enum { DT_STILL, DT_CALM, DT_FLOW, DT_WANDER, DT_N };
static const char *const HAAVE_TIDE[DT_N] = {"STILL", "CALM", "FLOWING", "WANDERING"};
static const uint8_t TIDE_BARS[DT_N] = {0, 4, 2, 1};   /* a chord lasts (0: for ever) */

/* -------------------------------------------------------- behaviours --- */
/* A behaviour (ALGORITHM) is the character of the whole piece: its pace (DRIFT and SWELL at half time), how the pad
 * breathes (haave_ui.c haave_apply: its LFO on the level, its attack, its brightness), how the melody moves, and how often
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
} haave_beh_t;
static const haave_beh_t HAAVE_BEH[DS_N] = {
    {1, 4, 40, 14, 1, MM_FLOAT, 12, 28, 16, 0},     /* DRIFT: slow and floating */
    {0, 2, 110, 20, 1, MM_LINE, 0, 0, 8, 0},        /* RIPPLE: flowing */
    {1, 8, 40, 8, 1, MM_FLOAT, 24, 48, 32, 0},      /* SWELL: breathing, very few notes */
    {0, 1, 60, 34, 3, MM_BURST, 10, 24, 8, 0},      /* CHIME: bright figures */
    {0, 2, 80, 16, 1, MM_MOTIF, 0, 0, 8, 1},        /* PULSE: a steady soft beat */
};

/* --------------------------------------------- shared with the UI ---- */
static volatile uint8_t haave_run;       /* PLAY: the music asked for (the UI; a stop fades out first: haave_on) */
static volatile uint8_t haave_on;        /* the music is going: notes come (from PLAY until its fade out has ended) */
static volatile uint32_t haave_fade_in_ms, haave_fade_out_ms;   /* PLAY's fades: none (the MASTER knob is the hand's own
                                                                 * fade); the machinery stays for the world change's dip */
static volatile uint8_t haave_xf_req;    /* the UI: a world change, fade out (the ISR sets haave_xf_ready at silence) */
static volatile uint8_t haave_xf_ready;  /* .. silent: the UI loads the new world, clears both, and it fades in */
static volatile uint8_t haave_style;     /* ALGORITHM: DS_* */
static volatile uint8_t haave_prog = 1;  /* the world's chord path (PROG) */
static volatile uint8_t haave_tide = DT_CALM;   /* SELECT: TIDE (DT_*) */
static volatile uint8_t haave_motion = 50;   /* KNOB 4: 0..127 */
static volatile int8_t haave_oct;        /* OCT- / OCT+: -1 .. +1 */
static volatile uint8_t haave_root = 2;  /* the world's key (D) */
static volatile uint8_t haave_minor;     /* the world's mode: 0 major, 1 natural minor */
static volatile int8_t haave_vox_reg;    /* the world's VOX register: semitones from the middle (-12, 0, +12) */
static volatile int8_t haave_reg;        /* the world's register: semitones the whole music sits lower (CAVE) or higher */
static volatile uint8_t haave_pad_noise; /* the world's pad is noise (OCEAN, RAIN): one voice, no shimmer, no chord */
static volatile uint8_t haave_bpm = 72;  /* the world's tempo */
static volatile uint8_t haave_resync;    /* the UI reloaded the parts' sounds: start the held notes again */
static volatile uint8_t haave_layers = 1u << DL_PAD | 1u << DL_MELODY;   /* the layers on (bit DL_*) */
static volatile uint8_t haave_bright;    /* the chord now is the add9 one (an upper black key chose it) */
static volatile uint8_t haave_act[DL_N]; /* each layer's activity: 255 as it plays a note, the screen lets it fade */
static volatile int8_t haave_deg = -1;   /* the chord sounding (0..6), -1 none: the screen shows it */
static volatile uint8_t haave_seen;      /* bumped when the chord or a toggle changes (the screen redraws) */
static volatile uint8_t haave_punch;     /* the punch-in effects playing (bit per button of HAAVE_PUNCH_BTN) */
static uint8_t haave_punch_want;         /* .. the buttons down; haave_punch follows on the beat (haave_punch_sync) */
static uint8_t haave_punch_tap;          /* pressed since the last beat: plays at least that beat, even if let go */

/* ------------------------------------------------------------- notes --- */
static int32_t haave_step(int32_t s)     /* a scale step of the key: semitones above haave_root (negative too) */
{
    int32_t o = s >= 0 ? s / 7 : -((6 - s) / 7);
    return 12 * o + HAAVE_SCALE[haave_minor & 1u][s - 7 * o];
}
static int32_t haave_fold(int32_t n, int32_t lo)   /* n moved by octaves into [lo, lo + 12) */
{
    while (n < lo)
        n += 12;
    while (n >= lo + 12)
        n -= 12;
    return n;
}
static uint32_t haave_rng = 0x2545F491u;
static uint32_t haave_rand(uint32_t n)   /* 0 .. n - 1 */
{
    haave_rng ^= haave_rng << 13;
    haave_rng ^= haave_rng >> 17;
    haave_rng ^= haave_rng << 5;
    return n ? haave_rng % n : 0u;
}
/* 1 with chance p / 128, more often when MOTION is high (x0.2 .. x2.2) */
static int haave_chance(uint32_t p, uint32_t scale)
{
    return haave_rand(scale) < p * (24u + haave_motion * 2u) / 128u;
}

/* a held voice of one part: the notes it sounds now -> want (n of them): only what changes moves */
typedef struct { uint8_t part, n, note[4]; } haave_held_t;
static haave_held_t haave_pad = {HAAVE_PAD, 0, {0}}, haave_bass = {HAAVE_BASS, 0, {0}},
                    haave_choir = {HAAVE_CHOIR, 0, {0}};
static void haave_hold_set(haave_held_t *h, const uint8_t *want, uint32_t n, uint32_t vel)
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

/* a bell (part 2): struck, held HAAVE_STRIKE, then let go to ring out on its release (a note-off in the block of its
 * note-on would end it before its attack: no sound) */
#define HAAVE_NBELL 6u
#define HAAVE_STRIKE (FS / 8u)
static uint8_t haave_bn[HAAVE_NBELL];
static int32_t haave_bt[HAAVE_NBELL];
static void haave_strike(uint32_t layer, uint32_t part, uint32_t note, uint32_t vel)
{
    uint32_t i, k = 0;
    haave_act[layer] = 255;
#ifdef HAAVE_TRACE
    printf("%7.2f s  %-6s  part %u  note %3u  vel %3u\n", fm1_ms / 1000.0, HAAVE_STYLE[haave_style], part + 1u, note,
           vel);
#endif
    if (note < 24u)
        return;
    if (part == HAAVE_BELL)
        while (note > 96u)                               /* (C7: higher, the bright patches fold back as fizz) */
            note -= 12u;
    if (part != HAAVE_BELL) {                        /* (the bass's pulse: its own held note, struck again) */
        trk_note_off(&trk[part], note);
        trk_note_on(&trk[part], note, vel);
        return;
    }
    for (i = 0; i < HAAVE_NBELL; i++) {              /* a free slot, else the one closest to its end */
        if (!haave_bn[i] || haave_bn[i] == note) {
            k = i;
            break;
        }
        if (haave_bt[i] < haave_bt[k])
            k = i;
    }
    if (haave_bn[k])
        trk_note_off(&trk[HAAVE_BELL], haave_bn[k]);
    trk_note_on(&trk[HAAVE_BELL], note, vel);
    haave_bn[k] = (uint8_t)note;
    haave_bt[k] = (int32_t)HAAVE_STRIKE;
}
static void haave_bells_block(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < HAAVE_NBELL; i++)
        if (haave_bn[i] && (haave_bt[i] -= (int32_t)n) <= 0) {
            trk_note_off(&trk[HAAVE_BELL], haave_bn[i]);
            haave_bn[i] = 0;
        }
}

/* ------------------------------------------------------------- chord --- */
static volatile uint8_t haave_pos;       /* the place in the progression (the screen shows it) */
static int8_t haave_cur;                 /* the chord's degree now */
static uint8_t haave_arp, haave_burst, haave_last;
static uint8_t haave_choir_tone = 1;     /* CHOIR: which of the chord's tones it sings */
static uint16_t haave_mel_wait, haave_choir_wait;
static int8_t haave_mel_step = 2;        /* MM_LINE: where the line is, a scale step above the chord's root */
static int8_t haave_mel_dir = 1;         /* .. and which way it goes */
static uint8_t haave_mel_burst;          /* MM_BURST: notes of the figure still to come */
static int8_t haave_motif[4];            /* MM_MOTIF: the motif, scale steps above the chord's root */
static uint8_t haave_motif_ok;           /* 0: draw a new one at the next beat (a new chord) */
static uint8_t haave_tide_wait;          /* TIDE: bars to its next layer change */
static volatile uint32_t haave_tick;     /* sixteenths since PLAY */
static volatile uint32_t haave_since;    /* the sixteenth the chord began on (the screen counts its bars) */
static int32_t haave_clock;              /* samples to the next sixteenth */

/* the register's low edge: middle, moved by OCT (and a little higher for an add9 chord) */
static int32_t haave_lo(void) { return 55 + 12 * haave_oct + haave_reg + (haave_bright ? 3 : 0); }

/* the chord's tones as scale steps above its degree: a triad, or its add9 (3rd 5th 9th; the bass has the root) */
static uint32_t haave_tones(int32_t *st)
{
    st[0] = haave_bright ? 2 : 0;
    st[1] = haave_bright ? 4 : 2;
    st[2] = haave_bright ? 8 : 4;
    if (haave_bright && haave_step(haave_cur + 2) - haave_step(haave_cur) == 3)
        st[2] = 6;                                       /* a minor chord: its 7th, not the 9th (a semitone off its 3rd) */
    return 3;
}

/* 1 = note n sits at least a whole tone from every tone of the chord now (its root, for the bass, too): no semitone
 * rubs against what holds */
static int haave_fits(int32_t n)
{
    int32_t st[3], t[4], i;
    haave_tones(st);
    t[0] = haave_root + haave_step(haave_cur);
    for (i = 0; i < 3; i++)
        t[i + 1] = haave_root + haave_step(haave_cur + st[i]);
    for (i = 0; i < 4; i++) {
        int32_t d = ((n - t[i]) % 12 + 12) % 12;
        if (d == 1 || d == 11)
            return 0;
    }
    return 1;
}
static uint32_t haave_note(int32_t steps_above, int32_t lo)   /* a tone of the chord now, folded into [lo, lo+12) */
{
    return (uint32_t)haave_fold(haave_root + haave_step(haave_cur + steps_above), lo);
}

/* note n, raised by octaves until it is at least lo */
static uint32_t haave_above(uint32_t n, uint32_t lo)
{
    while (n < lo)
        n += 12u;
    return n;
}

/* the held layers to the chord and the toggles now */
static void haave_voice_held(void)
{
    uint8_t want[4];
    int32_t st[3], lo = haave_lo();
    uint32_t n = 0, i, j, x;
    haave_tones(st);
    if (haave_on && haave_pad_noise && ((haave_layers >> DL_PAD) & 1u || (haave_layers >> DL_SHIMMER) & 1u))
        want[n++] = (uint8_t)haave_note(0, lo);          /* noise: one voice, its pitch no matter (the engine's TRK 0) */
    else if (haave_on && (haave_layers >> DL_PAD) & 1u) {
        for (i = 0; i < 3u; i++)
            want[n++] = (uint8_t)haave_note(st[i], lo - 3);
        for (i = 1; i < n; i++)                          /* ascending (the shimmer goes above the top) */
            for (j = i; j > 0 && want[j - 1] > want[j]; j--) {
                x = want[j];
                want[j] = want[j - 1];
                want[j - 1] = (uint8_t)x;
            }
    }
    if (haave_on && !haave_pad_noise && (haave_layers >> DL_SHIMMER) & 1u) {
        want[n] = n ? (uint8_t)(want[n - 1u] + 12u) : (uint8_t)haave_note(4, lo + 12);
        while (want[n] > 84u)                            /* (C6: higher, a bright pad turns thin and fizzy) */
            want[n] = (uint8_t)(want[n] - 12u);
        n++;
    }
    if (n)
        haave_act[DL_PAD] = haave_act[DL_SHIMMER] = 255;
    haave_hold_set(&haave_pad, want, n, 80);
    n = 0;
    if (haave_on && (haave_layers >> DL_BASS) & 1u)
        want[n++] = (uint8_t)haave_above(haave_note(0, lo - 22), 28u);   /* (E1, 41 Hz: lower, a small speaker loses it) */
    if (n)
        haave_act[DL_BASS] = 255;
    haave_hold_set(&haave_bass, want, n, 90);
    n = 0;
    if (haave_on && (haave_layers >> DL_CHOIR) & 1u) {
        uint32_t v = haave_above(haave_note(st[haave_choir_tone % 3u], lo + haave_vox_reg), 40u);   /* (E2 ..) */
        while (v > 72u)                                  /* (.. C5: higher, the voice turns shrill) */
            v -= 12u;
        want[n++] = (uint8_t)v;
    }
    if (n)
        haave_act[DL_CHOIR] = 255;
    haave_hold_set(&haave_choir, want, n, 76);
}

/* the chord path the music walks: the world's, or anywhere (TIDE WANDERING) */
static uint32_t haave_path(void) { return haave_tide == DT_WANDER ? DP_WANDER : haave_prog % DP_N; }

/* the next chord of the path (WANDER: another chord that fits, at random) */
static void haave_advance(void)
{
    const int8_t *p = PROG[haave_path()];
    if (haave_path() == DP_WANDER) {
        int8_t d;
        do
            d = HAAVE_WANDER[haave_minor & 1u][haave_rand(5u)];
        while (d == haave_cur);
        haave_cur = d;
    } else {
        haave_pos++;
        if (haave_pos >= 5u || p[haave_pos] < 0)
            haave_pos = 0;
        haave_cur = p[haave_pos];
    }
    haave_bright = 0;
    haave_since = haave_tick;
    haave_motif_ok = 0;
    haave_deg = haave_cur;
    haave_seen++;
}

/* a black key: to chord d now (bright: its add9), the progression's place moved to it when it has it */
static void haave_jump(int32_t d, uint32_t bright)
{
    const int8_t *p = PROG[haave_path()];
    uint32_t i;
    for (i = 0; i < 5u && p[i] >= 0; i++)
        if (p[i] == d) {
            haave_pos = (uint8_t)i;
            break;
        }
    haave_cur = (int8_t)d;
    haave_bright = (uint8_t)bright;
    haave_since = haave_tick;
    haave_motif_ok = 0;
    haave_deg = haave_cur;
    haave_seen++;
    haave_voice_held();
}

/* --------------------------------------------------------------- keys --- */
static uint32_t haave_keys;              /* the keys as the last block saw them */

static void haave_keys_block(void)
{
    uint32_t cur = fm1_in.notes, down = cur & ~haave_keys, k;
    haave_keys = cur;
    for (k = 0; down && k < 27u; k++) {
        uint32_t p;
        if (!((down >> k) & 1u))
            continue;
        p = key_place(k);
        if (!key_black(k)) {                             /* a layer on / off */
            haave_layers ^= (uint8_t)(1u << (p % DL_N));
            haave_voice_held();
        } else if (haave_on) {                          /* a chord: there now, for as long as TIDE's chords last */
            haave_jump(HAAVE_BLACK[haave_minor & 1u][p % 5u], p >= 5u);
        }
        haave_seen++;
    }
}

/* the whole sound's level (Q24): a world change dips it (PLAY's fades are off: haave_fade_in_ms); applied to the mix (audio.c
 * audio_block, before USB and the DAC) */
#define DG_ONE (1 << 24)
static int32_t haave_gain = DG_ONE, haave_gain_to = DG_ONE, haave_gain_step = DG_ONE;
static uint8_t haave_xf_in;              /* the fade in after a world change (2.5 s, not PLAY's) */
static void haave_fade_to(int32_t to, uint32_t ms)
{
    haave_gain_to = to;
    haave_gain_step = ms ? (int32_t)(167772160u / (ms * 441u)) + 1 : DG_ONE;   /* (1 << 24) * 10 / (ms * 441) */
}
static void haave_gain_block(int32_t *out, uint32_t n)
{
    uint32_t i;
    if (haave_gain == DG_ONE && haave_gain_to == DG_ONE)
        return;
    for (i = 0; i < n; i++) {
        if (haave_gain < haave_gain_to)
            haave_gain = haave_gain + haave_gain_step < haave_gain_to ? haave_gain + haave_gain_step : haave_gain_to;
        else if (haave_gain > haave_gain_to)
            haave_gain = haave_gain - haave_gain_step > haave_gain_to ? haave_gain - haave_gain_step : haave_gain_to;
        {   /* (a gentle feel: the square of the linear ramp) */
            int64_t g = (int64_t)haave_gain * haave_gain >> 24;
            out[2u * i] = (int32_t)((int64_t)out[2u * i] * g >> 24);
            out[2u * i + 1u] = (int32_t)((int64_t)out[2u * i + 1u] * g >> 24);
        }
    }
}

/* the punch-in effects playing -> `want` (bit per button): each that changes pressed or let go (perform.c) */
static void haave_punch_sync(uint32_t want)
{
    uint32_t i, b;
    for (i = 0; i < DP_NPUNCH; i++)
        if ((b = 1u << i) & (want ^ haave_punch))
            perf_press(HAAVE_PUNCH[i], (want & b) != 0u);
    if (want != haave_punch) {
        haave_punch = (uint8_t)want;
        haave_seen++;
    }
}

/* ------------------------------------------------------------ player --- */
static uint8_t haave_was_run;

/* the path from its first chord (WANDER: any that fits) */
static void haave_prog_start(void)
{
    haave_pos = 0;
    haave_cur = haave_path() == DP_WANDER ? HAAVE_WANDER[haave_minor & 1u][haave_rand(5u)] : PROG[haave_path()][0];
    haave_bright = 0;
    haave_since = haave_tick;
    haave_motif_ok = 0;
    haave_deg = haave_on ? haave_cur : -1;
    haave_seen++;
}

static const int8_t MEL_STEP[10] = {0, 0, 0, 2, 2, 4, 4, 4, 1, 5};   /* the chord's tones, sometimes the 2nd or the 6th */

/* a melody step above the chord's root: mostly its tones, sometimes the 2nd or the 6th of the scale when they fit */
static int8_t haave_mel_pick(void)
{
    uint32_t tries;
    for (tries = 0; tries < 4u; tries++) {
        int8_t s = MEL_STEP[haave_rand(10u)];
        if (haave_fits(haave_root + haave_step(haave_cur + s)))
            return s;
    }
    return 0;
}

/* TIDE: one of the optional layers joins or leaves, keeping one to three of them on (a noise pad has no shimmer) */
static void haave_tide_layer(void)
{
    static const uint8_t OPT[5] = {DL_ARP, DL_SPARKLE, DL_MELODY, DL_CHOIR, DL_SHIMMER};
    uint32_t i, on = 0, n = haave_pad_noise ? 4u : 5u, add, k;
    for (i = 0; i < n; i++)
        on += (haave_layers >> OPT[i]) & 1u;
    add = on <= 1u ? 1u : on >= 3u ? 0u : haave_rand(2u);
    for (i = 0, k = haave_rand(n); i < n; i++, k = (k + 1u) % n)   /* the first, from a random place, that can change */
        if (((haave_layers >> OPT[k]) & 1u) != add) {
            haave_layers ^= (uint8_t)(1u << OPT[k]);
            haave_voice_held();
            haave_seen++;
            return;
        }
}

/* MELODY, one sixteenth (q: its place in the bar), as the behaviour has it */
static void haave_melody(const haave_beh_t *b, uint32_t q, int32_t lo, const int32_t *st)
{
    uint32_t n = 0, i;
    switch (b->mel) {
    case MM_LINE:                                        /* a step of the scale up or down each eighth, turning at the edges */
        if (q & 1u)
            return;
        if (haave_rand(4u) == 0u)
            haave_mel_dir = (int8_t)-haave_mel_dir;
        for (i = 0; i < 3u; i++) {                       /* a step, and another past a note that would rub */
            haave_mel_step = (int8_t)(haave_mel_step + haave_mel_dir);
            if (haave_mel_step > 9 || haave_mel_step < 0) {
                haave_mel_dir = (int8_t)-haave_mel_dir;
                haave_mel_step = (int8_t)(haave_mel_step + 2 * haave_mel_dir);
            }
            n = (uint32_t)(haave_fold(haave_root + haave_step(haave_cur), lo + 5) + haave_step(haave_cur + haave_mel_step) -
                           haave_step(haave_cur));
            if (haave_fits((int32_t)n) && (n + 1u) % 12u != haave_last % 12u && (n + 11u) % 12u != haave_last % 12u)
                break;                                   /* (nor a semitone from its last note, still ringing) */
        }
        haave_last = (uint8_t)n;
        if (q % 4u == 0u || haave_chance(90u, 128u))
            haave_strike(DL_MELODY, HAAVE_BELL, n, q % 8u ? 42u + haave_rand(14u) : 60u);
        return;
    case MM_BURST:                                       /* three quick high notes up the chord, then a rest */
        if (haave_mel_burst) {
            if (q & 1u)
                return;
            haave_mel_burst--;
            haave_strike(DL_MELODY, HAAVE_BELL, haave_note(st[2u - haave_mel_burst % 3u], lo + 17) +
                         (haave_mel_burst ? 0u : 12u), 46u + haave_rand(18u));
            if (!haave_mel_burst)
                haave_mel_wait = (uint16_t)(b->mel_min + haave_rand(b->mel_max - b->mel_min + 1u));
            return;
        }
        if (!haave_mel_wait || !--haave_mel_wait)
            haave_mel_burst = 3;
        return;
    case MM_MOTIF:                                       /* four notes drawn at each chord, one on each beat */
        if (q % 4u)
            return;
        if (!haave_motif_ok) {
            for (i = 0; i < 4u; i++)
                haave_motif[i] = haave_mel_pick();
            haave_motif_ok = 1;
        }
        if (q == 0u || haave_chance(96u, 128u))
            haave_strike(DL_MELODY, HAAVE_BELL, haave_note(haave_motif[q / 4u], lo + 9), q ? 46u + haave_rand(12u) : 64u);
        return;
    default:                                             /* MM_FLOAT: a note, then a long rest; now and then an octave up */
        if (haave_mel_wait && --haave_mel_wait)
            return;
        for (i = 0; i < 3u; i++) {                       /* not the note just played, if it can help it */
            n = haave_note(haave_mel_pick(), lo + 9);
            if (n != haave_last)
                break;
        }
        haave_last = (uint8_t)n;
        haave_strike(DL_MELODY, HAAVE_BELL, n + (haave_rand(5u) ? 0u : 12u), 50u + haave_rand(22u));
        haave_mel_wait = (uint16_t)(b->mel_min + haave_rand(b->mel_max - b->mel_min + 1u));
        return;
    }
}

/* one sixteenth: the layers that strike */
static void haave_sixteenth(void)
{
    const haave_beh_t *b = &HAAVE_BEH[haave_style % DS_N];
    int32_t lo = haave_lo(), st[3];
    uint32_t q = haave_tick % 16u, L, bars = TIDE_BARS[haave_tide % DT_N];
    if (bars && q == 0u && haave_tick - haave_since >= 16u * bars) {   /* the next chord */
        haave_advance();
        haave_voice_held();
    }
    if (q == 0u && haave_tide >= DT_FLOW && (!haave_tide_wait || !--haave_tide_wait)) {   /* TIDE: a layer comes or goes */
        haave_tide_layer();
        haave_tide_wait = (uint8_t)(haave_tide == DT_FLOW ? 8u + haave_rand(9u) : 4u + haave_rand(5u));
    }
    L = haave_layers;
    if (q % 4u == 0u) {                                  /* a beat: the punch-ins held now play it; let go, they end */
        haave_punch_sync(haave_punch_want | haave_punch_tap);
        haave_punch_tap = 0;
    }
    haave_tones(st);
    if (((L >> DL_ARP) & 1u) && haave_tick % b->arp_div == 0u) {   /* ARP: up and back down the chord, two octaves */
        uint32_t a = haave_arp % 10u, k = a < 6u ? a : 10u - a;      /* 0 1 2 3 4 5 4 3 2 1: tone k % 3, octave k / 3 */
        if (q == 0u || haave_chance(b->arp_p, 128u))
            haave_strike(DL_ARP, HAAVE_BELL, haave_note(st[k % 3u], lo + 2) + 12u * (k / 3u),
                         q % 4u ? 40u + haave_rand(14u) : 58u);
        haave_arp++;
    }
    if ((L >> DL_SPARKLE) & 1u) {                        /* SPARKLE: high and quiet, now and then (in bursts) */
        if (haave_burst) {
            haave_burst--;
            haave_strike(DL_SPARKLE, HAAVE_BELL, haave_note(st[haave_rand(3u)], lo + 24), 34u + haave_rand(20u));
        } else if (haave_chance(b->spk_p, 1024u)) {
            haave_burst = (uint8_t)(b->spk_burst - 1u);
            haave_strike(DL_SPARKLE, HAAVE_BELL, haave_note(st[haave_rand(3u)], lo + 24), 38u + haave_rand(24u));
        }
    }
    if ((L >> DL_MELODY) & 1u)
        haave_melody(b, q, lo, st);
    if ((L >> DL_CHOIR) & 1u && (!haave_choir_wait || !--haave_choir_wait)) {  /* VOX: glides to another tone */
        haave_choir_tone = (uint8_t)(haave_choir_tone + 1u + haave_rand(2u));
        haave_voice_held();
        haave_choir_wait = b->choir_every;
    }
    if (b->bass_pulse && (L >> DL_BASS) & 1u && q % 4u == 0u && haave_bass.n)   /* PULSE: the bass on the quarters */
        haave_strike(DL_BASS, HAAVE_BASS, haave_bass.note[0], q ? 70u : 92u);
    haave_tick++;
}

static void haave_player(uint32_t n)
{
    if (haave_resync) {                                  /* new sounds: their notes were cut (ui.c apply_preset_to) */
        haave_resync = 0;
        haave_pad.n = haave_bass.n = haave_choir.n = 0;
        memset(haave_bn, 0, sizeof haave_bn);
        haave_prog_start();                              /* a new world: its progression from the start */
        haave_voice_held();
    }
    if (haave_run && !haave_on) {                        /* PLAY: on, fading in from silence */
        haave_on = 1;
        haave_gain = haave_fade_in_ms ? 0 : DG_ONE;
        haave_fade_to(DG_ONE, haave_fade_in_ms);
    } else if (!haave_run && haave_on) {                 /* PLAY again: a fade out, then the stop */
        if (!haave_fade_out_ms)
            haave_on = 0;                                /* (the tests: at once, the tails ringing) */
        else if (haave_gain_to)
            haave_fade_to(0, haave_fade_out_ms);
        else if (!haave_gain) {
            haave_on = 0;                                /* (silent: the tails go unheard; the next PLAY fades in) */
            haave_xf_req = 0;
        }
    } else if (haave_on && haave_xf_req) {               /* a world change: out quickly; at silence the UI loads it */
        if (haave_gain_to)
            haave_fade_to(0, 1500u);
        else if (!haave_gain)
            haave_xf_ready = 1;
    } else if (haave_on && haave_gain_to != DG_ONE)      /* back in: a new world, or PLAY pressed during a fade out */
        haave_fade_to(DG_ONE, haave_xf_in ? 2500u : haave_fade_in_ms);
    if (haave_gain_to == DG_ONE && haave_gain == DG_ONE)
        haave_xf_in = 0;
    if (haave_was_run != haave_on) {                     /* started (the progression from its first chord) or stopped */
        haave_was_run = haave_on;
        haave_tick = 0;
        haave_clock = 0;
        haave_mel_wait = 2;
        haave_mel_burst = 0;
        haave_choir_wait = 0;
        haave_tide_wait = 8;                             /* (TIDE's first layer change: not at once) */
        haave_prog_start();
        haave_voice_held();                              /* (stopped: everything held lets go, the tails ring) */
    }
    if (!haave_on)
        return;
    haave_clock -= (int32_t)n;
    while (haave_clock <= 0) {
        haave_clock += (int32_t)(FS * 15u / haave_bpm << HAAVE_BEH[haave_style % DS_N].half);   /* a sixteenth: 60 / bpm
                                                                                                 * / 4 s (half time x2) */
        haave_sixteenth();
    }
}

/* each audio block, before the voices */
static void haave_block(uint32_t n)
{
    static int8_t oct;
    if (kb_boot_hold) {                                  /* (main.c: under the splash the keys are silent) */
        haave_keys = fm1_in.notes;
        return;
    }
    haave_bells_block(n);
    haave_keys_block();
    {   /* the punch-in buttons: playing, they wait for the beat (haave_sixteenth); stopped, they act at once */
        uint32_t i, down = 0;
        for (i = 0; i < DP_NPUNCH; i++)
            if (fm1_in.buttons & haave_punch_bit[i])
                down |= 1u << i;
        haave_punch_tap |= (uint8_t)(down & ~haave_punch_want);
        haave_punch_want = (uint8_t)down;
        if (!haave_on)
            haave_punch_sync(haave_punch_want);
    }
    if (oct != haave_oct) {                              /* the register moved: the held notes */
        oct = haave_oct;
        haave_voice_held();
    }
    haave_player(n);
}
