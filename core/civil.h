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

/* civil.h — proleptic Gregorian calendar arithmetic, no libc time functions.
 *
 * Days are counted from 1970-01-01 (day 0). Weeks are ISO 8601: Monday first,
 * week 1 is the week containing 4 January.
 */
#ifndef SOON_CIVIL_H
#define SOON_CIVIL_H

#include <stdbool.h>

typedef long daynum_t;

typedef struct {
    int y;
    int m; /* 1..12 */
    int d; /* 1..31 */
} civil_t;

daynum_t civil_to_days(int y, int m, int d);
civil_t  days_to_civil(daynum_t z);

bool civil_valid(int y, int m, int d);
bool civil_is_leap(int y);
int  civil_days_in_month(int y, int m);
int  civil_day_of_year(daynum_t z);          /* 1..366 */

int      civil_weekday(daynum_t z);           /* 0 = Monday ... 6 = Sunday */
daynum_t civil_week_monday(daynum_t z);       /* Monday of the ISO week containing z */
void     civil_iso_week(daynum_t z, int *iso_year, int *week);
int      civil_iso_weeks_in_year(int iso_year); /* 52 or 53 */
daynum_t civil_iso_week_monday(int iso_year, int week);

daynum_t civil_add_months(daynum_t z, int n); /* day clamped to month length */
daynum_t civil_add_years(daynum_t z, int n);  /* 29 Feb clamps to 28 Feb */

extern const char *const civil_month_names[13];   /* 1-based, "January".. */
extern const char *const civil_weekday_names[7];  /* "Monday".. */

#endif
