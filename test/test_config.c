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

#include <string.h>
#include "check.h"
#include "../core/config.h"

int main(void)
{
    config_t c;
    char err[256];
    config_defaults(&c);
    CHECK_EQ(c.slot, 15); CHECK_EQ(c.sheet_slot, 30); CHECK_EQ(c.work_start, 480); CHECK_EQ(c.first_weekday, 0); CHECK_EQ(c.menu, 1);
    CHECK_EQ(config_parse(&c, config_template(), err, sizeof err), 0);   /* the template parses cleanly */
    CHECK_EQ(config_parse(&c, "slot: 30\nsheet_slot: 60 # comment\nwork_start: 7:30\nwork_end: 19:00\nfirst_weekday: Sunday\nmenu: off\nunicode: on\npalette: mono\ncolor event: bright-white on blue bold\ncolor todo: 11\ncolor overdue: red bold\ncolor duesoon: 11 italic\ndue_soon: 5\ncolor cursor: black on yellow inverse\n", err, sizeof err), 0);
    CHECK_EQ(c.slot, 30); CHECK_EQ(c.sheet_slot, 60); CHECK_EQ(c.work_start, 450); CHECK_EQ(c.work_end, 1140);
    CHECK_EQ(c.first_weekday, 6); CHECK_EQ(c.menu, 0); CHECK_EQ(c.unicode, 1);
    CHECK(!strcmp(c.palette, "mono"));
    CHECK(c.color_set[ROLE_EVENT] && c.colors[ROLE_EVENT].fg == 15 && c.colors[ROLE_EVENT].bg == 4 && c.colors[ROLE_EVENT].attr == 1);
    CHECK(c.color_set[ROLE_TODO] && c.colors[ROLE_TODO].fg == 11 && c.colors[ROLE_TODO].bg == 255);
    CHECK(c.color_set[ROLE_CURSOR] && c.colors[ROLE_CURSOR].fg == 0 && c.colors[ROLE_CURSOR].bg == 3 && c.colors[ROLE_CURSOR].attr == 8);
    CHECK(c.color_set[ROLE_OVERDUE] && c.colors[ROLE_OVERDUE].fg == 1 && c.colors[ROLE_OVERDUE].attr == 1);
    CHECK(c.color_set[ROLE_DUESOON] && c.colors[ROLE_DUESOON].fg == 11 && c.colors[ROLE_DUESOON].attr == 16);
    CHECK_EQ(c.due_soon, 5);
    CHECK(!c.color_set[ROLE_TODAY]);
    /* errors are counted and described, good lines still apply */
    CHECK_EQ(config_parse(&c, "slot: 7\nnonsense\ncolor nope: red\ncolor today: purple\nwork_end: 17:00\n", err, sizeof err), 4);
    CHECK(strstr(err, "line 1") && strstr(err, "line 4"));
    CHECK_EQ(c.slot, 30);
    CHECK_EQ(c.work_end, 1020);
    CHECK_DONE("config");
}
