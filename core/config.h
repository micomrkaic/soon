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

/* config.h — ~/.config/soon/config: key: value lines, # comments.
 *
 *   slot: 15                 minutes per slot in the week and day views
 *   sheet_slot: 30           minutes per slot in the days sheet
 *   work_start: 08:00        first hour shown; rows outside work hours are dimmed
 *   work_end: 18:00
 *   first_weekday: monday    monday | sunday
 *   due_soon: 3              days ahead within which a todo with a due date is "due soon"
 *   tags: medical financial  predefined tags offered in the tag pane and the # filter
 *   menu: on                 key menu at the bottom
 *   unicode: auto            auto | on | off
 *   palette: default         default | mono (mono uses no colour: weight, underline, italic, inverse)
 *   color <role>: <fg> [on <bg>] [bold] [dim] [underline] [inverse] [italic]
 *       roles: cursor today busy busier event eventsel allday todo overdue duesoon
 *              tododone sel box panel err now field focus rule titlebar
 *       colors: default black red green yellow blue magenta cyan white,
 *               bright-<name>, or 0-15
 */
#ifndef SOON_CONFIG_H
#define SOON_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

enum {
    ROLE_CURSOR, ROLE_TODAY, ROLE_BUSY, ROLE_BUSIER, ROLE_EVENT, ROLE_EVENTSEL, ROLE_ALLDAY, ROLE_TODO,
    ROLE_OVERDUE, ROLE_DUESOON, ROLE_TODODONE, ROLE_SEL, ROLE_BOX, ROLE_PANEL, ROLE_ERR, ROLE_NOW, ROLE_FIELD, ROLE_FOCUS, ROLE_RULE,
    ROLE_TITLEBAR, N_ROLES
};

typedef struct { uint8_t fg, bg, attr; } cfg_style_t;   /* 255 = default color; attr bits as ui/grid.h */

typedef struct {
    int slot, sheet_slot;
    int work_start, work_end;     /* minutes */
    int first_weekday;            /* 0 = Monday ... 6 = Sunday */
    int due_soon;                 /* days; a todo due within this many days is "due soon" */
    char tags[512];               /* predefined tags, space-separated */
    int menu;                     /* 1 / 0 */
    int unicode;                  /* -1 auto, 1, 0 */
    char palette[16];
    cfg_style_t colors[N_ROLES];
    bool color_set[N_ROLES];
} config_t;

extern const char *const config_role_names[N_ROLES];

void  config_defaults(config_t *c);
/* parse text; returns the number of lines that were not understood, described in err (may be empty) */
int   config_parse(config_t *c, const char *text, char *err, int errlen);
int   config_load(config_t *c, const char *path, char *err, int errlen);   /* 0 ok, 1 missing, -1 unreadable */
const char *config_template(void);                                        /* a commented file with the defaults */

#endif
