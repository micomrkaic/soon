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

/* term_posix.c — keys.h platform interface on Linux/macOS via termios + VT sequences */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "../ui/keys.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include "../core/utf8.h"

static struct termios orig;
static int raw_on = 0;
static volatile sig_atomic_t winch = 0;

static void on_winch(int sig)
{
    (void)sig;
    winch = 1;
}

void term_write(const char *s, unsigned long n)
{
    while (n > 0) {
        ssize_t k = write(STDOUT_FILENO, s, n);
        if (k < 0) { if (errno == EINTR) continue; return; }
        s += k;
        n -= (unsigned long)k;
    }
}

int term_open(void)
{
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return -1;
    if (tcgetattr(STDIN_FILENO, &orig) != 0) return -1;
    struct termios t = orig;
    t.c_iflag &= ~(unsigned)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    t.c_oflag &= ~(unsigned)OPOST;
    t.c_cflag |= CS8;
    t.c_lflag &= ~(unsigned)(ECHO | ICANON | IEXTEN | ISIG);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) != 0) return -1;
    raw_on = 1;
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_winch;
    sigaction(SIGWINCH, &sa, NULL);
    term_write("\x1b[?1049h\x1b[?25l\x1b[H\x1b[2J", 21);
    return 0;
}

void term_close(void)
{
    if (!raw_on) return;
    term_write("\x1b[0m\x1b[?25h\x1b[?1049l", 18);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
    raw_on = 0;
}

void term_size(int *w, int *h)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        *w = ws.ws_col;
        *h = ws.ws_row;
    } else {
        *w = 80;
        *h = 24;
    }
}

/* one byte, or KEY_NONE on timeout, KEY_RESIZE, KEY_EOF */
static int read_byte(int timeout_ms)
{
    for (;;) {
        if (winch) { winch = 0; return KEY_RESIZE; }
        struct pollfd p = { STDIN_FILENO, POLLIN, 0 };
        int r = poll(&p, 1, timeout_ms);
        if (r < 0) { if (errno == EINTR) continue; return KEY_EOF; }
        if (r == 0) return KEY_NONE;
        unsigned char c;
        ssize_t k = read(STDIN_FILENO, &c, 1);
        if (k == 1) return c;
        if (k == 0) return KEY_EOF;
        if (errno != EINTR) return KEY_EOF;
    }
}

static int csi_key(int final, int param)
{
    switch (final) {
    case 'A': return KEY_UP;
    case 'B': return KEY_DOWN;
    case 'C': return KEY_RIGHT;
    case 'D': return KEY_LEFT;
    case 'H': return KEY_HOME;
    case 'F': return KEY_END;
    case 'Z': return KEY_BTAB;
    case 'P': return KEY_F1;
    case 'Q': return KEY_F2;
    case 'R': return KEY_F3;
    case 'S': return KEY_F4;
    case '~':
        switch (param) {
        case 1: case 7: return KEY_HOME;
        case 4: case 8: return KEY_END;
        case 2: return KEY_INS;
        case 3: return KEY_DEL;
        case 5: return KEY_PGUP;
        case 6: return KEY_PGDN;
        case 11: return KEY_F1;
        case 12: return KEY_F2;
        case 13: return KEY_F3;
        case 14: return KEY_F4;
        case 15: return KEY_F5;
        default: return KEY_NONE;
        }
    default: return KEY_NONE;
    }
}

int term_read_key(int timeout_ms)
{
    int b = read_byte(timeout_ms);
    if (b < 0) return b;

    if (b == 27) {
        int c = read_byte(40);
        if (c == KEY_RESIZE) { winch = 1; return KEY_ESC; }
        if (c < 0) return KEY_ESC;
        if (c != '[' && c != 'O') return c >= 32 && c < 127 ? KEY_ALT(c) : KEY_ESC;   /* Meta/Alt + key */
        int param = 0, final = 0;
        for (int i = 0; i < 8; i++) {
            int d = read_byte(40);
            if (d < 0) return KEY_ESC;
            if (d >= '0' && d <= '9') { param = param * 10 + (d - '0'); continue; }
            if (d == ';') { param = 0; continue; }     /* drop modifier parameters */
            final = d;
            break;
        }
        int k = csi_key(final, param);
        return k == KEY_NONE ? KEY_ESC : k;
    }

    if (b == 8) return KEY_BACKSPACE;
    if (b == 10) return KEY_ENTER;
    if (b < 0x80) return b;

    /* UTF-8 sequence */
    int need = (b & 0xE0) == 0xC0 ? 1 : (b & 0xF0) == 0xE0 ? 2 : (b & 0xF8) == 0xF0 ? 3 : 0;
    char buf[5] = { (char)b, 0, 0, 0, 0 };
    for (int i = 0; i < need; i++) {
        int c = read_byte(40);
        if (c < 0 || (c & 0xC0) != 0x80) return UTF8_INVALID;
        buf[i + 1] = (char)c;
    }
    uint32_t cp;
    utf8_decode(buf, &cp);
    return (int)cp;
}
