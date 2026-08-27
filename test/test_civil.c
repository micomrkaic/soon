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
#include "../core/civil.h"

int main(void)
{
    /* epoch and known anchors */
    CHECK_EQ(civil_to_days(1970, 1, 1), 0);
    CHECK_EQ(civil_to_days(2000, 3, 1), 11017);
    CHECK_EQ(civil_to_days(2026, 8, 27), 20692);
    CHECK_EQ(civil_to_days(1969, 12, 31), -1);
    CHECK_EQ(civil_to_days(1600, 1, 1), -135140);

    /* round trip over a wide range, one day at a time */
    int bad = 0;
    for (daynum_t z = civil_to_days(1900, 1, 1); z <= civil_to_days(2200, 12, 31); z++) {
        civil_t c = days_to_civil(z);
        if (civil_to_days(c.y, c.m, c.d) != z || !civil_valid(c.y, c.m, c.d)) bad++;
    }
    CHECK_EQ(bad, 0);

    /* leap years */
    CHECK(civil_is_leap(2000));
    CHECK(!civil_is_leap(1900));
    CHECK(civil_is_leap(2024));
    CHECK(!civil_is_leap(2026));
    CHECK_EQ(civil_days_in_month(2024, 2), 29);
    CHECK_EQ(civil_days_in_month(2100, 2), 28);
    CHECK(!civil_valid(2026, 2, 29));
    CHECK(!civil_valid(2026, 13, 1));
    CHECK(!civil_valid(2026, 4, 31));

    /* weekdays: 1970-01-01 Thu, 2026-08-27 Thu, 2000-01-01 Sat, 1969-12-28 Sun */
    CHECK_EQ(civil_weekday(0), 3);
    CHECK_EQ(civil_weekday(civil_to_days(2026, 8, 27)), 3);
    CHECK_EQ(civil_weekday(civil_to_days(2000, 1, 1)), 5);
    CHECK_EQ(civil_weekday(civil_to_days(1969, 12, 28)), 6);
    CHECK_EQ(civil_weekday(civil_to_days(1969, 12, 29)), 0);

    /* ISO weeks, including the classic year-boundary cases */
    int iy, w;
    civil_iso_week(civil_to_days(2026, 8, 27), &iy, &w);   CHECK_EQ(iy, 2026); CHECK_EQ(w, 35);
    civil_iso_week(civil_to_days(2027, 1, 1), &iy, &w);    CHECK_EQ(iy, 2026); CHECK_EQ(w, 53);
    civil_iso_week(civil_to_days(2026, 12, 31), &iy, &w);  CHECK_EQ(iy, 2026); CHECK_EQ(w, 53);
    civil_iso_week(civil_to_days(2027, 1, 4), &iy, &w);    CHECK_EQ(iy, 2027); CHECK_EQ(w, 1);
    civil_iso_week(civil_to_days(2024, 12, 30), &iy, &w);  CHECK_EQ(iy, 2025); CHECK_EQ(w, 1);
    civil_iso_week(civil_to_days(2021, 1, 3), &iy, &w);    CHECK_EQ(iy, 2020); CHECK_EQ(w, 53);
    civil_iso_week(civil_to_days(2008, 12, 29), &iy, &w);  CHECK_EQ(iy, 2009); CHECK_EQ(w, 1);
    CHECK_EQ(civil_iso_weeks_in_year(2026), 53);
    CHECK_EQ(civil_iso_weeks_in_year(2025), 52);
    CHECK_EQ(civil_iso_weeks_in_year(2020), 53);
    CHECK_EQ(civil_iso_weeks_in_year(2027), 52);
    CHECK_EQ(civil_iso_week_monday(2026, 1), civil_to_days(2025, 12, 29));
    CHECK_EQ(civil_iso_week_monday(2027, 1), civil_to_days(2027, 1, 4));
    CHECK_EQ(civil_iso_week_monday(2026, 35), civil_to_days(2026, 8, 24));
    CHECK_EQ(civil_week_monday(civil_to_days(2026, 8, 30)), civil_to_days(2026, 8, 24)); /* Sunday -> same week */

    /* iso_week/iso_week_monday are inverses for every day of a decade */
    bad = 0;
    for (daynum_t z = civil_to_days(2018, 1, 1); z <= civil_to_days(2030, 12, 31); z++) {
        civil_iso_week(z, &iy, &w);
        daynum_t mon = civil_iso_week_monday(iy, w);
        if (z < mon || z > mon + 6 || civil_weekday(mon) != 0) bad++;
    }
    CHECK_EQ(bad, 0);

    /* month arithmetic with clamping */
    CHECK_EQ(civil_add_months(civil_to_days(2026, 1, 31), 1), civil_to_days(2026, 2, 28));
    CHECK_EQ(civil_add_months(civil_to_days(2024, 1, 31), 1), civil_to_days(2024, 2, 29));
    CHECK_EQ(civil_add_months(civil_to_days(2026, 3, 31), -1), civil_to_days(2026, 2, 28));
    CHECK_EQ(civil_add_months(civil_to_days(2026, 11, 15), 3), civil_to_days(2027, 2, 15));
    CHECK_EQ(civil_add_months(civil_to_days(2026, 2, 15), -3), civil_to_days(2025, 11, 15));
    CHECK_EQ(civil_add_months(civil_to_days(2026, 8, 27), -12 * 30), civil_to_days(1996, 8, 27));
    CHECK_EQ(civil_add_years(civil_to_days(2024, 2, 29), 1), civil_to_days(2025, 2, 28));
    CHECK_EQ(civil_add_years(civil_to_days(2024, 2, 29), 4), civil_to_days(2028, 2, 29));

    CHECK_EQ(civil_day_of_year(civil_to_days(2026, 1, 1)), 1);
    CHECK_EQ(civil_day_of_year(civil_to_days(2024, 12, 31)), 366);

    CHECK_DONE("civil");
}
