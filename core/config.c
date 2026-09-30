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

/* config.c — see config.h */
#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"

const char *const config_role_names[N_ROLES] = {
    "cursor", "today", "busy", "busier", "event", "eventsel", "allday", "todo", "overdue", "duesoon", "tododone", "sel",
    "box", "panel", "err", "now", "field", "focus", "rule", "titlebar"
};

void config_defaults(config_t *c)
{
    memset(c, 0, sizeof *c);
    c->slot = 15;
    c->sheet_slot = 30;
    c->work_start = 8 * 60;
    c->work_end = 18 * 60;
    c->first_weekday = 0;
    c->due_soon = 3;
    c->menu = 1;
    c->unicode = -1;
    snprintf(c->palette, sizeof c->palette, "default");
}

static int color_code(const char *name)
{
    static const char *const names[8] = { "black", "red", "green", "yellow", "blue", "magenta", "cyan", "white" };
    if (str_ieq(name, "default")) return 255;
    if (isdigit((unsigned char)name[0])) { int v = atoi(name); return v >= 0 && v <= 15 ? v : -1; }
    bool bright = str_iprefix("bright-", name) || str_iprefix("bright", name);
    const char *base = bright ? name + (name[6] == '-' ? 7 : 6) : name;
    for (int i = 0; i < 8; i++) if (str_ieq(base, names[i])) return i + (bright ? 8 : 0);
    return -1;
}

/* "white on blue bold" */
static bool parse_style(const char *v, cfg_style_t *st)
{
    cfg_style_t s = { 255, 255, 0 };
    char tok[32];
    const char *p = v;
    bool on = false, any = false;
    for (;;) {
        p = word(p, tok, sizeof tok);
        if (!tok[0]) break;
        any = true;
        if (str_ieq(tok, "on")) { on = true; continue; }
        if (str_ieq(tok, "bold")) { s.attr |= 1; continue; }
        if (str_ieq(tok, "dim")) { s.attr |= 2; continue; }
        if (str_ieq(tok, "underline")) { s.attr |= 4; continue; }
        if (str_ieq(tok, "inverse")) { s.attr |= 8; continue; }
        if (str_ieq(tok, "italic")) { s.attr |= 16; continue; }
        int c = color_code(tok);
        if (c < 0) return false;
        if (on) s.bg = (uint8_t)c; else s.fg = (uint8_t)c;
    }
    if (!any) return false;
    *st = s;
    return true;
}

static bool parse_minutes(const char *v, int *out)
{
    int h, m = 0;
    if (sscanf(v, "%d:%d", &h, &m) < 1) return false;
    if (h < 0 || h > 24 || m < 0 || m > 59) return false;
    *out = h * 60 + m;
    return true;
}

int config_parse(config_t *c, const char *text, char *err, int errlen)
{
    int bad = 0;
    if (errlen) err[0] = 0;
    char line[512];
    const char *p = text;
    int lineno = 0;
    while (next_line(&p, line, sizeof line)) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        rtrim(line);
        const char *s = skipws(line);
        if (!*s) continue;
        const char *colon = strchr(s, ':');
        if (!colon) { bad++; continue; }
        char key[64];
        size_t kl = (size_t)(colon - s);
        if (kl >= sizeof key) { bad++; continue; }
        memcpy(key, s, kl); key[kl] = 0;
        rtrim(key);
        const char *v = skipws(colon + 1);
        bool ok = true;
        int n;
        if (!strcmp(key, "slot") || !strcmp(key, "sheet_slot")) {
            n = atoi(v);
            ok = n == 5 || n == 10 || n == 15 || n == 30 || n == 60;
            if (ok) { if (key[1] == 'l') c->slot = n; else c->sheet_slot = n; }
        } else if (!strcmp(key, "tags")) {
            snprintf(c->tags, sizeof c->tags, "%s", v);
        } else if (!strcmp(key, "due_soon")) {
            n = atoi(v);
            ok = n >= 0 && n <= 365 && (*v == '0' || n > 0);
            if (ok) c->due_soon = n;
        } else if (!strcmp(key, "work_start")) ok = parse_minutes(v, &c->work_start);
        else if (!strcmp(key, "work_end")) ok = parse_minutes(v, &c->work_end);
        else if (!strcmp(key, "first_weekday")) {
            if (str_iprefix("mon", v)) c->first_weekday = 0;
            else if (str_iprefix("sun", v)) c->first_weekday = 6;
            else if (str_iprefix("sat", v)) c->first_weekday = 5;
            else ok = false;
        } else if (!strcmp(key, "menu")) { if (str_ieq(v, "on") || str_ieq(v, "yes") || str_ieq(v, "1")) c->menu = 1; else if (str_ieq(v, "off") || str_ieq(v, "no") || str_ieq(v, "0")) c->menu = 0; else ok = false; }
        else if (!strcmp(key, "unicode")) { if (str_ieq(v, "auto")) c->unicode = -1; else if (str_ieq(v, "on")) c->unicode = 1; else if (str_ieq(v, "off")) c->unicode = 0; else ok = false; }
        else if (!strcmp(key, "palette")) { if (str_ieq(v, "default") || str_ieq(v, "mono")) snprintf(c->palette, sizeof c->palette, "%s", v); else ok = false; }
        else if (!strncmp(key, "color ", 6)) {
            const char *role = skipws(key + 6);
            int r = -1;
            for (int i = 0; i < N_ROLES; i++) if (str_ieq(role, config_role_names[i])) r = i;
            if (r < 0) ok = false;
            else { ok = parse_style(v, &c->colors[r]); if (ok) c->color_set[r] = true; }
        } else ok = false;
        if (!ok) {
            bad++;
            if (errlen) { size_t l = strlen(err); snprintf(err + l, (size_t)errlen - l, "%sline %d: %s", l ? "; " : "", lineno, line); }
        }
    }
    return bad;
}

const char *config_template(void)
{
    return
        "# soon configuration — ~/.config/soon/config\n"
        "# minutes per slot in the week and day views: 5 10 15 30 60\n"
        "slot: 15\n"
        "# minutes per slot in the days sheet\n"
        "sheet_slot: 30\n"
        "# working hours: the views start here, and rows outside are dimmed\n"
        "work_start: 08:00\n"
        "work_end: 18:00\n"
        "# monday | sunday | saturday (week numbers stay ISO)\n"
        "first_weekday: monday\n"
        "# a todo whose due date falls within this many days is shown as due soon (0 = today only)\n"
        "due_soon: 3\n"
        "# predefined tags, offered in the Tags pane and the # filter alongside tags already in use\n"
        "#tags: medical financial family work\n"
        "# key menu at the bottom: on | off\n"
        "menu: on\n"
        "# box and marker glyphs: auto | on | off\n"
        "unicode: auto\n"
        "# default | mono   (mono: no colours, only bold / dim / underline / italic / inverse)\n"
        "palette: default\n"
        "# individual roles: color <role>: <fg> [on <bg>] [bold] [dim] [underline] [inverse] [italic]\n"
        "# roles: cursor today busy busier event eventsel allday todo overdue duesoon tododone\n"
        "#        sel box panel err now field focus rule titlebar\n"
        "# colours: default black red green yellow blue magenta cyan white, bright-<colour>, 0-15\n"
        "# the defaults are deutan-safe: blue, yellow, magenta, cyan; never red against green\n"
        "#color cursor: black on bright-yellow\n"
        "#color event: bright-white on blue\n"
        "#color todo: bright-yellow\n"
        "#color overdue: bright-magenta bold\n"
        "# due soon: use underline instead of italic if your terminal lacks italics\n"
        "#color duesoon: bright-yellow italic\n"
        "#color today: bright-blue bold underline\n";
}

#ifndef SOON_NO_FILE_IO
int config_load(config_t *c, const char *path, char *err, int errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) return errno == ENOENT ? 1 : -1;
    sbuf_t b = { 0, 0, 0 };
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) sb_put(&b, chunk, n);
    fclose(f);
    config_parse(c, b.s ? b.s : "", err, errlen);
    free(b.s);
    return 0;
}
#endif
