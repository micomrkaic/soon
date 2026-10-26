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

/* quick.h — one line of text in, one item out.
 *
 * Bare words become the title; everything else is a sigiled or recognized
 * token, in any order:
 *
 *   14:30  9:30-9:45       time (24h) — makes the line an event, default 60m
 *   +45m +1h30 +2h         duration (needs a unit; "+3" and "+2w" are dates)
 *   thu  tomorrow  5.11.   a date, anywhere datejump understands one;
 *   sep 14  next fri  eom  multi-word forms work too (longest match wins)
 *   due:5.11 due:eom       todo deadline (one token, compact datejump forms)
 *   #medical #car          tags, any number
 *   @Clinic @"Duluth GA"   place
 *   &wife &"Simon Podnar"  who
 *   !1 .. !5               priority — todos only
 *   *                      all-day event instead of a todo
 *   every ...              repeat; must come last. "every mon wed",
 *                          "every weekdays", "every 2 weeks until 20.12",
 *                          "every day", "every monthly" all work
 *   "quoted text"          always title, even if it looks like a date
 *
 * A time makes an event; otherwise it is a todo (unscheduled when no date).
 * A bare word that parses as a date IS one ("fri") — quote the title to
 * keep it. Lone integers are never read as dates ("Chapter 14" is safe).
 *
 * quick_parse returns 0 and fills *out (strings malloc'd — hand it to
 * store_add_item, then quick_item_free). msg gets a warning on 0, the
 * reason on -1. Pure: no I/O, no globals.
 */
#ifndef SOON_QUICK_H
#define SOON_QUICK_H

#include "model.h"

int  quick_parse(const char *line, daynum_t today, item_t *out, char *msg, size_t msglen);
void quick_item_free(item_t *it);

/* one-line description of a parsed item: "todo  Pay HOA  due 2026-10-31  #financial" */
void quick_describe(const item_t *it, char *out, size_t n);

/* the grammar cheat sheet as "# "-commented lines, for editor templates */
const char *quick_cheatsheet(void);

#endif
