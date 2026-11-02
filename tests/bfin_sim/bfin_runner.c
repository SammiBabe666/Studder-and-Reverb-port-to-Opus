/* The ports' compiled Blackfin code in the instruction-set simulator (binutils-gdb sim/bfin, model bf524): the
 * plugin source compiled with the Opus kit's own flags (-mcpu=bf524-any -O2 -ffreestanding -fno-builtin), the core's
 * names stubbed, bare-metal, playing tests/host/scenario.h exactly as tests/host/port_runner.c does on the PC (the same
 * dirty memory, the same order). The stutter's beat clock is written where the firmware keeps it (0x01AADABC, the tape
 * speed at 0xFF802970), so the port's own deep reads run.
 *   dump mode: the raw output on stdout (compared with Hundekraft's original on the PC, byte for byte);
 *   -DSIM_CYCLES: the worst and mean cycles of a process call (mono, stereo) and of init.
 * Built and run by tests/bfin_sim/run.sh. start.S and sim.ld follow Project_Hollow_Saint's (GPL-3.0-or-later).
 * Copyright (C) 2026 sammi. GPL-3.0-or-later with the OP-1 firmware permission. */
#define SCENARIO_OUT 1
#ifdef TEST_STUTTER
#define BEAT_ADDR 0x01AADABC
#endif
#include EFFECT_SRC
#include "scenario.h"

int sim_syscall(int nr, const int *args);
uint32_t sim_cycles(void);
static void out_bytes(const void *p, int n) { int a[3] = { 1, (int)p, n }; sim_syscall(5, a); }
#ifndef SIM_CYCLES
static void out(const int16_t *b, int n) { out_bytes(b, 2 * n); }
#else
static void out(const int16_t *b, int n) { (void)b; (void)n; }
static void out_text(const char *s) { int n = 0; while (s[n]) n++; out_bytes(s, n); }
static void out_num(uint32_t v) { char b[12]; int i = 11; b[i] = 0; do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v); out_text(b + i); }
#endif

/* the core, stubbed */
static const struct opus_effect *reg;
static int instrument, owner;
int opus_effect_register(struct opl_self *s, const struct opus_effect *e) { (void)s; reg = e; return 8; }
int opus_instrument_now(void) { return instrument; }
int opus_fx_owner(void) { return owner; }
int fw_snprintf(char *dst, unsigned int size, const char *fmt, ...) { (void)fmt; if (size) dst[0] = 0; return 0; }
/* the page's calls: counted, not drawn (their cost is the core's renderer, not the plugin's) */
static int page_shown, prims;
static void (*frame_begin_fn)(void);
static void (*frame_under_fn)(unsigned short *buf);
int opus_fx_page_shown(void) { return page_shown; }
void opus_on_frame_begin(struct opl_self *s, void (*fn)(void)) { (void)s; frame_begin_fn = fn; }
void opus_on_frame_under(struct opl_self *s, void (*fn)(unsigned short *buf)) { (void)s; frame_under_fn = fn; }
void opus_page_fill(int x0, int y0, int x1, int y1, int c) { (void)x0; (void)y0; (void)x1; (void)y1; (void)c; prims++; }
void opus_page_frame(int x0, int y0, int x1, int y1, int c) { (void)x0; (void)y0; (void)x1; (void)y1; (void)c; prims++; }
void opus_page_line(int x0, int y0, int x1, int y1, int c) { (void)x0; (void)y0; (void)x1; (void)y1; (void)c; prims++; }
void opus_page_dot(int x, int y, int r, int f, int c) { (void)x; (void)y; (void)r; (void)f; (void)c; prims++; }
void opus_sincos(int t, int *s, int *c) { (void)t; *s = 0; *c = 4096; }

/* the instances' memory: what need() asks for, 32-byte aligned, in one arena */
static uint8_t arena[3 * (131072 + 1024)] __attribute__((aligned(32)));
static struct opus_fx fx[3];
static struct opus_need nd[3];
static uint32_t dirt = 0xBADC0DEu;                     /* port_runner.c's generator: the same bytes */
static void dirty(void *p, unsigned n) { uint8_t *b = p; for (unsigned i = 0; i < n; i++) { dirt = dirt * 1103515245u + 12345u; b[i] = (uint8_t)(dirt >> 16); } }

#ifdef SIM_CYCLES
static uint32_t worst[3], count[3], init_worst;
static uint64_t sum[3];
static void charge(int pos, uint32_t c) { if (c > worst[pos]) worst[pos] = c; sum[pos] += c; count[pos]++; }
#define TIMED(pos, call) do { const uint32_t t0_ = sim_cycles(); call; charge((pos), sim_cycles() - t0_); } while (0)
#else
#define TIMED(pos, call) call
#endif

static void a_init(int pos)
{
    dirty(fx[pos].state, nd[pos].state);
    dirty(fx[pos].bulk, nd[pos].bulk);
    instrument = pos == 1;
#ifdef SIM_CYCLES
    const uint32_t t0 = sim_cycles();
    reg->init(&fx[pos]);
    const uint32_t c = sim_cycles() - t0;
    if (c > init_worst) init_worst = c;
#else
    reg->init(&fx[pos]);
#endif
}
#if defined(TRACE) && !defined(SIM_CYCLES)
static void out_text(const char *s) { int n = 0; while (s[n]) n++; out_bytes(s, n); }
static void out_num(uint32_t v) { char b[12]; int i = 11; b[i] = 0; do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v); out_text(b + i); }
#endif
#ifdef SIM_CYCLES
static uint32_t page_worst, page_count, prims_worst, nblk;
static uint64_t page_sum;
static void page(int pos, const int16_t *par)
{
    if (++nblk % 23) return;
    struct opus_fx_view v;
    owner = pos == 2 ? 16 : (pos == 1 ? 9 : 2);
    page_shown = 1; prims = 0;
    const uint32_t t0 = sim_cycles();
    frame_begin_fn();
    frame_under_fn(0);
    reg->view(par, &v);
    const uint32_t c = sim_cycles() - t0;
    if (c > page_worst) page_worst = c;
    if ((uint32_t)prims > prims_worst) prims_worst = (uint32_t)prims;
    page_sum += c; page_count++;
}
#else
#define page(pos, par) ((void)0)
#endif
static void a_mono(int pos, int16_t *buf, const int16_t *par) { instrument = pos; TIMED(pos, reg->mono(&fx[pos], buf, par)); page(pos, par);
#if defined(TRACE) && defined(TEST_STUTTER)
    const st_t *s = fx[pos].state; const uint32_t v[12] = { (uint32_t)pos, s->stut, s->frozen, (uint32_t)s->part, (uint32_t)s->ps, (uint32_t)s->len, (uint32_t)s->sub, s->start, s->w, s->mode, (uint32_t)s->fade, (uint32_t)s->duty };
    for (int i = 0; i < 12; i++) { out_num(v[i]); out_text(" "); } out_num((uint32_t)par[0]); out_text(" "); out_num((uint32_t)par[1]); out_text("\n");
#endif
}
static void a_stereo(int16_t *l, int16_t *r, const int16_t *par) { TIMED(2, reg->stereo(&fx[2], l, r, par)); page(2, par); }
static void a_freeze(int pos, int on)
{
    if (!reg->shift_knob) return;
    short par[8] = { 0 };
    owner = pos == 2 ? 16 : (pos == 1 ? 8 + 5 : 3);
    reg->shift_knob(par, 3, on ? 1 : -1);
}
#ifdef TEST_STUTTER
static void a_beat(int32_t len, int32_t next, int32_t step, int follows)
{
    volatile uint8_t *m = (volatile uint8_t *)BEAT_ADDR;
    *(volatile int32_t *)(m + 0x1C) = len;
    *(volatile int32_t *)(m + 0x20) = next;
    m[0x14] = (uint8_t)follows;
    *(volatile uint32_t *)0xFF802970 = ((uint32_t)step >> 14) << 21;   /* the render's (sp >> 21) << 14 gives step back */
    *(volatile int32_t *)0xFF802974 = 0;
}
#else
#define a_beat 0
#endif

int main(void);
int main(void)
{
    static struct opl_self self = { sizeof(struct opl_self), "test", "/tmp", 0 };
    if (opl_init(&self) != 0 || !reg) return 1;
    uint8_t *p = arena;
    for (int pos = 0; pos < 3; pos++) {
        reg->need(pos == 2, &nd[pos]);
        if (nd[pos].state + nd[pos].bulk + 64 > 131072 + 1024) return 2;
        fx[pos].state = p; p += (nd[pos].state + 31) & ~31u;
        fx[pos].bulk = p; p += (nd[pos].bulk + 31) & ~31u;
        fx[pos].fast = 0;
        fx[pos].stereo = pos == 2;
    }
    const fx_api a = { a_init, a_mono, a_stereo, a_freeze, a_beat };
#ifdef TEST_STUTTER
    stutter_scenarios(&a);
#else
    reverb_scenarios(&a);
#endif
#ifdef SIM_CYCLES
    static const char *const where[3] = { "synth (mono)", "drum (mono)", "master (stereo)" };
    out_text(reg->name); out_text(": cycles per 64-sample block in the simulator (no SDRAM/cache model)\n");
    for (int i = 0; i < 3; i++) {
        if (!count[i]) continue;
        out_text("  "); out_text(where[i]); out_text(": worst "); out_num(worst[i]);
        out_text(", mean "); out_num((uint32_t)(sum[i] / count[i])); out_text(" over "); out_num(count[i]); out_text(" blocks\n");
    }
    out_text("  init: worst "); out_num(init_worst); out_text("\n");
    out_text("  page frame (the plugin's own drawing code and view; the core's renderer not included): worst ");
    out_num(page_worst); out_text(", mean "); out_num(page_count ? (uint32_t)(page_sum / page_count) : 0);
    out_text(" cycles, worst "); out_num(prims_worst); out_text(" primitives\n");
#endif
    return 0;
}
