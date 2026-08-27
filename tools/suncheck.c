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

/* suncheck — print the sun panel for one day: suncheck YYYY-MM-DD LAT LON ZONE */
#include <stdio.h>
#include <stdlib.h>
#include "../core/civil.h"
#include "../core/datejump.h"
#include "../core/sun.h"
#include "../core/tz.h"

static void line(const char *label, double t, sun_state_t st, int off)
{
    int hh, mm, dd;
    if (st == SUN_ALWAYS_ABOVE) { printf("  %-16s (sun stays above)\n", label); return; }
    if (st == SUN_ALWAYS_BELOW) { printf("  %-16s (sun stays below)\n", label); return; }
    sun_local_hhmm(t, off, &hh, &mm, &dd);
    printf("  %-16s %02d:%02d%s\n", label, hh, mm, dd > 0 ? " (+1d)" : dd < 0 ? " (-1d)" : "");
}

int main(int argc, char **argv)
{
    if (argc != 5) { fprintf(stderr, "usage: %s YYYY-MM-DD LAT LON ZONE\n", argv[0]); return 2; }
    daynum_t day;
    char err[64];
    if (datejump_parse(argv[1], 0, &day, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 2; }
    double lat = atof(argv[2]), lon = atof(argv[3]);
    const char *zone = argv[4];

    int off = 0;
    if (tz_offset(zone, (long long)day * 86400 + 43200, &off)) { fprintf(stderr, "unknown zone %s\n", zone); return 2; }

    sun_t s;
    sun_compute(day, lat, lon, &s);
    civil_t c = days_to_civil(day);
    printf("%04d-%02d-%02d  %s  lat %.4f lon %.4f  %s  UTC%+d:%02d\n", c.y, c.m, c.d,
           civil_weekday_names[civil_weekday(day)], lat, lon, zone, off / 60, abs(off % 60));
    line("astro dawn", s.astro_dawn, s.astro, off);
    line("nautical dawn", s.nautical_dawn, s.nautical, off);
    line("civil dawn", s.civil_dawn, s.civil, off);
    line("sunrise", s.rise, s.rise_set, off);
    line("solar noon", s.noon, SUN_NORMAL, off);
    line("sunset", s.set, s.rise_set, off);
    line("civil dusk", s.civil_dusk, s.civil, off);
    line("nautical dusk", s.nautical_dusk, s.nautical, off);
    line("astro dusk", s.astro_dusk, s.astro, off);
    printf("  %-16s %dh%02dm   declination %+.2f°   eq. of time %+.1f min\n", "day length",
           (int)(s.day_length / 60), (int)s.day_length % 60, s.declination, s.eqtime);
    return 0;
}
