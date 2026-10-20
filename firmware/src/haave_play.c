/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dominik Siemon */
/* HAAVE: the music, in the audio ISR (seq.c events_block calls haave_block where Felucca's keyboard_block was).
 * Included by seq.c with FELUCCA_HAAVE. HAAVE.md has the design.
 *   PLAY: a chord progression (SELECT) walks on by itself on a sixteenth-note clock at the world's tempo.
 *   White keys 1..7: toggle the seven layers (PAD BASS ARP SPARKLE MELODY CHOIR SHIMMER); 8..16: the instrument
 *   (SAVE ARP SEQ, haave_ui.c: SYNTH BELLS BASS), every note fitting the chord.
 *   Black keys 1..5: chords to jump to (vi ii IV V I; again: the same, coloured): pressed, the music moves there, for
 *   as long as TIDE's chords last (STILL: until the next), then its path carries on from there; 6..11: the world's
 *   sprinkles (haave_spr.c, included at the end).
 *   ALGORITHM: how every layer behaves (DS_*: how long a chord lasts, how often each layer plays).
 *   FX SCL ENV LFO EDIT GLO: punch-in effects on the whole sound while held, fading in and out over a beat (WASH PUMP
 *   FREEZE SHIMMER SWIRL BREATHE: haave_punch_block). While the music plays they snap to the beat: a press starts its
 *   effect on the next beat, a release ends it at the end of the beat it is in (a tap: one beat). HOME: WARP.
 * Every note comes from the scale of the world (major or natural minor, from haave_root) and mostly from the chord
 * sounding now: nothing can clash.
 * Parts: PAD and SHIMMER on part 1, ARP SPARKLE MELODY on part 2 (struck bells), BASS on 3, CHOIR on 4; the instrument
 * borrows one (SYNTH 4, BELLS 2, BASS 3). The FM-1 sounds 8 voices across the parts (core.h NVOICE): the held layers
 * use at most 6 (pad 3, shimmer 1, bass 1, choir 1), the instrument what is left but one, a layer's bell steps aside
 * when none is free (haave_strike), and a held note the budget takes anyway comes back (haave_heal). */
#define HAAVE_PAD 0u
#define HAAVE_BELL 1u
#define HAAVE_BASS 2u
#define HAAVE_CHOIR 3u
enum { DL_PAD, DL_BASS, DL_ARP, DL_SPARKLE, DL_MELODY, DL_CHOIR, DL_SHIMMER, DL_N };
#define DL_USER 0x80u                    /* (with a layer, to haave_strike: a note played by hand, never skipped) */
static const char *const HAAVE_LAYER[DL_N] = {"PAD", "BASS", "ARP", "SPARK", "MEL", "VOX", "SHIM"};
/* the chord of each black key of a group: major vi ii IV V I, minor VI iv v VII i (no diminished chord in either) */
static const int8_t HAAVE_BLACK[2][5] = {{5, 1, 3, 4, 0}, {5, 3, 4, 6, 0}};
static const int8_t HAAVE_WANDER[2][5] = {{0, 1, 3, 4, 5}, {0, 2, 3, 5, 6}};   /* the chords WANDER visits */
enum { DS_DRIFT, DS_RIPPLE, DS_SWELL, DS_CHIME, DS_PULSE, DS_N };
static const char *const HAAVE_STYLE[DS_N] = {"DRIFT", "RIPPLE", "SWELL", "CHIME", "PULSE"};
/* the punch-in effects of the six buttons, and their names on the screen; each fades in over a beat from the beat it
 * starts on and out over a beat when let go (haave_punch_block): FREEZE and SHIMMER are perform.c's (pressed while
 * their level is up, the level perf_haave_lvl), WASH PUMP SWIRL BREATHE HAAVE's */
#define DP_NPUNCH 6u
static volatile uint32_t haave_punch_bit[DP_NPUNCH];   /* the buttons' bits in fm1_in.buttons (haave_ui.c sets them:
                                                         * FX SCL ENV LFO EDIT GLO; panel.c comes after this file) */
static const char *const HAAVE_PUNCH_NAME[DP_NPUNCH] = {"WASH", "PUMP", "FREEZE", "SHIMMER", "SWIRL", "BREATHE"};
enum { DP_WASH, DP_PUMP, DP_FREEZE, DP_SHIMMER, DP_SWIRL, DP_BREATHE };
static int32_t haave_plvl[DP_NPUNCH];    /* each one's level now, Q15 */
static volatile int32_t haave_motion_depth;   /* MOTION up: the PHASER's depth, Q15 (haave_ui.c; 0: none) */
static volatile int32_t haave_pan_to;      /* MOTION up: the sound drifting left and right, how far (Q15, 0..0.5) */
static volatile int16_t haave_rev_base[4]; /* SPACE: each part's reverb send (haave_ui.c); WASH lifts them (the ISR) */
static volatile int32_t haave_dry_space = 32768;   /* SPACE: the dry level (haave_ui.c); WASH lowers it (the ISR) */
static volatile int8_t haave_fx_glow;    /* BREATHE: GLOW down while it plays (haave_ui.c adds it to the knob's) */
static uint8_t haave_breathe;            /* BREATHE playing: the layers but the pad rest (haave_lay) */
static volatile uint32_t haave_warp_bit; /* HOME's bit in fm1_in.buttons (haave_ui.c) */
static volatile int32_t haave_warp = 65536;   /* WARP: the music's speed, Q16 (HOME held: gliding to a half) */
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

/* the held notes of a part, counted by who holds them (the pad's chord and the keys share the pad): a note sounds
 * from its first holder and ends with its last */
static uint8_t haave_nref[NPART][128];
static void haave_ref_on(uint32_t part, uint32_t note, uint32_t vel)
{
    if (!haave_nref[part][note & 127u]++)
        trk_note_on(&trk[part], note, vel);
}
static void haave_ref_off(uint32_t part, uint32_t note)
{
    uint8_t *r = &haave_nref[part][note & 127u];
    if (*r && !--*r)
        trk_note_off(&trk[part], note);
}

/* a held voice of one part: the notes it sounds now -> want (n of them): only what changes moves */
typedef struct { uint8_t part, n, note[8], vel; } haave_held_t;
static haave_held_t haave_pad = {HAAVE_PAD, 0, {0}, 80}, haave_bass = {HAAVE_BASS, 0, {0}, 90},
                    haave_choir = {HAAVE_CHOIR, 0, {0}, 76}, haave_kpad = {HAAVE_CHOIR, 0, {0}, 96};   /* (the keys') */
static void haave_hold_set(haave_held_t *h, const uint8_t *want, uint32_t n)
{
    uint32_t i, j, vel = h->vel;
    for (i = 0; i < h->n; i++) {
        for (j = 0; j < n && want[j] != h->note[i]; j++)
            ;
        if (j == n)
            haave_ref_off(h->part, h->note[i]);
    }
    for (j = 0; j < n; j++) {
        for (i = 0; i < h->n && h->note[i] != want[j]; i++)
            ;
        if (i == h->n)
            haave_ref_on(h->part, want[j], vel);
    }
    for (j = 0; j < n; j++)
        h->note[j] = want[j];
    h->n = (uint8_t)n;
}

/* 1 = part p sounds note held (gate on): the budget has not taken it */
static int haave_gated(uint32_t p, uint32_t note)
{
    const track_t *t = &trk[p];
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate && t->v[i].note == note)
            return 1;
    return 0;
}
static uint32_t haave_ngated(void)       /* the voices held now, every part */
{
    uint32_t p, i, n = 0;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            n += trk[p].v[i].active && trk[p].v[i].gate;
    return n;
}

/* the held notes the budget took (the CPU's shedding, a crowd of strikes): on again (a sixteenth's look) */
static uint32_t haave_heals;             /* (how many: the tests) */
static void haave_heal(void)
{
    haave_held_t *const H[3] = {&haave_pad, &haave_bass, &haave_choir};   /* (not the instrument's: a held note of a
                                                                         * sound that fades by itself, KEYS, ends there,
                                                                         * as a key held on a piano) */
    uint32_t h, j;
    for (h = 0; h < 3u; h++) {
        track_t *t = &trk[H[h]->part];
        if (t->xf_on || t->eng_req != t->engine)         /* (a sound change: its notes come after the fade) */
            continue;
        for (j = 0; j < H[h]->n; j++)
            if (!haave_gated(H[h]->part, H[h]->note[j])) {
                trk_note_on(t, H[h]->note[j], H[h]->vel);
                haave_heals++;
            }
    }
}

/* a bell (part 2): struck, held HAAVE_STRIKE, then let go to ring out on its release (a note-off in the block of its
 * note-on would end it before its attack: no sound) */
#define HAAVE_NBELL 6u
#define HAAVE_STRIKE (FS / 8u)
static uint8_t haave_bn[HAAVE_NBELL];
static int32_t haave_bt[HAAVE_NBELL];

/* every voice held: the oldest bell let go first (it rings out on its release), so the next note takes no held pad,
 * bass, voice or key note */
static void haave_room(void)
{
    uint32_t i;
    while (haave_ngated() >= NVOICE) {
        uint32_t o = HAAVE_NBELL;
        for (i = 0; i < HAAVE_NBELL; i++)
            if (haave_bn[i] && haave_bt[i] < (int32_t)HAAVE_STRIKE && (o == HAAVE_NBELL || haave_bt[i] < haave_bt[o]))
                o = i;                                   /* (struck in an earlier block: its attack under way) */
        if (o == HAAVE_NBELL)
            break;
        trk_note_off(&trk[HAAVE_BELL], haave_bn[o]);
        haave_bn[o] = 0;
    }
}
static void haave_strike(uint32_t layer, uint32_t part, uint32_t note, uint32_t vel)
{
    uint32_t i, k = 0, user = layer & DL_USER;
    layer &= ~DL_USER;
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
    haave_room();
    if (!user && haave_ngated() >= NVOICE)
        return;                                      /* (still none free: a layer's bell steps aside rather than take
                                                      * a held note; a bell played by hand never) */
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

static void haave_keys_held(void);

/* the right white keys' instrument (SAVE ARP SEQ, haave_ui.c): SYNTH on the voice's part (its own sound then: the VOX
 * layer rests), BELLS on the bells' part, BASS on the bass's (a punchier sound then: the BASS layer rests) */
enum { DI_SYNTH, DI_BELLS, DI_BASS, DI_N };
static volatile uint8_t haave_inst = DI_BELLS;
static volatile uint8_t haave_resync_part;   /* the parts haave_ui.c gave another sound: their notes were cut */

/* the layers sounding: BREATHE rests all but the pad */
static uint32_t haave_lay(void) { return haave_breathe ? haave_layers & (1u << DL_PAD) : haave_layers; }

/* the held layers to the chord and the toggles now */
static void haave_voice_held(void)
{
    uint8_t want[4];
    int32_t st[3], lo = haave_lo();
    uint32_t n = 0, i, j, x, L = haave_lay();
    haave_tones(st);
    if (haave_on && haave_pad_noise && ((L >> DL_PAD) & 1u || (L >> DL_SHIMMER) & 1u))
        want[n++] = (uint8_t)haave_note(0, lo);          /* noise: one voice, its pitch no matter (the engine's TRK 0) */
    else if (haave_on && (L >> DL_PAD) & 1u) {
        for (i = 0; i < 3u; i++)
            want[n++] = (uint8_t)haave_note(st[i], lo - 3);
        for (i = 1; i < n; i++)                          /* ascending (the shimmer goes above the top) */
            for (j = i; j > 0 && want[j - 1] > want[j]; j--) {
                x = want[j];
                want[j] = want[j - 1];
                want[j - 1] = (uint8_t)x;
            }
    }
    if (haave_on && !haave_pad_noise && (L >> DL_SHIMMER) & 1u) {
        want[n] = n ? (uint8_t)(want[n - 1u] + 12u) : (uint8_t)haave_note(4, lo + 12);
        while (want[n] > 84u)                            /* (C6: higher, a bright pad turns thin and fizzy) */
            want[n] = (uint8_t)(want[n] - 12u);
        n++;
    }
    if (n)
        haave_act[DL_PAD] = haave_act[DL_SHIMMER] = 255;
    haave_hold_set(&haave_pad, want, n);
    n = 0;
    if (haave_on && (L >> DL_BASS) & 1u && haave_inst != DI_BASS)
        want[n++] = (uint8_t)haave_above(haave_note(0, lo - 22), 28u);   /* (E1, 41 Hz: lower, a small speaker loses it) */
    if (n)
        haave_act[DL_BASS] = 255;
    haave_hold_set(&haave_bass, want, n);
    n = 0;
    if (haave_on && (L >> DL_CHOIR) & 1u && haave_inst != DI_SYNTH) {
        uint32_t v = haave_above(haave_note(st[haave_choir_tone % 3u], lo + haave_vox_reg), 40u);   /* (E2 ..) */
        while (v > 72u)                                  /* (.. C5: higher, the voice turns shrill) */
            v -= 12u;
        want[n++] = (uint8_t)v;
    }
    if (n)
        haave_act[DL_CHOIR] = 255;
    haave_hold_set(&haave_choir, want, n);
    haave_keys_held();                                   /* (the keys' notes follow the chord) */
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
/* the white keys: the first seven the layers, the other nine the instrument (haave_inst), every note fitting the chord:
 *   SYNTH  the chord's scale (its steps that fit the chord, about five an octave) from C4 up, held while the key is
 *   BELLS  the first the GONG, then the chord's tones from C4 up, struck
 *   BASS   the chord's scale in the bass, from E1 .. E2 up, one at a time (the latest held), punchy
 * held notes follow the chord: a chord change moves them to the new one's; the black keys: the first five the chords
 * (again: the same one coloured), the other six the sprinkles (haave_spr.c) */
#define DW_INST 7u
#define DW_N 9u
static uint32_t haave_keys;              /* the keys as the last block saw them */

/* the chord's tones going up from lo: note i (0..2 the three in the octave from lo, then the same octaves higher), the
 * whole an octave lower while note `top` would be above hi (the bells fold above C7, a pad turns thin above E6: the
 * run would turn back) */
static uint32_t haave_chord_up(uint32_t i, int32_t lo, uint32_t top, int32_t hi)
{
    int32_t st[3], t[3], x;
    uint32_t a, b;
    haave_tones(st);
    for (a = 0; a < 3u; a++)
        t[a] = haave_fold(haave_root + haave_step(haave_cur + st[a]), lo);
    for (a = 1; a < 3u; a++)                             /* ascending */
        for (b = a; b > 0 && t[b - 1u] > t[b]; b--) {
            x = t[b];
            t[b] = t[b - 1u];
            t[b - 1u] = x;
        }
    while (t[top % 3u] + 12 * (int32_t)(top / 3u) > hi)
        for (a = 0; a < 3u; a++)
            t[a] -= 12;
    return (uint32_t)(t[i % 3u] + 12 * (int32_t)(i / 3u));
}

/* the chord's scale going up from lo: its note i, of the key's notes those that fit the chord now (haave_fits: a whole
 * tone at least from each of its tones; about five an octave) */
static uint32_t haave_scale_up(uint32_t i, int32_t lo)
{
    int32_t n;
    uint32_t k = 0, d;
    for (n = lo; n < lo + 60; n++) {
        d = (uint32_t)(((n - (int32_t)haave_root) % 12 + 12) % 12);
        if (((0xAB5u >> d) & 1u) == 0u && !haave_minor)  /* (major: 0 2 4 5 7 9 11) */
            continue;
        if (((0x5ADu >> d) & 1u) == 0u && haave_minor)   /* (minor: 0 2 3 5 7 8 10) */
            continue;
        if (!haave_fits(n))
            continue;
        if (k++ == i)
            return (uint32_t)n;
    }
    return (uint32_t)lo;
}

/* the instrument's note for its key i (0..8) */
static uint32_t haave_key_note(uint32_t i)
{
    if (haave_inst == DI_BASS) {                         /* (from E1 .. E2 whatever OCT: a bass stays a bass) */
        int32_t lo = haave_lo() - 22;
        return haave_scale_up(i, lo < 28 ? 28 : lo > 40 ? 40 : lo);
    }
    if (haave_inst == DI_SYNTH)
        return haave_scale_up(i, haave_lo() + 5);
    return haave_chord_up(i ? i - 1u : 0u, haave_lo() + 5, 7u, 96);
}

static uint16_t haave_kplay;             /* the instrument's keys held, a bit each (0: the lowest) */
static uint32_t haave_kage[DW_N], haave_kseq;   /* .. when each was pressed (the latest win when the voices are few) */

/* the instrument's held notes to the chord now: SYNTH as many as the voices allow (all of them, one kept for the
 * bells, and its part's own: an FM6 sound has 6, the voice 4), BASS one, the latest pressed first; BELLS none (struck) */
static void haave_keys_held(void)
{
    uint8_t want[8];
    uint32_t n = 0, chosen = 0, k, best, part = haave_inst == DI_BASS ? HAAVE_BASS : HAAVE_CHOIR, room;
    uint32_t others = haave_pad.n + haave_bass.n + haave_choir.n, poly = trk_nvoice(&trk[part]);
    if (haave_inst == DI_BELLS || haave_kpad.part != part) {   /* (another instrument: what the last held lets go) */
        haave_hold_set(&haave_kpad, want, 0);
        haave_kpad.part = (uint8_t)part;
        if (haave_inst == DI_BELLS)
            return;
    }
    room = NVOICE > others + 1u ? NVOICE - others - 1u : 1u;
    if (poly < room)
        room = poly;
    if (haave_inst == DI_BASS)
        room = 1;
    while (n < room && n < 8u) {
        for (k = 0, best = DW_N; k < DW_N; k++)
            if ((haave_kplay >> k) & 1u && !((chosen >> k) & 1u) && (best == DW_N || haave_kage[k] > haave_kage[best]))
                best = k;
        if (best == DW_N)
            break;
        chosen |= 1u << best;
        want[n++] = (uint8_t)haave_key_note(best);
    }
    if (n > haave_kpad.n)
        haave_room();                                    /* (a bell lets go before a key takes a held voice) */
    haave_hold_set(&haave_kpad, want, n);
    if (n)
        haave_act[haave_inst == DI_BASS ? DL_BASS : DL_CHOIR] = 255;
}

/* the GONG: the chord's root low on the world's bells, its octave softly above */
static void haave_gong(uint32_t vel)
{
    uint32_t n = (uint32_t)haave_fold(haave_root + haave_step(haave_cur), haave_lo() - 12);
    haave_strike(DL_MELODY | DL_USER, HAAVE_BELL, n, vel);
    haave_strike(DL_MELODY | DL_USER, HAAVE_BELL, n + 12u, vel / 2u);
}

static void haave_spr_key(uint32_t k);   /* (haave_spr.c) */

static void haave_keys_block(void)
{
    uint32_t cur = fm1_in.notes, down = cur & ~haave_keys, up = haave_keys & ~cur, k;
    haave_keys = cur;
    for (k = 0; (down | up) && k < 27u; k++) {
        uint32_t p, dn = (down >> k) & 1u;
        if (!(((down | up) >> k) & 1u))
            continue;
        p = key_place(k);
        if (key_black(k)) {
            if (!dn)
                continue;
            if (p >= 5u)                                 /* a sprinkle (stopped too) */
                haave_spr_key(p - 5u);
            else if (haave_on) {                         /* a chord; the one sounding: coloured, or plain again */
                int32_t d = HAAVE_BLACK[haave_minor & 1u][p];
                haave_jump(d, haave_cur == d && haave_deg == d ? !haave_bright : 0u);
            }
        } else if (p >= DW_INST && p < DW_INST + DW_N) { /* the instrument (stopped too) */
            uint32_t i = p - DW_INST;
            if (dn) {
                haave_kplay |= (uint16_t)(1u << i);
                haave_kage[i] = ++haave_kseq;
                if (haave_inst == DI_BELLS) {
                    if (i)
                        haave_strike(DL_MELODY | DL_USER, HAAVE_BELL, haave_key_note(i), 74);
                    else
                        haave_gong(100);
                }
            } else
                haave_kplay &= (uint16_t)~(1u << i);
            haave_keys_held();
        } else if (dn && p < DL_N) {                     /* a layer on / off */
            haave_layers ^= (uint8_t)(1u << p);
            haave_voice_held();
        }
        if (dn)
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
/* SPACE and ECHO: the more of them, the further away rather than the louder (haave_ui.c haave_apply sets the trim) */
static volatile int32_t haave_trim_to = DG_ONE;
static int32_t haave_trim = DG_ONE;
#define DG_TRIM_STEP (DG_ONE / 22050)            /* (the whole way in half a second: no zipper as a knob turns) */

/* PUMP (a punch-in): on every beat the whole sound dips softly (-8 dB, in ~20 ms) and swells back over three quarters
 * of the beat, a sidechain's breath; its depth glides in and out (~90 ms). MOTION's drift: the sound leaning left and
 * right, a cycle every two bars, the far side down to -6 dB at most. Both on the mix here, after the effects */
static uint32_t haave_pump_pos = 0xFFFFFFu;  /* samples into the beat (haave_sixteenth: 0 on each beat) */
static uint32_t haave_beat_len = FS;     /* a beat's samples now (haave_player: WARP too) */
static int32_t haave_pump_d, haave_pan_d;   /* the depths now, Q15 */
static uint32_t haave_pan_ph;
static int32_t haave_pump_env(uint32_t pos)   /* how far down at pos samples into the beat, 0..32768 */
{
    uint32_t a = FS / 50u, r = haave_beat_len / 4u * 3u, u;
    if (pos < a)
        return (int32_t)(pos * 32768u / a);
    if (pos >= r)
        return 0;
    u = 32768u - (uint32_t)(((uint64_t)(pos - a) << 15) / (r - a));
    return (int32_t)((u * u) >> 15);                     /* (back up quickly, then gently: a breath) */
}
static void haave_gain_block(int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t tt = haave_trim_to, gl = 32768, gr = 32768, e = 0, de = 0, g = 0;
    int steady;
    haave_pump_d = (haave_plvl[DP_PUMP] * 19661) >> 15;   /* (0.6: -8 dB at the dip; fading with the punch-in) */
    if (haave_pump_d) {                                  /* (the curve a block at a time, straight between: no divide a
                                                          * sample) */
        e = haave_pump_env(haave_pump_pos);
        de = (haave_pump_env(haave_pump_pos + n) - e) / (int32_t)n;
    }
    if (haave_pump_pos < 0xFFFFFFu)
        haave_pump_pos += n;
    {
        int32_t to = haave_pan_to;
        haave_pan_d += to > haave_pan_d ? (to - haave_pan_d < 64 ? to - haave_pan_d : 64)
                                        : (haave_pan_d - to < 64 ? to - haave_pan_d : -64);
        haave_pan_ph += n * (0xFFFFFFFFu / (8u * haave_beat_len));
    }
    if (haave_pan_d) {
        int32_t p = osc_sine(haave_pan_ph);
        if (p > 0)
            gl = 32768 - ((haave_pan_d * p) >> 15);
        else
            gr = 32768 + ((haave_pan_d * p) >> 15);
    }
    if (haave_gain == DG_ONE && haave_gain_to == DG_ONE && haave_trim == tt && tt == DG_ONE && !haave_pump_d &&
        !haave_pan_d)
        return;
    steady = haave_gain == haave_gain_to && haave_trim == tt;
    if (steady)
        g = (int32_t)(((int64_t)haave_gain * haave_gain >> 24) * haave_trim >> 24);
    for (i = 0; i < n; i++) {
        int32_t gs;
        if (!steady) {
            if (haave_gain < haave_gain_to)
                haave_gain = haave_gain + haave_gain_step < haave_gain_to ? haave_gain + haave_gain_step : haave_gain_to;
            else if (haave_gain > haave_gain_to)
                haave_gain = haave_gain - haave_gain_step > haave_gain_to ? haave_gain - haave_gain_step : haave_gain_to;
            if (haave_trim < tt)
                haave_trim = haave_trim + DG_TRIM_STEP < tt ? haave_trim + DG_TRIM_STEP : tt;
            else if (haave_trim > tt)
                haave_trim = haave_trim - DG_TRIM_STEP > tt ? haave_trim - DG_TRIM_STEP : tt;
            g = (int32_t)(((int64_t)haave_gain * haave_gain >> 24) * haave_trim >> 24);   /* (a gentle feel: the
                                                                                         * square of the ramp) */
        }
        gs = g;
        if (haave_pump_d) {
            gs = (int32_t)(((int64_t)gs * (32768 - ((haave_pump_d * e) >> 15))) >> 15);
            e += de;
        }
        out[2u * i] = (int32_t)(((int64_t)out[2u * i] * gs >> 24) * gl >> 15);
        out[2u * i + 1u] = (int32_t)(((int64_t)out[2u * i + 1u] * gs >> 24) * gr >> 15);
    }
}

/* the punch-in effects playing -> `want` (bit per button), at a beat: their levels follow (haave_punch_block) */
static void haave_punch_sync(uint32_t want)
{
    if (want != haave_punch) {
        haave_punch = (uint8_t)want;
        haave_seen++;
    }
}

/* a block of the punch-ins: each level toward its button (a beat each way, at the tempo now), and what it moves:
 *   WASH     every part's reverb send up to full and the dry sound down to 40 %: everything into the room
 *   PUMP     haave_gain_block's dip on each beat, its depth
 *   FREEZE   perform.c's, pressed while its level is up, its level perf_haave_lvl (SHIMMER the same)
 *   SHIMMER  perform.c's OCT UP
 *   SWIRL    perform.c's PHASER, its depth perf_k[3] (MOTION's softer one under it); not over FREEZE or SHIMMER
 *   BREATHE  the layers but the pad rest (haave_breathe: their notes ring out) and GLOW goes down */
static void haave_voice_held(void);
static void haave_punch_block(uint32_t n)
{
    static uint8_t pressed;              /* perform.c's effects pressed: FREEZE, SHIMMER, the PHASER (bit 2, 3, 4) */
    int32_t step = (int32_t)((32768u * n) / (haave_beat_len > n ? haave_beat_len : n)) + 1, w, ph;
    uint32_t i, k3;
    for (i = 0; i < DP_NPUNCH; i++) {
        int32_t to = (haave_punch >> i) & 1u ? 32768 : 0, d = to - haave_plvl[i];
        haave_plvl[i] += d > step ? step : d < -step ? -step : d;
    }
    for (i = DP_FREEZE; i <= DP_SHIMMER; i++) {         /* FREEZE, SHIMMER: pressed from the start of the fade in to the
                                                         * end of the fade out */
        uint32_t on = haave_plvl[i] > 0 || ((haave_punch >> i) & 1u);
        if (on != ((pressed >> i) & 1u)) {
            perf_press(i == DP_FREEZE ? PF_FRZ : PF_OUP, (int)on);
            pressed ^= (uint8_t)(1u << i);
        }
    }
    perf_haave_lvl = haave_plvl[DP_FREEZE] > haave_plvl[DP_SHIMMER] ? haave_plvl[DP_FREEZE] : haave_plvl[DP_SHIMMER];
    /* the PHASER: SWIRL's level or MOTION's depth, the larger; resting under FREEZE and SHIMMER (perf_k[3] is their
     * shimmer's feedback too) */
    ph = haave_plvl[DP_SWIRL] > haave_motion_depth ? haave_plvl[DP_SWIRL] : haave_motion_depth;
    if (haave_plvl[DP_FREEZE] || haave_plvl[DP_SHIMMER] || ((pressed >> DP_FREEZE) & 1u) || ((pressed >> DP_SHIMMER) & 1u))
        ph = 0;
    k3 = ph ? 100u - (uint32_t)ph * 100u / 32768u : 0u;
    perf_k[3] = (int8_t)k3;
    if ((ph > 0) != ((pressed >> DP_SWIRL) & 1u)) {
        perf_press(PF_PHS, ph > 0);
        pressed ^= (uint8_t)(1u << DP_SWIRL);
    }
    /* WASH: the sends and the dry level (SPACE's, from haave_ui.c) */
    w = haave_plvl[DP_WASH];
    for (i = 0; i < 4u; i++) {
        int32_t b = haave_rev_base[i];
        trk[i].p[P_REV] = (int16_t)(b + (((127 - b) * w) >> 15));
    }
    fx_dry_to = (int32_t)(((int64_t)haave_dry_space * (32768 - ((w * 19661) >> 15))) >> 15);
    /* BREATHE */
    if (((haave_punch >> DP_BREATHE) & 1u) != haave_breathe) {
        haave_breathe = (uint8_t)((haave_punch >> DP_BREATHE) & 1u);
        haave_voice_held();
    }
    haave_fx_glow = (int8_t)-((haave_plvl[DP_BREATHE] * 24) >> 15);
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
        for (i = 0; i < 6u; i++) {                       /* a step, and another past a note that would rub */
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
        if (i == 6u)
            return;                                      /* (none fits: a rest, never a note that rubs) */
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
    /* TIDE: a layer comes or goes (not while BREATHE rests them) */
    if (q == 0u && haave_tide >= DT_FLOW && !haave_breathe && (!haave_tide_wait || !--haave_tide_wait)) {
        haave_tide_layer();
        haave_tide_wait = (uint8_t)(haave_tide == DT_FLOW ? 8u + haave_rand(9u) : 4u + haave_rand(5u));
    }
    haave_heal();
    L = haave_lay();
    if (q % 4u == 0u) {                                  /* a beat: the punch-ins held now play it; let go, they end */
        haave_pump_pos = 0;                              /* (PUMP's dip: from here) */
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
    if (haave_resync_part) {                             /* a part's new sound (an instrument chosen): its notes were cut */
        haave_held_t *const H[4] = {&haave_pad, &haave_bass, &haave_choir, &haave_kpad};
        uint32_t m = haave_resync_part, h;
        haave_resync_part = 0;
        for (h = 0; h < 4u; h++)
            if ((m >> H[h]->part) & 1u) {
                H[h]->n = 0;
                memset(haave_nref[H[h]->part], 0, sizeof haave_nref[0]);
            }
        haave_voice_held();
    }
    if (haave_resync) {                                  /* new sounds: their notes were cut (ui.c apply_preset_to) */
        haave_resync = 0;
        haave_pad.n = haave_bass.n = haave_choir.n = haave_kpad.n = 0;
        memset(haave_nref, 0, sizeof haave_nref);
        haave_resync_part = 0;
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
        uint32_t six = (uint32_t)(((uint64_t)(FS * 15u / haave_bpm << HAAVE_BEH[haave_style % DS_N].half) << 16) /
                                  (uint32_t)haave_warp);   /* a sixteenth: 60 / bpm / 4 s (half time x2; WARP slower) */
        haave_clock += (int32_t)six;
        haave_beat_len = 4u * six;
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
    haave_punch_block(n);
    {   /* WARP: HOME held, the music glides to half speed (~0.8 s), let go, back */
        int32_t to = haave_warp_bit && (fm1_in.buttons & haave_warp_bit) ? 32768 : 65536, d = to - haave_warp;
        haave_warp += d / 64 ? d / 64 : d;
    }
    if (oct != haave_oct) {                              /* the register moved: the held notes */
        oct = haave_oct;
        haave_voice_held();
    }
    haave_player(n);
}

#include "haave_spr.c"                   /* the sprinkles (black keys 6..11) */
