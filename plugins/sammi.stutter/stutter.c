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
 *   - the peak meter word and the OS SVG screen: the core's effect page shows the four values (view); the picture,
 *     "the defective tape head", is drawn under the OS's screen (frame_under) from the instance's state (shown[]), so
 *     the OS's own off overlay dims it (see the page's notes below);
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
static volatile struct {
    int32_t slice, part, ps, sub, len, pos, rd, rate, duty, fade;
    uint32_t hist, blocks;
    uint8_t frozen, mode, stut, reps, ndiv;
} shown[3];

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
    /* what the page draws (read by the display only; nothing here is read back by the sound) */
    shown[pos].slice = s->slice; shown[pos].part = s->part; shown[pos].ps = s->ps; shown[pos].sub = s->sub;
    shown[pos].len = s->len; shown[pos].pos = s->pos; shown[pos].rd = s->rd; shown[pos].rate = s->rate;
    shown[pos].duty = s->duty; shown[pos].fade = s->fade; shown[pos].hist = s->hist;
    shown[pos].frozen = s->frozen; shown[pos].mode = s->mode; shown[pos].stut = s->stut;
    shown[pos].reps = s->reps; shown[pos].ndiv = s->ndiv;
    shown[pos].blocks++;
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
static int owner_pos(int o) { return o >= 16 ? 2 : (o >= 8 ? 1 : 0); }
static int page_pos(void) { return owner_pos(opus_fx_owner()); }

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

/* ======================= the page: the defective tape head (display thread) =======================
 * The core draws the four values in the corners (view); the machine is drawn under the OS's screen
 * (opus_on_frame_under, the layer the stock effects draw on), so the OS's own "off" overlay dims it exactly as it dims
 * a stock effect's page when the effect is switched off (fx_bypass). It is drawn from the instance's own state
 * (shown[]: the slice, the part, the mode, as the audio left them) and the page's parameters, nothing else. The
 * encoders' colours (the user's theme):
 *   blue   E1  the playback position: the timeline sweeping the beat, the rate's grid of slices above, BRAKE's speed
 *   green  E2  the fragment and the repetition: the trapped piece, the part playing on the ruler, the slices that
 *              stuttered along the bottom
 *   white  E3  the machine: the head, the rollers, the fragment's box (its corners soft with a soft shape), FREEZE's
 *              clamps
 *   orange E4  destructive events: the catch at each repeat's start (a reversal's in REVERSE), the fragment itself
 *              when the repeats are fast (8 or 16 parts)
 * States: idle, the timeline sweeps the beat and the tape runs; a stuttering slice's first part, the box fills as the
 * part is recorded; its repeats, the timeline pinches into the head and the fragment shakes in its box, once per
 * repeat (REPEAT left to right, REVERSE right to left, BRAKE slowing, as the read position goes), the tape stops (runs
 * backwards in REVERSE, slows in BRAKE); GATE, the box's shutter opens and closes with the gate; FREEZE, the box is
 * clamped. */
static volatile uint32_t frames;
static uint32_t view_frame = 0x80000000u;
static int view_owner = -1;
static short page_par[8];
static int32_t tape_phase;                    /* the tape's texture, units x 256, moved by the state each frame */
static uint32_t last_blocks;

static void frame_begin(void) { frames++; }

static void view(const short *par, struct opus_fx_view *v)
{
    static const char *const label[4] = { "rate", "mode", "shape", "chance" };
    static const char *const rates[6] = { "1/4", "1/8", "1/8t", "1/16", "1/16t", "1/32" };
    static const char *const modes[4] = { "repeat", "brake", "reverse", "gate" };
    static char value[4][12];
    const int ri = (int)sx_range(q15(par, 0), 6), mi = (int)sx_range(q15(par, 1), 4);
    const int32_t shape = q15(par, 2);
    fw_snprintf(value[0], 12, "%s", rates[ri]);
    fw_snprintf(value[1], 12, "%s", modes[mi]);
    if (mi == M_GATE)
        fw_snprintf(value[2], 12, "%i%%", (int)((3277 + ((shape * 26214) >> 15)) * 100 >> 15));
    else
        fw_snprintf(value[2], 12, "%i ms", (int)(((88 + ((shape * 441) >> 15)) * 10 + 220) / 441));   /* 2 .. 12 */
    fw_snprintf(value[3], 12, "%i%%", (int)(q15(par, 3) * 100 / 32767));
    for (int i = 0; i < 4; i++) { v->label[i] = label[i]; v->value[i] = value[i]; }
    for (int i = 0; i < 8; i++) page_par[i] = par[i];
    v->waves = 0;                                    /* the middle is the machine (frame_under) */
    v->caption = 0;                                  /* REPEATS and FREEZE are drawn: the ruler and the clamps */
    view_owner = opus_fx_owner();
    view_frame = frames;
}

#define HX 160                                       /* the head gap, units */
#define HY 74
static void box_outline(int x0, int y0, int x1, int y1, int soft, int colour)
{
    if (!soft) { opus_page_frame(x0, y0, x1, y1, colour); return; }
    opus_page_fill(x0 + 1, y0, x1 - 1, y0, colour); opus_page_fill(x0 + 1, y1, x1 - 1, y1, colour);
    opus_page_fill(x0, y0 + 1, x0, y1 - 1, colour); opus_page_fill(x1, y0 + 1, x1, y1 - 1, colour);
}
static void roller(int cx, int cy, int32_t phase)
{
    int sn, cs;
    opus_page_dot(cx, cy, 9, 0, OPUS_COLOR_E3);
    opus_page_dot(cx, cy, 2, 1, OPUS_COLOR_E3);
    opus_sincos((int)((phase >> 4) & 4095) - 2048, &sn, &cs);
    opus_page_line(cx, cy, cx + (cs * 7 >> 12), cy + (sn * 7 >> 12), OPUS_COLOR_DIM);
    opus_page_line(cx, cy, cx - (cs * 7 >> 12), cy - (sn * 7 >> 12), OPUS_COLOR_DIM);
}

static void machine(int pos, const short *par)
{
    /* the state, only while the instance runs (its blocks counted since the last frame); still, the machine stands */
    const uint32_t b = shown[pos].blocks;
    const int running = b != last_blocks;
    last_blocks = b;
    const int mode = shown[pos].mode, stut = shown[pos].stut, frozen = shown[pos].frozen;
    const int32_t sub = shown[pos].sub > 0 ? shown[pos].sub : 1, len = shown[pos].len > 0 ? shown[pos].len : 1;
    const int32_t part = shown[pos].part, ps = shown[pos].ps < sub ? shown[pos].ps : sub - 1;
    const int reps = shown[pos].reps ? shown[pos].reps : 4, ndiv = shown[pos].ndiv ? shown[pos].ndiv : 1;
    const int trapped = stut && mode != M_GATE && (part >= 1 || frozen);
    const int soft = q15(par, 2) > 16384;
    /* the tape's speed this frame, from the state: runs, stops on a repeat, runs back in REVERSE, slows in BRAKE */
    int32_t speed = 768;
    if (trapped) speed = mode == M_REVERSE ? -768 : (mode == M_BRAKE ? (int32_t)(((int64_t)768 * shown[pos].rate) >> 16) : 0);
    if (frozen || !running) speed = 0;
    tape_phase += speed;
    /* the tape: its two edges, its texture of ticks moving with it */
    opus_page_fill(92, 50, 228, 50, OPUS_COLOR_DIM);
    opus_page_fill(92, 98, 228, 98, OPUS_COLOR_DIM);
    const int off = (int)(((tape_phase >> 8) % 12 + 12) % 12);
    for (int x = 92 + off; x <= 228; x += 12) {
        opus_page_fill(x, 54, x, 56, OPUS_COLOR_OFF);
        opus_page_fill(x, 92, x, 94, OPUS_COLOR_OFF);
    }
    roller(104, HY, tape_phase);
    roller(216, HY, tape_phase);
    /* the head */
    opus_page_frame(138, 34, 182, 114, OPUS_COLOR_E3);
    opus_page_fill(HX, 34, HX, 48, OPUS_COLOR_E3);
    opus_page_fill(HX, 100, HX, 114, OPUS_COLOR_E3);
    /* the rate's grid above: one tick per slice of the beat */
    for (int i = 0; i < ndiv; i++) { const int x = 96 + i * 128 / ndiv; opus_page_fill(x, 18, x, 21, OPUS_COLOR_E1); }
    /* the timeline: where the beat is, across the head */
    int32_t at = (int32_t)shown[pos].slice * len + (shown[pos].pos < len ? shown[pos].pos : len);
    const int xt = 96 + (int)(((int64_t)at * 128) / ((int64_t)len * ndiv));
    const int xc = xt < 96 ? 96 : (xt > 224 ? 224 : xt);
    /* the fragment's box: smaller with more parts, shaking while trapped */
    const int bw = 6 + 48 / reps, bh = 20;
    int jx = 0, jy = 0;
    if (trapped && !frozen) { jx = (int)((part * 5 + (ps >> 6)) % 3) - 1; jy = (int)((part * 3 + (ps >> 7)) % 3) - 1; }
    const int bx0 = HX - bw / 2 + jx, bx1 = bx0 + bw, by0 = HY - bh / 2 + jy, by1 = by0 + bh;
    if (trapped) {                                   /* the timeline pinched into the head */
        opus_page_line(xc, 22, HX, by0 - 1, OPUS_COLOR_E1);
        opus_page_line(HX, by1 + 1, xc, 128, OPUS_COLOR_E1);
    } else opus_page_fill(xc, 22, xc, 128, OPUS_COLOR_E1);
    if (stut) {
        const int32_t f = shown[pos].fade > 0 ? shown[pos].fade : 1;
        box_outline(bx0, by0, bx1, by1, soft, OPUS_COLOR_E3);
        if (mode == M_GATE) {                        /* the gate's shutter: open while the part is open */
            const int open = ps < shown[pos].duty;
            if (open) {
                for (int x = bx0 + 2 + (int)((ps >> 5) % 3); x < bx1 - 1; x += 3) opus_page_fill(x, by0 + 3, x, by1 - 3, OPUS_COLOR_E2);
            } else opus_page_fill(bx0 + 1, by0 + 1, bx1 - 1, by1 - 1, OPUS_COLOR_E3);
        } else if (!trapped) {                       /* the first part, being recorded: the box fills */
            const int w = (int)(((int64_t)(bw - 2) * ps) / sub);
            if (w > 0) opus_page_fill(bx0 + 1, by1 - 3, bx0 + w, by1 - 2, OPUS_COLOR_E2);
        } else {                                     /* the repeats: the fragment shakes once per repeat */
            int32_t rp = ps;                         /* the read position in the part, as the sound reads it */
            if (mode == M_REVERSE) rp = sub - 1 - ps;
            else if (mode == M_BRAKE) rp = shown[pos].rd >> 16;
            if (rp < 0) rp = 0;
            if (rp >= sub) rp = sub - 1;
            const int gx = bx0 + 1 + (int)(((int64_t)(bw - 4) * rp) / sub);
            int32_t e = 32768;                       /* the repeat's own edges, as shape makes them */
            if (ps < f) e = ps * 32768 / f;
            if (ps >= sub - f) { const int32_t d = (sub - ps) * 32768 / f; if (d < e) e = d; }
            const int gh = 3 + (int)((7 * e) >> 15);
            const int intense = reps >= 8;
            if (reps >= 6) opus_page_fill(gx - 3 > bx0 ? gx - 3 : bx0 + 1, HY - gh / 2 + jy, gx - 2 > bx0 ? gx - 2 : bx0 + 1, HY + gh / 2 + jy, OPUS_COLOR_DIM);
            opus_page_fill(gx, HY - gh + jy, gx + 1, HY + gh + jy, intense ? OPUS_COLOR_E4 : OPUS_COLOR_E2);
            if (part >= 1 && ps < sub / 6 && !frozen) {   /* the catch: sparks off the box (a reversal: on its right) */
                const int sx = mode == M_REVERSE ? bx1 : bx0, d = mode == M_REVERSE ? 1 : -1;
                opus_page_line(sx, by0, sx + 4 * d, by0 - 4, OPUS_COLOR_E4);
                opus_page_line(sx, by1, sx + 4 * d, by1 + 4, OPUS_COLOR_E4);
            }
            if (mode == M_BRAKE)                     /* the playback slowing: the speed under the box */
                opus_page_fill(bx0, by1 + 3, bx0 + (int)(((int64_t)bw * shown[pos].rate) >> 16), by1 + 3, OPUS_COLOR_E1);
        }
        if (frozen) {                                /* FREEZE: the box clamped */
            opus_page_frame(bx0 - 2, by0 - 2, bx1 + 2, by1 + 2, OPUS_COLOR_E3);
            opus_page_fill(HX - 4, by0 - 5, HX + 4, by0 - 4, OPUS_COLOR_E3);
            opus_page_fill(HX - 4, by1 + 4, HX + 4, by1 + 5, OPUS_COLOR_E3);
        }
    }
    /* the ruler: REPEATS as ticks, the part playing now lit */
    const int rx0 = HX - 24, rx1 = HX + 24;
    for (int i = 0; i <= reps; i++) {
        const int x = rx0 + i * (rx1 - rx0) / reps;
        opus_page_fill(x, 104, x, 106, OPUS_COLOR_DIM);
    }
    if (stut && mode != M_GATE) {
        const int i = (int)(part % reps), x0 = rx0 + i * (rx1 - rx0) / reps, x1 = rx0 + (i + 1) * (rx1 - rx0) / reps;
        opus_page_fill(x0 + 1, 105, x1 - 1, 106, OPUS_COLOR_E2);
    }
    /* the last 16 slices along the bottom, newest at the right: green stuttered, grey played through */
    const uint32_t hist = shown[pos].hist;
    for (int c = 0; c < HIST; c++) {
        const int x = 96 + c * 8;
        if ((hist >> (HIST - 1 - c)) & 1) opus_page_fill(x, 122, x + 5, 126, OPUS_COLOR_E2);
        else opus_page_frame(x, 122, x + 5, 126, OPUS_COLOR_OFF);
    }
}

/* under the OS's screen, every frame: only while this type's own page shows (its view ran last frame for this owner) */
static void frame_under(unsigned short *buf)
{
    (void)buf;
    if (!opus_fx_page_shown() || frames - view_frame > 2 || opus_fx_owner() != view_owner) return;
    machine(owner_pos(view_owner), page_par);
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
    if (opus_effect_register(self, &STUTTER) < 0) return -1;
    opus_on_frame_begin(self, frame_begin);
    opus_on_frame_under(self, frame_under);
    return 0;
}
