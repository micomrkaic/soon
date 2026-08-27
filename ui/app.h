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

/* app.h — the interactive agenda: state, drawing, key handling. Platform
 * independent: talks to the terminal only through keys.h and the grid.
 */
#ifndef SOON_APP_H
#define SOON_APP_H

#include "../core/config.h"
#include "../core/contacts.h"
#include "../core/model.h"

typedef struct {
    store_t *st;
    contacts_t *cs;
    const config_t *conf;         /* may be NULL: defaults */
    long long (*now_utc)(void);   /* wall clock source */
    int slot;                     /* minutes per slot: 5 10 15 30 60 */
    int work_start;               /* first minute shown by default in week/day views */
    bool unicode;                 /* draw box/marker glyphs in Unicode */
    bool menu;                    /* show the key menu at the bottom */
} app_config_t;

/* Runs until the user quits. Returns 0 normally. */
int app_run(const app_config_t *cfg, daynum_t start_day);

#endif
