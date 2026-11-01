/* sammi.stutter: Hundekraft's STUTTER effect (Hundekraft OP-1 Workshop 3.1.1, ext/kennel/synth_sdk/plugins/stutter/
 * stutter.c) as a native Opus 2 effect type ("hkstutter") for the synth, the drum and the master.
 *
 * A beat-locked stutter, Hundekraft's code: the OP-1's beat clock (the metronome's) cuts time into slices; at each
 * slice a dice roll decides whether the slice plays through or stutters: its first part is played and recorded, then
 * repeated until the slice ends, so the repeats always land on the grid.
 *   blue   RATE     slice length: 1/4, 1/8, 1/8 triplet, 1/16, 1/16 triplet, 1/32 of a beat (6 steps)
 *   green  MODE     REPEAT (plain), BRAKE (each repeat slower and lower, a tape stop), REVERSE (repeats backwards),
 *                   GATE (no repeats: the slice is chopped into short bursts of the live sound)
 *   white  SHAPE    edge softness of each repeat (2 .. 12 ms fades); in GATE the open time (10 .. 90 %)
 *   orange CHANCE   how many slices stutter: none .. all
 *   shift + blue    REPEATS: how many parts a stuttering slice is cut into: 2, 3, 4, 6, 8, 16 (parameter 5, saved
 *                   with the sound; one step a detent, round and round as kennel's page-2 word went)
 *   shift + orange  FREEZE: clockwise on, anticlockwise off: keep repeating the part playing now, across slices
 * With no clock (the tape stopped while it follows the tape, or no tempo yet) the grid runs on at 120 BPM.
 * Ring buffer: 65536 samples (1.49 s); a slice longer than that (1/4 under 40 BPM) is cut short. While frozen the ring
 * keeps the frozen part. On the master: a mono sum, the difference added to both sides, as kennel's dispatcher did.
 *
 * What this port changes (GPL-3.0 section 5: modified 2026-11-01 by sammi), and nothing else:
 *   - the state lives in the instance's memory (struct opus_fx: state, and the ring in bulk), not in a static array
 *     per kennel FX position and op1_fx_scratch(pos);
 *   - parameters arrive as 0 .. 32767 (this type's limits), so kennel's op1_fx_param_q15 is a clamp; the defaults are
 *     kennel's plugin.json knob positions (and its page-2 default for word 5);
 *   - kennel's encoder presses become the Opus effect type's shift layer (Opus has no press handler for an effect
 *     type): blue's page 2 (REPEATS, word 5) is shift + blue, orange's latch (FREEZE) shift + orange. As on kennel,
 *     FREEZE is kept per effect position (synth, drum, master) and is not saved with the sound;
 *   - init clears the ring as kennel's does (128 KB in 32-bit stores; kennel did it in the audio callback, an Opus init
 *     may run in the audio interrupt too); it also resets rec, which kennel left to its zeroed statics;
 *   - the master: kennel's mono-sum routing, here in stereo();
 *   - the peak meter word and the OS SVG screen: the Opus effect page instead (view): the four values, the last 16
 *     slices as kennel's projected picture drew them, and REPEATS and FREEZE in the caption;
 *   - the beat clock is read where kennel's op1_beat_clock reads it (deep, read-only: see beat_clock below).
 *
 * Copyright (C) 2026 Hundekraft. GPL-3.0-or-later with the OP-1 firmware permission. This port: GPL-3.0-or-later with
 * the same permission (see the repository's LICENSE and NOTICE). */
#include <stdint.h>
#include "opus_plugin.h"
#include "op1fw.h"

#define OP1_FX_BLOCK 64
#define NBUF 65536
#define MASK (NBUF - 1)
#define HIST 16
enum { M_REPEAT, M_BRAKE, M_REVERSE, M_GATE };

static const uint8_t DIVS[6] = { 1, 2, 3, 4, 6, 8 };          /* slices per beat */
static const uint8_t REPS[6] = { 2, 3, 4, 6, 8, 16 };

/* ---- the beat clock (kennel's op1_beat_clock; read once per block, never per sample) ----
 * The stock metronome's beat clock (metronome.cpp state at 0x01AADABC, firmware #246, as kennel's SDK documents it).
 * It runs whether the metronome is heard or not, at the OP-1 tempo (BPM setting or MIDI clock), restarts on a beat
 * when the tape starts and, while it follows the tape, moves with the tape speed. Opus 2 has no tempo service, so
 * this is a deep read: only read, never written. Times in 1/16384 samples: beat length = 2646000 / BPM * 16384. */
typedef struct {
    int32_t len;            /* beat length, 1/16384 samples (<= 0: no clock yet) */
    int32_t next;           /* time to the next beat from the first sample of this block, 1/16384 samples */
    int32_t step;           /* clock advance per 64-sample block: 0x100000 = real time, else the tape speed */
    int32_t tape_pos;       /* tape position (samples) */
    uint8_t follows_tape;   /* 1: the clock moves with the tape */
} op1_beat_t;
#ifdef STUTTER_HOST
extern op1_beat_t host_beat;                                   /* tests/host sets it */
static inline void beat_clock(op1_beat_t *b) { *b = host_beat; }
#else
static inline void beat_clock(op1_beat_t *b)
{
    const volatile uint8_t *m = (const volatile uint8_t *)0x01AADABC;
    b->len = *(const volatile int32_t *)(m + 0x1C);
    b->next = *(const volatile int32_t *)(m + 0x20);
    b->follows_tape = m[0x14];
    uint32_t sp = *(const volatile uint32_t *)0xFF802970;       /* tape speed: phase step (sp >> 21) << 14 */
    b->step = b->follows_tape ? (int32_t)((sp >> 21) << 14) : 0x100000;
    b->tape_pos = *(const volatile int32_t *)0xFF802974 >> 6;  /* 1/64 samples */
}
#endif

typedef struct {
    uint32_t w;                     /* write index into the ring */
    int32_t bp;                     /* free-running beat phase (no clock), 1/16384 samples to the next beat */
    int32_t pos, len, sub;          /* sample in the slice, slice length, part length */
    int32_t ps, part;               /* sample in the part, part number */
    uint32_t start;                 /* ring index of the slice start (the recorded part) */
    uint8_t stut, mode, faded;      /* this slice stutters; its mode; the last slice ended faded out */
    int32_t fade, finc, duty;       /* fade samples, 32768 / fade, gate open samples per part */
    int32_t rd, rate;               /* BRAKE: read position in the part (Q16) and rate (Q16) */
    int32_t gin;                    /* fade-in after a stutter, samples left */
    uint8_t latch, frozen;
    int32_t rec;                    /* frozen: samples of the part still to record (then the ring is kept) */
    uint32_t rng;
    /* screen */
    int32_t slice;                  /* slice number in the beat */
    uint32_t hist;                  /* stutter flags of the last HIST slices, bit 0 = newest */
    uint8_t ndiv, reps;
    uint8_t seeded;                 /* this port: the instance learnt its position (the dice's seed) */
} st_t;

/* kennel's op1_fx_page[pos][3]: FREEZE per effect position (0 synth, 1 drum, 2 master); the key thread writes it
 * (shift + orange), the audio reads it. Not saved, as kennel's latch. */
static volatile uint8_t freeze_latch[3];
/* what the page shows of the position's instance (kennel's screen read st[pos]); the audio writes, the display reads */
static volatile struct { int32_t slice; uint32_t hist; uint8_t frozen, mode; } shown[3];

static unsigned sx_range(int32_t q, unsigned count) { unsigned v = (unsigned)(q < 0 ? 0 : q); unsigned n = v * count >> 15; return n < count ? n : count - 1; }
static unsigned sx_select(int32_t q, unsigned count) { return ((unsigned)(q < 0 ? 0 : q) >> 9) % count; }
static inline int32_t q15(const int16_t *p, int i) { return p[i] < 0 ? 0 : p[i]; }   /* this type's params: 0..32767 */
static inline uint32_t lcg(uint32_t *s) { return *s = *s * 1664525u + 1013904223u; }

/* kennel's sx_init(pos): the state, and the ring cleared at once with 32-bit stores, as kennel does it (without its
 * FX_SPREAD_INIT option). The clear is heard: REVERSE while frozen reads the part's end before it is recorded, which is
 * silence after a clear. rec is also reset (kennel's state started zeroed; it is only read once a freeze sets it). */
static void sx_reset(st_t *s, int pos, int16_t *ring)
{
    s->w = 0; s->bp = -1; s->pos = 0; s->len = 0; s->sub = 1; s->start = 0; s->stut = 0; s->faded = 0;
    s->gin = 0; s->latch = freeze_latch[pos]; s->frozen = 0; s->rec = 0;
    s->rng = 0x2545F491u + (uint32_t)pos * 0x9E3779B9u;
    s->slice = 0; s->hist = 0;
    if (ring) for (int i = 0; i < NBUF / 2; i++) ((uint32_t *)(void *)ring)[i] = 0;
}

/* the next grid point in this block: its sample 0..63 or -1; *len = slice length (samples, real time) */
static int grid(st_t *s, int div, int32_t *slen)
{
    op1_beat_t c;
    beat_clock(&c);
    int32_t len = c.len, p, step;
    const int moving = len >= (256 << 14) && c.step > 0 && c.step <= 0x400000 && c.next >= 0 && c.next <= len;
    if (len < (256 << 14)) len = 22050 << 14;                                /* no clock: 120 BPM */
    if (moving) { p = c.next; step = c.step; }
    else { p = s->bp < 0 || s->bp > len ? len : s->bp; step = 0x100000; }
    const int32_t np = p - step;
    s->bp = np < 0 ? np + len : np;
    const int32_t sq = step >> 14;
    const int32_t bl = sq > 0 && sq != 64 ? len / sq * 64 : len >> 14;      /* the beat in real time, samples */
    *slen = bl / div;
    const int32_t q = len / div;
    for (int j = div - 1; j >= 0; j--) {                                     /* the earliest point in [0, step) */
        const int32_t t = p - j * q;
        if (t < 0 || t >= step) continue;
        s->slice = j == 0 ? 0 : div - j;                                     /* slice that starts here (0 = on the beat) */
        return step == 0x100000 ? t >> 14 : (int)(((int64_t)t * 64) / step);
    }
    return -1;
}

static void slice_start(st_t *s, int32_t slen, const int16_t *p, int pos)
{
    const int32_t chance = q15(p, 3);
    const int32_t shape = q15(p, 2);
    const int32_t w5 = p[5] < 0 ? 0 : p[5];
    const int reps = REPS[sx_select(w5, 6)];
    const int frz = freeze_latch[pos] != 0;
    if (slen > NBUF - 64) slen = NBUF - 64;
    if (slen < 64) slen = 64;
    s->len = slen;
    s->pos = 0;
    s->ps = 0;
    s->part = 0;
    s->mode = (uint8_t)(sx_range(q15(p, 1), 4));
    const uint8_t was = s->stut;
    if (frz) {
        s->stut = 1;
        if (!s->frozen) { s->frozen = 1; s->start = s->w; s->sub = slen / reps; s->rec = s->sub; }   /* freeze this slice's first part */
    } else {
        s->frozen = 0;
        s->stut = (uint8_t)((int32_t)((lcg(&s->rng) >> 17) & 0x7FFF) < chance);
        s->start = s->w;
        s->sub = slen / reps;
    }
    if (s->sub < 32) s->sub = 32;
    int32_t f = 88 + ((shape * 441) >> 15);                                 /* 2 .. 12 ms */
    if (f > s->sub / 4) f = s->sub / 4;
    s->fade = f < 1 ? 1 : f;
    s->finc = 32768 / s->fade;
    s->duty = (int32_t)(((int64_t)s->sub * (3277 + ((shape * 26214) >> 15))) >> 15);
    s->rd = 0;
    s->rate = 65536;
    s->gin = was && !s->stut ? s->fade : 0;                                   /* fade the dry sound back in */
    s->faded = was && s->stut && s->mode != M_GATE;
    s->hist = (s->hist << 1) | s->stut;
    s->reps = (uint8_t)reps;
}

/* samples [a, b) of the block */
static void run(st_t *s, int16_t *buf, int16_t *ring, int a, int b)
{
    for (int i = a; i < b; i++) {
        const int32_t x = buf[i];
        if (!s->frozen || s->rec > 0) { ring[s->w & MASK] = (int16_t)x; if (s->rec > 0) s->rec--; }   /* frozen: keep the part */
        int32_t y = x;
        if (s->stut && s->len) {
            const int32_t sub = s->sub, ps = s->ps, part = s->part, f = s->fade, fi = s->finc;
            int32_t e = 32768;                                          /* unity: the repeats are exact copies */
            if (s->mode == M_GATE) {
                if (ps >= s->duty) e = ps < s->duty + f ? 32768 - (ps - s->duty) * fi : 0;
                else if (part && ps < f) e = ps * fi;
            } else {
                if (part == 0 && !s->frozen) {                               /* the live part, recorded */
                    if (s->faded && ps < f) e = ps * fi;
                } else {
                    int32_t r;
                    if (ps == 0 && s->mode == M_BRAKE) {                    /* each repeat slower: -12 %, down to 30 % */
                        s->rate = 65536 - part * 7864;
                        if (s->rate < 19661) s->rate = 19661;
                        s->rd = 0;
                    }
                    if (s->mode == M_BRAKE) { r = s->rd >> 16; s->rd += s->rate; }
                    else if (s->mode == M_REVERSE) r = sub - 1 - ps;
                    else r = ps;
                    y = ring[(s->start + (uint32_t)r) & MASK];
                    if (ps < f) e = ps * fi;
                }
                if (ps >= sub - f) { const int32_t d = (sub - ps) * fi; if (d < e) e = d; }
            }
            y = (y * e) >> 15;
        } else if (s->gin > 0) {
            y = (y * (32768 - s->gin * s->finc)) >> 15;
            s->gin--;
        }
        buf[i] = (int16_t)y;
        s->w++;
        s->pos++;
        if (++s->ps >= s->sub) { s->ps = 0; s->part++; }
    }
}

/* kennel's sx_render(pos, buf, p) on the instance's state and ring */
static void sx_render(st_t *s, int16_t *ring, int pos, int16_t *buf, const int16_t *p)
{
    if (!ring) return;
    if (!s->seeded) { s->rng = 0x2545F491u + (uint32_t)pos * 0x9E3779B9u; s->latch = freeze_latch[pos]; s->seeded = 1; }
    const int div = DIVS[sx_range(q15(p, 0), 6)];
    s->ndiv = (uint8_t)div;
    int32_t slen;
    const int off = grid(s, div, &slen);
    if (s->len == 0) slice_start(s, slen, p, pos);                           /* first block */
    if (off < 0) {
        run(s, buf, ring, 0, OP1_FX_BLOCK);
        if (s->pos >= s->len + s->len / 4) slice_start(s, slen, p, pos);    /* the clock jumped: start anew */
    } else {
        run(s, buf, ring, 0, off);
        slice_start(s, slen, p, pos);
        run(s, buf, ring, off, OP1_FX_BLOCK);
    }
    shown[pos].slice = s->slice;
    shown[pos].hist = s->hist;
    shown[pos].frozen = s->frozen;
    shown[pos].mode = s->mode;
}

/* ======================= the Opus effect type ======================= */

static void need(int stereo, struct opus_need *n)
{
    (void)stereo;
    n->state = sizeof(st_t);
    n->bulk = NBUF * sizeof(int16_t);                      /* 131,072 bytes: the ring */
    n->fast = 0;
}

/* The core calls init at every factory call (a sound loading, the instrument or the slot changing), in a thread or
 * the audio interrupt; which position it is for is learnt at the first block (the dice's seed is the position's, as
 * kennel's), so it resets with position 0's and the first render reseeds. */
static void init(struct opus_fx *fx)
{
    st_t *s = (st_t *)fx->state;
    const int pos = fx->stereo ? 2 : 0;
    sx_reset(s, pos, (int16_t *)fx->bulk);
    s->seeded = (uint8_t)fx->stereo;                       /* the master's position is known now */
}

static void mono(struct opus_fx *fx, short *buf, const short *par)
{
    const int pos = opus_instrument_now() == 1 ? 1 : 0;   /* the instrument this block plays: 0 synth, 1 drum */
    sx_render((st_t *)fx->state, (int16_t *)fx->bulk, pos, buf, par);
}

/* the master, as kennel's dispatcher ran a mono effect there: on m = (L + R) / 2, its change added to both sides */
static void stereo(struct opus_fx *fx, short *l, short *r, const short *par)
{
    int16_t m[OP1_FX_BLOCK], y[OP1_FX_BLOCK];
    for (int i = 0; i < OP1_FX_BLOCK; i++) y[i] = m[i] = (int16_t)((l[i] + r[i]) >> 1);
    sx_render((st_t *)fx->state, (int16_t *)fx->bulk, 2, y, par);
    for (int i = 0; i < OP1_FX_BLOCK; i++) {
        const int32_t d = y[i] - m[i];
        const int32_t a = l[i] + d, b = r[i] + d;
        l[i] = (short)(a > 32767 ? 32767 : (a < -32768 ? -32768 : a));
        r[i] = (short)(b > 32767 ? 32767 : (b < -32768 ? -32768 : b));
    }
}

/* the effect page's owner (0-7 the synth's slots, 8-15 the drum's, 16 the master) as a kennel FX position */
static int page_pos(void)
{
    const int o = opus_fx_owner();
    return o >= 16 ? 2 : (o >= 8 ? 1 : 0);
}

/* The shift layer: shift + blue REPEATS, one step a detent (cyclic, as kennel's page word read through % 6);
 * shift + orange FREEZE (clockwise on, anticlockwise off). Green and white keep the OS's fine steps. */
static int shift_knob(short *par, int encoder, int detents)
{
    if (encoder == 0) {
        int i = (int)sx_select(par[5], 6) + detents % 6;
        i = ((i % 6) + 6) % 6;
        par[5] = (short)(i * 512 + 256);                  /* the middle of step i in kennel's (word >> 9) % 6 */
        return 1;
    }
    if (encoder == 3) {
        if (detents > 0) freeze_latch[page_pos()] = 1;
        else if (detents < 0) freeze_latch[page_pos()] = 0;
        return 1;
    }
    return 0;
}

/* Its page, in the encoders' order (blue top left, green bottom left, white top right, orange bottom right):
 * rate, mode, shape (fade ms, or the gate's open time) and chance; the picture is kennel's projected one, the last 16
 * slices, newest at the right: a slice that stuttered is a tall bar (REPEAT full, BRAKE stepping down, REVERSE
 * slanted, GATE dashed), one that played through a short one; the caption says REPEATS and FREEZE. */
static void view(const short *par, struct opus_fx_view *v)
{
    static const char *const label[4] = { "rate", "mode", "shape", "chance" };
    static const char *const rates[6] = { "1/4", "1/8", "1/8t", "1/16", "1/16t", "1/32" };
    static const char *const modes[4] = { "repeat", "brake", "reverse", "gate" };
    static char value[4][12];
    static char caption[32];
    static short wave[OPUS_FX_WAVE];
    const int pos = page_pos();
    const int ri = (int)sx_range(q15(par, 0), 6), mi = (int)sx_range(q15(par, 1), 4);
    const int32_t shape = q15(par, 2);
    fw_snprintf(value[0], 12, "%s", rates[ri]);
    fw_snprintf(value[1], 12, "%s", modes[mi]);
    if (mi == M_GATE)
        fw_snprintf(value[2], 12, "%i%%", (int)((3277 + ((shape * 26214) >> 15)) * 100 >> 15));
    else
        fw_snprintf(value[2], 12, "%i ms", (int)(((88 + ((shape * 441) >> 15)) * 10 + 220) / 441));   /* 2 .. 12 */
    fw_snprintf(value[3], 12, "%i%%", (int)(q15(par, 3) * 100 / 32767));
    const uint32_t hist = shown[pos].hist;
    const int frozen = shown[pos].frozen || freeze_latch[pos];
    fw_snprintf(caption, 32, frozen ? "%i parts  freeze" : "%i parts", REPS[sx_select(par[5], 6)]);
    /* 16 slices x 8 points: a stuttered slice's bar fills points 1-6, a played-through one a short stub */
    for (int c = 0; c < HIST; c++) {
        const int on = (hist >> (HIST - 1 - c)) & 1;
        for (int k = 0; k < 8; k++) {
            int32_t y = 0;
            if (k >= 1 && k <= 6) {
                if (!on) y = k == 3 || k == 4 ? 6000 : 0;
                else if (mi == M_GATE) y = (k & 1) ? 26000 : 0;
                else if (mi == M_REVERSE) y = 30000 - (k - 1) * 4800;
                else if (mi == M_BRAKE) y = 26000 - (c & 3) * 5000;
                else y = 26000;
            }
            wave[c * 8 + k] = (short)y;
        }
    }
    for (int i = 0; i < 4; i++) { v->label[i] = label[i]; v->value[i] = value[i]; }
    v->wave[0] = wave;
    v->waves = 1;
    v->caption = caption;
}

/* kennel's plugin.json defaults (knob positions 0..32767): rate 17152 (1/16), mode 2304 (repeat), shape 8000, chance
 * 12000; its page-2 default for word 5, 13568 (4 parts). One detent turns a knob 1024 (step 8192 = 1024 x 8, as the
 * starter pack's rows). Parameter 5 is turned by the shift layer only (step 0: no encoder or LFO moves it). */
static const struct opus_effect STUTTER = {
    .size = sizeof(struct opus_effect),
    .name = "hkstutter",
    .defaults = { 17152, 2304, 8000, 12000, 0, 13568, 0, 0 },
    .limits = { { 0, 32767, 8192 }, { 0, 32767, 8192 }, { 0, 32767, 8192 }, { 0, 32767, 8192 },
                { 0, 32767, 0 }, { 0, 32767, 0 }, { 0, 32767, 0 }, { 0, 32767, 0 } },
    .need = need,
    .init = init,
    .mono = mono,
    .stereo = stereo,
    .view = view,
    .shift_knob = shift_knob,
};

#ifdef __bfin__
#define NO_LIBCALL __attribute__((optimize("no-tree-loop-distribute-patterns")))
NO_LIBCALL void *memset(void *d, int c, unsigned n);
NO_LIBCALL void *memcpy(void *d, const void *s, unsigned n);
void *memset(void *d, int c, unsigned n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }
#endif

int opl_init(struct opl_self *self)
{
    return opus_effect_register(self, &STUTTER) < 0 ? -1 : 0;
}
