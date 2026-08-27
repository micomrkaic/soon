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

/* civil.c — see civil.h. Date <-> day-count conversions follow Howard Hinnant's
 * public-domain algorithms (era/year-of-era/day-of-era form), exact over the
 * whole proleptic Gregorian range that fits in a long.
 */
#include "civil.h"

const char *const civil_month_names[13] = {
    "", "January", "February", "March", "April", "May", "June", "July",
    "August", "September", "October", "November", "December"
};

const char *const civil_weekday_names[7] = {
    "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"
};

daynum_t civil_to_days(int y, int m, int d)
{
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);                     /* 0..399 */
    const unsigned mp = (unsigned)(m + (m > 2 ? -3 : 9));               /* Mar=0 */
    const unsigned doy = (153 * mp + 2) / 5 + (unsigned)d - 1;           /* 0..365 */
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          /* 0..146096 */
    return era * 146097 + (long)doe - 719468;
}

civil_t days_to_civil(daynum_t z)
{
    z += 719468;
    const long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long y = (long)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    civil_t c = { (int)(y + (m <= 2)), (int)m, (int)d };
    return c;
}

bool civil_is_leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

int civil_days_in_month(int y, int m)
{
    static const int len[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m < 1 || m > 12) return 0;
    return m == 2 && civil_is_leap(y) ? 29 : len[m];
}

bool civil_valid(int y, int m, int d)
{
    return m >= 1 && m <= 12 && d >= 1 && d <= civil_days_in_month(y, m);
}

int civil_day_of_year(daynum_t z)
{
    civil_t c = days_to_civil(z);
    return (int)(z - civil_to_days(c.y, 1, 1)) + 1;
}

int civil_weekday(daynum_t z)
{
    /* 1970-01-01 was a Thursday (ISO index 3). */
    long r = (z + 3) % 7;
    return (int)(r < 0 ? r + 7 : r);
}

daynum_t civil_week_monday(daynum_t z)
{
    return z - civil_weekday(z);
}

void civil_iso_week(daynum_t z, int *iso_year, int *week)
{
    /* The ISO week-year of a day is the calendar year of that week's Thursday. */
    daynum_t thu = z - civil_weekday(z) + 3;
    civil_t c = days_to_civil(thu);
    daynum_t jan1 = civil_to_days(c.y, 1, 1);
    if (iso_year) *iso_year = c.y;
    if (week) *week = (int)((thu - jan1) / 7) + 1;
}

daynum_t civil_iso_week_monday(int iso_year, int week)
{
    daynum_t jan4 = civil_to_days(iso_year, 1, 4);
    return civil_week_monday(jan4) + (daynum_t)(week - 1) * 7;
}

int civil_iso_weeks_in_year(int iso_year)
{
    /* A year has 53 ISO weeks iff 28 December falls in week 53. */
    int w;
    civil_iso_week(civil_to_days(iso_year, 12, 28), 0, &w);
    return w;
}

daynum_t civil_add_months(daynum_t z, int n)
{
    civil_t c = days_to_civil(z);
    long mi = (long)c.y * 12 + (c.m - 1) + n;
    int y = (int)(mi >= 0 ? mi / 12 : (mi - 11) / 12);
    int m = (int)(mi - (long)y * 12) + 1;
    int dim = civil_days_in_month(y, m);
    return civil_to_days(y, m, c.d > dim ? dim : c.d);
}

daynum_t civil_add_years(daynum_t z, int n)
{
    return civil_add_months(z, 12 * n);
}
