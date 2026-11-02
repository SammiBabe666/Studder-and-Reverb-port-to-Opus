/* The Opus side of a port, alone: what the core sees (registration, defaults, limits, needs), the page (view), the
 * shift layer, the synth and drum instances sharing one bulk as the core gives it, silence, determinism, and for the
 * stutter the musical behaviour measured on a marked input (the grid, the slice and part lengths, every mode). It also
 * writes the renders analysis.py measures (reverb impulse responses, the wet/dry and the master's stereo).
 *   cc -DTEST_REVERB -DEFFECT_SRC='"plugins/sammi.reverb/reverb.c"' ...   or   -DSTUTTER_HOST -DEFFECT_SRC=... (stutter)
 * argv[1]: the folder for the renders (default .). Exit 1 on any failure.
 * Copyright (C) 2026 sammi. GPL-3.0-or-later with the OP-1 firmware permission. */
#include <stdlib.h>
#include <string.h>
#include "opus_mock.h"
#include EFFECT_SRC

static int fails, passes;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static const char *dir = ".";
static void save(const char *name, const int16_t *b, size_t n)
{
    char p[512];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "wb");
    if (!f) { fails++; fprintf(stderr, "FAIL cannot write %s\n", p); return; }
    fwrite(b, 2, n, f);
    fclose(f);
}

typedef struct { struct opus_fx fx; struct opus_need nd; } inst_t;
static void make(inst_t *in, int stereo, void *shared_bulk)
{
    mock_reg->need(stereo, &in->nd);
    in->fx.state = calloc(1, in->nd.state ? in->nd.state : 1);
    in->fx.bulk = shared_bulk ? shared_bulk : calloc(1, in->nd.bulk ? in->nd.bulk : 1);
    in->fx.fast = 0;
    in->fx.stereo = stereo;
}
static void drop(inst_t *in, int shared) { free(in->fx.state); if (!shared) free(in->fx.bulk); }

/* the page's strings: lowercase for the OP-1's font (capitals draw icons), within the struct's buffers */
static void check_text(const char *s, const char *what)
{
    CHECK(s != 0, "%s missing", what);
    if (!s) return;
    CHECK(strlen(s) > 0 && strlen(s) < 24, "%s length %zu", what, strlen(s));
    for (const char *c = s; *c; c++) CHECK(!(*c >= 'A' && *c <= 'Z'), "%s has a capital: '%s'", what, s);
}
static void check_view(const short *par)
{
    struct opus_fx_view v;
    memset(&v, 0, sizeof v);
    mock_reg->view(par, &v);
    for (int i = 0; i < 4; i++) { check_text(v.label[i], "label"); check_text(v.value[i], "value"); }
    CHECK(v.waves >= 0 && v.waves <= 3, "waves %d", v.waves);
    for (int w = 0; w < v.waves; w++) CHECK(v.wave[w] != 0, "wave %d missing", w);
    if (v.caption) check_text(v.caption, "caption");
}

static void common_checks(const char *name)
{
    CHECK(mock_reg_count == 1, "registered %d times", mock_reg_count);
    CHECK(mock_reg->size == sizeof(struct opus_effect), "size");
    CHECK(strcmp(mock_reg->name, name) == 0 && strlen(mock_reg->name) <= 15, "name %s", mock_reg->name);
    for (const char *c = mock_reg->name; *c; c++) CHECK((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9'), "name char");
    CHECK(mock_reg->need && mock_reg->init && mock_reg->mono && mock_reg->stereo && mock_reg->view, "calls");
    for (int i = 0; i < 8; i++) {
        CHECK(mock_reg->limits[i][0] <= mock_reg->defaults[i] && mock_reg->defaults[i] <= mock_reg->limits[i][1], "default %d in limits", i);
        CHECK(mock_reg->limits[i][0] == 0 && mock_reg->limits[i][1] == 32767, "limits %d", i);
    }
    for (int i = 0; i < 4; i++) CHECK(mock_reg->limits[i][2] > 0, "knob %d turns", i);
    for (int i = 4; i < 8; i++) CHECK(mock_reg->limits[i][2] == 0, "param %d turned by no knob or LFO", i);
    /* the page at every corner of the knobs */
    static const short v[] = { 0, 1, 5461, 8192, 16383, 16384, 24576, 32766, 32767 };
    short par[8];
    for (int a = 0; a < 9; a++) for (int b = 0; b < 9; b++) {
        for (int i = 0; i < 8; i++) par[i] = mock_reg->defaults[i];
        par[0] = v[a]; par[1] = v[b]; par[2] = v[(a + b) % 9]; par[3] = v[(a * 3 + b) % 9]; par[5] = v[(a * 5 + b * 7) % 9];
        check_view(par);
    }
}

/* the input: a decaying saw, a burst of noise, silence */
static void signal(int16_t *x, size_t n, uint32_t seed)
{
    uint32_t ph = 0, r = seed;
    for (size_t i = 0; i < n; i++) {
        ph += 7290000u;
        r = r * 1664525u + 1013904223u;
        int32_t s = ((int32_t)(ph >> 16) - 32768) / 2;
        if ((i / 11025) % 3 == 1) s = ((int32_t)(r >> 16) - 32768) / 3;
        if ((i / 11025) % 3 == 2) s = 0;
        x[i] = (int16_t)s;
    }
}

static void render_mono(inst_t *in, int16_t *buf, size_t nblocks, const short *par)
{
    for (size_t b = 0; b < nblocks; b++) mock_reg->mono(&in->fx, buf + 64 * b, par);
}

#ifdef TEST_REVERB
/* ---------------------------------------------------------------- the reverb */
static void reverb_checks(void)
{
    common_checks("hkreverb");
    const short def[8] = { 16000, 20000, 16384, 12000, 0, 0, 0, 0 };
    for (int i = 0; i < 8; i++) CHECK(mock_reg->defaults[i] == def[i], "kennel default %d", i);
    CHECK(mock_reg->shift_knob == 0, "no shift layer");
    struct opus_need n0, n1;
    mock_reg->need(0, &n0); mock_reg->need(1, &n1);
    CHECK(n0.bulk == 86652 && n1.bulk == 86652 && n0.fast == 0, "needs: bulk %u", n0.bulk);
    CHECK(n0.state == sizeof(reverb_t) && n0.state < 512, "state %u", n0.state);

    const size_t NB = 6000, N = 64 * NB;                    /* about 8.7 s */
    int16_t *x = malloc(N * 2), *y = malloc(N * 2), *z = malloc(N * 2);
    /* impulse responses for analysis.py: time x tone, all wet */
    static const short tv[3] = { 0, 16000, 32767 }, kv[3] = { 0, 20000, 32767 };
    for (int t = 0; t < 3; t++) for (int k = 0; k < 3; k++) {
        inst_t in; make(&in, 0, 0);
        memset(y, 0, N * 2); y[0] = 24000;
        short par[8] = { tv[t], kv[k], 16384, 32767, 0, 0, 0, 0 };
        mock_reg->init(&in.fx);
        render_mono(&in, y, NB, par);
        char nm[64]; snprintf(nm, sizeof nm, "ir_t%d_k%d.raw", t, k); save(nm, y, N);
        drop(&in, 0);
    }
    /* diffusion's ends */
    for (int d = 0; d < 2; d++) {
        inst_t in; make(&in, 0, 0);
        memset(y, 0, N * 2); y[0] = 24000;
        short par[8] = { 16000, 20000, (short)(d ? 32767 : 0), 32767, 0, 0, 0, 0 };
        mock_reg->init(&in.fx);
        render_mono(&in, y, NB, par);
        char nm[64]; snprintf(nm, sizeof nm, "ir_diff%d.raw", d); save(nm, y, N);
        drop(&in, 0);
    }
    /* wet / dry: the input through mix 0, a quarter, a half, all */
    signal(x, N, 5);
    save("mix_in.raw", x, N);
    static const short mv[4] = { 0, 8192, 16384, 32767 };
    for (int m = 0; m < 4; m++) {
        inst_t in; make(&in, 0, 0);
        memcpy(y, x, N * 2);
        short par[8] = { 16000, 20000, 16384, mv[m], 0, 0, 0, 0 };
        mock_reg->init(&in.fx);
        render_mono(&in, y, NB, par);
        if (m == 0) CHECK(memcmp(x, y, N * 2) == 0, "mix 0 is the dry input exactly");
        char nm[64]; snprintf(nm, sizeof nm, "mix_%d.raw", m); save(nm, y, N);
        drop(&in, 0);
    }
    /* the master: L only in; the wet is centred, the dry image kept (L - R out == L - R in) */
    {
        inst_t in; make(&in, 1, 0);
        int16_t *l = y, *r = z;
        for (size_t i = 0; i < N; i++) { l[i] = x[i]; r[i] = 0; }
        short par[8] = { 16000, 20000, 16384, 16384, 0, 0, 0, 0 };
        mock_reg->init(&in.fx);
        for (size_t b = 0; b < NB; b++) mock_reg->stereo(&in.fx, l + 64 * b, r + 64 * b, par);
        save("master_l.raw", l, N); save("master_r.raw", r, N);
        long bad = 0;
        for (size_t i = 0; i < N; i++) {
            int32_t want = x[i], got = l[i] - r[i];
            if (got != want && l[i] != 32767 && l[i] != -32768 && r[i] != 32767 && r[i] != -32768) bad++;
        }
        CHECK(bad == 0, "master: the dry stereo difference kept (%ld samples off)", bad);
        drop(&in, 0);
    }
    /* silence in: at the defaults and a long setting the tail dies to exact silence (no DC limit cycle). At the very
     * top of time (loop gain 0.98) Hundekraft's original keeps a small idle limit cycle at some tone settings (±8 at
     * time, tone, diffusion 32767 / 20000 / 16384, -72 dBFS; the port is bit-identical): check it is bounded and does
     * not grow, at every corner of the other knobs. */
    {
        static const short quiet[2][8] = { { 16000, 20000, 16384, 32767 }, { 26000, 32767, 32767, 32767 } };
        int16_t *s = calloc(64 * 30000, 2);
        for (int k = 0; k < 2; k++) {
            inst_t in; make(&in, 0, 0);
            memcpy(y, x, N * 2);
            mock_reg->init(&in.fx);
            render_mono(&in, y, 400, quiet[k]);
            memset(s, 0, 64 * 30000 * 2);
            render_mono(&in, s, 30000, quiet[k]);         /* 43 s of silence */
            int last = 0;
            for (int i = 64 * 29000; i < 64 * 30000; i++) if (s[i]) last = 1;
            CHECK(last == 0, "set %d: the tail dies to exact silence", k);
            drop(&in, 0);
        }
        static const short kv2[3] = { 0, 20000, 32767 };
        for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) {
            inst_t in; make(&in, 0, 0);
            short par[8] = { 32767, kv2[a], kv2[b], 32767, 0, 0, 0, 0 };
            memcpy(y, x, N * 2);
            mock_reg->init(&in.fx);
            render_mono(&in, y, 400, par);
            for (int rep = 0; rep < 3; rep++) {           /* 130 s of silence (each render's input zeroed); the last 43 s kept */
                memset(s, 0, 64 * 30000 * 2);
                render_mono(&in, s, 30000, par);
            }
            int pk1 = 0, pk2 = 0;
            for (int i = 0; i < 64 * 15000; i++) { int v = abs(s[i]); if (v > pk1) pk1 = v; }
            for (int i = 64 * 15000; i < 64 * 30000; i++) { int v = abs(s[i]); if (v > pk2) pk2 = v; }
            CHECK(pk2 <= 16 && pk2 <= (pk1 > 0 ? pk1 : 16), "max time, tone %d diffusion %d: idle residue %d (earlier %d)", kv2[a], kv2[b], pk2, pk1);
            drop(&in, 0);
        }
        free(s);
    }
    /* the synth and drum instances share one bulk (as the core gives them): after the synth played, the instrument
     * changes, the core re-inits; the drum's output equals a fresh instance's on a clean line */
    {
        inst_t syn, drm, ref;
        make(&syn, 0, 0);
        make(&drm, 0, syn.fx.bulk);
        make(&ref, 0, 0);
        short par[8] = { 26000, 30000, 20000, 20000, 0, 0, 0, 0 };
        memcpy(y, x, N * 2);
        mock_reg->init(&syn.fx);
        render_mono(&syn, y, 900, par);                   /* fills the shared line */
        memcpy(y, x, N * 2); memcpy(z, x, N * 2);
        mock_reg->init(&drm.fx);
        mock_reg->init(&ref.fx);
        render_mono(&drm, y, NB, par);
        render_mono(&ref, z, NB, par);
        CHECK(memcmp(y, z, N * 2) == 0, "shared bulk: the drum after the synth == a fresh instance");
        drop(&syn, 0); drop(&drm, 1); drop(&ref, 0);
    }
    /* determinism: the same input twice */
    {
        inst_t in; make(&in, 0, 0);
        short par[8] = { 20000, 10000, 30000, 25000, 0, 0, 0, 0 };
        memcpy(y, x, N * 2); mock_reg->init(&in.fx); render_mono(&in, y, NB, par);
        memcpy(z, x, N * 2); mock_reg->init(&in.fx); render_mono(&in, z, NB, par);
        CHECK(memcmp(y, z, N * 2) == 0, "deterministic after a re-init");
        drop(&in, 0);
    }
    free(x); free(y); free(z);
}
#else
/* ---------------------------------------------------------------- the stutter */
op1_beat_t host_beat;

/* a marked input: sample i is (i % 20000) + 100, so every output sample says which input it came from */
static int16_t mark(size_t i) { return (int16_t)(i % 20000 + 100); }

static void stutter_checks(void)
{
    common_checks("hkstutter");
    const short def[8] = { 17152, 2304, 8000, 12000, 0, 13568, 0, 0 };
    for (int i = 0; i < 8; i++) CHECK(mock_reg->defaults[i] == def[i], "kennel default %d", i);
    CHECK(REPS[sx_select(13568, 6)] == 4, "default repeats 4");
    CHECK(DIVS[sx_range(17152, 6)] == 4 && sx_range(2304, 4) == M_REPEAT, "default rate 1/16, mode repeat");
    struct opus_need n0, n1;
    mock_reg->need(0, &n0); mock_reg->need(1, &n1);
    CHECK(n0.bulk == 131072 && n1.bulk == 131072 && n0.fast == 0, "needs: bulk %u", n0.bulk);

    /* the shift layer: shift + blue steps REPEATS 2 3 4 6 8 16, round and round; shift + orange FREEZE per position */
    CHECK(mock_reg->shift_knob != 0, "shift layer");
    short par[8];
    memcpy(par, mock_reg->defaults, sizeof par);
    static const int order[] = { 6, 8, 16, 2, 3, 4, 6 };
    for (int k = 0; k < 7; k++) {
        CHECK(mock_reg->shift_knob(par, 0, 1) == 1, "blue taken");
        CHECK(REPS[sx_select(par[5], 6)] == order[k], "repeats step %d: %d", k, REPS[sx_select(par[5], 6)]);
    }
    mock_reg->shift_knob(par, 0, -2);
    CHECK(REPS[sx_select(par[5], 6)] == 3, "repeats back two: %d", REPS[sx_select(par[5], 6)]);
    mock_reg->shift_knob(par, 0, 0);
    CHECK(REPS[sx_select(par[5], 6)] == 3, "a turn under a detent changes nothing");
    mock_reg->shift_knob(par, 0, -13);
    CHECK(sx_select(par[5], 6) < 6 && par[5] >= 0, "a big turn stays in range");
    CHECK(mock_reg->shift_knob(par, 1, 1) == 0 && mock_reg->shift_knob(par, 2, -1) == 0, "green and white: the OS's fine steps");
    for (int o = 0; o < 17; o++) {
        mock_owner = o;
        const int p = o >= 16 ? 2 : (o >= 8 ? 1 : 0);
        mock_reg->shift_knob(par, 3, 1);
        CHECK(freeze_latch[p] == 1 && freeze_latch[(p + 1) % 3] == 0 && freeze_latch[(p + 2) % 3] == 0, "freeze owner %d", o);
        mock_reg->shift_knob(par, 3, 0);
        CHECK(freeze_latch[p] == 1, "freeze: no detent, no change");
        mock_reg->shift_knob(par, 3, -1);
        CHECK(freeze_latch[p] == 0, "freeze off owner %d", o);
    }
    /* the page's names for every rate and mode */
    static const char *const rn[6] = { "1/4", "1/8", "1/8t", "1/16", "1/16t", "1/32" };
    static const char *const mn[4] = { "repeat", "brake", "reverse", "gate" };
    for (int r = 0; r < 6; r++) for (int m = 0; m < 4; m++) {
        struct opus_fx_view v;
        short pp[8] = { (short)(r * 5461 + 2000), (short)(m * 8192 + 2000), 0, 32767, 0, 13568, 0, 0 };
        mock_reg->view(pp, &v);
        CHECK(strcmp(v.value[0], rn[r]) == 0 && strcmp(v.value[1], mn[m]) == 0, "page %s %s: %s %s", rn[r], mn[m], v.value[0], v.value[1]);
        CHECK(strcmp(v.value[3], "100%") == 0, "chance 100%%: %s", v.value[3]);
        CHECK(v.caption == 0 && v.waves == 0, "the middle is the machine: no caption, no waves");
        if (m == M_GATE) CHECK(strcmp(v.value[2], "10%") == 0, "gate open at shape 0: %s", v.value[2]);
        else CHECK(strcmp(v.value[2], "2 ms") == 0, "fade at shape 0: %s", v.value[2]);
    }
    { struct opus_fx_view v; short pp[8] = { 0, 0, 32767, 0, 0, 2816, 0, 0 }; mock_reg->view(pp, &v);
      CHECK(strcmp(v.value[2], "12 ms") == 0 && strcmp(v.value[3], "0%") == 0, "page ends: %s %s", v.value[2], v.value[3]); }

    /* the musical behaviour at 120 BPM (beat 22050 samples), the clock running, the synth */
    static const int32_t slice_len[6] = { 22050, 11025, 7350, 5512, 3675, 2756 };
    const size_t NB = 1400, N = 64 * NB;
    int16_t *x = malloc(N * 2), *y = malloc(N * 2);
    for (size_t i = 0; i < N; i++) x[i] = mark(i);
    for (int rate = 0; rate < 6; rate++)
        for (int mode = 0; mode < 4; mode++)
            for (int rp = 0; rp < 6; rp++) {
                inst_t in; make(&in, 0, 0);
                mock_instrument = 0;
                short p[8] = { (short)(rate * 5461 + 2730), (short)(mode * 8192 + 4096), (short)(mode == M_GATE ? 32767 : 0), 32767, 0, (short)(rp * 512 + 256), 0, 0 };
                memcpy(y, x, N * 2);
                const int32_t blen = 22050 << 14;
                int32_t next = 0;                              /* a beat on the first sample */
                mock_reg->init(&in.fx);
                for (size_t b = 0; b < NB; b++) {
                    host_beat.len = blen; host_beat.next = next; host_beat.step = 0x100000; host_beat.follows_tape = 0;
                    mock_reg->mono(&in.fx, y + 64 * b, p);
                    next -= 0x100000; while (next < 0) next += blen;
                }
                const st_t *s = in.fx.state;
                CHECK(s->len == (slice_len[rate] > NBUF - 64 ? NBUF - 64 : slice_len[rate]), "rate %d slice %d", rate, s->len);
                const int32_t sl = slice_len[rate], reps = REPS[rp], sub = sl / reps < 32 ? 32 : sl / reps;
                CHECK(s->sub == sub, "rate %d reps %d: part %d want %d", rate, reps, s->sub, sub);
                /* every slice stutters (chance all): check slices 2..4 sample by sample, away from the fades */
                long bad = 0, checked = 0;
                const int32_t f = s->fade;
                for (int k = 2; k < 5; k++) {
                    const int32_t st0 = (int32_t)((int64_t)k * 22050 / DIVS[rate]);   /* the grid: k slices after the beat */
                    if (st0 + sl > (int32_t)N) break;
                    for (int32_t t = 0; t < sl; t++) {
                        const int32_t part = t / sub, ps = t % sub, i = st0 + t;
                        if (part >= reps && t >= sub * reps) continue;      /* the slice's rest after the last whole part */
                        if (ps < f + 1 || ps >= sub - f - 1) continue;
                        int32_t want;
                        if (mode == M_GATE) {
                            if (ps >= s->duty) continue;                    /* closed (or its fade) */
                            want = x[i];
                        } else if (part == 0) want = x[i];
                        else if (mode == M_REPEAT) want = x[st0 + ps];
                        else if (mode == M_REVERSE) want = x[st0 + sub - 1 - ps];
                        else {                                              /* BRAKE: slower each part, down to 30 % */
                            int32_t rate16 = 65536 - part * 7864; if (rate16 < 19661) rate16 = 19661;
                            want = x[st0 + (int32_t)(((int64_t)ps * rate16) >> 16)];
                        }
                        checked++;
                        if (y[i] != want) bad++;
                    }
                }
                CHECK(checked > 0 && bad == 0, "rate %s mode %s reps %d: %ld of %ld samples off the grid/mode law", rn[rate], mn[mode], reps, bad, checked);
                drop(&in, 0);
            }
    /* chance 0: the dry sound, exactly; freeze: the frozen part keeps playing across slices */
    {
        inst_t in; make(&in, 0, 0);
        short p[8] = { 17152, 2304, 8000, 0, 0, 13568, 0, 0 };
        memcpy(y, x, N * 2);
        host_beat.len = 22050 << 14; host_beat.step = 0x100000;
        mock_reg->init(&in.fx);
        int32_t next = 5000 << 14;
        for (size_t b = 0; b < NB; b++) { host_beat.next = next; mock_reg->mono(&in.fx, y + 64 * b, p); next -= 0x100000; while (next < 0) next += 22050 << 14; }
        CHECK(memcmp(x, y, N * 2) == 0, "chance 0 is the dry sound exactly");
        drop(&in, 0);
    }
    {
        inst_t in; make(&in, 0, 0);
        short p[8] = { 17152, 2304, 0, 0, 0, 13568, 0, 0 };   /* 1/16, repeat, 4 parts (1378 samples), chance 0 */
        memcpy(y, x, N * 2);
        freeze_latch[0] = 1;
        mock_reg->init(&in.fx);
        int32_t next = 0;
        for (size_t b = 0; b < NB; b++) { host_beat.next = next; mock_reg->mono(&in.fx, y + 64 * b, p); next -= 0x100000; while (next < 0) next += 22050 << 14; }
        freeze_latch[0] = 0;
        long bad = 0;
        for (int k = 3; k < 12; k++) {                         /* slices 3..11 (at k x 5512.5): still slice 0's first part */
            const int32_t st0 = (int32_t)((int64_t)k * 22050 / 4);
            for (int32_t t = 0; t < 4 * 1378; t++) {
                const int32_t ps = t % 1378;
                if (ps < 100 || ps > 1270) continue;
                if (y[st0 + t] != x[ps]) bad++;
            }
        }
        CHECK(bad == 0, "freeze holds the frozen part across slices (%ld off)", bad);
        drop(&in, 0);
    }
    save("stutter_marked_in.raw", x, N);
    /* tempo: the clock lost (len 0): the grid runs on at 120 BPM (22050); the tape stopped while followed: too */
    for (int c = 0; c < 2; c++) {
        inst_t in; make(&in, 0, 0);
        short p[8] = { 0, 2304, 0, 32767, 0, 256, 0, 0 };    /* 1/4, repeat, 2 parts */
        memcpy(y, x, N * 2);
        host_beat.len = c ? 22050 << 14 : 0; host_beat.next = 3; host_beat.step = c ? 0 : 0x100000; host_beat.follows_tape = (uint8_t)c;
        mock_reg->init(&in.fx);
        render_mono(&in, y, NB, p);
        const st_t *s = in.fx.state;
        CHECK(s->len == 22050 && s->sub == 11025, "%s: free-running 120 BPM (slice %d)", c ? "tape stopped" : "no clock", s->len);
        drop(&in, 0);
    }
    /* 30 BPM: a 1/4 slice (88200) is cut to the ring (65472); nothing reads outside it (ASan) */
    {
        inst_t in; make(&in, 0, 0);
        short p[8] = { 0, 18000, 32767, 32767, 0, 256, 0, 0 };
        memcpy(y, x, N * 2);
        host_beat.len = (int32_t)((2646000LL << 14) / 30); host_beat.next = 0; host_beat.step = 0x100000; host_beat.follows_tape = 0;
        mock_reg->init(&in.fx);
        render_mono(&in, y, NB, p);
        CHECK(((const st_t *)in.fx.state)->len == NBUF - 64, "30 BPM: the slice is cut to the ring");
        drop(&in, 0);
    }
    /* the synth and drum share one bulk: after the synth, the instrument changes and the core re-inits */
    {
        inst_t syn, drm, ref;
        make(&syn, 0, 0); make(&drm, 0, syn.fx.bulk); make(&ref, 0, 0);
        short p[8] = { 20000, 18000, 8000, 30000, 0, 13568, 0, 0 };
        host_beat.len = 22050 << 14; host_beat.next = 0; host_beat.step = 0x100000;
        memcpy(y, x, N * 2); mock_instrument = 0; mock_reg->init(&syn.fx); render_mono(&syn, y, 900, p);
        int16_t *z = malloc(N * 2);
        memcpy(y, x, N * 2); memcpy(z, x, N * 2);
        mock_instrument = 1;
        mock_reg->init(&drm.fx); render_mono(&drm, y, NB, p);
        mock_reg->init(&ref.fx); render_mono(&ref, z, NB, p);
        CHECK(memcmp(y, z, N * 2) == 0, "shared bulk: the drum after the synth == a fresh instance");
        free(z);
        drop(&syn, 0); drop(&drm, 1); drop(&ref, 0);
    }
    free(x); free(y);
}
#endif

/* ---------------------------------------------------------------- the page's picture (both) */
/* One frame as the core runs it: the frame's start, the drawing under the OS's screen, then the core's page (view).
 * Returns the number of primitives drawn under. */
static int page_frame(const short *par)
{
    struct opus_fx_view v;
    mock_frame_clear();
    if (mock_frame_begin) mock_frame_begin();
    if (mock_frame_under) mock_frame_under((unsigned short *)0);
    const int n = mock_prims;
    if (mock_page_shown) mock_reg->view(par, &v);
    return n;
}
static unsigned long frame_hash(void)
{
    unsigned long h = 1469598103u;
    for (int y = 0; y < MOCK_H; y++) for (int x = 0; x < MOCK_W; x++) for (int c = 0; c < 3; c++) h = (h ^ mock_fb[y][x][c]) * 16777619u;
    return h;
}
/* the art's box, clear of the corners' labels and values the core draws, and within the window (y 2-146) */
#define ART_X0 88
#define ART_X1 232
#define ART_Y0 14
#define ART_Y1 134

static void display_checks(void)
{
    CHECK(mock_frame_begin != 0 && mock_frame_under != 0, "frame handlers registered");
    const size_t N = 64 * 6000;
    int16_t *x = malloc(N * 2);
    signal(x, N, 77);
    for (size_t i = 0; i < N; i++) if ((i / 4410) % 7 == 0 && (i % 4410) < 40) x[i] = (int16_t)((i & 1) ? 30000 : -30000);   /* clicks: onsets */
    unsigned used = 0;
    int worst = 0, outside = 0, badc = 0, frames_drawn = 0;
#ifdef TEST_REVERB
    static const short sets[4][8] = { { 16000, 20000, 16384, 12000 }, { 32767, 0, 32767, 32767 }, { 0, 32767, 0, 0 }, { 26000, 12000, 20000, 20000 } };
    const int nsets = 4;
#else
    static const short sets[8][8] = {
        { 17152, 2304, 8000, 32767, 0, 13568 }, { 17152, 10000, 8000, 32767, 0, 13568 }, { 17152, 18000, 30000, 32767, 0, 13568 },
        { 17152, 30000, 20000, 32767, 0, 13568 }, { 0, 2304, 0, 20000, 0, 256 }, { 32767, 2304, 32767, 32767, 0, 2816 },
        { 25000, 10000, 16000, 32767, 0, 2304 }, { 12000, 18000, 1000, 32767, 0, 1792 } };
    const int nsets = 8;
    unsigned long mode_hash[4] = { 0, 0, 0, 0 };
    host_beat.len = 22050 << 14; host_beat.step = 0x100000; host_beat.follows_tape = 0;
#endif
    for (int pos = 0; pos < 3; pos++)
        for (int k = 0; k < nsets; k++) {
            inst_t in; make(&in, pos == 2, 0);
            mock_instrument = pos == 1;
            mock_owner = pos == 2 ? 16 : (pos == 1 ? 9 : 2);
            mock_page_shown = 1;
            mock_reg->init(&in.fx);
            int16_t l[64], r[64];
#ifndef TEST_REVERB
            int32_t next = 0;
#endif
            for (size_t b = 0; b < N / 64; b++) {
#ifndef TEST_REVERB
                host_beat.next = next; next -= 0x100000; while (next < 0) next += 22050 << 14;
#endif
                for (int i = 0; i < 64; i++) { l[i] = x[64 * b + i]; r[i] = (int16_t)(x[64 * b + i] / 2); }
                if (pos == 2) mock_reg->stereo(&in.fx, l, r, sets[k]); else mock_reg->mono(&in.fx, l, sets[k]);
                if (b % 23 == 0) {                         /* a frame about every 33 ms */
                    const int n = page_frame(sets[k]);
                    if (b == 0) continue;                  /* the first frame: the page not yet seen */
                    frames_drawn += n > 0;
                    if (n > worst) worst = n;
                    if (n && (mock_minx < ART_X0 || mock_maxx > ART_X1 || mock_miny < ART_Y0 || mock_maxy > ART_Y1)) outside++;
                    badc += mock_bad_colour;
                    used |= mock_colours_used;
#ifndef TEST_REVERB
                    const int m = (int)sx_range(sets[k][1], 4);
                    if (((const st_t *)in.fx.state)->stut && ((const st_t *)in.fx.state)->part >= 1) mode_hash[m] ^= frame_hash();
#endif
                }
            }
            /* the audio stops (as when the effect is switched off and its blocks no longer run): the picture settles
             * and stays still: no animation claims processing that is not happening */
            unsigned long h1 = 0, h2 = 1;
            for (int f = 0; f < 20; f++) { page_frame(sets[k]); h1 = h2; h2 = frame_hash(); }
            CHECK(h1 == h2, "pos %d set %d: still when no audio block runs", pos, k);
#ifdef TEST_REVERB
            page_frame(sets[k]);
            CHECK(!(mock_colours_used & (4 | 16)), "pos %d set %d: with no audio, no reflections and no orange (idle tunnel)", pos, k);
#endif
            drop(&in, 0);
        }
    CHECK(frames_drawn > 100, "frames drawn: %d", frames_drawn);
    CHECK(outside == 0, "%d frames drew outside the art box %d-%d x %d-%d", outside, ART_X0, ART_X1, ART_Y0, ART_Y1);
    CHECK(badc == 0, "only the encoders' colours, dim and off: %d others", badc);
    CHECK((used & 30) == 30, "all four encoder colours used over the run (mask %x)", used);
    CHECK(worst <= 200, "at most 200 primitives a frame: %d", worst);
    printf("  page: %d frames drawn, worst %d primitives a frame\n", frames_drawn, worst);
#ifndef TEST_REVERB
    for (int a = 0; a < 4; a++) for (int b = a + 1; b < 4; b++) CHECK(mode_hash[a] != mode_hash[b], "modes %d and %d draw differently", a, b);
#endif
    /* only on its own page: no page, a stale view (another type's page), another owner: nothing drawn */
    {
        inst_t in; make(&in, 0, 0);
        mock_reg->init(&in.fx);
        mock_page_shown = 1; mock_owner = 3;
        page_frame(sets[0]); page_frame(sets[0]);
        CHECK(page_frame(sets[0]) > 0, "draws on its own page");
        mock_page_shown = 0;
        CHECK(page_frame(sets[0]) == 0, "nothing without an effect page");
        mock_page_shown = 1;
        mock_owner = 11;
        CHECK(page_frame(sets[0]) == 0, "nothing for another owner's page");
        mock_owner = 3;
        page_frame(sets[0]);
        struct opus_fx_view v;
        for (int f = 0; f < 4; f++) { mock_frame_clear(); mock_frame_begin(); mock_frame_under(0); }   /* view stops: another type */
        (void)v;
        CHECK(mock_prims == 0, "nothing once its view stops (another type's page)");
        drop(&in, 0);
    }
    free(x);
}

int main(int argc, char **argv)
{
    if (argc > 1) dir = argv[1];
    struct opl_self self = { sizeof self, "test", "/tmp", 0 };
    if (opl_init(&self) != 0 || !mock_reg) { fprintf(stderr, "opl_init failed\n"); return 1; }
#ifdef TEST_REVERB
    reverb_checks();
#else
    stutter_checks();
#endif
    display_checks();
    printf("%s %s port checks: %d passed, %d failed\n", fails ? "FAIL" : "PASS", mock_reg->name, passes, fails);
    return fails ? 1 : 0;
}
