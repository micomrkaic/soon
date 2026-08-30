/* soon — keyboard-driven agenda and todo TUI
 * Copyright (C) 2026 Mico Mrkaić
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/* grid.c — see grid.h */
#include "grid.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/utf8.h"

style_t style(uint8_t fg, uint8_t bg, uint8_t attr)
{
    style_t s = { fg, bg, attr };
    return s;
}

static bool same_style(style_t a, style_t b)
{
    return a.fg == b.fg && a.bg == b.bg && a.attr == b.attr;
}

void grid_init(grid_t *g)
{
    memset(g, 0, sizeof *g);
}

void grid_free(grid_t *g)
{
    free(g->cells);
    free(g->prev);
    memset(g, 0, sizeof *g);
}

void grid_resize(grid_t *g, int w, int h)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    size_t n = (size_t)w * (size_t)h;
    cell_t *c = realloc(g->cells, n * sizeof *c);
    cell_t *p = realloc(g->prev, n * sizeof *p);
    if (!c || !p) { free(c ? c : g->cells); free(p ? p : g->prev); g->cells = g->prev = NULL; g->w = g->h = 0; return; }
    g->cells = c;
    g->prev = p;
    g->w = w;
    g->h = h;
    g->prev_valid = false;
    grid_clear(g, style(C_DEFAULT, C_DEFAULT, 0));
}

void grid_clear(grid_t *g, style_t st)
{
    cell_t blank = { ' ', st };
    for (int i = 0; i < g->w * g->h; i++) g->cells[i] = blank;
    g->cur_show = false;
}

static cell_t *at(grid_t *g, int x, int y)
{
    if (x < 0 || y < 0 || x >= g->w || y >= g->h) return NULL;
    return &g->cells[y * g->w + x];
}

/* Placing over half of a wide character must not leave an orphan half. */
static void clear_wide_at(grid_t *g, int x, int y)
{
    cell_t *c = at(g, x, y);
    if (!c) return;
    if (c->cp == GRID_CONT) {
        cell_t *l = at(g, x - 1, y);
        if (l) l->cp = ' ';
    } else if (utf8_width(c->cp) == 2) {
        cell_t *r = at(g, x + 1, y);
        if (r && r->cp == GRID_CONT) r->cp = ' ';
    }
}

void grid_put(grid_t *g, int x, int y, uint32_t cp, style_t st)
{
    cell_t *c = at(g, x, y);
    if (!c) return;
    int w = utf8_width(cp);
    if (w == 0) return;
    if (w == 2) {
        if (x + 1 >= g->w) { clear_wide_at(g, x, y); c->cp = ' '; c->st = st; return; }
        clear_wide_at(g, x, y);
        clear_wide_at(g, x + 1, y);
        c->cp = cp;
        c->st = st;
        cell_t *r = at(g, x + 1, y);
        r->cp = GRID_CONT;
        r->st = st;
        return;
    }
    clear_wide_at(g, x, y);
    c->cp = cp;
    c->st = st;
}

int grid_puts(grid_t *g, int x, int y, const char *s, int maxw, style_t st)
{
    int used = 0;
    uint32_t cp;
    int n;
    while ((n = utf8_decode(s, &cp)) > 0) {
        int w = utf8_width(cp);
        if (w == 0) { s += n; continue; }      /* combining marks: not rendered separately (see README) */
        if (used + w > maxw) break;
        grid_put(g, x + used, y, cp, st);
        used += w;
        s += n;
    }
    return used;
}

void grid_field(grid_t *g, int x, int y, const char *s, int w, style_t st, bool unicode)
{
    if (w <= 0) return;
    int sw = utf8_swidth(s);
    if (sw <= w) {
        int used = grid_puts(g, x, y, s, w, st);
        for (int i = used; i < w; i++) grid_put(g, x + i, y, ' ', st);
        return;
    }
    int used = grid_puts(g, x, y, s, w - 1, st);
    for (int i = used; i < w - 1; i++) grid_put(g, x + i, y, ' ', st);
    grid_put(g, x + w - 1, y, unicode ? 0x2026 : '~', st);
}

void grid_fill(grid_t *g, int x, int y, int w, int h, uint32_t cp, style_t st)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) grid_put(g, i, j, cp, st);
}

void grid_style(grid_t *g, int x, int y, int w, int h, style_t st)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) {
            cell_t *c = at(g, i, j);
            if (c) c->st = st;
        }
}

void grid_box(grid_t *g, int x, int y, int w, int h, style_t st, bool unicode)
{
    uint32_t tl, tr, bl, br, hz, vt;
    if (unicode) { tl = 0x250C; tr = 0x2510; bl = 0x2514; br = 0x2518; hz = 0x2500; vt = 0x2502; }
    else { tl = tr = bl = br = '+'; hz = '-'; vt = '|'; }
    if (w < 2 || h < 2) return;
    grid_put(g, x, y, tl, st);
    grid_put(g, x + w - 1, y, tr, st);
    grid_put(g, x, y + h - 1, bl, st);
    grid_put(g, x + w - 1, y + h - 1, br, st);
    for (int i = x + 1; i < x + w - 1; i++) { grid_put(g, i, y, hz, st); grid_put(g, i, y + h - 1, hz, st); }
    for (int j = y + 1; j < y + h - 1; j++) { grid_put(g, x, j, vt, st); grid_put(g, x + w - 1, j, vt, st); }
}

/* ---------- output ---------- */

void outbuf_put(outbuf_t *b, const char *s, size_t n)
{
    if (b->n + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 8192;
        while (nc < b->n + n + 1) nc *= 2;
        char *p = realloc(b->s, nc);
        if (!p) return;
        b->s = p;
        b->cap = nc;
    }
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = 0;
}

void outbuf_printf(outbuf_t *b, const char *fmt, ...)
{
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n > 0) outbuf_put(b, tmp, (size_t)(n < (int)sizeof tmp ? n : (int)sizeof tmp - 1));
}

static void emit_style(outbuf_t *b, style_t st)
{
    char seq[64];
    int n = snprintf(seq, sizeof seq, "\x1b[0");
    if (st.attr & A_BOLD) n += snprintf(seq + n, sizeof seq - (size_t)n, ";1");
    if (st.attr & A_DIM) n += snprintf(seq + n, sizeof seq - (size_t)n, ";2");
    if (st.attr & A_UL) n += snprintf(seq + n, sizeof seq - (size_t)n, ";4");
    if (st.attr & A_INV) n += snprintf(seq + n, sizeof seq - (size_t)n, ";7");
    if (st.attr & A_ITALIC) n += snprintf(seq + n, sizeof seq - (size_t)n, ";3");
    if (st.fg != C_DEFAULT) n += snprintf(seq + n, sizeof seq - (size_t)n, ";%d", st.fg < 8 ? 30 + st.fg : 90 + st.fg - 8);
    if (st.bg != C_DEFAULT) n += snprintf(seq + n, sizeof seq - (size_t)n, ";%d", st.bg < 8 ? 40 + st.bg : 100 + st.bg - 8);
    n += snprintf(seq + n, sizeof seq - (size_t)n, "m");
    outbuf_put(b, seq, (size_t)n);
}

void grid_render(grid_t *g, outbuf_t *out)
{
    style_t cur = style(C_DEFAULT, C_DEFAULT, 0);
    bool have_style = false;
    outbuf_put(out, "\x1b[?25l", 6);
    for (int y = 0; y < g->h; y++) {
        cell_t *row = &g->cells[y * g->w];
        cell_t *prow = &g->prev[y * g->w];
        if (g->prev_valid && memcmp(row, prow, (size_t)g->w * sizeof *row) == 0) continue;
        outbuf_printf(out, "\x1b[%d;1H", y + 1);
        for (int x = 0; x < g->w; x++) {
            cell_t *c = &row[x];
            if (c->cp == GRID_CONT) continue;
            if (!have_style || !same_style(cur, c->st)) { emit_style(out, c->st); cur = c->st; have_style = true; }
            char u[4];
            int n = utf8_encode(c->cp, u);
            outbuf_put(out, u, (size_t)n);
        }
        memcpy(prow, row, (size_t)g->w * sizeof *row);
    }
    g->prev_valid = true;
    outbuf_put(out, "\x1b[0m", 4);
    if (g->cur_show) outbuf_printf(out, "\x1b[%d;%dH\x1b[?25h", g->cur_y + 1, g->cur_x + 1);
}
