/* Hundekraft's original effect, unchanged (kennel 3.1.1's plugins/reverb/reverb.c or plugins/stutter/stutter.c,
 * included by path), on the PC with op1r's host mocks (harness/op1_host.h), playing scenario.h.
 * Its FX positions run as kennel's dispatcher runs them: synth 0 and drum 1 mono on their own scratch, the master
 * (2) on the mono sum m = (L + R) / 2 with its change y - m added to both sides (dispatcher.c,
 * fx_render_stereo_dispatch). The scratch buffers start dirty, as after another effect.
 *   cc -DOP1_HOST -DEFFECT_SRC='"/path/reverb.c"' -DFX=FX_reverb -DREVERB -I KENNEL/synth_sdk -I tests/host ...
 * Copyright (C) 2026 sammi. GPL-3.0-or-later with the OP-1 firmware permission. */
#include "op1_plugin.h"
#include <stdlib.h>
#include "scenario.h"

int16_t host_sine[8192]; int32_t host_pitch_inc[128]; uint8_t host_voice_mem[6 * 0x24]; int16_t host_voice_vel[6];
int16_t host_voice_env[128]; int16_t host_accel[3];
int16_t host_knobs[8][8]; int host_slot; const uint8_t *host_sample_buffer[8];
int16_t host_fx_knobs[3][8]; int16_t host_fx_meter[3]; void *host_fx_scratch[3];
volatile uint8_t op1_fx_page[3][4]; volatile uint8_t op1_eng_page[4];
void *host_find_node(void *s, const char *id) { (void)s; (void)id; return 0; }
void host_node_set_transform(void *n, const int32_t *m) { (void)n; (void)m; }
void host_node_set_visible(void *n, int on) { (void)n; (void)on; }
uint8_t *host_node_style_rgba(void *s, const char *id) { static uint8_t c[4]; (void)s; (void)id; return c; }
void host_screen_draw(void *s, int dt) { (void)s; (void)dt; }
void *host_raster(void) { return 0; }
void host_raster_line(void *r, const void *a, const void *b, int32_t w, const uint8_t *c) { (void)r; (void)a; (void)b; (void)w; (void)c; }
void host_raster_dim(void *r) { (void)r; }
void *read_fx_param_safe(int slot, int pos) { (void)slot; (void)pos; return 0; }

#include EFFECT_SRC

static uint32_t dirt = 0xC0FFEEu;
static void dirty(void *p, size_t n) { uint8_t *b = p; for (size_t i = 0; i < n; i++) { dirt = dirt * 1103515245u + 12345u; b[i] = (uint8_t)(dirt >> 16); } }

static void a_init(int pos) { dirty(host_fx_scratch[pos], OP1_FX_SCRATCH_BYTES); FX.init(pos); }
static long nb;
static void a_mono(int pos, int16_t *buf, const int16_t *par) { FX.render(pos, buf, par);
#if defined(TRACE) && !defined(REVERB)
    const st_t *s = &st[pos]; fprintf(stderr, "%ld p%d st%d fr%d part%d ps%d len%d sub%d start%u w%u rec%d\n", nb, pos, s->stut, s->frozen, s->part, s->ps, s->len, s->sub, s->start, s->w, s->rec);
#endif
    nb++; }
static void a_stereo(int16_t *l, int16_t *r, const int16_t *par)
{
    int16_t m[OP1_FX_BLOCK], y[OP1_FX_BLOCK];
    for (int i = 0; i < OP1_FX_BLOCK; i++) y[i] = m[i] = (int16_t)((l[i] + r[i]) >> 1);
    FX.render(OP1_FX_MASTER, y, par);
    for (int i = 0; i < OP1_FX_BLOCK; i++) {
        int32_t d = y[i] - m[i];
        l[i] = op1_sat16(l[i] + d);
        r[i] = op1_sat16(r[i] + d);
    }
}
static void a_freeze(int pos, int on) { op1_fx_page[pos][3] = (uint8_t)on; }
static void a_beat(int32_t len, int32_t next, int32_t step, int follows)
{
    op1_beat_t *b = host_beat();
    b->len = len; b->next = next; b->step = step; b->follows_tape = (uint8_t)follows; b->tape_pos = 0;
}

int main(void)
{
    for (int i = 0; i < 3; i++) host_fx_scratch[i] = malloc(OP1_FX_SCRATCH_BYTES);
    const fx_api a = { a_init, a_mono, a_stereo, a_freeze, a_beat };
#ifdef REVERB
    reverb_scenarios(&a);
#else
    stutter_scenarios(&a);
#endif
    for (int i = 0; i < 3; i++) free(host_fx_scratch[i]);
    return 0;
}
