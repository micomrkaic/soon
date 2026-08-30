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

/* grid.h — screen as a grid of cells; the only thing the views draw into.
 * Rendering to VT escape sequences is line-diffed against the previous frame.
 * The browser backend consumes the same grid without going through escapes.
 */
#ifndef SOON_GRID_H
#define SOON_GRID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* colors: 0..15 are the ANSI palette; 255 = terminal default */
#define C_DEFAULT 255

enum { A_BOLD = 1, A_DIM = 2, A_UL = 4, A_INV = 8, A_ITALIC = 16 };

typedef struct { uint8_t fg, bg, attr; } style_t;

#define GRID_CONT 0xFFFFFFFFu   /* second cell of a double-width character */

typedef struct { uint32_t cp; style_t st; } cell_t;

typedef struct {
    int w, h;
    cell_t *cells, *prev;
    bool prev_valid;
    int cur_x, cur_y;
    bool cur_show;
} grid_t;

typedef struct { char *s; size_t n, cap; } outbuf_t;

style_t style(uint8_t fg, uint8_t bg, uint8_t attr);

void grid_init(grid_t *g);
void grid_free(grid_t *g);
void grid_resize(grid_t *g, int w, int h);       /* also invalidates the diff */
void grid_clear(grid_t *g, style_t st);

void grid_put(grid_t *g, int x, int y, uint32_t cp, style_t st);
/* Draw s from (x,y), at most maxw cells; returns cells used. Truncates on cell boundaries. */
int  grid_puts(grid_t *g, int x, int y, const char *s, int maxw, style_t st);
/* Like grid_puts but pads with spaces to exactly w cells and ends with … if cut. */
void grid_field(grid_t *g, int x, int y, const char *s, int w, style_t st, bool unicode);
void grid_fill(grid_t *g, int x, int y, int w, int h, uint32_t cp, style_t st);
void grid_style(grid_t *g, int x, int y, int w, int h, style_t st);   /* restyle, keep glyphs */
void grid_box(grid_t *g, int x, int y, int w, int h, style_t st, bool unicode);

/* Append escape sequences for everything that changed since the last render. */
void grid_render(grid_t *g, outbuf_t *out);

void outbuf_put(outbuf_t *b, const char *s, size_t n);
void outbuf_printf(outbuf_t *b, const char *fmt, ...);

#endif
