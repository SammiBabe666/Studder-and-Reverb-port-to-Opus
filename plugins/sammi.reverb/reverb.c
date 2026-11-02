/* sammi.reverb: Hundekraft's "reverb" effect (Hundekraft OP-1 Workshop 3.1.1, ext/kennel/synth_sdk/plugins/reverb/
 * reverb.c) as a native Opus 2 effect type ("hkreverb") for the synth, the drum and the master.
 *
 * The sound is Hundekraft's, unchanged: a fixed-point port of the Mutable Instruments Elements reverb
 * (elements/dsp/fx/reverb.h), Griesinger topology from the Dattorro paper, 4 input allpasses (the first smeared by an
 * LFO) into a loop of 2 x (2 allpasses + low-pass + delay), the long delay modulated by a slow LFO; run at half rate
 * (two input samples averaged in, the wet interpolated back linearly with a gentle high boost), the dry path at full
 * rate, a soft knee above -2.5 dBFS. Mono: both loop outputs are summed, as in the original.
 *   blue   time       decay, 0.35 .. 0.98 loop gain
 *   green  tone       loop low-pass 0.3 (dark) .. 0.95 (bright), the same cutoff at half rate: 1 - (1 - k)^2
 *   white  diffusion  allpass gain 0.5 .. 0.75
 *   orange mix        dry .. wet
 *
 * What this port changes (GPL-3.0 section 5: modified 2026-11-01 by sammi), and nothing else:
 *   - the state lives in the instance's memory (struct opus_fx: state, and the delay line in bulk) instead of a
 *     static array per kennel FX position and op1_fx_scratch(pos); the core makes one per slot;
 *   - parameters arrive as 0 .. 32767 (this type's limits), so kennel's op1_fx_param_q15 (raw filter-slot words
 *     through the firmware's range table) is a clamp here; the defaults are kennel's plugin.json knob positions;
 *   - the master: kennel's dispatcher ran a mono effect on the mono sum m = (L + R) / 2 and added its change y - m to
 *     both channels (dispatcher.c, fx_render_stereo_dispatch); stereo() below does exactly that;
 *   - the peak meter word kennel wrote for its screen (op1_fx_meter) is not written: the Opus effect page has no meter;
 *   - the screen: the core's effect page shows the four values (view); the picture, "the endless mirror", is drawn
 *     under the OS's screen (frame_under) from the instance's levels, so the OS's own off overlay dims it (see below);
 *   - two levels are published for the page after each block (the input's peak, the loop's level): read-only;
 *   - satb's Blackfin builtin is used on the Blackfin; elsewhere (the host tests) the portable form, as kennel's own
 *     OP1_HOST build does.
 *
 * Original code (c) 2014 Emilie Gillet, MIT license. Hundekraft's port: Copyright (C) 2026 Hundekraft,
 * GPL-3.0-or-later with the OP-1 firmware permission. This file: GPL-3.0-or-later with the same permission (see the
 * repository's LICENSE and NOTICE). */
#include <stdint.h>
#include "opus_plugin.h"
#include "op1fw.h"

#define OP1_FX_BLOCK 64

#define RV_SIZE 16384
#define RV_MASK (RV_SIZE - 1)
#define RV_KNEE 24576
/* Half rate. ap1..ap4, dap1a, dap1b, del1, dap2a, dap2b, del2: Elements' lengths * 22050 / 32000,
 * 103, 148, 220, 363, 1504, 1854, 3102, 1740, 1514, 4349. Each line takes length + 1 words from its base B_i;
 * T_i = B_i + length - 1 is its tail. Total 14907 words. */
enum { B0 = 0, B1 = 104, B2 = 253, B3 = 474, B4 = 838, B5 = 2343, B6 = 4198, B7 = 7301, B8 = 9042, B9 = 10557,
       T0 = 102, T1 = 251, T2 = 472, T3 = 836, T4 = 2341, T5 = 4196, T6 = 7299, T7 = 9040, T8 = 10555 };
/* The line is written twice, at q[o] and q[o + RV_SIZE] (q = buf + wp), so a read is q[o] with no index mask.
 * Reads stay below 2 * RV_SIZE, writes below 2 * RV_SIZE + B9 + 1 words. */
#define RV_WORDS (2 * RV_SIZE + B9 + 1)

typedef struct {
    int16_t *buf, *mirror;                                /* mirror = buf + RV_SIZE */
    int32_t wp;
    int32_t lp1, lp2, wo, we;                             /* wo: the last wet sample (interpolation), we: before emphasis */
    int32_t lfo_iir[2], lfo_y0[2], lfo_y1[2], lfo[2];    /* stmlib CosineOscillator (approximate), Q29; lfo Q15 0..1 */
    int32_t k0, f0, k9, f9;                               /* the two LFO taps' whole and fractional offsets */
    int16_t raw[4];                                       /* knob words the coefficients below were made from */
    int32_t age;                                          /* blocks since then (redone every 64, as the original) */
    int32_t krt, amount, kap, klp;
    int32_t zn;                                           /* blocks since init while lines are still being cleared */
    int32_t zlo[10];                                      /* lowest word already cleared per line (lazy_clear) */
} reverb_t;

/* saturate to 32 - n bits: one saturating shift and one arithmetic shift on the Blackfin, no constants */
#ifndef __bfin__
static inline int32_t satb(int32_t x, int n) { int32_t h = (int32_t)(0x7fffffff >> n); return x > h ? h : (x < -h - 1 ? -h - 1 : x); }
#else
#define satb(x, n) ((int32_t)__builtin_bfin_shl_fr1x32((x), (n)) >> (n))
#endif
/* x / 32768 truncated towards zero: >> alone biases the loop into a DC limit cycle and rounding into a
 * low-level one, magnitude truncation lets the tail die out. Odd: t15(-x) = -t15(x) */
static inline int32_t t15(int32_t x) { return (x + (int32_t)((uint32_t)(x >> 31) >> 17)) >> 15; }
/* a clamped to 17 bits (-65536 .. 65535) times |b| <= 32767: fits 32 bit */
static inline int32_t m15(int32_t a, int32_t b) { return t15(satb(a, 15) * b); }
#define sat(x) ((int16_t)satb((x), 16))
/* both factors fit int16: a 16 x 16 multiply */
#define MUL16(a, b) ((int32_t)(int16_t)(a) * (int16_t)(b))

/* kennel's op1_fx_param_q15: here the parameters are this type's own, 0 .. 32767 */
static inline int32_t param_q15(const int16_t *p, int i) { return p[i] < 0 ? 0 : p[i]; }

/* the LFO taps: Interpolate(line, offset + amp * lfo) in Q15, smear ap1 at 7 + 55 lfo1, del2 at 4275 + 69 lfo2 */
static void lfo_taps(reverb_t *r)
{
    int32_t a = 55 * r->lfo[0], b = 69 * r->lfo[1];
    r->k0 = B0 + 7 + (a >> 15); r->f0 = a & 32767;
    r->k9 = B9 + 4275 + (b >> 15); r->f9 = b & 32767;
}

/* one LFO step (every 32 samples): y0' = (iir * y0 >> 29) - y1 in 32 bits */
static void lfo_step(reverb_t *r)
{
    for (int k = 0; k < 2; k++) {
        int32_t y = r->lfo_y0[k], z = -y, d = (1 << 30) - r->lfo_iir[k];
        int32_t fl = (d * (z >> 15) + ((d * (z & 32767)) >> 15)) >> 14;
        r->lfo_y0[k] = 2 * y + fl - r->lfo_y1[k];
        r->lfo_y1[k] = y;
        r->lfo[k] = (y + (1 << 28)) >> 14;
    }
    lfo_taps(r);
}

/* Lazy clear (Hundekraft 2026-10-27): only a word that is read before it is written needs to be zero. Each block
 * clears the part the next 32 samples can reach, so every read sees what a full clear gave (bit-identical output on
 * a dirty line; tests/host checks it). Line 0 is cleared at init. */
static const uint16_t rv_lo[10] = { B0, B1, B2, B3, B4, B5, B6, B7, B8, B9 };
static const uint16_t rv_hi[10] = { T0 + 1, T1 + 1, T2 + 1, T3 + 1, T4 + 1, T5 + 1, T6 + 1, T7 + 1, T8 + 1, B9 + 4349 };
static const uint16_t rv_rd[10] = { 0, T1, T2, T3, T4, T5, T6, T7, T8, B9 + 4275 };   /* lowest read offset */
typedef uint32_t __attribute__((may_alias)) rv_u32;
static inline void zero_words(int16_t *b, int32_t lo, int32_t hi)
{
    if (lo >= hi) return;
    if (lo & 1) b[lo++] = 0;
    if (hi & 1) b[--hi] = 0;
    rv_u32 *w = (rv_u32 *)(b + lo);
    for (int32_t k = (hi - lo) >> 1; k > 0; k--) *w++ = 0;
}

static __attribute__((noinline)) void lazy_clear(reverb_t *r)
{
    int32_t n = r->zn++, left = 0;
    for (int i = 1; i < 10; i++) {
        int32_t lo = RV_SIZE + rv_lo[i], z = r->zlo[i];
        if (z <= lo) continue;
        int32_t want = RV_SIZE - 32 - 32 * n + rv_rd[i];   /* the lowest word this block's 32 samples can read */
        if (want < lo) want = lo;
        zero_words(r->buf, want, z);
        r->zlo[i] = want < z ? want : z;
        left |= r->zlo[i] > lo;
    }
    if (!left) r->zn = -1;
}

/* kennel's reverb_init(pos), on the instance's own memory */
static void reverb_reset(reverb_t *r, int16_t *bulk)
{
    r->buf = bulk;
    if (!r->buf) return;
    zero_words(r->buf, RV_SIZE, RV_SIZE + T0 + 2);                       /* line 0 now, the rest per block */
    for (int i = 1; i < 10; i++) r->zlo[i] = RV_SIZE + rv_hi[i] + 1;
    r->zn = 0;
    r->mirror = r->buf + RV_SIZE;
    r->wp = 0; r->lp1 = r->lp2 = r->wo = r->we = 0;
    for (int k = 0; k < 2; k++) {                          /* 0.5 Hz, 0.3 Hz (stepped every 32 samples) */
        int64_t f = ((int64_t)(k ? 3 : 5) * 32 << 29) / (10LL * 44100);
        int64_t fr = (1LL << 27) - f;
        r->lfo_iir[k] = (int32_t)((16 * fr * ((1LL << 29) - 2 * fr)) >> 29);
        r->lfo_y1[k] = r->lfo_iir[k] / 4;
        r->lfo_y0[k] = 1 << 28;
        r->lfo[k] = (r->lfo_y1[k] + (1 << 28)) >> 14;
    }
    lfo_taps(r);
    r->age = 64;
}

/* dry .. wet (wet can exceed 16 bit), soft knee above -2.5 dBFS */
static inline int32_t mixout(int32_t in, int32_t wet, int32_t amount)
{
    int32_t o = in + t15(in * -amount) + (((wet >> 3) * amount) >> 12);
    int32_t ao = o < 0 ? -o : o;
    if (ao > RV_KNEE) {
        int32_t d = ao - RV_KNEE;
        ao = d < 16384 ? RV_KNEE + (d >> 2) : 28672 + ((d - 16384) >> 4);
        o = sat(o < 0 ? -ao : ao);
    }
    return o;
}

#define WR(o, v) do { const int16_t v_ = (v); q[o] = v_; qn[o] = v_; } while (0)
#define AP(B, T) do { const int32_t prev_ = q[T]; acc += t15(MUL16(prev_, kap)); WR(B, sat(acc)); \
                      acc = prev_ - m15(acc, kap); } while (0)
#define APN(B, T) do { const int32_t prev_ = q[T]; acc -= t15(MUL16(prev_, kap)); WR(B, sat(acc)); \
                       acc = prev_ + m15(acc, kap); } while (0)

/* 2n samples (n at the reverb's half rate) with the LFO taps fixed; returns the peak */
static int32_t rv_run(reverb_t *r, int16_t *buf, int n, int32_t peak)
{
    int16_t *const line = r->buf, *const mirror = r->mirror;
    int32_t wp = r->wp, lp1 = r->lp1, lp2 = r->lp2, wo = r->wo, we = r->we;
    const int32_t krt = r->krt, amount = r->amount, kap = r->kap, klp = r->klp;
    const int32_t k0 = r->k0, f0 = r->f0, k9 = r->k9, f9 = r->f9;
    for (int t = 0; t < 2 * n; t += 2) {
        wp = (wp - 1) & RV_MASK;
        int16_t *const q = line + wp, *const qn = mirror + wp;
        int32_t in0 = buf[t], in1 = buf[t + 1], acc, apout, wet, a, b;
        /* smear AP1 inside the loop */
        a = q[k0]; b = q[k0 + 1];
        WR(B0 + 69, (int16_t)(a + t15((b - a) * f0)));
        acc = t15((in0 + in1) * 3277);                /* the two samples' mean x 0.2 as Elements (headroom) */
        AP(B0, T0);
        AP(B1, T1);
        AP(B2, T2);
        AP(B3, T3);
        apout = acc;
        /* loop half 1 */
        a = q[k9]; b = q[k9 + 1];
        acc = apout + t15((a + t15((b - a) * f9)) * krt);
        lp1 += m15(acc - lp1, klp); acc = lp1;
        APN(B4, T4);
        AP(B5, T5);
        WR(B6, sat(acc));
        wet = acc;
        /* loop half 2 */
        acc = apout + t15(q[T6] * krt);
        lp2 += m15(acc - lp2, klp); acc = lp2;
        AP(B7, T7);
        APN(B8, T8);
        WR(B9, sat(acc));
        wet = (wet + acc) * 5;                                         /* mono: 2 L + 2 R, makeup for the 0.2 input */
        /* + 1/4 of the change: a gentle high boost that makes up for the two-sample mean and the interpolation */
        a = wet; wet += (wet - we) >> 2; we = a;
        /* back to the full rate: linear interpolation (half way, then the new sample) */
        int32_t o0 = mixout(in0, (wo + wet) >> 1, amount), o1 = mixout(in1, wet, amount);
        wo = wet;
        buf[t] = (int16_t)o0; buf[t + 1] = (int16_t)o1;
        if (o0 < 0) o0 = -o0;
        if (o1 < 0) o1 = -o1;
        if (o1 > o0) o0 = o1;
        if (o0 > peak) peak = o0;
    }
    r->wp = wp; r->lp1 = lp1; r->lp2 = lp2; r->wo = wo; r->we = we;
    return peak;
}

/* the knob scaling, as kennel's reverb_render (only when a knob moved, and every 64 blocks) */
static void coefficients(reverb_t *r, const int16_t *p)
{
    for (int i = 0; i < 4; i++) r->raw[i] = p[i];
    r->age = 0;
    r->krt = 11469 + (param_q15(p, 0) * 20644 >> 15);      /* 0.35 .. 0.98 per loop pass */
    r->amount = param_q15(p, 3);
    r->kap = 16384 + (param_q15(p, 2) * 8192 >> 15);       /* 0.5 .. 0.75 */
    int32_t k = 9830 + (param_q15(p, 1) * 21299 >> 15);    /* 0.3 .. 0.95 */
    r->klp = 32768 - (((32768 - k) * (32768 - k)) >> 15);  /* the same cutoff at half rate: 1 - (1 - k)^2 */
}

static void reverb_render(reverb_t *r, int16_t *buf, const int16_t *p)
{
    if (!r->buf) return;
    if (++r->age >= 64 || p[0] != r->raw[0] || p[1] != r->raw[1] || p[2] != r->raw[2] || p[3] != r->raw[3])
        coefficients(r, p);
    if (r->zn >= 0) lazy_clear(r);
    (void)rv_run(r, buf, OP1_FX_BLOCK / 4, 0);                        /* LFO stepped every 32 samples */
    lfo_step(r);
    (void)rv_run(r, buf + OP1_FX_BLOCK / 2, OP1_FX_BLOCK / 4, 0);
    lfo_step(r);
}

/* ======================= the Opus effect type ======================= */

/* What the page shows of the position's instance, written by the audio after each block and read by the display
 * (single words; a torn read only draws one frame a little off). Nothing here is read back by the sound. */
static volatile struct { int16_t in, tail; uint32_t blocks; } seen[3];

static inline int32_t peak64(const short *b)
{
    int32_t pk = 0;
    for (int i = 0; i < OP1_FX_BLOCK; i++) { const int32_t a = b[i] < 0 ? -b[i] : b[i]; if (a > pk) pk = a; }
    return pk;
}
static inline int16_t clamp16(int32_t v) { return (int16_t)(v > 32767 ? 32767 : v); }
/* the tail's level: the loop's two low-pass states after the block (the tank's own signal, read, never written) */
static void publish(int pos, const reverb_t *r, int32_t in)
{
    const int32_t a = r->lp1 < 0 ? -r->lp1 : r->lp1, b = r->lp2 < 0 ? -r->lp2 : r->lp2;
    seen[pos].in = clamp16(in);
    seen[pos].tail = clamp16(5 * (a > b ? a : b));          /* x 5: the render's own wet makeup */
    seen[pos].blocks++;
}

static void need(int stereo, struct opus_need *n)
{
    (void)stereo;
    n->state = sizeof(reverb_t);
    n->bulk = RV_WORDS * sizeof(int16_t);                 /* 86,652 bytes */
    n->fast = 0;
}

static void init(struct opus_fx *fx)
{
    reverb_reset((reverb_t *)fx->state, (int16_t *)fx->bulk);
}

static void mono(struct opus_fx *fx, short *buf, const short *par)
{
    const int pos = opus_instrument_now() == 1 ? 1 : 0;   /* the instrument this block plays: 0 synth, 1 drum */
    const int32_t in = peak64(buf);
    reverb_render((reverb_t *)fx->state, buf, par);
    publish(pos, (const reverb_t *)fx->state, in);
}

/* the master, as kennel's dispatcher ran a mono effect there: on m = (L + R) / 2, its change added to both sides */
static void stereo(struct opus_fx *fx, short *l, short *r, const short *par)
{
    int16_t m[OP1_FX_BLOCK], y[OP1_FX_BLOCK];
    for (int i = 0; i < OP1_FX_BLOCK; i++) y[i] = m[i] = (int16_t)((l[i] + r[i]) >> 1);
    const int32_t in = peak64(y);
    reverb_render((reverb_t *)fx->state, y, par);
    for (int i = 0; i < OP1_FX_BLOCK; i++) {
        const int32_t d = y[i] - m[i];
        const int32_t a = l[i] + d, b = r[i] + d;
        l[i] = (short)(a > 32767 ? 32767 : (a < -32768 ? -32768 : a));
        r[i] = (short)(b > 32767 ? 32767 : (b < -32768 ? -32768 : b));
    }
    publish(2, (const reverb_t *)fx->state, in);
}

/* ======================= the page: the endless mirror (display thread) =======================
 * The core draws the four values in the corners (view); the tunnel is drawn under the OS's screen
 * (opus_on_frame_under, the layer the stock effects draw on), so the OS's own "off" overlay dims it exactly as it dims
 * a stock effect's page when the effect is switched off (fx_bypass). Colours are the encoders' (the user's theme):
 *   blue   E1  the tunnel's structure: the mouth and the rails to the horizon (the depth reference)
 *   green  E2  the reflections: the tail's level, as it was a frame ago, two frames ago ..., one frame deeper each,
 *              in four steps (green, grey, dark grey, gone): a long time keeps them lit, a short one lets them go
 *   white  E3  the horizon point, and the mouth while the dry sound leads (mix)
 *   orange E4  the dry signal entering: the mouth flashes, and an onset sends one orange frame in
 * The knobs: tone thins the reflections (full outlines bright, dashes, corners only dark), diffusion doubles them
 * (spread), mix moves the weight between the mouth (dry) and the reflections (wet). Everything moves only with the
 * instance's own state (seen[]: written by its audio blocks) and its parameters. */
#define TX 160                                   /* the horizon point, units */
#define TY 74
#define DEPTHS 11
static int16_t rect[DEPTHS][4];                  /* x0 y0 x1 y1 of each depth's frame, made in opl_init */
static volatile uint32_t frames;                 /* frame_begin's count */
static uint32_t view_frame = 0x80000000u;
static int view_owner = -1;
static short page_par[8];
static int16_t hist[DEPTHS];                     /* the tail's level, one frame older per depth */
static int16_t in_slow;                          /* the input's slow level, for onsets */
static int32_t pulse = -1;                       /* an onset's depth x 256, or -1 */
static uint32_t last_blocks;
static int last_pos = -1;

static void frame_begin(void) { frames++; }

static int pct(int v) { return (v < 0 ? 0 : v) * 99 / 32767; }

static void view(const short *par, struct opus_fx_view *v)
{
    static const char *const label[4] = { "time", "tone", "diffusion", "mix" };
    static char value[4][8];
    for (int i = 0; i < 4; i++) fw_snprintf(value[i], 8, "%i", pct(par[i]));
    for (int i = 0; i < 4; i++) { v->label[i] = label[i]; v->value[i] = value[i]; }
    for (int i = 0; i < 8; i++) page_par[i] = par[i];
    v->waves = 0;                                    /* the middle is the tunnel (frame_under) */
    v->caption = 0;
    view_owner = opus_fx_owner();
    view_frame = frames;
}

/* an outline in one of three densities: 0 whole, 1 dashed, 2 corners only */
static void outline(int x0, int y0, int x1, int y1, int style, int colour)
{
    if (style == 0) { opus_page_frame(x0, y0, x1, y1, colour); return; }
    const int w = x1 - x0, h = y1 - y0;
    const int cx = w < 12 ? 2 : w / 6, cy = h < 12 ? 2 : h / 6;
    opus_page_fill(x0, y0, x0 + cx, y0, colour); opus_page_fill(x0, y0, x0, y0 + cy, colour);
    opus_page_fill(x1 - cx, y0, x1, y0, colour); opus_page_fill(x1, y0, x1, y0 + cy, colour);
    opus_page_fill(x0, y1, x0 + cx, y1, colour); opus_page_fill(x0, y1 - cy, x0, y1, colour);
    opus_page_fill(x1 - cx, y1, x1, y1, colour); opus_page_fill(x1, y1 - cy, x1, y1, colour);
    if (style == 1 && w >= 16) {                     /* and a dash in the middle of each side */
        const int mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
        opus_page_fill(mx - cx / 2, y0, mx + cx / 2, y0, colour); opus_page_fill(mx - cx / 2, y1, mx + cx / 2, y1, colour);
        opus_page_fill(x0, my - cy / 2, x0, my + cy / 2, colour); opus_page_fill(x1, my - cy / 2, x1, my + cy / 2, colour);
    }
}

static void tunnel(int pos, const short *par)
{
    const int32_t tone = par[1] < 0 ? 0 : par[1], diff = par[2] < 0 ? 0 : par[2], mix = par[3] < 0 ? 0 : par[3];
    /* this frame's levels: only when the instance ran since the last frame (its audio blocks counted); otherwise the
     * reflections drain away with no new energy, as a silent tail would */
    if (pos != last_pos) { for (int k = 0; k < DEPTHS; k++) hist[k] = 0; in_slow = 0; pulse = -1; last_pos = pos; last_blocks = seen[pos].blocks; }
    const uint32_t b = seen[pos].blocks;
    const int fresh = b != last_blocks;
    last_blocks = b;
    const int32_t tail = fresh ? seen[pos].tail : 0, in = fresh ? seen[pos].in : 0;
    for (int k = DEPTHS - 1; k > 0; k--) hist[k] = hist[k - 1];
    hist[0] = (int16_t)tail;
    if (in > 2000 && in > 2 * in_slow && pulse < 0) pulse = 0;   /* an onset: one frame travels in */
    in_slow = (int16_t)(in_slow + ((in - in_slow) >> 3));
    /* the stable tunnel: every depth in the unlit grey, the rails and the mouth in blue */
    for (int k = 1; k < DEPTHS; k++) opus_page_frame(rect[k][0], rect[k][1], rect[k][2], rect[k][3], OPUS_COLOR_OFF);
    const int16_t *o = rect[0], *f = rect[DEPTHS - 1];
    opus_page_line(o[0], o[1], f[0], f[1], OPUS_COLOR_E1); opus_page_line(o[2], o[1], f[2], f[1], OPUS_COLOR_E1);
    opus_page_line(o[0], o[3], f[0], f[3], OPUS_COLOR_E1); opus_page_line(o[2], o[3], f[2], f[3], OPUS_COLOR_E1);
    /* the reflections: the tail's history, deeper = older, in four steps; mix shifts the steps (wet up, dry down) */
    const int style = tone > 21845 ? 0 : (tone > 10922 ? 1 : 2);
    const int spread = diff > 21845 ? 2 : (diff > 10922 ? 1 : 0);
    const int shift = mix > 24576 ? 1 : (mix < 8192 ? -1 : 0);
    for (int k = 1; k < DEPTHS; k++) {
        const int32_t h = hist[k];
        int step = h > 12000 ? 3 : h > 4000 ? 2 : h > 1200 ? 1 : h > 300 ? 0 : -1;
        if (step >= 0) step += shift;
        if (step < 0) continue;
        if (step > 3) step = 3;
        const int colour = step >= 2 ? OPUS_COLOR_E2 : (step == 1 ? OPUS_COLOR_DIM : OPUS_COLOR_OFF);
        const int16_t *q = rect[k];
        outline(q[0], q[1], q[2], q[3], style, colour);
        if (step == 3 && spread && k <= 4)            /* diffusion: the strong near reflections smear into a double outline */
            outline(q[0] - spread, q[1] - spread, q[2] + spread, q[3] + spread, style ? 2 : 0, colour);
    }
    /* the onset travelling in: orange while it enters, then a green reflection, gone at the horizon */
    if (pulse >= 0) {
        const int k = pulse >> 8;
        if (k < DEPTHS - 1) {
            const int16_t *q = rect[k];
            opus_page_frame(q[0], q[1], q[2], q[3], k < 2 ? OPUS_COLOR_E4 : OPUS_COLOR_E2);
            pulse += 160 + (int32_t)(32767 - (par[0] < 0 ? 0 : par[0])) / 128;   /* a short time travels faster */
        } else pulse = -1;
    }
    /* the mouth: the dry foreground; white and doubled while the dry leads, orange on the input's peaks */
    const int dry = mix < 16384;
    const int mouth = in > 8000 ? OPUS_COLOR_E4 : (dry ? OPUS_COLOR_E3 : OPUS_COLOR_E1);
    opus_page_frame(o[0], o[1], o[2], o[3], mouth);
    if (dry) opus_page_frame(o[0] + 1, o[1] + 1, o[2] - 1, o[3] - 1, mouth);
    /* the horizon point: white, a little larger while the tail is strong */
    opus_page_fill(TX - 1, TY - 1, TX + 1, TY + 1, OPUS_COLOR_E3);
    if (tail > 12000) opus_page_frame(TX - 3, TY - 3, TX + 3, TY + 3, OPUS_COLOR_E3);
}

/* the effect page's owner (0-7 the synth's slots, 8-15 the drum's, 16 the master) as a position */
static int owner_pos(int o) { return o >= 16 ? 2 : (o >= 8 ? 1 : 0); }

/* under the OS's screen, every frame: only while this type's own page shows (its view ran last frame for this owner) */
static void frame_under(unsigned short *buf)
{
    (void)buf;
    if (!opus_fx_page_shown() || frames - view_frame > 2 || opus_fx_owner() != view_owner) return;
    tunnel(owner_pos(view_owner), page_par);
}

/* kennel's plugin.json defaults (knob positions 0..32767): time 16000, tone 20000, diffusion 16384, mix 12000.
 * One detent turns a knob 1024 (step 8192 = 1024 x 8, as the starter pack's rows), 32 detents end to end; shift turns
 * are the OS's fine steps. Parameters 4-7 are unused (step 0). */
static const struct opus_effect REVERB = {
    .size = sizeof(struct opus_effect),
    .name = "hkreverb",
    .defaults = { 16000, 20000, 16384, 12000, 0, 0, 0, 0 },
    .limits = { { 0, 32767, 8192 }, { 0, 32767, 8192 }, { 0, 32767, 8192 }, { 0, 32767, 8192 },
                { 0, 32767, 0 }, { 0, 32767, 0 }, { 0, 32767, 0 }, { 0, 32767, 0 } },
    .need = need,
    .init = init,
    .mono = mono,
    .stereo = stereo,
    .view = view,
    .shift_knob = 0,
};

/* The compiler may emit calls to these (a struct copy, a cleared array); the core exports neither. Their own loops
 * must not become calls to themselves. */
#ifdef __bfin__
#define NO_LIBCALL __attribute__((optimize("no-tree-loop-distribute-patterns")))
NO_LIBCALL void *memset(void *d, int c, unsigned n);
NO_LIBCALL void *memcpy(void *d, const void *s, unsigned n);
void *memset(void *d, int c, unsigned n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }
#endif

int opl_init(struct opl_self *self)
{
    /* the tunnel's depths: each frame 0.74 of the one before, around the horizon point (integer only) */
    int32_t s = 1024;
    for (int k = 0; k < DEPTHS; k++) {
        const int32_t hw = (64 * s) >> 10, hh = (56 * s) >> 10;
        rect[k][0] = (int16_t)(TX - hw); rect[k][1] = (int16_t)(TY - hh);
        rect[k][2] = (int16_t)(TX + hw); rect[k][3] = (int16_t)(TY + hh);
        s = s * 758 >> 10;
    }
    if (opus_effect_register(self, &REVERB) < 0) return -1;
    opus_on_frame_begin(self, frame_begin);
    opus_on_frame_under(self, frame_under);
    return 0;
}
