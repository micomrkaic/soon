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

/* keys.h — normalized key codes. Printable keys are their Unicode code point. */
#ifndef SOON_KEYS_H
#define SOON_KEYS_H

enum {
    KEY_NONE = -1,
    KEY_RESIZE = -2,
    KEY_EOF = -3,

    KEY_TAB = 9,
    KEY_ENTER = 13,
    KEY_ESC = 27,
    KEY_BACKSPACE = 127,

    KEY_SPECIAL = 0x110000,
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN,
    KEY_INS, KEY_DEL, KEY_BTAB,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5,
};

#define KEY_CTRL(c) ((c) & 0x1f)
#define KEY_ALT(c)  (KEY_SPECIAL + 0x1000 + (c))   /* Meta/Alt + printable key */

/* platform interface (platform/term_*.c) */
int  term_open(void);                 /* raw mode, alternate screen; 0 on success */
void term_close(void);
void term_size(int *w, int *h);
int  term_read_key(int timeout_ms);   /* -1 timeout = block */
void term_write(const char *s, unsigned long n);

#endif
