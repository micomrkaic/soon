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

#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "../core/civil.h"
#include "../core/tz.h"

static long long utc(int y, int m, int d, int hh, int mm)
{
    return (long long)civil_to_days(y, m, d) * 86400 + hh * 3600 + mm * 60;
}

int main(void)
{
    int off, y, m, d, hh, mm;
    long long t;

    CHECK(tz_zone_exists("Europe/Ljubljana"));
    CHECK(tz_zone_exists("America/New_York"));
    CHECK(tz_zone_exists("UTC"));
    CHECK(!tz_zone_exists("Mars/Olympus_Mons"));
    CHECK(!tz_zone_exists("../../etc/passwd"));
    CHECK(!tz_zone_exists(""));

    /* summer / winter offsets */
    CHECK_EQ(tz_offset("Europe/Ljubljana", utc(2026, 7, 1, 12, 0), &off), 0); CHECK_EQ(off, 120);
    CHECK_EQ(tz_offset("Europe/Ljubljana", utc(2026, 1, 15, 12, 0), &off), 0); CHECK_EQ(off, 60);
    CHECK_EQ(tz_offset("America/New_York", utc(2026, 7, 1, 12, 0), &off), 0);  CHECK_EQ(off, -240);
    CHECK_EQ(tz_offset("America/New_York", utc(2026, 1, 15, 12, 0), &off), 0); CHECK_EQ(off, -300);
    CHECK_EQ(tz_offset("Asia/Kolkata", utc(2026, 7, 1, 12, 0), &off), 0);      CHECK_EQ(off, 330);
    CHECK_EQ(tz_offset("Australia/Adelaide", utc(2026, 1, 15, 12, 0), &off), 0); CHECK_EQ(off, 630);
    CHECK_EQ(tz_offset("UTC", utc(2026, 7, 1, 12, 0), &off), 0);               CHECK_EQ(off, 0);
    CHECK_EQ(tz_offset("Mars/Olympus_Mons", 0, &off), -1);

    /* EU transition 2026-03-29 01:00 UTC: 00:59 UTC is CET, 01:00 UTC is CEST */
    CHECK_EQ(tz_offset("Europe/Ljubljana", utc(2026, 3, 29, 0, 59), &off), 0); CHECK_EQ(off, 60);
    CHECK_EQ(tz_offset("Europe/Ljubljana", utc(2026, 3, 29, 1, 0), &off), 0);  CHECK_EQ(off, 120);
    /* US transition 2026-11-01 06:00 UTC (02:00 EDT -> 01:00 EST) */
    CHECK_EQ(tz_offset("America/New_York", utc(2026, 11, 1, 5, 59), &off), 0); CHECK_EQ(off, -240);
    CHECK_EQ(tz_offset("America/New_York", utc(2026, 11, 1, 6, 0), &off), 0);  CHECK_EQ(off, -300);

    /* wall clock -> UTC */
    CHECK_EQ(tz_wall_to_utc("Europe/Ljubljana", 2026, 9, 15, 9, 0, &t), 0);
    CHECK_EQ(t, utc(2026, 9, 15, 7, 0));
    CHECK_EQ(tz_wall_to_utc("America/New_York", 2026, 9, 15, 9, 0, &t), 0);
    CHECK_EQ(t, utc(2026, 9, 15, 13, 0));
    CHECK_EQ(tz_wall_to_utc("Europe/Ljubljana", 2026, 1, 15, 9, 0, &t), 0);
    CHECK_EQ(t, utc(2026, 1, 15, 8, 0));

    /* ambiguous wall time (fall back): 2026-10-25 02:30 in Ljubljana occurs twice; take the earlier (CEST) */
    CHECK_EQ(tz_wall_to_utc("Europe/Ljubljana", 2026, 10, 25, 2, 30, &t), 0);
    CHECK_EQ(t, utc(2026, 10, 25, 0, 30));
    /* nonexistent wall time (spring forward): 2026-03-29 02:30 does not exist; result lands after the gap */
    CHECK_EQ(tz_wall_to_utc("Europe/Ljubljana", 2026, 3, 29, 2, 30, &t), 0);
    CHECK(t >= utc(2026, 3, 29, 1, 0));
    CHECK(t <= utc(2026, 3, 29, 2, 0));

    /* UTC -> wall clock, crossing a date line */
    CHECK_EQ(tz_utc_to_wall("Pacific/Auckland", utc(2026, 12, 31, 12, 0), &y, &m, &d, &hh, &mm, &off), 0);
    CHECK_EQ(y, 2027); CHECK_EQ(m, 1); CHECK_EQ(d, 1); CHECK_EQ(hh, 1); CHECK_EQ(mm, 0); CHECK_EQ(off, 780);
    CHECK_EQ(tz_utc_to_wall("America/Los_Angeles", utc(2026, 1, 1, 3, 0), &y, &m, &d, &hh, &mm, &off), 0);
    CHECK_EQ(y, 2025); CHECK_EQ(m, 12); CHECK_EQ(d, 31); CHECK_EQ(hh, 19); CHECK_EQ(off, -480);

    /* round trip every hour of a DST year in two zones */
    int bad = 0;
    const char *zones[2] = { "Europe/Ljubljana", "America/New_York" };
    for (int z = 0; z < 2; z++)
        for (long long s = utc(2026, 1, 1, 0, 0); s < utc(2027, 1, 1, 0, 0); s += 3600) {
            long long back;
            if (tz_utc_to_wall(zones[z], s, &y, &m, &d, &hh, &mm, &off)) { bad++; continue; }
            if (tz_wall_to_utc(zones[z], y, m, d, hh, mm, &back)) { bad++; continue; }
            /* the ambiguous hour legitimately maps to the earlier instant; allow exactly that */
            if (back != s && back != s - 3600) bad++;
        }
    CHECK_EQ(bad, 0);

    char ab[16];
    tz_abbrev("America/New_York", utc(2026, 7, 1, 12, 0), ab, sizeof ab);  CHECK(!strcmp(ab, "EDT"));
    tz_abbrev("America/New_York", utc(2026, 1, 15, 12, 0), ab, sizeof ab); CHECK(!strcmp(ab, "EST"));
    tz_abbrev("Europe/Ljubljana", utc(2026, 7, 1, 12, 0), ab, sizeof ab);  CHECK(!strcmp(ab, "CEST"));
    tz_abbrev("Asia/Kolkata", utc(2026, 7, 1, 12, 0), ab, sizeof ab);      CHECK(!strcmp(ab, "IST"));
    tz_abbrev("Asia/Dubai", utc(2026, 7, 1, 12, 0), ab, sizeof ab);        CHECK(ab[0] == 0);   /* database says "+04" */
    tz_abbrev("Mars/Olympus_Mons", 0, ab, sizeof ab);                       CHECK(ab[0] == 0);

    /* TZ environment is restored after use */
    CHECK(getenv("TZ") == NULL);

    CHECK_DONE("tz");
}
