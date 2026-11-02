/* The Opus 2 core's calls a ported effect makes, on the PC: the kit's own headers (opus_plugin.h, op1fw.h) for the
 * declarations, these definitions for the calls. Header-only: one test program each.
 * The page calls (opus_page_*) draw into a 320 x 160 RGB frame (one unit = one pixel, as the OP-1 FE's panel) with
 * plain integer rasterising (no smoothing), and count and bound every primitive, for the checks and the previews.
 * Colours: the stock encoder colours as kennel's screens use them (white: Opus's "lavender" white); this is a stand-in
 * for the OS's renderer, not the OS's renderer.
 * Copyright (C) 2026 sammi. GPL-3.0-or-later with the OP-1 firmware permission. */
#ifndef OPUS_MOCK_H
#define OPUS_MOCK_H
#include <stdarg.h>
#include <stdio.h>
#include "opus_plugin.h"
#include "op1fw.h"

static const struct opus_effect *mock_reg;     /* what opl_init registered */
static int mock_instrument;                    /* opus_instrument_now(): the instrument the block plays */
static int mock_owner;                         /* opus_fx_owner(): the effect page's owner (0-7, 8-15, 16) */
static int mock_page_shown;                    /* opus_fx_page_shown() */
static int mock_reg_count;
static void (*mock_frame_begin)(void);
static void (*mock_frame_under)(unsigned short *buf);

int opus_effect_register(struct opl_self *self, const struct opus_effect *e)
{
    (void)self;
    if (!e || e->size < sizeof(struct opus_effect) || !e->name) return -1;
    mock_reg = e;
    mock_reg_count++;
    return 8;
}
int opus_instrument_now(void) { return mock_instrument; }
int opus_fx_owner(void) { return mock_owner; }
int opus_fx_page_shown(void) { return mock_page_shown; }
void opus_on_frame_begin(struct opl_self *self, void (*fn)(void)) { (void)self; mock_frame_begin = fn; }
void opus_on_frame_under(struct opl_self *self, void (*fn)(unsigned short *buf)) { (void)self; mock_frame_under = fn; }
int fw_snprintf(char *dst, unsigned int size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(dst, size, fmt, ap);
    va_end(ap);
    return n;
}

/* ---- the page calls: a frame, its primitives' count and bounds ---- */
#define MOCK_W 320
#define MOCK_H 160
static unsigned char mock_fb[MOCK_H][MOCK_W][3];
static int mock_prims, mock_minx, mock_miny, mock_maxx, mock_maxy, mock_bad_colour;
static unsigned mock_colours_used;             /* bit i: colour id class i (1 E1, 2 E2, 3 E3, 4 E4, 5 DIM, 6 OFF, 0 other) */
static void mock_frame_clear(void)
{
    for (int y = 0; y < MOCK_H; y++) for (int x = 0; x < MOCK_W; x++) mock_fb[y][x][0] = mock_fb[y][x][1] = mock_fb[y][x][2] = 0;
    mock_prims = 0; mock_minx = mock_miny = 100000; mock_maxx = mock_maxy = -100000; mock_bad_colour = 0; mock_colours_used = 0;
}
static unsigned mock_rgb(int c)
{
    switch (c) {
    case OPUS_COLOR_E1: mock_colours_used |= 2; return 0x698EFF;
    case OPUS_COLOR_E2: mock_colours_used |= 4; return 0x00ED95;
    case OPUS_COLOR_E3: mock_colours_used |= 8; return 0xE6E1FF;
    case OPUS_COLOR_E4: mock_colours_used |= 16; return 0xFF3A5D;
    case OPUS_COLOR_DIM: mock_colours_used |= 32; return 0x8C8C8C;
    case OPUS_COLOR_OFF: mock_colours_used |= 64; return 0x3A3A3A;
    case OPUS_COLOR_TEXT: return 0xFFFFFF;
    default: mock_bad_colour++; mock_colours_used |= 1; return 0xFF00FF;
    }
}
static void mock_px(int x, int y, unsigned rgb)
{
    if (x < 0 || y < 0 || x >= MOCK_W || y >= MOCK_H) return;
    mock_fb[y][x][0] = (unsigned char)(rgb >> 16); mock_fb[y][x][1] = (unsigned char)(rgb >> 8); mock_fb[y][x][2] = (unsigned char)rgb;
}
static void mock_bound(int x0, int y0, int x1, int y1)
{
    mock_prims++;
    if (x0 < mock_minx) mock_minx = x0;
    if (x1 > mock_maxx) mock_maxx = x1;
    if (y0 < mock_miny) mock_miny = y0;
    if (y1 > mock_maxy) mock_maxy = y1;
}
static void mock_order(int *a, int *b) { if (*a > *b) { const int t = *a; *a = *b; *b = t; } }
void opus_page_fill(int x0, int y0, int x1, int y1, int colour)
{
    mock_order(&x0, &x1); mock_order(&y0, &y1); mock_bound(x0, y0, x1, y1);
    const unsigned c = mock_rgb(colour);
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) mock_px(x, y, c);
}
void opus_page_frame(int x0, int y0, int x1, int y1, int colour)
{
    mock_order(&x0, &x1); mock_order(&y0, &y1); mock_bound(x0, y0, x1, y1);
    const unsigned c = mock_rgb(colour);
    for (int x = x0; x <= x1; x++) { mock_px(x, y0, c); mock_px(x, y1, c); }
    for (int y = y0; y <= y1; y++) { mock_px(x0, y, c); mock_px(x1, y, c); }
}
void opus_page_line(int x0, int y0, int x1, int y1, int colour)
{
    int a = x0, b = x1, c0 = y0, d = y1;
    mock_order(&a, &b); mock_order(&c0, &d); mock_bound(a, c0, b, d);
    const unsigned c = mock_rgb(colour);
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0, sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        mock_px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}
void opus_page_dot(int x, int y, int r, int filled, int colour)
{
    mock_bound(x - r, y - r, x + r, y + r);
    const unsigned c = mock_rgb(colour);
    for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) {
        const int d = i * i + j * j;
        if (filled ? d <= r * r : (d <= r * r && d > (r - 1) * (r - 1))) mock_px(x + i, y + j, c);
    }
}
void opus_sincos(int turn4096, int *s, int *c)
{
    /* integer sine by a short table and linear steps: a stand-in for the core's */
    static const short q[17] = { 0, 401, 799, 1189, 1567, 1931, 2276, 2598, 2896, 3166, 3406, 3612, 3784, 3920, 4017, 4076, 4096 };
    int t = turn4096 & 4095;
    int sg = 1;
    if (t >= 2048) { t -= 2048; sg = -1; }
    int u = t <= 1024 ? t : 2048 - t;               /* 0..1024: a quarter turn */
    const int i = u >> 6, f = u & 63;
    const int sv = q[i] + (((q[i < 16 ? i + 1 : 16] - q[i]) * f) >> 6);
    int ct = (turn4096 + 1024) & 4095, cs = 1;
    if (ct >= 2048) { ct -= 2048; cs = -1; }
    int v = ct <= 1024 ? ct : 2048 - ct;
    const int k = v >> 6, g = v & 63;
    const int cv = q[k] + (((q[k < 16 ? k + 1 : 16] - q[k]) * g) >> 6);
    *s = sg * sv;
    *c = cs * cv;
}
#endif
