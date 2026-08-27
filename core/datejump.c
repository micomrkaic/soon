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

/* datejump.c — see datejump.h */
#include "datejump.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define MAXTOK 4
#define TOKLEN 32

typedef struct {
    char tok[MAXTOK][TOKLEN];
    int n;
} toks_t;

static int fail(char *err, size_t errlen, const char *msg)
{
    if (err && errlen) snprintf(err, errlen, "%s", msg);
    return -1;
}

static void tokenize(const char *text, toks_t *t)
{
    t->n = 0;
    const char *p = text;
    while (*p && t->n < MAXTOK) {
        while (*p == ' ' || *p == ',' || *p == '\t') p++;
        if (!*p) break;
        int k = 0;
        while (*p && *p != ' ' && *p != ',' && *p != '\t') {
            if (k < TOKLEN - 1) t->tok[t->n][k++] = (char)tolower((unsigned char)*p);
            p++;
        }
        t->tok[t->n][k] = 0;
        t->n++;
    }
}

/* tok (already lowercase) is a prefix of name, at least minlen long */
static int prefix_ci(const char *tok, const char *name, size_t minlen)
{
    size_t n = strlen(tok);
    if (n < minlen || n > strlen(name)) return 0;
    for (size_t i = 0; i < n; i++)
        if (tok[i] != tolower((unsigned char)name[i])) return 0;
    return 1;
}

int datejump_month_from_name(const char *tok)
{
    for (int m = 1; m <= 12; m++)
        if (prefix_ci(tok, civil_month_names[m], 3)) return m;
    return 0;
}

int datejump_weekday_from_name(const char *tok)
{
    for (int w = 0; w < 7; w++)
        if (prefix_ci(tok, civil_weekday_names[w], 2)) return w;
    return -1;
}

static int parse_uint(const char *s, int *out)
{
    if (!*s) return 0;
    long v = 0;
    for (; *s; s++) {
        if (!isdigit((unsigned char)*s)) return 0;
        v = v * 10 + (*s - '0');
        if (v > 1000000) return 0;
    }
    *out = (int)v;
    return 1;
}

/* Split "a<sep>b[<sep>c][<sep>]" into up to three unsigned numbers.
 * Returns the count, or -1 if malformed. */
static int split_num(const char *s, char sep, int parts[3])
{
    int n = 0;
    const char *p = s;
    while (*p) {
        if (n == 3) return -1;
        int v = 0, digits = 0;
        while (isdigit((unsigned char)*p)) { v = v * 10 + (*p - '0'); p++; digits++; if (v > 1000000) return -1; }
        if (!digits) return -1;
        parts[n++] = v;
        if (*p == sep) { p++; continue; }
        if (*p) return -1;
    }
    return n;
}

static int full_year(int y)
{
    return y < 100 ? 2000 + y : y;
}

static int make(daynum_t *out, int y, int m, int d, char *err, size_t errlen)
{
    if (!civil_valid(y, m, d)) return fail(err, errlen, "no such date");
    *out = civil_to_days(y, m, d);
    return 0;
}

/* "w38", "2027w38", "2027-w38" */
static int parse_week_token(const char *tok, int *year, int *week)
{
    const char *w = strchr(tok, 'w');
    if (!w) return 0;
    if (!parse_uint(w + 1, week)) return 0;
    if (w == tok) { *year = 0; return 1; }
    char head[TOKLEN];
    size_t hl = (size_t)(w - tok);
    if (hl >= sizeof head) return 0;
    memcpy(head, tok, hl);
    head[hl] = 0;
    if (hl && head[hl - 1] == '-') head[hl - 1] = 0;
    if (!parse_uint(head, year)) return 0;
    *year = full_year(*year);
    return 1;
}

static int week_monday_checked(int year, int week, daynum_t *out, char *err, size_t errlen)
{
    if (week < 1 || week > civil_iso_weeks_in_year(year))
        return fail(err, errlen, "no such ISO week in that year");
    *out = civil_iso_week_monday(year, week);
    return 0;
}

static int parse_relative(const char *tok, daynum_t today, daynum_t *out, char *err, size_t errlen)
{
    int sign = tok[0] == '-' ? -1 : 1;
    const char *p = tok + 1;
    int n = 0, digits = 0;
    while (isdigit((unsigned char)*p)) { n = n * 10 + (*p - '0'); p++; digits++; if (n > 100000) return fail(err, errlen, "offset too large"); }
    if (!digits) return fail(err, errlen, "expected a number after the sign");
    char unit = *p ? *p : 'd';
    if (p[0] && p[1]) return fail(err, errlen, "unit must be one of d w m y");
    n *= sign;
    switch (unit) {
    case 'd': *out = today + n; return 0;
    case 'w': *out = today + 7L * n; return 0;
    case 'm': *out = civil_add_months(today, n); return 0;
    case 'y': *out = civil_add_years(today, n); return 0;
    default:  return fail(err, errlen, "unit must be one of d w m y");
    }
}

static int parse_single(const char *tok, daynum_t today, daynum_t *out, char *err, size_t errlen)
{
    civil_t now = days_to_civil(today);
    int parts[3], n, v;

    if (!strcmp(tok, "t") || !strcmp(tok, "today")) { *out = today; return 0; }
    if (!strcmp(tok, "tom") || !strcmp(tok, "tomorrow")) { *out = today + 1; return 0; }
    if (!strcmp(tok, "yes") || !strcmp(tok, "yesterday")) { *out = today - 1; return 0; }
    if (!strcmp(tok, "bom")) return make(out, now.y, now.m, 1, err, errlen);
    if (!strcmp(tok, "eom")) return make(out, now.y, now.m, civil_days_in_month(now.y, now.m), err, errlen);
    if (!strcmp(tok, "boy")) return make(out, now.y, 1, 1, err, errlen);
    if (!strcmp(tok, "eoy")) return make(out, now.y, 12, 31, err, errlen);

    if (tok[0] == '+' || tok[0] == '-')
        return parse_relative(tok, today, out, err, errlen);

    int wy, ww;
    if (parse_week_token(tok, &wy, &ww))
        return week_monday_checked(wy ? wy : now.y, ww, out, err, errlen);

    if (strchr(tok, '-')) {                       /* ISO */
        n = split_num(tok, '-', parts);
        if (n == 3) return make(out, full_year(parts[0]), parts[1], parts[2], err, errlen);
        if (n == 2) return make(out, full_year(parts[0]), parts[1], 1, err, errlen);
        return fail(err, errlen, "expected YYYY-MM-DD or YYYY-MM");
    }
    if (strchr(tok, '.')) {                       /* European D.M[.Y][.] */
        n = split_num(tok, '.', parts);
        if (n == 3) return make(out, full_year(parts[2]), parts[1], parts[0], err, errlen);
        if (n == 2) return make(out, now.y, parts[1], parts[0], err, errlen);
        return fail(err, errlen, "expected D.M. or D.M.YYYY");
    }
    if (strchr(tok, '/')) {                       /* US M/D[/Y] */
        n = split_num(tok, '/', parts);
        if (n == 3) return make(out, full_year(parts[2]), parts[0], parts[1], err, errlen);
        if (n == 2) return make(out, now.y, parts[0], parts[1], err, errlen);
        return fail(err, errlen, "expected M/D or M/D/YYYY");
    }
    if (parse_uint(tok, &v)) {
        if (v >= 1 && v <= 31) return make(out, now.y, now.m, v, err, errlen);
        if (v >= 1000) return make(out, v, 1, 1, err, errlen);
        return fail(err, errlen, "a bare number is a day of this month (1-31) or a year");
    }
    if ((v = datejump_month_from_name(tok)) != 0)
        return make(out, now.y, v, 1, err, errlen);
    if ((v = datejump_weekday_from_name(tok)) >= 0) {
        *out = civil_week_monday(today) + v;
        return 0;
    }
    return fail(err, errlen, "cannot parse date");
}

/* "next"/"last" + weekday | week | month | year; dir = +1 / -1 */
static int parse_shift(const char *what, int dir, daynum_t today, daynum_t *out, char *err, size_t errlen)
{
    int wd = datejump_weekday_from_name(what);
    if (wd >= 0) { *out = civil_week_monday(today) + 7L * dir + wd; return 0; }
    if (prefix_ci(what, "week", 1))  { *out = today + 7L * dir; return 0; }
    if (prefix_ci(what, "month", 1)) { *out = civil_add_months(today, dir); return 0; }
    if (prefix_ci(what, "year", 1))  { *out = civil_add_years(today, dir); return 0; }
    return fail(err, errlen, "expected a weekday, week, month or year after next/last");
}

/* month-name and day in either order, optional year */
static int parse_month_day(const toks_t *t, daynum_t today, daynum_t *out, char *err, size_t errlen)
{
    civil_t now = days_to_civil(today);
    int m = 0, d = 0, y = now.y, v;

    if ((m = datejump_month_from_name(t->tok[0])) != 0 && parse_uint(t->tok[1], &v)) {
        if (v >= 1000) {                            /* "sep 2026" */
            if (t->n == 3) return fail(err, errlen, "cannot parse date");
            return make(out, full_year(v), m, 1, err, errlen);
        }
        d = v;
    } else if (parse_uint(t->tok[0], &v) && (m = datejump_month_from_name(t->tok[1])) != 0) {
        d = v;
    } else {
        return fail(err, errlen, "cannot parse date");
    }
    if (t->n == 3) {
        if (!parse_uint(t->tok[2], &v)) return fail(err, errlen, "expected a year");
        y = full_year(v);
    }
    return make(out, y, m, d, err, errlen);
}

int datejump_parse(const char *text, daynum_t today, daynum_t *out, char *err, size_t errlen)
{
    toks_t t;
    if (!text) return fail(err, errlen, "empty");
    tokenize(text, &t);
    if (t.n == 0) return fail(err, errlen, "empty");

    if (t.n >= 2 && !strcmp(t.tok[0], "this")) { /* "this fri" == "fri" */
        memmove(&t.tok[0], &t.tok[1], sizeof t.tok[0] * (size_t)(t.n - 1));
        t.n--;
    }

    if (t.n == 1)
        return parse_single(t.tok[0], today, out, err, errlen);

    if (t.n == 2) {
        if (!strcmp(t.tok[0], "next")) return parse_shift(t.tok[1], +1, today, out, err, errlen);
        if (!strcmp(t.tok[0], "last") || !strcmp(t.tok[0], "prev"))
            return parse_shift(t.tok[1], -1, today, out, err, errlen);
        int wy, ww, y;
        if (parse_week_token(t.tok[0], &wy, &ww) && wy == 0 && parse_uint(t.tok[1], &y))
            return week_monday_checked(full_year(y), ww, out, err, errlen);
        return parse_month_day(&t, today, out, err, errlen);
    }

    if (t.n == 3)
        return parse_month_day(&t, today, out, err, errlen);

    return fail(err, errlen, "too many words");
}
