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

/* text.c — see text.h */
#include "text.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void sb_put(sbuf_t *b, const char *s, size_t n)
{
    if (b->n + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 4096;
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

void sb_puts(sbuf_t *b, const char *s)
{
    sb_put(b, s, strlen(s));
}

void sb_printf(sbuf_t *b, const char *fmt, ...)
{
    char tmp[4200];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof tmp) n = (int)sizeof tmp - 1;
    sb_put(b, tmp, (size_t)n);
}

char *sb_take(sbuf_t *b)
{
    char *s = b->s ? b->s : xstrdup("");
    b->s = NULL;
    b->n = b->cap = 0;
    return s;
}

char *xstrdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

void str_set(char **field, const char *value)
{
    free(*field);
    *field = value && *value ? xstrdup(value) : NULL;
}

void str_append_line(char **field, const char *line)
{
    if (!*field) { *field = xstrdup(line); return; }
    size_t a = strlen(*field), b = strlen(line);
    char *p = realloc(*field, a + b + 2);
    if (!p) return;
    p[a] = '\n';
    memcpy(p + a + 1, line, b + 1);
    *field = p;
}

void rtrim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

const char *skipws(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

const char *word(const char *s, char *tok, size_t n)
{
    s = skipws(s);
    size_t k = 0;
    while (*s && *s != ' ' && *s != '\t') { if (k < n - 1) tok[k++] = *s; s++; }
    tok[k] = 0;
    return s;
}

bool str_ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    return *a == *b;
}

bool str_iprefix(const char *prefix, const char *s)
{
    for (; *prefix; prefix++, s++)
        if (tolower((unsigned char)*prefix) != tolower((unsigned char)*s)) return false;
    return true;
}

bool next_line(const char **p, char *line, size_t n)
{
    if (!**p) return false;
    const char *nl = strchr(*p, '\n');
    size_t len = nl ? (size_t)(nl - *p) : strlen(*p);
    if (len >= n) len = n - 1;
    memcpy(line, *p, len);
    line[len] = 0;
    rtrim(line);
    *p += nl ? (size_t)(nl - *p) + 1 : strlen(*p);
    return true;
}
