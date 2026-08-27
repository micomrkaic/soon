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

/* datejump.h — parser for the "go to date" command.
 *
 * Accepted forms (case-insensitive, tokens separated by spaces or commas):
 *
 *   t  today          tom  tomorrow          yes  yesterday
 *   +3 +3d -2w +1m -1y                relative to today (d default)
 *   2026-09-14   2026-9-14   2026-09  2026   ISO date, month, year
 *   14.9.  14.9.2026  14.9              European day.month[.year]
 *   9/14   9/14/2026                     US month/day[/year]
 *   14                                   day 14 of the current month
 *   sep   september                      1 September of the current year
 *   sep 14   14 sep   sep 14 2026   14 sep 2026
 *   mon .. sun   monday ..               that weekday of the current ISO week
 *   next fri   last fri   prev fri       weekday of next / previous ISO week
 *   next week  next month  next year     same weekday / day-of-month / date
 *   last week  last month  last year     (last == prev)
 *   w38   w38 2027   2027-w38   2027w38  Monday of ISO week
 *   bom  eom  boy  eoy                   first / last day of month, year
 *
 * Returns 0 and sets *out on success; -1 with a message in err otherwise.
 */
#ifndef SOON_DATEJUMP_H
#define SOON_DATEJUMP_H

#include <stddef.h>
#include "civil.h"

int datejump_parse(const char *text, daynum_t today, daynum_t *out,
                   char *err, size_t errlen);

/* Name lookup helpers shared with the item editor. Prefix match; return 0 / -1 on miss. */
int datejump_month_from_name(const char *tok);    /* 1..12 or 0 */
int datejump_weekday_from_name(const char *tok);  /* 0..6 (Mon..Sun) or -1 */

#endif
