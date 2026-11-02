/* Previews of an effect's page: the plugin's own drawing code, run on the PC with its own audio code on a real input,
 * frame by frame as the core runs them (the frame's start, the drawing under the OS's screen, the core's page), into
 * tests/host/opus_mock.h's stand-in rasteriser (one unit = one pixel; the OS's renderer smooths lines, this one does
 * not). Writes OUT/NAME.frames (320 x 160 RGB per frame) and OUT/NAME.txt (per frame: the four labels and values the
 * core would draw in the corners), which tests/host/preview.py turns into PNG and GIF files.
 *   cc -DTEST_REVERB -DEFFECT_SRC='"plugins/sammi.reverb/reverb.c"' ... or -DSTUTTER_HOST ... (the stutter)
 *   preview OUT
 * Copyright (C) 2026 sammi. GPL-3.0-or-later with the OP-1 firmware permission. */
#include <stdlib.h>
#include <string.h>
#include "opus_mock.h"
#include EFFECT_SRC

#ifndef TEST_REVERB
op1_beat_t host_beat;
#endif
static const char *dir = ".";
static FILE *ff, *ft;
static struct opus_fx fx;
static struct opus_need nd;

static void open_seq(const char *name)
{
    char p[512];
    snprintf(p, sizeof p, "%s/%s.frames", dir, name); ff = fopen(p, "wb");
    snprintf(p, sizeof p, "%s/%s.txt", dir, name); ft = fopen(p, "w");
    if (!ff || !ft) { fprintf(stderr, "cannot write %s\n", p); exit(1); }
}
static void close_seq(void) { fclose(ff); fclose(ft); }
static void frame(const short *par, int keep)
{
    struct opus_fx_view v;
    mock_frame_clear();
    mock_frame_begin();
    mock_frame_under(0);
    mock_reg->view(par, &v);
    if (!keep) return;
    fwrite(mock_fb, 1, sizeof mock_fb, ff);
    for (int i = 0; i < 4; i++) fprintf(ft, "%s|%s%s", v.label[i], v.value[i], i < 3 ? "|" : "\n");
}
static void fresh(void)
{
    memset(fx.state, 0, nd.state);
    memset(fx.bulk, 0, nd.bulk);
    mock_reg->init(&fx);
}

/* the input: chords of decaying saws with a click on each beat (120 BPM), silent stretches */
static uint32_t ph[3], rng = 1;
static int32_t env;
static int16_t input(long t, int silent_after)
{
    if (silent_after >= 0 && t >= silent_after) return 0;
    if (t % 22050 == 0) env = 26000;
    static const uint32_t inc[3] = { 6130000u, 7720000u, 9180000u };
    int32_t s = 0;
    for (int k = 0; k < 3; k++) { ph[k] += inc[k]; s += ((int32_t)(ph[k] >> 16) - 32768) / 3; }
    s = s * env >> 15;
    env -= env >> 11;
    if (t % 22050 < 30) { rng = rng * 1664525u + 1013904223u; s = (int32_t)(rng >> 16) - 32768; }
    return (int16_t)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
}

/* nblocks of audio through the effect (the synth), a frame every 23 blocks (about 30 a second), kept if keep */
static long t;
static void play_seq(const short *par, int nblocks, int keep, int silent_after)
{
    int16_t b[64];
#ifndef TEST_REVERB
    static int32_t next = 0;
#endif
    for (int k = 0; k < nblocks; k++) {
#ifndef TEST_REVERB
        host_beat.len = 22050 << 14; host_beat.step = 0x100000; host_beat.next = next;
        next -= 0x100000; while (next < 0) next += 22050 << 14;
#endif
        for (int i = 0; i < 64; i++) b[i] = input(t++, silent_after);
        mock_reg->mono(&fx, b, par);
        if (k % 23 == 0) frame(par, keep);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1) dir = argv[1];
    struct opl_self self = { sizeof self, "preview", "/tmp", 0 };
    if (opl_init(&self) != 0) return 1;
    mock_reg->need(0, &nd);
    fx.state = calloc(1, nd.state); fx.bulk = calloc(1, nd.bulk); fx.stereo = 0;
    mock_instrument = 0; mock_owner = 0; mock_page_shown = 1;
#ifdef TEST_REVERB
    static const struct { const char *name; short par[8]; int silent; } seq[] = {
        { "reverb_default", { 16000, 20000, 16384, 12000 }, -1 },
        { "reverb_long", { 32767, 26000, 26000, 26000 }, 44100 },
        { "reverb_short", { 0, 20000, 16384, 26000 }, 44100 },
        { "reverb_dark", { 20000, 0, 0, 24000 }, -1 },
        { "reverb_dry", { 16000, 32767, 32767, 3000 }, -1 },
    };
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++) {
        fresh(); t = 0; env = 0;
        open_seq(seq[i].name);
        play_seq(seq[i].par, 3 * 689, 1, seq[i].silent);        /* 3 s */
        close_seq();
    }
    /* idle: no input at all */
    fresh(); t = 0; open_seq("reverb_idle"); { short p[8] = { 16000, 20000, 16384, 12000 }; play_seq(p, 300, 1, 0); } close_seq();
#else
    static const struct { const char *name; short par[8]; } seq[] = {
        { "stutter_repeat", { 17152, 2304, 8000, 32767, 0, 13568 } },
        { "stutter_brake", { 10000, 10000, 8000, 32767, 0, 1792 } },
        { "stutter_reverse", { 17152, 18000, 20000, 32767, 0, 13568 } },
        { "stutter_gate", { 21000, 30000, 16000, 32767, 0, 13568 } },
        { "stutter_fast", { 32767, 2304, 2000, 32767, 0, 2816 } },
        { "stutter_chance", { 17152, 2304, 8000, 12000, 0, 13568 } },
    };
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++) {
        fresh(); t = 0; env = 0;
        open_seq(seq[i].name);
        play_seq(seq[i].par, 4 * 689, 1, -1);                    /* 4 s */
        close_seq();
    }
    fresh(); t = 0;
    open_seq("stutter_freeze");
    { short p[8] = { 17152, 2304, 8000, 12000, 0, 1792 };
      play_seq(p, 300, 1, -1); freeze_latch[0] = 1; play_seq(p, 1400, 1, -1); freeze_latch[0] = 0; play_seq(p, 300, 1, -1); }
    close_seq();
#endif
    free(fx.state); free(fx.bulk);
    return 0;
}
