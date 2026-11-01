/* The Opus 2 core's calls a ported effect makes, on the PC: the kit's own headers (opus_plugin.h, op1fw.h) for the
 * declarations, these few definitions for the calls. Header-only: one test program each.
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
static int mock_reg_count;

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
int fw_snprintf(char *dst, unsigned int size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(dst, size, fmt, ap);
    va_end(ap);
    return n;
}
#endif
