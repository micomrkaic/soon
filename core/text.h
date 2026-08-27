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

/* text.h — small string utilities shared by the codecs */
#ifndef SOON_TEXT_H
#define SOON_TEXT_H

#include <stdbool.h>
#include <stddef.h>

typedef struct { char *s; size_t n, cap; } sbuf_t;

void  sb_put(sbuf_t *b, const char *s, size_t n);
void  sb_puts(sbuf_t *b, const char *s);
void  sb_printf(sbuf_t *b, const char *fmt, ...);
char *sb_take(sbuf_t *b);              /* returns the buffer (never NULL), resets b */

char *xstrdup(const char *s);          /* NULL -> NULL */
void  str_set(char **field, const char *value);   /* strdup; empty/NULL clears */
void  str_append_line(char **field, const char *line);
void  rtrim(char *s);
const char *skipws(const char *s);
const char *word(const char *s, char *tok, size_t n);   /* next whitespace-delimited word */
bool  str_ieq(const char *a, const char *b);
bool  str_iprefix(const char *prefix, const char *s);

/* iterate lines of a text: copies the next line (without newline) into line, advances *p; false at end */
bool  next_line(const char **p, char *line, size_t n);

#endif
