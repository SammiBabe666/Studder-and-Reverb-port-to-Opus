/* The Opus port (plugins/sammi.reverb/reverb.c or plugins/sammi.stutter/stutter.c, included by path) on the PC,
 * through its registered struct opus_effect as the core calls it, playing scenario.h.
 * Each position's instance has its own state and bulk, both filled with garbage before every init (stricter than the
 * core, which clears them once); the master runs stereo(). FREEZE goes through the type's shift layer (shift + orange),
 * with the page's owner set to the position, as the key thread would.
 *   cc -DSTUTTER_HOST -DEFFECT_SRC='"/path/stutter.c"' -I OPUS_KIT/sdk -I tests/host ...
 * Copyright (C) 2026 sammi. GPL-3.0-or-later with the OP-1 firmware permission. */
#include <stdarg.h>
#include <stdlib.h>
#include "opus_mock.h"
#include "scenario.h"
#include EFFECT_SRC

static struct opus_fx fx[3];
static struct opus_need nd[3];
static uint32_t dirt = 0xBADC0DEu;
static void dirty(void *p, size_t n) { uint8_t *b = p; for (size_t i = 0; i < n; i++) { dirt = dirt * 1103515245u + 12345u; b[i] = (uint8_t)(dirt >> 16); } }

static void a_init(int pos)
{
    dirty(fx[pos].state, nd[pos].state);
    dirty(fx[pos].bulk, nd[pos].bulk);
    mock_instrument = pos == 1;
    mock_reg->init(&fx[pos]);
}
static long nb;
static void a_mono(int pos, int16_t *buf, const int16_t *par) { mock_instrument = pos; mock_reg->mono(&fx[pos], buf, par);
#if defined(TRACE) && defined(STUTTER_HOST)
    const st_t *s = fx[pos].state; fprintf(stderr, "%d %d %d %d %d %d %d %u %u %d %d %d %d %d\n", pos, s->stut, s->frozen, s->part, s->ps, s->len, s->sub, s->start, s->w, s->mode, s->fade, s->duty, par[0], par[1]);
#endif
    nb++; }
static void a_stereo(int16_t *l, int16_t *r, const int16_t *par) { mock_reg->stereo(&fx[2], l, r, par); }
static void a_freeze(int pos, int on)
{
    if (!mock_reg->shift_knob) return;
    short par[8] = { 0 };
    mock_owner = pos == 2 ? 16 : (pos == 1 ? 8 + 5 : 3);
    mock_reg->shift_knob(par, 3, on ? 1 : -1);
}
#ifdef STUTTER_HOST
op1_beat_t host_beat;
static void a_beat(int32_t len, int32_t next, int32_t step, int follows)
{
    host_beat.len = len; host_beat.next = next; host_beat.step = step; host_beat.follows_tape = (uint8_t)follows; host_beat.tape_pos = 0;
}
#else
#define a_beat 0
#endif

int main(void)
{
    struct opl_self self = { sizeof self, "test", "/tmp", 0 };
    if (opl_init(&self) != 0 || !mock_reg) { fprintf(stderr, "opl_init failed\n"); return 1; }
    for (int pos = 0; pos < 3; pos++) {
        mock_reg->need(pos == 2, &nd[pos]);
        fx[pos].state = nd[pos].state ? malloc(nd[pos].state) : 0;
        fx[pos].bulk = nd[pos].bulk ? malloc(nd[pos].bulk) : 0;    /* exactly what it asked for: ASan sees any overrun */
        fx[pos].fast = 0;
        fx[pos].stereo = pos == 2;
    }
    const fx_api a = { a_init, a_mono, a_stereo, a_freeze, a_beat };
#ifdef STUTTER_HOST
    stutter_scenarios(&a);
#else
    reverb_scenarios(&a);
#endif
    for (int pos = 0; pos < 3; pos++) { free(fx[pos].state); free(fx[pos].bulk); }
    return 0;
}
