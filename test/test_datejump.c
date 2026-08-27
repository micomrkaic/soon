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

#include "check.h"
#include "../core/datejump.h"

static daynum_t today; /* 2026-08-27, a Thursday in ISO week 35 */

static daynum_t D(int y, int m, int d) { return civil_to_days(y, m, d); }

static void ok(const char *s, daynum_t want)
{
    daynum_t got = -1;
    char err[64] = "";
    int rc = datejump_parse(s, today, &got, err, sizeof err);
    if (rc != 0 || got != want) {
        check_fail++;
        civil_t g = days_to_civil(got), w = days_to_civil(want);
        fprintf(stderr, "FAIL \"%s\": got %04d-%02d-%02d (rc=%d, %s), want %04d-%02d-%02d\n",
                s, g.y, g.m, g.d, rc, err, w.y, w.m, w.d);
    } else {
        check_pass++;
    }
}

static void bad(const char *s)
{
    daynum_t got;
    char err[64] = "";
    int rc = datejump_parse(s, today, &got, err, sizeof err);
    if (rc == 0 || !err[0]) {
        check_fail++;
        fprintf(stderr, "FAIL \"%s\": expected an error, rc=%d err=\"%s\"\n", s, rc, err);
    } else {
        check_pass++;
    }
}

int main(void)
{
    today = D(2026, 8, 27);

    ok("t", today);
    ok("today", today);
    ok("TODAY", today);
    ok("tom", today + 1);
    ok("tomorrow", today + 1);
    ok("yes", today - 1);
    ok("yesterday", today - 1);

    ok("+3", today + 3);
    ok("+3d", today + 3);
    ok("-10d", today - 10);
    ok("+2w", today + 14);
    ok("-1w", today - 7);
    ok("+1m", D(2026, 9, 27));
    ok("+6m", D(2027, 2, 27));
    ok("-1y", D(2025, 8, 27));
    ok("+0", today);

    ok("2026-09-14", D(2026, 9, 14));
    ok("2026-9-14", D(2026, 9, 14));
    ok("2027-02", D(2027, 2, 1));
    ok("2027", D(2027, 1, 1));
    ok("1999", D(1999, 1, 1));

    ok("14.9.", D(2026, 9, 14));
    ok("14.9", D(2026, 9, 14));
    ok("14.9.2027", D(2027, 9, 14));
    ok("14.9.27", D(2027, 9, 14));
    ok("29.2.2028", D(2028, 2, 29));

    ok("9/14", D(2026, 9, 14));
    ok("9/14/2027", D(2027, 9, 14));

    ok("14", D(2026, 8, 14));
    ok("1", D(2026, 8, 1));
    ok("31", D(2026, 8, 31));

    ok("sep", D(2026, 9, 1));
    ok("September", D(2026, 9, 1));
    ok("sept", D(2026, 9, 1));
    ok("mar", D(2026, 3, 1));
    ok("may", D(2026, 5, 1));
    ok("sep 14", D(2026, 9, 14));
    ok("14 sep", D(2026, 9, 14));
    ok("Sep 14, 2027", D(2027, 9, 14));
    ok("14 sep 2027", D(2027, 9, 14));
    ok("sep 2027", D(2027, 9, 1));
    ok("feb 29 2028", D(2028, 2, 29));

    /* current ISO week is Mon 24 Aug .. Sun 30 Aug 2026 */
    ok("mon", D(2026, 8, 24));
    ok("thu", today);
    ok("fri", D(2026, 8, 28));
    ok("sun", D(2026, 8, 30));
    ok("Sunday", D(2026, 8, 30));
    ok("this fri", D(2026, 8, 28));
    ok("next fri", D(2026, 9, 4));
    ok("next mon", D(2026, 8, 31));
    ok("last fri", D(2026, 8, 21));
    ok("prev sun", D(2026, 8, 23));
    ok("next week", today + 7);
    ok("last week", today - 7);
    ok("next month", D(2026, 9, 27));
    ok("last month", D(2026, 7, 27));
    ok("next year", D(2027, 8, 27));
    ok("next m", D(2026, 9, 27));

    ok("w38", D(2026, 9, 14));
    ok("W1", D(2025, 12, 29));
    ok("w53", D(2026, 12, 28));
    ok("w38 2027", D(2027, 9, 20));
    ok("2027-w38", D(2027, 9, 20));
    ok("2027w38", D(2027, 9, 20));

    ok("bom", D(2026, 8, 1));
    ok("eom", D(2026, 8, 31));
    ok("boy", D(2026, 1, 1));
    ok("eoy", D(2026, 12, 31));

    /* errors, each with a message */
    bad("");
    bad("   ");
    bad("foo");
    bad("32");
    bad("31.9.");
    bad("2026-02-29");
    bad("2026-13-01");
    bad("w54");
    bad("w53 2027");
    bad("+x");
    bad("+3q");
    bad("next foo");
    bad("sep 31");
    bad("14 sep 2027 extra");
    bad("ju");        /* ambiguous month prefix */
    bad("14.9.2027.1");

    /* today on a Sunday: bare "mon" is the Monday six days earlier */
    today = D(2026, 8, 30);
    ok("mon", D(2026, 8, 24));
    ok("sun", today);
    ok("next mon", D(2026, 8, 31));

    /* month clamping through +1m at month end */
    today = D(2026, 1, 31);
    ok("+1m", D(2026, 2, 28));
    ok("next month", D(2026, 2, 28));

    CHECK_DONE("datejump");
}
