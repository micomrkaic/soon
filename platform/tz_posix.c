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

/* tz_posix.c — tz.h on Linux/macOS via the system zoneinfo database.
 *
 * Strategy: temporarily set TZ, call tzset()/localtime_r()/mktime(), restore TZ.
 * Not thread-safe; the app is single-threaded. tm_gmtoff is a BSD/glibc
 * extension that both platforms have.
 */
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include "../core/tz.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

bool tz_zone_exists(const char *zone)
{
    if (!zone || !*zone || zone[0] == '/' || strstr(zone, "..")) return false;
    if (!strcmp(zone, "UTC") || !strcmp(zone, "GMT")) return true;
    const char *dir = getenv("TZDIR");
    if (!dir || !*dir) dir = "/usr/share/zoneinfo";
    char path[1024];
    if (snprintf(path, sizeof path, "%s/%s", dir, zone) >= (int)sizeof path) return false;
    return access(path, R_OK) == 0;
}

typedef struct {
    char *saved;   /* copy of previous TZ or NULL */
    bool had;
} tzguard_t;

static void tz_push(tzguard_t *g, const char *zone)
{
    const char *old = getenv("TZ");
    g->had = old != NULL;
    g->saved = old ? strdup(old) : NULL;
    setenv("TZ", zone, 1);
    tzset();
}

static void tz_pop(tzguard_t *g)
{
    if (g->had) setenv("TZ", g->saved, 1); else unsetenv("TZ");
    tzset();
    free(g->saved);
}

int tz_offset(const char *zone, long long utc_seconds, int *offset_minutes)
{
    if (!tz_zone_exists(zone)) return -1;
    tzguard_t g;
    tz_push(&g, zone);
    time_t t = (time_t)utc_seconds;
    struct tm tm;
    int rc = localtime_r(&t, &tm) ? 0 : -1;
    if (rc == 0 && offset_minutes) *offset_minutes = (int)(tm.tm_gmtoff / 60);
    tz_pop(&g);
    return rc;
}

int tz_utc_to_wall(const char *zone, long long utc_seconds,
                   int *y, int *m, int *d, int *hh, int *mm, int *offset_minutes)
{
    if (!tz_zone_exists(zone)) return -1;
    tzguard_t g;
    tz_push(&g, zone);
    time_t t = (time_t)utc_seconds;
    struct tm tm;
    int rc = localtime_r(&t, &tm) ? 0 : -1;
    if (rc == 0) {
        if (y) *y = tm.tm_year + 1900;
        if (m) *m = tm.tm_mon + 1;
        if (d) *d = tm.tm_mday;
        if (hh) *hh = tm.tm_hour;
        if (mm) *mm = tm.tm_min;
        if (offset_minutes) *offset_minutes = (int)(tm.tm_gmtoff / 60);
    }
    tz_pop(&g);
    return rc;
}

/* mktime with a fixed isdst guess, returning whether the round trip reproduces the wall time */
static bool try_mktime(int y, int m, int d, int hh, int mm, int isdst, time_t *out)
{
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
    tm.tm_hour = hh;
    tm.tm_min = mm;
    tm.tm_isdst = isdst;
    time_t t = mktime(&tm);
    if (t == (time_t)-1) return false;
    struct tm back;
    if (!localtime_r(&t, &back)) return false;
    *out = t;
    return back.tm_year == y - 1900 && back.tm_mon == m - 1 && back.tm_mday == d
        && back.tm_hour == hh && back.tm_min == mm;
}

int tz_wall_to_utc(const char *zone, int y, int m, int d, int hh, int mm, long long *utc_seconds)
{
    if (!tz_zone_exists(zone)) return -1;
    tzguard_t g;
    tz_push(&g, zone);

    time_t t_dst, t_std, t_any;
    bool ok_dst = try_mktime(y, m, d, hh, mm, 1, &t_dst);
    bool ok_std = try_mktime(y, m, d, hh, mm, 0, &t_std);
    int rc = 0;
    if (ok_dst && ok_std) {
        *utc_seconds = t_dst < t_std ? t_dst : t_std;   /* ambiguous: earlier instant */
    } else if (ok_dst) {
        *utc_seconds = t_dst;
    } else if (ok_std) {
        *utc_seconds = t_std;
    } else {
        /* nonexistent wall time (spring-forward gap): let mktime normalise it */
        struct tm tm;
        memset(&tm, 0, sizeof tm);
        tm.tm_year = y - 1900; tm.tm_mon = m - 1; tm.tm_mday = d;
        tm.tm_hour = hh; tm.tm_min = mm; tm.tm_isdst = -1;
        t_any = mktime(&tm);
        if (t_any == (time_t)-1) rc = -1; else *utc_seconds = t_any;
    }
    tz_pop(&g);
    return rc;
}

void tz_abbrev(const char *zone, long long utc_seconds, char *out, int n)
{
    out[0] = 0;
    if (!tz_zone_exists(zone)) return;
    tzguard_t g;
    tz_push(&g, zone);
    time_t t = (time_t)utc_seconds;
    struct tm tm;
    if (localtime_r(&t, &tm) && tm.tm_zone && tm.tm_zone[0] && !(tm.tm_zone[0] == '+' || tm.tm_zone[0] == '-' || isdigit((unsigned char)tm.tm_zone[0])))
        snprintf(out, (size_t)n, "%s", tm.tm_zone);
    tz_pop(&g);
}
