/* The scenarios both runners play: kennel's original effect (orig_runner.c) and the Opus port (port_runner.c).
 * Each runner gives the same small API; every scenario writes its output (int16, the order below) to stdout, so the
 * two outputs compare byte for byte (tests/host/run.sh). Deterministic: integer generators only.
 * Copyright (C) 2026 sammi. GPL-3.0-or-later with the OP-1 firmware permission. */
#ifndef SCENARIO_H
#define SCENARIO_H
#include <stdint.h>
#ifndef SCENARIO_OUT                 /* the Blackfin simulator's runner gives its own out() and has no stdio */
#include <stdio.h>
#endif

/* bfin-elf-gcc 13.3 at -O2 vectorises loops such as (int16_t)((b * k * 32) & 32767) into paired 16-bit multiplies
 * in (IS) mode, which saturate where C wraps (found by tests/bfin_sim: the knob values came out clipped). The plugins'
 * own code has no 16-bit-result multiply (checked in their disassembly), so only these scenario functions are kept
 * from being vectorised; the plugin sources the Blackfin runner includes keep the kit's exact flags. */
#ifdef __bfin__
#define SCENARIO_FN __attribute__((optimize("no-tree-vectorize")))
#else
#define SCENARIO_FN
#endif

typedef struct {
    void (*init)(int pos);                                         /* the effect (re)made at pos, on dirty memory */
    void (*mono)(int pos, int16_t *buf, const int16_t *par);       /* pos 0 synth, 1 drum: 64 samples */
    void (*stereo)(int16_t *l, int16_t *r, const int16_t *par);    /* the master */
    void (*freeze)(int pos, int on);                               /* stutter's latch (kennel: op1_fx_page[pos][3]) */
    void (*beat)(int32_t len, int32_t next, int32_t step, int follows);   /* the clock the next block sees */
} fx_api;

/* the input: plucked saw notes, noise bursts and silences, L and R apart */
typedef struct { uint32_t n, rng, ph; int32_t inc, env, k; } sig_t;
SCENARIO_FN static int16_t sig_next(sig_t *s, int ch)
{
    const uint32_t t = s->n++;
    const uint32_t note = t / 11025;                          /* a new note every 0.25 s */
    if (t % 11025 == 0) {
        static const int32_t incs[8] = { 4870000, 6130000, 7290000, 9740000, 3650000, 12260000, 5460000, 8180000 };
        s->inc = incs[(note + (uint32_t)ch * 3) & 7];
        s->env = 30000;
    }
    s->ph += (uint32_t)s->inc;
    s->env -= s->env >> 12;
    int32_t x = ((int32_t)(s->ph >> 16) - 32768) * s->env >> 15;   /* saw */
    s->rng = s->rng * 1664525u + 1013904223u;
    if ((note & 7) == 5) x = ((int32_t)(s->rng >> 16) - 32768) * 3 / 4;    /* a noise burst */
    if ((note & 7) == 7) x = 0;                                /* silence */
    if ((note & 15) == 13) x = (t & 64) ? 32767 : -32768;      /* a full-scale square: clipping */
    return (int16_t)(x > 32767 ? 32767 : (x < -32768 ? -32768 : x));
}

#ifndef SCENARIO_OUT
static void out(const int16_t *b, int n) { fwrite(b, 2, (size_t)n, stdout); }
#else
static void out(const int16_t *b, int n);
#endif

/* a clock that runs: next counts down by step each block and wraps by len (kennel's metronome state) */
typedef struct { int32_t len, next, step; int follows; } clk_t;
SCENARIO_FN static void clk_tick(clk_t *c)
{
    if (c->len <= 0) return;
    c->next -= c->step;
    while (c->next < 0) c->next += c->len;
    if (c->next > c->len) c->next = c->len;
}
SCENARIO_FN static int32_t bpm_len(int bpm) { return (int32_t)((2646000LL << 14) / bpm); }

/* nblocks of the input through the effect at pos (0, 1 mono; 2 the master), the clock ticking; silent: no input */
SCENARIO_FN static void play(const fx_api *a, int pos, const int16_t *par, sig_t *sg, clk_t *c, int nblocks, int silent)
{
    int16_t l[64], r[64];
    for (int b = 0; b < nblocks; b++) {
        if (a->beat) a->beat(c->len, c->next, c->step, c->follows);
        for (int i = 0; i < 64; i++) {
            const int16_t x = sig_next(sg, 0), y = sig_next(sg, 1);
            l[i] = silent ? 0 : x;
            r[i] = silent ? 0 : y;
        }
        if (pos == 2) { a->stereo(l, r, par); out(l, 64); out(r, 64); }
        else { a->mono(pos, l, par); out(l, 64); }
        clk_tick(c);
    }
}

/* ---- the reverb ---- */
SCENARIO_FN static void reverb_scenarios(const fx_api *a)
{
    static const int16_t sets[][8] = {
        { 16000, 20000, 16384, 12000 },          /* kennel's defaults */
        { 0, 0, 0, 0 }, { 32767, 32767, 32767, 32767 }, { 16384, 16384, 16384, 16384 },
        { 32767, 0, 32767, 32767 }, { 0, 32767, 0, 32767 }, { 32767, 32767, 0, 0 }, { 8000, 26000, 30000, 24000 },
    };
    clk_t c = { 0, 0, 0x100000, 0 };
    for (int pos = 0; pos < 3; pos++)
        for (unsigned k = 0; k < sizeof sets / sizeof sets[0]; k++) {
            sig_t sg = { k * 777u, 0x1234u + k, 0, 0, 0, 0 };
            a->init(pos);
            play(a, pos, sets[k], &sg, &c, 380, 0);
            play(a, pos, sets[k], &sg, &c, 520, 1);           /* the tail */
        }
    /* every knob moving every block (an LFO), then re-made twice in a row (repeated loading) */
    for (int pos = 0; pos < 3; pos++) {
        sig_t sg = { 0, 99, 0, 0, 0, 0 };
        a->init(pos);
        int16_t p[8] = { 0 };
        for (int b = 0; b < 600; b++) {
            for (int i = 0; i < 4; i++) p[i] = (int16_t)((b * (97 + 31 * i) * 64) & 32767);
            play(a, pos, p, &sg, &c, 1, 0);
        }
        a->init(pos); a->init(pos);
        play(a, pos, p, &sg, &c, 300, 0);
    }
}

/* ---- the stutter ---- */
SCENARIO_FN static void stutter_scenarios(const fx_api *a)
{
    static const int16_t reps_w[6] = { 256, 768, 13568, 1792, 2304, 2816 };     /* word 5: (w >> 9) % 6 = 0 .. 5 */
    /* every rate, mode and repeats, all slices stuttering, at 120 BPM on the synth */
    for (int rate = 0; rate < 6; rate++)
        for (int mode = 0; mode < 4; mode++)
            for (int rp = 0; rp < 6; rp++) {
                int16_t p[8] = { (int16_t)(rate * 5461 + 2730), (int16_t)(mode * 8192 + 4096), 8000, 32767, 0, reps_w[rp], 0, 0 };
                clk_t c = { bpm_len(120), 3000 << 14, 0x100000, 0 };
                sig_t sg = { (uint32_t)(rate * 1000 + mode * 100 + rp), 7, 0, 0, 0, 0 };
                a->init(0);
                play(a, 0, p, &sg, &c, 160, 0);
            }
    /* shape and chance at their ends and between, every mode, on the drum */
    static const int16_t sc[] = { 0, 1, 16384, 32766, 32767 };
    for (int mode = 0; mode < 4; mode++)
        for (int i = 0; i < 5; i++)
            for (int j = 0; j < 5; j++) {
                int16_t p[8] = { 21000, (int16_t)(mode * 8192 + 100), sc[i], sc[j], 0, 13568, 0, 0 };
                clk_t c = { bpm_len(97), 0, 0x100000, 0 };
                sig_t sg = { (uint32_t)(i * 50 + j), 11, 0, 0, 0, 0 };
                a->init(1);
                play(a, 1, p, &sg, &c, 140, 0);
            }
    /* tempo: slow (a 1/4 longer than the ring), fast, a change, the clock lost and back, never there, the tape's
     * speed (half, stopped, double), a jump of the clock; on the synth and the master */
    for (int pos = 0; pos < 3; pos += 2) {
        int16_t p[8] = { 0, 4096, 12000, 24000, 0, 13568, 0, 0 };
        struct { int bpm, step, follows, blocks; } seg[] = {
            { 30, 0x100000, 0, 400 }, { 40, 0x100000, 0, 300 }, { 240, 0x100000, 0, 300 }, { 120, 0x100000, 0, 200 },
            { 0, 0x100000, 0, 300 },                      /* lost: len 0 */
            { 133, 0x100000, 0, 200 }, { 133, 0x80000, 1, 300 }, { 133, 0, 1, 200 }, { 133, 0x200000, 1, 200 },
            { 90, 0x100000, 0, 200 },
        };
        for (int rate = 0; rate < 6; rate += 1) {
            p[0] = (int16_t)(rate * 5461 + 100);
            clk_t c = { 0, 0, 0x100000, 0 };
            sig_t sg = { (uint32_t)rate, 5, 0, 0, 0, 0 };
            a->init(pos);
            for (unsigned s = 0; s < sizeof seg / sizeof seg[0]; s++) {
                c.len = seg[s].bpm ? bpm_len(seg[s].bpm) : 0;
                c.step = seg[s].step; c.follows = seg[s].follows;
                if (c.next > c.len) c.next = c.len;
                if (s == 9) c.next = c.len / 3;                /* the clock jumps */
                p[1] = (int16_t)((s & 3) * 8192 + 1000);       /* the mode changes with each segment */
                play(a, pos, p, &sg, &c, seg[s].blocks, 0);
            }
        }
    }
    /* no clock at all from the start (120 BPM free-running), every rate */
    for (int rate = 0; rate < 6; rate++) {
        int16_t p[8] = { (int16_t)(rate * 5461 + 2000), 0, 0, 20000, 0, 2304, 0, 0 };
        clk_t c = { 0, 0, 0x100000, 0 };
        sig_t sg = { 3, 3, 0, 0, 0, 0 };
        a->init(0);
        play(a, 0, p, &sg, &c, 250, 0);
    }
    /* FREEZE: on mid-slice, held across slices and mode changes, off, on again; on each position */
    for (int pos = 0; pos < 3; pos++) {
        int16_t p[8] = { 17152, 2304, 8000, 12000, 0, 13568, 0, 0 };
        clk_t c = { bpm_len(126), 1 << 20, 0x100000, 0 };
        sig_t sg = { 9, 9, 0, 0, 0, 0 };
        a->freeze(pos, 0);
        a->init(pos);
        play(a, pos, p, &sg, &c, 77, 0);
        a->freeze(pos, 1);
        play(a, pos, p, &sg, &c, 200, 0);
        p[1] = 10000; play(a, pos, p, &sg, &c, 100, 0);    /* brake while frozen */
        p[1] = 18000; play(a, pos, p, &sg, &c, 100, 0);    /* reverse while frozen */
        a->freeze(pos, 0);
        play(a, pos, p, &sg, &c, 120, 0);
        a->freeze(pos, 1);
        play(a, pos, p, &sg, &c, 90, 0);
        a->freeze(pos, 0);
    }
    /* every knob moving every block (an LFO), and repeated loading */
    {
        int16_t p[8] = { 0, 0, 0, 0, 0, 13568, 0, 0 };
        clk_t c = { bpm_len(140), 0, 0x100000, 0 };
        sig_t sg = { 1, 1, 0, 0, 0, 0 };
        a->init(0);
        for (int b = 0; b < 900; b++) {
            for (int i = 0; i < 4; i++) p[i] = (int16_t)((b * (61 + 47 * i) * 32) & 32767);
            p[5] = (int16_t)((b * 37) & 32767);
            play(a, 0, p, &sg, &c, 1, 0);
            if (b == 400 || b == 401 || b == 650) a->init(0);
        }
    }
}
#endif
