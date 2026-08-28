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

/* app.c — see app.h
 *
 * Screen layout (rows):
 *   0            title bar
 *   1..cal_h     calendar (year / 3-month / month grid), or the slot grid in week/day
 *   +ag_h        agenda panel for the cursor day (calendar views only)
 *   H-1          status line / prompts
 * layout() decides cal_h and ag_h from the terminal size on every redraw.
 */
#include "app.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/cities.h"
#include "../core/config.h"
#include "../core/datejump.h"
#include "../core/sun.h"
#include "../core/tz.h"
#include "../core/text.h"
#include "../core/utf8.h"
#include "grid.h"
#include "keys.h"

typedef enum { V_YEAR, V_QUARTER, V_MONTH, V_WEEK, V_DAY } view_t;

#define MAX_DAY_ITEMS 64
#define MONTH_BOX_W 24
#define MONTH_BOX_H 9
#define TOP 2                 /* rows above the body: title bar + info bar */
#define STATUS_Y(a) ((a)->H - 1 - (a)->menu_rows)   /* status line / prompts */
#define BOTTOM(a)   (1 + (a)->menu_rows)

typedef enum { TBL_NONE, TBL_SLOTS, TBL_DAYLOC, TBL_TODOS, TBL_EVENTS, TBL_PLACES, TBL_CONTACTS, TBL_SEARCH } table_t;
#define TBL_LAST TBL_CONTACTS   /* TBL_SEARCH is outside the v cycle */

typedef struct {
    store_t *st;
    contacts_t *cs;
    app_config_t cfg;
    grid_t g;
    outbuf_t out;
    int W, H;

    daynum_t today, cur;
    int now_min;
    int now_sec;         /* seconds after midnight, home zone */
    long long now;       /* UTC seconds */
    int menu_rows;       /* 2 when the nano-style key menu is shown, else 0 */
    int cur_min;
    int slot;            /* week/day views */
    int sheet_slot;      /* days sheet */
    int first_wd;        /* 0 = Monday ... 6 = Sunday */
    int work_start, work_end;
    view_t view;
    int sel;
    bool itemmode;       /* Tab: hjkl/arrows move through the day's items */
    int mscroll;
    int tscroll;
    bool show_sun, show_help, quit;
    table_t table;       /* TBL_NONE: calendar views; else a spreadsheet view */
    int trow, tcol, trow0, tcol0;
    daynum_t tbase;      /* first day shown in the day-based sheets */
    char query[128];     /* search sheet */
    table_t table_before_search;
    char msg[256];
    bool msg_err;
} app_t;

/* ---------- palette (deutan-safe defaults; overridable from the config file) ---------- */

typedef struct {
    style_t cursor, today, busy, busier, event, eventsel, allday, todo, overdue, tododone, sel,
            box, panel, err, now, field, focus, rule, titlebar;
} palette_t;

static palette_t pal;

#define S_NORMAL   style(C_DEFAULT, C_DEFAULT, 0)
#define S_DIM      style(C_DEFAULT, C_DEFAULT, A_DIM)
#define S_BOLD     style(C_DEFAULT, C_DEFAULT, A_BOLD)
#define S_GUTTER   style(C_DEFAULT, C_DEFAULT, A_DIM)
#define S_LABEL    style(C_DEFAULT, C_DEFAULT, A_DIM)
#define S_TITLEBAR pal.titlebar
#define S_CURSOR   pal.cursor
#define S_TODAY    pal.today
#define S_BUSY1    pal.busy
#define S_BUSY2    pal.busier
#define S_EVENT    pal.event
#define S_EVENTSEL pal.eventsel
#define S_ALLDAY   pal.allday
#define S_TODO     pal.todo
#define S_OVERDUE  pal.overdue
#define S_TODODONE pal.tododone
#define S_SEL      pal.sel
#define S_BOX      pal.box
#define S_PANEL    pal.panel
#define S_ERR      pal.err
#define S_NOW      pal.now
#define S_FIELD    pal.field
#define S_FOCUS    pal.focus
#define S_RULE     pal.rule

static void palette_init(const config_t *c)
{
    bool mono = c && str_ieq(c->palette, "mono");
    pal.titlebar = style(C_DEFAULT, C_DEFAULT, A_INV);
    pal.cursor   = mono ? style(C_DEFAULT, C_DEFAULT, A_INV | A_BOLD) : style(0, 11, 0);
    pal.today    = mono ? style(C_DEFAULT, C_DEFAULT, A_BOLD | A_UL) : style(12, C_DEFAULT, A_BOLD | A_UL);
    pal.busy     = mono ? style(C_DEFAULT, C_DEFAULT, A_BOLD) : style(12, C_DEFAULT, A_BOLD);
    pal.busier   = mono ? style(C_DEFAULT, C_DEFAULT, A_INV) : style(15, 4, A_BOLD);
    pal.event    = mono ? style(C_DEFAULT, C_DEFAULT, A_INV) : style(15, 4, 0);
    pal.eventsel = mono ? style(C_DEFAULT, C_DEFAULT, A_INV | A_BOLD | A_UL) : style(0, 13, A_BOLD);
    pal.allday   = mono ? style(C_DEFAULT, C_DEFAULT, A_BOLD) : style(14, C_DEFAULT, A_BOLD);
    pal.todo     = mono ? style(C_DEFAULT, C_DEFAULT, 0) : style(11, C_DEFAULT, 0);
    pal.overdue  = mono ? style(C_DEFAULT, C_DEFAULT, A_BOLD) : style(13, C_DEFAULT, A_BOLD);
    pal.tododone = style(C_DEFAULT, C_DEFAULT, A_DIM);
    pal.sel      = mono ? style(C_DEFAULT, C_DEFAULT, A_INV | A_BOLD) : style(0, 13, A_BOLD);
    pal.box      = mono ? style(C_DEFAULT, C_DEFAULT, A_BOLD) : style(12, C_DEFAULT, 0);
    pal.panel    = mono ? style(C_DEFAULT, C_DEFAULT, A_BOLD) : style(14, C_DEFAULT, 0);
    pal.err      = mono ? style(C_DEFAULT, C_DEFAULT, A_BOLD | A_INV) : style(13, C_DEFAULT, A_BOLD);
    pal.now      = mono ? style(C_DEFAULT, C_DEFAULT, A_BOLD) : style(11, C_DEFAULT, A_BOLD);
    pal.field    = style(C_DEFAULT, C_DEFAULT, A_UL);
    pal.focus    = mono ? style(C_DEFAULT, C_DEFAULT, A_INV) : style(0, 11, 0);
    pal.rule     = mono ? style(C_DEFAULT, C_DEFAULT, A_DIM) : style(12, C_DEFAULT, 0);
    if (!c) return;
    style_t *slots[N_ROLES] = { &pal.cursor, &pal.today, &pal.busy, &pal.busier, &pal.event, &pal.eventsel, &pal.allday, &pal.todo,
                                &pal.overdue, &pal.tododone, &pal.sel, &pal.box, &pal.panel, &pal.err, &pal.now, &pal.field, &pal.focus, &pal.rule, &pal.titlebar };
    for (int i = 0; i < N_ROLES; i++)
        if (c->color_set[i]) *slots[i] = style(c->colors[i].fg, c->colors[i].bg, c->colors[i].attr);
}

static const char *const view_names[] = { "year", "3 months", "month", "week", "day" };
static const char *const table_names[] = { "", "days", "locations by date", "todos", "events", "places", "contacts", "search" };

/* ---------- utilities ---------- */

static void set_msg(app_t *a, bool err, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->msg, sizeof a->msg, fmt, ap);
    va_end(ap);
    a->msg_err = err;
}

static const char *place_zone(const place_t *p)
{
    return p && p->zone ? p->zone : "UTC";
}

static int day_offset(app_t *a, daynum_t day)
{
    const place_t *p = store_place_for_day(a->st, day);
    int off = 0;
    if (tz_offset(place_zone(p), (long long)day * 86400 + 43200, &off) != 0) off = 0;
    return off;
}

/* "tz America/New_York (UTC-4)", "tz Asia/Kolkata (UTC+5:30)" */
static void zone_label(const place_t *p, daynum_t day, char *out, size_t n)
{
    int off = 0;
    tz_offset(place_zone(p), (long long)day * 86400 + 43200, &off);
    int h = off / 60, m = abs(off % 60);
    if (m) snprintf(out, n, "tz %s (UTC%+d:%02d)", place_zone(p), h, m);
    else snprintf(out, n, "tz %s (UTC%+d)", place_zone(p), h);
}

/* first day of the week containing d, per the configured first weekday */
static daynum_t wk_start(app_t *a, daynum_t d)
{
    return d - ((civil_weekday(d) - a->first_wd + 7) % 7);
}

static bool in_work_hours(app_t *a, int minute)
{
    return minute >= a->work_start && minute < a->work_end;
}

static void refresh_clock(app_t *a)
{
    long long now = a->cfg.now_utc ? a->cfg.now_utc() : 0;
    const place_t *home = a->st->home ? store_find_place(a->st, a->st->home) : NULL;
    int y, m, d, hh, mm, off;
    a->now = now;
    if (tz_utc_to_wall(place_zone(home), now, &y, &m, &d, &hh, &mm, &off) == 0) {
        a->today = civil_to_days(y, m, d);
        a->now_min = hh * 60 + mm;
        a->now_sec = hh * 3600 + mm * 60 + (int)(((now % 60) + 60) % 60);
    } else {
        a->today = (daynum_t)(now / 86400);
        a->now_min = (int)((now % 86400) / 60);
        a->now_sec = (int)(now % 86400);
    }
}

static void save(app_t *a)
{
    if (store_save(a->st) != 0) set_msg(a, true, "save failed: %s (%s)", strerror(errno), a->st->path);
}

static int day_items(app_t *a, daynum_t day, int *idx)
{
    return store_day_items(a->st, day, idx, MAX_DAY_ITEMS);
}

static int selected(app_t *a)
{
    int idx[MAX_DAY_ITEMS];
    int n = day_items(a, a->cur, idx);
    if (a->sel < 0 || a->sel >= n) return -1;
    return idx[a->sel];
}

static void select_store_index(app_t *a, int si)
{
    int idx[MAX_DAY_ITEMS];
    int n = day_items(a, a->cur, idx);
    a->sel = -1;
    for (int k = 0; k < n; k++) if (idx[k] == si) a->sel = k;
}

static const char *todo_box(app_t *a, const item_t *it, daynum_t day)
{
    bool done = todo_done_on(it, day);
    if (a->cfg.unicode) return done ? "\xE2\x98\x91" : "\xE2\x98\x90";
    return done ? "[x]" : "[ ]";
}

static void item_label(app_t *a, const item_t *it, char *out, size_t n, bool with_time, daynum_t day)
{
    const char *title = it->title ? it->title : "";
    if (it->kind == ITEM_TODO) snprintf(out, n, "%s %s", todo_box(a, it, day), title);
    else if (it->start < 0 || !with_time) snprintf(out, n, "%s", title);
    else snprintf(out, n, "%02d:%02d %s", it->start / 60, it->start % 60, title);
}

static style_t item_style(const app_t *a, const item_t *it, bool sel, daynum_t day)
{
    if (sel) return S_SEL;
    if (it->kind == ITEM_TODO) return todo_done_on(it, day) ? S_TODODONE : todo_overdue_on(it, day, a->today) ? S_OVERDUE : S_TODO;
    if (it->start < 0) return S_ALLDAY;
    return S_NORMAL;
}

static void flush(app_t *a)
{
    a->out.n = 0;
    grid_render(&a->g, &a->out);
    term_write(a->out.s, a->out.n);
}

static void resize(app_t *a)
{
    term_size(&a->W, &a->H);
    grid_resize(&a->g, a->W, a->H);
}

static void format_day(daynum_t d, char *out, size_t n, bool with_year)
{
    civil_t c = days_to_civil(d);
    if (with_year) snprintf(out, n, "%.3s %d %.3s %d", civil_weekday_names[civil_weekday(d)], c.d, civil_month_names[c.m], c.y);
    else snprintf(out, n, "%.3s %d %.3s", civil_weekday_names[civil_weekday(d)], c.d, civil_month_names[c.m]);
}

/* ---------- layout ---------- */

static int month_cols(app_t *a, int count)
{
    int cols = a->W / MONTH_BOX_W;
    if (cols < 1) cols = 1;
    if (cols > count) cols = count;
    return cols;
}

/* rows available to the calendar and to the agenda panel below it */
static void layout(app_t *a, int *cal_h, int *ag_h)
{
    int body = a->H - TOP - BOTTOM(a);
    if (body < 1) body = 1;
    *ag_h = 0;
    *cal_h = body;
    if (a->view == V_YEAR || a->view == V_QUARTER) {
        int count = a->view == V_YEAR ? 12 : 3;
        int cols = month_cols(a, count);
        int need = (count + cols - 1) / cols;
        int pref = body / 3 > 5 ? body / 3 : 5;
        int rows = (body - pref) / MONTH_BOX_H;
        if (rows < 1) rows = 1;
        if (rows > need) rows = need;
        int ch = rows * MONTH_BOX_H;
        if (ch > body) ch = body;
        int ag = body - ch;
        if (ag < 4) { *cal_h = body; *ag_h = 0; }
        else { *cal_h = ch; *ag_h = ag; }
    } else if (a->view == V_MONTH) {
        int pref = body / 4 > 4 ? body / 4 : 4;
        int ch = body - pref;
        if (ch < 13) { *cal_h = body; *ag_h = 0; }
        else { *cal_h = ch; *ag_h = pref; }
    }
}

/* ---------- title and status ---------- */

static int *table_rows(app_t *a, int *n);

static void draw_title(app_t *a)
{
    char left[256], right[256], clock[128], there[128] = "";
    civil_t c = days_to_civil(a->today);
    const place_t *home = a->st->home ? store_find_place(a->st, a->st->home) : NULL;
    char ab[16];
    tz_abbrev(place_zone(home), a->now, ab, sizeof ab);
    snprintf(clock, sizeof clock, "%.3s %d %.3s %d  %02d:%02d:%02d %s", civil_weekday_names[civil_weekday(a->today)], c.d,
             civil_month_names[c.m], c.y, a->now_sec / 3600, a->now_sec / 60 % 60, a->now_sec % 60, ab);
    /* when the cursor day is somewhere in another zone, show the clock there too */
    const place_t *p = store_place_for_day(a->st, a->cur);
    if (p && strcmp(place_zone(p), place_zone(home)) != 0) {
        int y, m, d, hh, mm, off;
        if (tz_utc_to_wall(place_zone(p), a->now, &y, &m, &d, &hh, &mm, &off) == 0) {
            tz_abbrev(place_zone(p), a->now, ab, sizeof ab);
            snprintf(there, sizeof there, "  \xC2\xB7  %02d:%02d %s in %s", hh, mm, ab, p->name);
        }
    }
    snprintf(left, sizeof left, " soon  %s%s", clock, there);
    if (a->table == TBL_SLOTS || a->table == TBL_DAYLOC || a->table == TBL_SEARCH) snprintf(right, sizeof right, "[%s] ", table_names[a->table]);
    else if (a->table != TBL_NONE) { int nrow; int *rows = table_rows(a, &nrow); free(rows); snprintf(right, sizeof right, "[sheet: %s  %d/%d] ", table_names[a->table], nrow ? a->trow + 1 : 0, nrow); }
    else snprintf(right, sizeof right, "[%s]  %d items ", view_names[a->view], a->st->nitems);
    grid_fill(&a->g, 0, 0, a->W, 1, ' ', S_TITLEBAR);
    int rw = utf8_swidth(right);
    grid_puts(&a->g, 0, 0, left, a->W - rw - 1 > 0 ? a->W - rw - 1 : a->W, S_TITLEBAR);
    if (rw < a->W && utf8_swidth(left) + rw < a->W) grid_puts(&a->g, a->W - rw, 0, right, rw, S_TITLEBAR);
}

/* Row 1: the cursor day, its location and its sun times. Segments are dropped
 * from the right when the terminal is too narrow. */
static void draw_infobar(app_t *a)
{
    civil_t c = days_to_civil(a->cur);
    int iy, w;
    civil_iso_week(a->cur, &iy, &w);
    const place_t *p = store_place_for_day(a->st, a->cur);
    int off = day_offset(a, a->cur);
    char seg[6][128];
    int nseg = 0;
    snprintf(seg[nseg++], sizeof seg[0], "%s %d %s %d  W%02d", civil_weekday_names[civil_weekday(a->cur)], c.d, civil_month_names[c.m], c.y, w);
    if (!p) {
        snprintf(seg[nseg++], sizeof seg[0], "no location (H sets home)");
    } else {
        bool away = a->st->home && strcmp(p->name, a->st->home) != 0;
        char zl[48];
        zone_label(p, a->cur, zl, sizeof zl);
        snprintf(seg[nseg++], sizeof seg[0], "%s%s  %s", away ? "@ " : "", p->name, zl);
        sun_t s;
        sun_compute(a->cur, p->lat, p->lon, &s);
        int h1, m1, h2, m2, dd;
        const char *sun = a->cfg.unicode ? "\xE2\x98\xBC" : "sun";
        if (s.rise_set == SUN_NORMAL) {
            sun_local_hhmm(s.rise, off, &h1, &m1, &dd);
            sun_local_hhmm(s.set, off, &h2, &m2, &dd);
            snprintf(seg[nseg++], sizeof seg[0], "%s %02d:%02d-%02d:%02d", sun, h1, m1, h2, m2);
        } else {
            snprintf(seg[nseg++], sizeof seg[0], "%s %s", sun, s.rise_set == SUN_ALWAYS_ABOVE ? "up all day" : "down all day");
        }
        if (s.civil == SUN_NORMAL) {
            sun_local_hhmm(s.civil_dawn, off, &h1, &m1, &dd);
            sun_local_hhmm(s.civil_dusk, off, &h2, &m2, &dd);
            snprintf(seg[nseg++], sizeof seg[0], "civil %02d:%02d-%02d:%02d", h1, m1, h2, m2);
        }
        snprintf(seg[nseg++], sizeof seg[0], "day %dh%02dm", (int)(s.day_length / 60), (int)s.day_length % 60);
    }
    char line[1000] = "";
    for (int i = 0; i < nseg; i++) {
        char cand[1000];
        snprintf(cand, sizeof cand, "%s%s%s", line, i ? "  \xC2\xB7  " : " ", seg[i]);
        if (utf8_swidth(cand) > a->W) break;
        snprintf(line, sizeof line, "%s", cand);
    }
    grid_fill(&a->g, 0, 1, a->W, 1, ' ', S_NORMAL);
    grid_puts(&a->g, 0, 1, line, a->W, S_NORMAL);
    /* date and place in bold */
    int dw = utf8_swidth(seg[0]);
    grid_style(&a->g, 1, 1, dw, 1, S_BOLD);
    if (nseg > 1) {
        int pw = utf8_swidth(seg[1]);
        int px = 1 + dw + 5;
        if (px + pw <= a->W) grid_style(&a->g, px, 1, pw, 1, p && a->st->home && strcmp(p->name, a->st->home) ? S_ALLDAY : S_BOLD);
    }
}

static void draw_status(app_t *a)
{
    int y = STATUS_Y(a);
    grid_fill(&a->g, 0, y, a->W, 1, ' ', S_NORMAL);
    if (a->msg[0]) { grid_puts(&a->g, 0, y, a->msg, a->W, a->msg_err ? S_ERR : S_NORMAL); return; }
    int si = selected(a);
    if (si >= 0) {
        const item_t *it = &a->st->items[si];
        char lab[512], line[1100];
        item_label(a, it, lab, sizeof lab, true, a->cur);
        snprintf(line, sizeof line, "%s   [Enter/e edit  x done  m move  d delete  j/k next  Esc leave]", lab);
        grid_puts(&a->g, 0, y, line, a->W, S_NORMAL);
        return;
    }
    if (a->table == TBL_SEARCH) {
        grid_puts(&a->g, 0, y, "j/k move  Enter goes to the item (or contact)  e edits  x done  / searches again  Esc back", a->W, S_DIM);
        return;
    }
    if (a->table == TBL_SLOTS) {
        grid_puts(&a->g, 0, y, "arrows move  Enter: edit the cell's item or create an event here  T todo  d delete  x done  z slot  v next sheet  Esc", a->W, S_DIM);
        return;
    }
    if (a->table == TBL_DAYLOC) {
        grid_puts(&a->g, 0, y, "arrows move  Enter sets this day's place  f fills it down to a date  L range form  d back to home  v next sheet  Esc", a->W, S_DIM);
        return;
    }
    if (a->table != TBL_NONE) {
        grid_puts(&a->g, 0, y, "arrows move  Enter edits the cell  e edits the row  a adds  d deletes  x done  v next sheet  Esc calendar", a->W, S_DIM);
        return;
    }
    const char *hint = a->view >= V_WEEK
        ? "hjkl move  +/- zoom  n/p period  a event  T todo  Tab select  g go  s sun  z slot  q quit"
        : "hjkl move  +/- zoom  n/p period  a event  T todo  Tab select  g go  s sun  L location  q quit";
    grid_puts(&a->g, 0, y, hint, a->W, S_DIM);
}

/* ---------- nano-style key menu (two rows at the bottom) ---------- */

typedef enum { MENU_MAIN, MENU_ITEMS, MENU_FORM, MENU_PICK, MENU_PROMPT, MENU_CONFIRM, MENU_TABLE, MENU_SLOTS, MENU_DAYLOC, MENU_SEARCH } menu_t;
typedef struct { const char *key, *label; } menu_item_t;

static void draw_menu_row(app_t *a, int y, const menu_item_t *items, int n)
{
    grid_fill(&a->g, 0, y, a->W, 1, ' ', S_NORMAL);
    int x = 0;
    for (int i = 0; i < n; i++) {
        int kw = utf8_swidth(items[i].key), lw = utf8_swidth(items[i].label);
        if (x + kw + 1 + lw > a->W) break;
        grid_puts(&a->g, x, y, items[i].key, kw, S_TITLEBAR);
        grid_puts(&a->g, x + kw + 1, y, items[i].label, lw, S_NORMAL);
        x += kw + 1 + lw + 2;
    }
}

static void draw_menu(app_t *a, menu_t which)
{
    if (a->menu_rows < 2) return;
    static const menu_item_t main1[] = { {".","Today"}, {"[ ]","Week"}, {"{ }","Month"}, {"( )","Year"}, {"n p","Period"}, {"+ -","Zoom"}, {"g","Go to"}, {"Tab","Items"}, {"s","Sun"}, {"z","Slot"} };
    static const menu_item_t main2[] = { {"a","Event"}, {"T","Todo"}, {"e","Edit"}, {"x","Done"}, {"d","Delete"}, {"L","Place"}, {"v","Sheets"}, {"/","Search"}, {"C","Contacts"}, {"M","Menu"}, {"?","Help"}, {"q","Quit"} };
    static const menu_item_t items1[] = { {"j k","Item"}, {"h l","Day"}, {"Enter","Edit"}, {"x","Done"}, {"m","Move"}, {"d","Delete"}, {"Esc","Leave"} };
    static const menu_item_t items2[] = { {"a","Event"}, {"T","Todo"}, {".","Today"}, {"s","Sun"}, {"M","Menu"}, {"?","Help"}, {"q","Quit"} };
    static const menu_item_t form1[] = { {"Tab","Next field"}, {"S-Tab","Previous"}, {"Enter","Save"}, {"Esc","Cancel"} };
    static const menu_item_t form2[] = { {"Arrows","Adjust / calendar"}, {"t","Today"}, {"n","Now"}, {"-","None"}, {"^U","Clear text"} };
    static const menu_item_t pick1[] = { {"Arrows","Move"}, {"n p","Month"}, {"N P","Year"}, {"t","Today"}, {"g","Type a date"} };
    static const menu_item_t pick2[] = { {"Enter","Pick"}, {"Esc","Cancel"} };
    static const menu_item_t prompt1[] = { {"Enter","OK"}, {"Esc","Cancel"}, {"^U","Clear"}, {"^W","Delete word"} };
    static const menu_item_t confirm1[] = { {"y","Yes"}, {"n","No"} };
    static const menu_item_t table1[] = { {"Arrows","Cell"}, {"Enter","Edit cell"}, {"e","Edit row"}, {"a","Add"}, {"d","Delete"}, {"x","Done"}, {"PgUp/Dn","Page"} };
    static const menu_item_t table2[] = { {"v","Next sheet"}, {"V","Previous"}, {"C","Contacts"}, {"Esc","Calendar"}, {"?","Help"}, {"q","Quit"} };
    static const menu_item_t slots1[] = { {"Arrows","Cell"}, {"Enter","Edit / new event"}, {"T","Todo"}, {"d","Delete"}, {"x","Done"}, {"z","Slot size"}, {"[ ]","Week"} };
    static const menu_item_t search1[] = { {"j k","Hit"}, {"Enter","Go there"}, {"e","Edit"}, {"x","Done"}, {"/","New search"}, {"Esc","Back"} };
    static const menu_item_t dayloc1[] = { {"Arrows","Day"}, {"Enter","Set place"}, {"f","Fill down"}, {"L","Range"}, {"d","Home"}, {"[ ]","Week"}, {"{ }","Month"} };
    static const menu_item_t none[] = { {"",""} };
#define N(m) ((int)(sizeof (m) / sizeof (m)[0]))
    const menu_item_t *r1 = none, *r2 = none;
    int n1 = 0, n2 = 0;
    #define ROW(v) (v), (int)(sizeof (v) / sizeof (v)[0])
    switch (which) {
    case MENU_MAIN:    r1 = main1; n1 = N(main1); r2 = main2; n2 = N(main2); break;
    case MENU_ITEMS:   r1 = items1; n1 = N(items1); r2 = items2; n2 = N(items2); break;
    case MENU_FORM:    r1 = form1; n1 = N(form1); r2 = form2; n2 = N(form2); break;
    case MENU_PICK:    r1 = pick1; n1 = N(pick1); r2 = pick2; n2 = N(pick2); break;
    case MENU_PROMPT:  r1 = prompt1; n1 = N(prompt1); break;
    case MENU_CONFIRM: r1 = confirm1; n1 = N(confirm1); break;
    case MENU_TABLE:   r1 = table1; n1 = N(table1); r2 = table2; n2 = N(table2); break;
    case MENU_SLOTS:   r1 = slots1; n1 = N(slots1); r2 = table2; n2 = N(table2); break;
    case MENU_DAYLOC:  r1 = dayloc1; n1 = N(dayloc1); r2 = table2; n2 = N(table2); break;
    case MENU_SEARCH:  r1 = search1; n1 = N(search1); r2 = table2; n2 = N(table2); break;
    }
    #undef ROW
    draw_menu_row(a, a->H - 2, r1, n1);
    draw_menu_row(a, a->H - 1, r2, n2);
#undef N
}

/* ---------- month grid (year and 3-month views, and the date picker) ---------- */

static style_t day_style(app_t *a, daynum_t d, daynum_t cursor, int load)
{
    if (d == cursor) return S_CURSOR;
    if (d == a->today) return S_TODAY;
    if (load >= 3) return S_BUSY2;
    if (load >= 1) return S_BUSY1;
    if (civil_weekday(d) >= 5) return S_DIM;
    return S_NORMAL;
}

static void draw_month_box(app_t *a, int x, int y, int yr, int mo, daynum_t cursor)
{
    char t[40];
    snprintf(t, sizeof t, "%s %d", civil_month_names[mo], yr);
    int tw = (int)strlen(t);
    grid_puts(&a->g, x + (23 - tw) / 2, y, t, 23, S_BOLD);
    grid_puts(&a->g, x, y + 1, "wk", 2, S_DIM);
    static const char *const wd[7] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
    for (int c = 0; c < 7; c++) { int w = (a->first_wd + c) % 7; grid_puts(&a->g, x + 3 + c * 3, y + 1, wd[w], 2, w >= 5 ? S_DIM : S_BOLD); }

    daynum_t first = civil_to_days(yr, mo, 1);
    int wd0 = (civil_weekday(first) - a->first_wd + 7) % 7, nd = civil_days_in_month(yr, mo);
    for (int r = 0; r < 6; r++) {
        int dn0 = r * 7 - wd0 + 1;
        if (dn0 > nd) break;
        int iy, wk;
        civil_iso_week(first + dn0 - 1 + 3, &iy, &wk);
        char ws[16];
        snprintf(ws, sizeof ws, "%2d", wk);
        grid_puts(&a->g, x, y + 2 + r, ws, 2, S_DIM);
        for (int c = 0; c < 7; c++) {
            int dn = dn0 + c;
            if (dn < 1 || dn > nd) continue;
            daynum_t d = first + dn - 1;
            int pend = 0;
            int load = store_day_load(a->st, d, &pend);
            char num[16];
            snprintf(num, sizeof num, "%2d", dn);
            style_t st = day_style(a, d, cursor, load);
            grid_puts(&a->g, x + 3 + c * 3, y + 2 + r, num, 2, st);
            uint32_t mark = pend ? (a->cfg.unicode ? 0x00B7 : '*') : ' ';
            grid_put(&a->g, x + 5 + c * 3, y + 2 + r, mark, d == cursor ? st : d < a->today ? S_OVERDUE : S_TODO);
        }
    }
}

static void draw_months(app_t *a, int yr, int mo, int count, int cal_h)
{
    int cols = month_cols(a, count);
    int rows = (count + cols - 1) / cols;
    int avail = cal_h / MONTH_BOX_H;
    if (avail < 1) avail = 1;
    civil_t c = days_to_civil(a->cur);
    int ci = (c.y * 12 + c.m - 1) - (yr * 12 + mo - 1);
    if (ci < 0) ci = 0;
    if (ci >= count) ci = count - 1;
    int r = ci / cols;
    if (r < a->mscroll) a->mscroll = r;
    if (r >= a->mscroll + avail) a->mscroll = r - avail + 1;
    if (a->mscroll > rows - avail) a->mscroll = rows - avail;
    if (a->mscroll < 0) a->mscroll = 0;

    int total = cols * MONTH_BOX_W - 1;
    int x0 = total < a->W ? (a->W - total) / 2 : 0;
    for (int i = 0; i < count; i++) {
        int rr = i / cols - a->mscroll;
        if (rr < 0 || rr >= avail) continue;
        int ym = yr * 12 + mo - 1 + i;
        draw_month_box(a, x0 + (i % cols) * MONTH_BOX_W, TOP + rr * MONTH_BOX_H, ym / 12, ym % 12 + 1, a->cur);
    }
    if (rows > avail) {
        char s[40];
        int last_shown = (a->mscroll + avail) * cols;
        if (last_shown > count) last_shown = count;
        snprintf(s, sizeof s, "months %d-%d of %d", a->mscroll * cols + 1, last_shown, count);
        grid_puts(&a->g, a->W - (int)strlen(s) - 1, TOP + cal_h - 1, s, (int)strlen(s), S_DIM);
    }
}

/* ---------- single month, big cells ---------- */

static void draw_month_big(app_t *a, int cal_h)
{
    civil_t c = days_to_civil(a->cur);
    int cw = (a->W - 3) / 7;
    if (cw < 6) { draw_months(a, c.y, c.m, 1, cal_h); return; }
    daynum_t first = civil_to_days(c.y, c.m, 1);
    int wd0 = (civil_weekday(first) - a->first_wd + 7) % 7, nd = civil_days_in_month(c.y, c.m);
    int nrows = (wd0 + nd + 6) / 7;
    int ch = (cal_h - 1) / nrows;
    if (ch < 2) ch = 2;
    int ymax = TOP + cal_h;

    static const char *const wd[7] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };
    for (int col = 0; col < 7; col++) { int w = (a->first_wd + col) % 7; grid_field(&a->g, 3 + col * cw, TOP, wd[w], cw - 1, w >= 5 ? S_DIM : S_BOLD, a->cfg.unicode); }

    for (int r = 0; r < nrows; r++) {
        int y = TOP + 1 + r * ch;
        if (y >= ymax) break;
        int iy, wk;
        civil_iso_week(first + r * 7 - wd0 + 3, &iy, &wk);
        char ws[16];
        snprintf(ws, sizeof ws, "%2d", wk);
        grid_puts(&a->g, 0, y, ws, 2, S_DIM);
        for (int col = 0; col < 7; col++) {
            int dn = r * 7 + col - wd0 + 1;
            if (dn < 1 || dn > nd) continue;
            daynum_t d = first + dn - 1;
            int x = 3 + col * cw;
            int pend = 0;
            int load = store_day_load(a->st, d, &pend);
            char num[16];
            snprintf(num, sizeof num, "%2d", dn);
            style_t st = day_style(a, d, a->cur, load);
            if (d != a->cur && d != a->today && load) st = civil_weekday(d) >= 5 ? S_DIM : S_NORMAL;
            grid_fill(&a->g, x, y, cw - 1, 1, ' ', d == a->cur ? S_CURSOR : S_NORMAL);
            grid_puts(&a->g, x, y, num, 2, st);
            if (pend) grid_put(&a->g, x + 2, y, a->cfg.unicode ? 0x00B7 : '*', d == a->cur ? S_CURSOR : d < a->today ? S_OVERDUE : S_TODO);

            int idx[MAX_DAY_ITEMS];
            int n = day_items(a, d, idx);
            int lines = ch - 1;
            for (int k = 0; k < n && k < lines; k++) {
                if (y + 1 + k >= ymax) break;
                const item_t *it = &a->st->items[idx[k]];
                char lab[512];
                if (k == lines - 1 && n > lines) snprintf(lab, sizeof lab, "+%d more", n - lines + 1);
                else item_label(a, it, lab, sizeof lab, true, d);
                bool sel = d == a->cur && a->sel == k;
                grid_field(&a->g, x, y + 1 + k, lab, cw - 1, item_style(a, it, sel, d), a->cfg.unicode);
            }
        }
    }
}

/* ---------- agenda panel: the cursor day's items and open todos ---------- */

static void todo_note(app_t *a, const item_t *it, char *out, size_t n, daynum_t day)
{
    out[0] = 0;
    if (todo_done_on(it, day)) { snprintf(out, n, "done"); return; }
    bool has_due = it->due >= 0 && !item_is_repeating(it);
    daynum_t deadline = has_due ? it->due : day;
    if (deadline < 0) return;
    long left = deadline - a->today;
    if (left < 0) snprintf(out, n, "overdue %ld d", -left);
    else if (!has_due) return;                       /* scheduled ahead: nothing to say */
    else if (left == 0) snprintf(out, n, "due today");
    else snprintf(out, n, "due in %ld d", left);
}

/* one agenda line: left column (time / box) and right column (title, place, tags, note) */
static void agenda_line(app_t *a, int y, int x, int w, const item_t *it, bool sel, bool with_day, daynum_t day)
{
    char left[64], right[640], note[64] = "";
    style_t st = item_style(a, it, sel, day);
    if (it->kind == ITEM_TODO) {
        snprintf(left, sizeof left, "   %s", todo_box(a, it, day));
        todo_note(a, it, note, sizeof note, day);
    } else if (it->start < 0) {
        snprintf(left, sizeof left, "  all day");
        if (it->until >= 0 && it->until != it->day) {
            char u[32];
            format_day(it->until, u, sizeof u, false);
            snprintf(note, sizeof note, "until %s", u);
        }
    } else {
        int end = it->start + it->dur;
        snprintf(left, sizeof left, "%02d:%02d-%02d:%02d", it->start / 60, it->start % 60, (end / 60) % 24, end % 60);
    }
    char dayp[32] = "";
    if (with_day && day >= 0) { format_day(day, dayp, sizeof dayp, false); strncat(dayp, "  ", sizeof dayp - strlen(dayp) - 1); }
    snprintf(right, sizeof right, "%s%s%s%s%s%s%s%s", dayp, it->title ? it->title : "",
             it->at ? "  @" : "", it->at ? it->at : "",
             it->tags ? "  #" : "", it->tags ? it->tags : "",
             note[0] ? "  \xC2\xB7 " : "", note);
    if (sel) grid_fill(&a->g, x, y, w, 1, ' ', st);
    grid_puts(&a->g, x + 1, y, left, 12, sel ? st : S_DIM);
    grid_field(&a->g, x + 14, y, right, w - 15, st, a->cfg.unicode);
}

/* full attributes of one item, in a box of the given size */
static void draw_details(app_t *a, const item_t *it, int x, int y, int w, int h)
{
    if (w < 20 || h < 2) return;
    int row = y, last = y + h;
    char s[600], ds[64], du[16];
    grid_field(&a->g, x, row++, it->title ? it->title : "", w, S_BOLD, a->cfg.unicode);
    if (row >= last) return;
    if (it->kind == ITEM_EVENT) {
        format_day(it->day, ds, sizeof ds, true);
        if (it->start < 0) {
            if (it->until >= 0 && it->until != it->day) {
                char u[64];
                format_day(it->until, u, sizeof u, true);
                snprintf(s, sizeof s, "all day  %s - %s", ds, u);
            } else snprintf(s, sizeof s, "all day  %s", ds);
        } else {
            int end = it->start + it->dur;
            model_format_dur(it->dur, du, sizeof du);
            snprintf(s, sizeof s, "%s  %02d:%02d-%02d:%02d  (%s)", ds, it->start / 60, it->start % 60, (end / 60) % 24, end % 60, du);
        }
        grid_field(&a->g, x, row++, s, w, S_NORMAL, a->cfg.unicode);
    } else {
        if (it->day >= 0) { format_day(it->day, ds, sizeof ds, true); snprintf(s, sizeof s, "scheduled %s", ds); }
        else snprintf(s, sizeof s, "unscheduled");
        if (it->due >= 0) {
            char dd[64], note[64];
            format_day(it->due, dd, sizeof dd, true);
            todo_note(a, it, note, sizeof note, it->due);
            size_t n = strlen(s);
            snprintf(s + n, sizeof s - n, "  \xC2\xB7  due %s (%s)", dd, note);
        }
        grid_field(&a->g, x, row++, s, w, S_NORMAL, a->cfg.unicode);
        if (row < last) {
            if (it->done_day >= 0) {
                format_day(it->done_day, ds, sizeof ds, true);
                snprintf(s, sizeof s, "done %s %02d:%02d%s%d", ds, it->done_min / 60, it->done_min % 60, "  \xC2\xB7  priority ", it->pri);
            } else snprintf(s, sizeof s, "open  \xC2\xB7  priority %s%d", it->pri ? "" : "none ", it->pri);
            if (!it->pri) snprintf(s, sizeof s, "%s  \xC2\xB7  no priority", it->done_day >= 0 ? "done" : "open");
            grid_field(&a->g, x, row++, s, w, it->done_day >= 0 ? S_DIM : S_NORMAL, a->cfg.unicode);
        }
    }
    if (row < last) {
        const place_t *p = it->at ? store_find_place(a->st, it->at) : NULL;
        if (!it->at && it->day >= 0) p = store_place_for_day(a->st, it->day);
        if (it->at && !p) snprintf(s, sizeof s, "place %s", it->at);
        else if (p) {
            int off = 0;
            tz_offset(place_zone(p), (long long)(it->day >= 0 ? it->day : a->today) * 86400 + 43200, &off);
            snprintf(s, sizeof s, "place %s%s  %s  UTC%+d:%02d", p->name, it->at ? "" : " (day's location)", place_zone(p), off / 60, abs(off % 60));
        } else s[0] = 0;
        if (s[0]) grid_field(&a->g, x, row++, s, w, S_DIM, a->cfg.unicode);
    }
    if (row < last && it->rep.freq != REP_NONE) {
        char rs[128], nx[64] = "";
        repeat_format(&it->rep, rs, sizeof rs);
        daynum_t n = item_next_occurrence(it, a->cur);
        if (n >= 0) { char nd[32]; format_day(n, nd, sizeof nd, false); snprintf(nx, sizeof nx, "  \xC2\xB7  next %s", nd); }
        snprintf(s, sizeof s, "repeats %s%s%s", rs, it->nskips ? "  (with skips)" : "", nx);
        grid_field(&a->g, x, row++, s, w, S_NORMAL, a->cfg.unicode);
    }
    if (row < last && it->tags) { snprintf(s, sizeof s, "tags  %s", it->tags); grid_field(&a->g, x, row++, s, w, S_DIM, a->cfg.unicode); }
    if (row < last && it->who) {
        const contact_t *c = contacts_find(a->cs, it->who);
        if (c) snprintf(s, sizeof s, "who   %s%s%s%s%s", c->name, c->phone ? "  \xE2\x98\x8E " : "", c->phone ? c->phone : "", c->email ? "  " : "", c->email ? c->email : "");
        else snprintf(s, sizeof s, "who   %s", it->who);
        grid_field(&a->g, x, row++, s, w, c ? S_NORMAL : S_DIM, a->cfg.unicode);
    }
    if (it->note) {
        const char *n = it->note;
        while (*n && row < last) {
            const char *nl = strchr(n, '\n');
            size_t len = nl ? (size_t)(nl - n) : strlen(n);
            snprintf(s, sizeof s, "%.*s", (int)(len < sizeof s - 1 ? len : sizeof s - 1), n);
            grid_field(&a->g, x, row++, s, w, S_NORMAL, a->cfg.unicode);
            n += len + (nl ? 1 : 0);
        }
    }
    if (row < last) { snprintf(s, sizeof s, "id %s", it->id); grid_field(&a->g, x, row, s, w, S_DIM, a->cfg.unicode); }
}

static void draw_agenda(app_t *a, int y0, int h)
{
    if (h < 2) return;
    int y = y0;
    char head[128], ds[64];
    format_day(a->cur, ds, sizeof ds, true);
    snprintf(head, sizeof head, " agenda %s ", ds);
    for (int x = 0; x < a->W; x++) grid_put(&a->g, x, y, a->cfg.unicode ? 0x2500 : '-', S_RULE);
    grid_puts(&a->g, 2, y, head, a->W - 4, S_BOLD);
    if (a->itemmode) grid_puts(&a->g, a->W - 44, y, " items: j/k move  Enter edit  Esc leave ", 42, S_DIM);
    y++;

    int si = selected(a);
    int last = y0 + h;
    int lx = 0, lw = a->W;
    /* details: beside the list on wide terminals, below it otherwise */
    if (si >= 0) {
        if (a->W >= 110) {
            lw = a->W / 2 - 1;
            int dx = lw + 2;
            for (int j = y; j < last; j++) grid_put(&a->g, lw, j, a->cfg.unicode ? 0x2502 : '|', S_RULE);
            draw_details(a, &a->st->items[si], dx, y, a->W - dx - 1, last - y);
        } else {
            int dh = (last - y) / 2 < 6 ? (last - y) / 2 : 6;
            if (dh >= 2) {
                int dy = last - dh;
                for (int x = 0; x < a->W; x++) grid_put(&a->g, x, dy, a->cfg.unicode ? 0x2504 : '-', S_RULE);
                draw_details(a, &a->st->items[si], 2, dy + 1, a->W - 3, dh - 1);
                last = dy;
            }
        }
    }

    int idx[MAX_DAY_ITEMS];
    int n = day_items(a, a->cur, idx);
    if (n == 0 && y < last) { grid_puts(&a->g, lx + 2, y, "nothing on this day", lw - 2, S_DIM); y++; }
    for (int k = 0; k < n && y < last; k++, y++)
        agenda_line(a, y, lx, lw, &a->st->items[idx[k]], a->sel == k, false, a->cur);

    /* open todos from other days: overdue, carried over, or unscheduled without a deadline;
     * a repeating todo contributes its latest missed occurrence */
    int shown = 0;
    for (int i = 0; i < a->st->nitems && y < last; i++) {
        const item_t *it = &a->st->items[i];
        if (it->kind != ITEM_TODO) continue;
        daynum_t day;
        if (item_is_repeating(it)) {
            day = todo_last_open_before(it, a->today);
            if (day < 0 || day == a->cur) continue;
        } else {
            if (it->done_day >= 0) continue;
            bool on_day = false;
            for (int k = 0; k < n; k++) if (idx[k] == i) on_day = true;
            if (on_day) continue;
            bool open = (it->day >= 0 && it->day < a->today) || (it->due >= 0 && it->due < a->today);
            bool undated = it->day < 0 && it->due < 0;
            if (!open && !undated) continue;
            day = it->day >= 0 ? it->day : it->due;
        }
        if (shown == 0) {
            if (y + 1 >= last) break;
            grid_puts(&a->g, lx + 1, y, "open todos", lw - 2, S_LABEL);
            y++;
        }
        agenda_line(a, y, lx, lw, it, false, true, day);
        y++;
        shown++;
    }

    /* soon: the following days, as far as the panel reaches */
    bool header = false;
    for (daynum_t d = a->cur + 1; d <= a->cur + 120 && y < last; d++) {
        int di[MAX_DAY_ITEMS];
        int dn = day_items(a, d, di);
        if (dn == 0) continue;
        if (!header) {
            if (y + 2 >= last) break;
            grid_puts(&a->g, lx + 1, y, "soon", lw - 2, S_LABEL);
            y++;
            header = true;
        }
        for (int k = 0; k < dn && y < last; k++) {
            const item_t *it = &a->st->items[di[k]];
            if (it->kind == ITEM_TODO && todo_done_on(it, d)) continue;
            if (it->kind == ITEM_EVENT && !item_occurs_on(it, d)) continue;     /* multi-day: list once, on its first day */
            agenda_line(a, y, lx, lw, it, false, true, d);
            y++;
        }
    }
}

/* details box for the week and day views (they have no agenda panel) */
static void draw_details_overlay(app_t *a)
{
    int si = selected(a);
    if (si < 0) return;
    int w = a->W / 2 > 44 ? a->W / 2 : (a->W - 2 < 44 ? a->W - 2 : 44);
    int h = 11;
    int x = a->W - w - 1, y = STATUS_Y(a) - h;
    if (x < 0) x = 0;
    if (y < TOP) y = TOP;
    grid_fill(&a->g, x, y, w, h, ' ', S_NORMAL);
    grid_box(&a->g, x, y, w, h, S_BOX, a->cfg.unicode);
    grid_puts(&a->g, x + 2, y, " item ", 6, S_BOX);
    draw_details(a, &a->st->items[si], x + 2, y + 1, w - 4, h - 2);
    grid_puts(&a->g, x + 2, y + h - 1, " j/k move  Enter edit  Esc ", w - 4, S_DIM);
}

/* ---------- week and day views (time slots) ---------- */

typedef struct { int idx, lane; } laned_t;

static int assign_lanes(app_t *a, const int *idx, int n, laned_t *out, int *nout)
{
    int lane_end[16], nl = 0, k = 0;
    for (int i = 0; i < n; i++) {
        const item_t *it = &a->st->items[idx[i]];
        if (it->kind != ITEM_EVENT || it->start < 0) continue;
        int lane = -1;
        for (int l = 0; l < nl; l++) if (lane_end[l] <= it->start) { lane = l; break; }
        if (lane < 0) { if (nl < 16) lane = nl++; else lane = nl - 1; }
        lane_end[lane] = it->start + (it->dur > 0 ? it->dur : a->slot);
        out[k].idx = idx[i];
        out[k].lane = lane;
        k++;
    }
    *nout = k;
    return nl ? nl : 1;
}

static void draw_slots(app_t *a, daynum_t first, int ndays)
{
    const int gutter = 6;
    int cw = (a->W - gutter) / ndays;
    if (cw < 4) cw = 4;
    int allday_rows = ndays == 1 ? 4 : 2;
    if (a->H < 16) allday_rows = 1;

    for (int d = 0; d < ndays; d++) {
        daynum_t day = first + d;
        civil_t c = days_to_civil(day);
        char h[64];
        if (ndays == 1) snprintf(h, sizeof h, "%s %d %s %d", civil_weekday_names[civil_weekday(day)], c.d, civil_month_names[c.m], c.y);
        else snprintf(h, sizeof h, "%.3s %d", civil_weekday_names[civil_weekday(day)], c.d);
        style_t st = day == a->cur ? S_CURSOR : day == a->today ? S_TODAY : civil_weekday(day) >= 5 ? S_DIM : S_BOLD;
        grid_field(&a->g, gutter + d * cw, TOP, h, cw - 1, st, a->cfg.unicode);
    }

    int ys = TOP + 1 + allday_rows;
    for (int d = 0; d < ndays; d++) {
        daynum_t day = first + d;
        int idx[MAX_DAY_ITEMS];
        int n = day_items(a, day, idx);
        int row = 0;
        for (int k = 0; k < n; k++) {
            const item_t *it = &a->st->items[idx[k]];
            if (it->kind == ITEM_EVENT && it->start >= 0) continue;
            if (row >= allday_rows) break;
            char lab[512];
            item_label(a, it, lab, sizeof lab, false, day);
            int untimed_left = 0;
            for (int j = k + 1; j < n; j++) if (a->st->items[idx[j]].kind == ITEM_TODO || a->st->items[idx[j]].start < 0) untimed_left++;
            if (row == allday_rows - 1 && untimed_left > 0) snprintf(lab, sizeof lab, "+%d more", untimed_left + 1);
            bool sel = day == a->cur && a->sel == k;
            grid_field(&a->g, gutter + d * cw, TOP + 1 + row, lab, cw - 1, item_style(a, it, sel, day), a->cfg.unicode);
            row++;
        }
    }

    int nrows = STATUS_Y(a) - ys;
    if (nrows < 1) return;
    int nslots = 1440 / a->slot;
    int cs = a->cur_min / a->slot;
    if (cs < a->tscroll) a->tscroll = cs;
    if (cs >= a->tscroll + nrows) a->tscroll = cs - nrows + 1;
    if (a->tscroll > nslots - nrows) a->tscroll = nslots - nrows;
    if (a->tscroll < 0) a->tscroll = 0;

    bool today_shown = a->today >= first && a->today < first + ndays;
    for (int i = 0; i < nrows; i++) {
        int s = a->tscroll + i;
        if (s >= nslots) break;
        int minute = s * a->slot;
        int y = ys + i;
        char lab[16] = "";
        if (minute % 60 == 0) snprintf(lab, sizeof lab, "%02d:00", minute / 60);
        else if (a->slot <= 30 && minute % 30 == 0) snprintf(lab, sizeof lab, "  :30");
        bool is_now_row = today_shown && a->now_min >= minute && a->now_min < minute + a->slot;
        grid_puts(&a->g, 0, y, lab, 5, is_now_row ? S_NOW : in_work_hours(a, minute) ? S_NORMAL : S_GUTTER);
        if (is_now_row) grid_put(&a->g, 5, y, a->cfg.unicode ? 0x25B6 : '>', S_NOW);
        if (minute % 60 == 0)
            for (int x = gutter; x < a->W; x++) grid_put(&a->g, x, y, a->cfg.unicode ? 0x2508 : '.', S_DIM);
    }

    for (int d = 0; d < ndays; d++) {
        daynum_t day = first + d;
        int idx[MAX_DAY_ITEMS];
        int n = day_items(a, day, idx);
        laned_t ln[MAX_DAY_ITEMS];
        int nln = 0, nlanes = 1;
        if (ndays == 1) nlanes = assign_lanes(a, idx, n, ln, &nln);
        else for (int k = 0; k < n; k++) {
            const item_t *it = &a->st->items[idx[k]];
            if (it->kind == ITEM_EVENT && it->start >= 0) { ln[nln].idx = idx[k]; ln[nln].lane = 0; nln++; }
        }
        int lw = (cw - 1) / nlanes;
        if (lw < 3) lw = 3;
        for (int k = 0; k < nln; k++) {
            const item_t *it = &a->st->items[ln[k].idx];
            int r0 = it->start / a->slot;
            int r1 = (it->start + (it->dur > 0 ? it->dur : a->slot) - 1) / a->slot;
            int x = gutter + d * cw + ln[k].lane * lw;
            int w = ln[k].lane == nlanes - 1 ? (cw - 1) - ln[k].lane * lw : lw - 1;
            int selk = -1;
            for (int j = 0; j < n; j++) if (idx[j] == ln[k].idx) selk = j;
            bool sel = day == a->cur && a->sel == selk;
            style_t st = sel ? S_EVENTSEL : S_EVENT;
            bool labeled = false;
            for (int r = r0; r <= r1; r++) {
                int i = r - a->tscroll;
                if (i < 0 || i >= nrows) continue;
                int y = ys + i;
                grid_fill(&a->g, x, y, w, 1, ' ', st);
                if (!labeled) {
                    char lab[512];
                    item_label(a, it, lab, sizeof lab, true, day);
                    grid_field(&a->g, x, y, lab, w, st, a->cfg.unicode);
                    labeled = true;
                }
            }
        }
    }

    int ci = cs - a->tscroll;
    if (ci >= 0 && ci < nrows) {
        int d = (int)(a->cur - first);
        if (d >= 0 && d < ndays) {
            int x = gutter + d * cw;
            for (int i = 0; i < cw - 1 && x + i < a->W; i++) {
                cell_t *c = &a->g.cells[(ys + ci) * a->g.w + x + i];
                if (c->cp != GRID_CONT) c->st = S_CURSOR;
            }
        }
    }
}

/* ---------- overlays ---------- */

static void sun_line(app_t *a, int x, int y, int w, const char *label, double t, sun_state_t st, int off)
{
    char s[64];
    int hh, mm, dd;
    if (st == SUN_ALWAYS_ABOVE) snprintf(s, sizeof s, "%-15s  above horizon", label);
    else if (st == SUN_ALWAYS_BELOW) snprintf(s, sizeof s, "%-15s  below horizon", label);
    else {
        sun_local_hhmm(t, off, &hh, &mm, &dd);
        snprintf(s, sizeof s, "%-15s  %02d:%02d%s", label, hh, mm, dd > 0 ? " +1d" : dd < 0 ? " -1d" : "");
    }
    grid_field(&a->g, x, y, s, w, S_NORMAL, a->cfg.unicode);
}

static void draw_sun_panel(app_t *a)
{
    const int w = 40, h = 17;
    int x = a->W - w - 1, y = TOP;
    if (x < 0) x = 0;
    const place_t *p = store_place_for_day(a->st, a->cur);
    grid_fill(&a->g, x, y, w, h, ' ', S_NORMAL);
    grid_box(&a->g, x, y, w, h, S_PANEL, a->cfg.unicode);
    char s[128];
    civil_t c = days_to_civil(a->cur);
    snprintf(s, sizeof s, " Sun · %d %s %d ", c.d, civil_month_names[c.m], c.y);
    grid_puts(&a->g, x + 2, y, s, w - 4, S_PANEL);
    if (!p) { grid_puts(&a->g, x + 2, y + 2, "no location for this day (L / H)", w - 4, S_ERR); return; }
    int off = day_offset(a, a->cur);
    sun_t sun;
    sun_compute(a->cur, p->lat, p->lon, &sun);
    grid_field(&a->g, x + 2, y + 2, p->name, w - 4, S_BOLD, a->cfg.unicode);
    snprintf(s, sizeof s, "%.4f %.4f  %s", p->lat, p->lon, place_zone(p));
    grid_field(&a->g, x + 2, y + 3, s, w - 4, S_DIM, a->cfg.unicode);
    snprintf(s, sizeof s, "UTC%+d:%02d", off / 60, abs(off % 60));
    grid_field(&a->g, x + 2, y + 4, s, w - 4, S_NORMAL, a->cfg.unicode);
    int r = y + 6;
    sun_line(a, x + 2, r++, w - 4, "astro dawn", sun.astro_dawn, sun.astro, off);
    sun_line(a, x + 2, r++, w - 4, "nautical dawn", sun.nautical_dawn, sun.nautical, off);
    sun_line(a, x + 2, r++, w - 4, "civil dawn", sun.civil_dawn, sun.civil, off);
    sun_line(a, x + 2, r++, w - 4, "sunrise", sun.rise, sun.rise_set, off);
    sun_line(a, x + 2, r++, w - 4, "solar noon", sun.noon, SUN_NORMAL, off);
    sun_line(a, x + 2, r++, w - 4, "sunset", sun.set, sun.rise_set, off);
    sun_line(a, x + 2, r++, w - 4, "civil dusk", sun.civil_dusk, sun.civil, off);
    sun_line(a, x + 2, r++, w - 4, "nautical dusk", sun.nautical_dusk, sun.nautical, off);
    sun_line(a, x + 2, r++, w - 4, "astro dusk", sun.astro_dusk, sun.astro, off);
    snprintf(s, sizeof s, "day length %dh%02dm", (int)(sun.day_length / 60), (int)sun.day_length % 60);
    grid_field(&a->g, x + 2, r, s, w - 4, S_BOLD, a->cfg.unicode);
}

static void draw_help(app_t *a)
{
    static const char *const lines[] = {
        "h j k l / arrows   move by day (by time slot in week and day views)",
        ". t               today        [ ]  week     { }  month     ( )  year",
        "+ Enter  - Backspace   zoom in / out:  year > 3 months > month > week > day",
        "n p  PgDn PgUp     next / previous period of the current view",
        "g                  go to a date: 14.9.  sep 14  +3w  fri  next mon  w38  eom",
        "Tab                into the day's items: j/k move, Enter edits, Esc leaves",
        "a  T               new event / new todo: a form where arrows move on a",
        "                   calendar for dates and a strip of slots for times",
        "e x m d            edit / done / move / delete the selected item",
        "s                  sun and twilight panel for the day's location",
        "L  H               location for a range of days (from/until) / home",
        "z                  cycle slot size 5 10 15 30 60 min",
        "w  q  Q            save / save and quit / quit without saving",
        "v  V               sheets: days (slots x days), locations by date, todos,",
        "                   events, places, contacts; arrows move, Enter edits/creates",
        "C                  the contacts sheet directly (a adds, Enter edits a cell, e the row)",
        "/                  search items and contacts (Enter jumps to the hit)",
        "M                  show / hide the key menu at the bottom",
        "?  Esc             this help / close panel or deselect",
    };
    int n = (int)(sizeof lines / sizeof lines[0]);
    int w = 78 < a->W - 2 ? 78 : a->W - 2, h = n + 4;
    int x = (a->W - w) / 2, y = (a->H - h) / 2;
    if (y < TOP) y = TOP;
    grid_fill(&a->g, x, y, w, h, ' ', S_NORMAL);
    grid_box(&a->g, x, y, w, h, S_BOX, a->cfg.unicode);
    grid_puts(&a->g, x + 2, y, " keys ", 6, S_BOX);
    for (int i = 0; i < n && y + 2 + i < STATUS_Y(a); i++) grid_field(&a->g, x + 2, y + 2 + i, lines[i], w - 4, S_NORMAL, a->cfg.unicode);
}

/* ---------- full redraw ---------- */

static void draw_table(app_t *a);
static void draw_slots_sheet(app_t *a);
static void draw_dayloc_sheet(app_t *a);
static void draw_search_sheet(app_t *a);

static void draw_all(app_t *a)
{
    grid_clear(&a->g, S_NORMAL);
    if (a->table != TBL_NONE) {
        if (a->table == TBL_SLOTS) draw_slots_sheet(a);
        else if (a->table == TBL_SEARCH) draw_search_sheet(a);
        else if (a->table == TBL_DAYLOC) draw_dayloc_sheet(a);
        else draw_table(a);
        if (a->show_help) draw_help(a);
        draw_title(a);
        draw_infobar(a);
        draw_status(a);
        draw_menu(a, a->table == TBL_SLOTS ? MENU_SLOTS : a->table == TBL_DAYLOC ? MENU_DAYLOC : a->table == TBL_SEARCH ? MENU_SEARCH : MENU_TABLE);
        return;
    }
    int cal_h, ag_h;
    layout(a, &cal_h, &ag_h);
    civil_t c = days_to_civil(a->cur);
    switch (a->view) {
    case V_YEAR:    draw_months(a, c.y, 1, 12, cal_h); break;
    case V_QUARTER: {
        int ym = c.y * 12 + c.m - 2;
        draw_months(a, ym / 12, ym % 12 + 1, 3, cal_h);
        break;
    }
    case V_MONTH:   draw_month_big(a, cal_h); break;
    case V_WEEK:    draw_slots(a, wk_start(a, a->cur), 7); break;
    case V_DAY:     draw_slots(a, a->cur, 1); break;
    }
    if (ag_h > 0) draw_agenda(a, TOP + cal_h, ag_h);
    else if (a->itemmode) draw_details_overlay(a);
    if (a->show_sun) draw_sun_panel(a);
    if (a->show_help) draw_help(a);
    draw_title(a);
    draw_infobar(a);
    draw_status(a);
    draw_menu(a, a->itemmode ? MENU_ITEMS : MENU_MAIN);
}

/* ---------- prompts ---------- */

static bool edit_text_key(char *buf, size_t size, int k)
{
    size_t len = strlen(buf);
    if (k == KEY_BACKSPACE || k == KEY_DEL) { if (len) { len = utf8_prev(buf, len); buf[len] = 0; } return true; }
    if (k == KEY_CTRL('u')) { buf[0] = 0; return true; }
    if (k == KEY_CTRL('w')) {
        while (len && buf[len - 1] == ' ') len--;
        while (len && buf[len - 1] != ' ') len = utf8_prev(buf, len);
        buf[len] = 0;
        return true;
    }
    if (k >= 32 && k < KEY_SPECIAL) {
        char u[4];
        int n = utf8_encode((uint32_t)k, u);
        if (len + (size_t)n < size) { memcpy(buf + len, u, (size_t)n); buf[len + (size_t)n] = 0; }
        return true;
    }
    return false;
}

static bool prompt(app_t *a, const char *label, char *buf, size_t size)
{
    for (;;) {
        int y = STATUS_Y(a);
        draw_menu(a, MENU_PROMPT);
        grid_fill(&a->g, 0, y, a->W, 1, ' ', S_NORMAL);
        int lw = grid_puts(&a->g, 0, y, label, a->W - 1, S_BOLD);
        int avail = a->W - lw - 1;
        const char *show = buf;
        while (utf8_swidth(show) > avail && *show) show += utf8_decode(show, &(uint32_t){0});
        int used = grid_puts(&a->g, lw, y, show, avail, S_NORMAL);
        a->g.cur_x = lw + used;
        a->g.cur_y = y;
        a->g.cur_show = true;
        flush(a);
        int k = term_read_key(-1);
        if (k == KEY_RESIZE) { resize(a); draw_all(a); continue; }
        if (k == KEY_EOF) { a->quit = true; return false; }
        if (k == KEY_ENTER) break;
        if (k == KEY_ESC) { a->g.cur_show = false; return false; }
        edit_text_key(buf, size, k);
    }
    a->g.cur_show = false;
    size_t len = strlen(buf);
    while (len && buf[len - 1] == ' ') buf[--len] = 0;
    return true;
}

static bool confirm(app_t *a, const char *question)
{
    int y = STATUS_Y(a);
    draw_menu(a, MENU_CONFIRM);
    grid_fill(&a->g, 0, y, a->W, 1, ' ', S_NORMAL);
    grid_puts(&a->g, 0, y, question, a->W, S_BOLD);
    flush(a);
    for (;;) {
        int k = term_read_key(-1);
        if (k == 'y' || k == 'Y') return true;
        if (k == KEY_RESIZE) { resize(a); draw_all(a); flush(a); continue; }
        if (k == KEY_EOF) { a->quit = true; return false; }
        if (k >= 0) return false;
    }
}

static bool prompt_date(app_t *a, const char *label, daynum_t *out)
{
    char buf[128] = "";
    while (prompt(a, label, buf, sizeof buf)) {
        char err[64];
        if (datejump_parse(buf, a->cur, out, err, sizeof err) == 0) return true;
        set_msg(a, true, "%s", err);
        draw_all(a);
    }
    return false;
}

/* ---------- calendar date picker ---------- */

typedef void (*redraw_fn)(app_t *a, void *ctx);

static bool pick_date(app_t *a, daynum_t *d, redraw_fn behind, void *ctx)
{
    daynum_t cur = *d >= 0 ? *d : a->today;
    for (;;) {
        draw_all(a);
        if (behind) behind(a, ctx);
        a->g.cur_show = false;
        int w = MONTH_BOX_W + 3, h = MONTH_BOX_H + 2;
        int x = (a->W - w) / 2, y = (a->H - h) / 2;
        if (x < 0) x = 0;
        if (y < TOP) y = TOP;
        grid_fill(&a->g, x, y, w, h, ' ', S_NORMAL);
        grid_box(&a->g, x, y, w, h, S_BOX, a->cfg.unicode);
        civil_t c = days_to_civil(cur);
        draw_month_box(a, x + 2, y + 1, c.y, c.m, cur);
        char ds[64];
        format_day(cur, ds, sizeof ds, true);
        grid_puts(&a->g, x + 2, y + h - 1, " Enter · n/p month · t ", w - 4, S_DIM);
        grid_fill(&a->g, 0, STATUS_Y(a), a->W, 1, ' ', S_NORMAL);
        grid_puts(&a->g, 0, STATUS_Y(a), ds, a->W, S_BOLD);
        draw_menu(a, MENU_PICK);
        flush(a);
        int k = term_read_key(1000);
        switch (k) {
        case KEY_NONE: refresh_clock(a); break;
        case KEY_RESIZE: resize(a); break;
        case KEY_EOF: a->quit = true; return false;
        case KEY_ESC: case 'q': return false;
        case KEY_ENTER: case ' ': *d = cur; return true;
        case 'h': case KEY_LEFT: cur--; break;
        case 'l': case KEY_RIGHT: cur++; break;
        case 'k': case KEY_UP: cur -= 7; break;
        case 'j': case KEY_DOWN: cur += 7; break;
        case 'n': case KEY_PGDN: cur = civil_add_months(cur, 1); break;
        case 'p': case KEY_PGUP: cur = civil_add_months(cur, -1); break;
        case 'N': cur = civil_add_years(cur, 1); break;
        case 'P': cur = civil_add_years(cur, -1); break;
        case 't': cur = a->today; break;
        case 'g': { daynum_t g; if (prompt_date(a, "go to: ", &g)) cur = g; break; }
        default: break;
        }
    }
}

/* ---------- places ---------- */

static const char *resolve_place(app_t *a, const char *text)
{
    static char name[128];
    char zone[64];
    double lat, lon;
    if (strchr(text, '|')) {
        if (!model_parse_place_spec(text, name, sizeof name, &lat, &lon, zone, sizeof zone)) {
            set_msg(a, true, "expected: Name | lat lon | Zone/Name");
            return NULL;
        }
        if (!tz_zone_exists(zone)) { set_msg(a, true, "unknown time zone '%s'", zone); return NULL; }
        store_add_place(a->st, name, lat, lon, zone);
        return name;
    }
    place_t *p = store_find_place(a->st, text);
    if (p) { snprintf(name, sizeof name, "%s", p->name); return name; }
    const city_t *c = city_find(text);
    if (c) {
        store_add_place(a->st, c->name, c->lat, c->lon, c->zone);
        snprintf(name, sizeof name, "%s", c->name);
        return name;
    }
    set_msg(a, true, "unknown place '%s' — type 'Name | lat lon | Zone/Name' to define it", text);
    return NULL;
}

/* ---------- forms ----------
 * A form is a column of fields plus a "pane" that shows the control for the
 * focused field: a month calendar for dates (arrows move on it), a strip of
 * slots for times, a list of presets for durations, completions for places.
 */

typedef enum { F_TEXT, F_DATE, F_TIME, F_DUR, F_TOGGLE, F_PRI, F_CHOICE } ftype_t;

typedef struct {
    const char *label;
    ftype_t type;
    char text[512];   /* F_TEXT */
    daynum_t day;     /* F_DATE; -1 = none */
    int val;          /* F_TIME / F_DUR minutes, F_TOGGLE 0/1, F_PRI 0..5 */
    bool optional;    /* F_DATE may be cleared with '-' */
    bool enabled;     /* greyed out when false (time of an all-day event) */
    bool complete;    /* F_TEXT: complete against places and cities */
    int choice;       /* F_CHOICE: highlighted row in the candidate list */
    bool contacts;    /* F_CHOICE: candidates are contacts instead of places */
    const char *const *list; int nlist;   /* F_CHOICE: explicit candidates instead */
    char typed[8];    /* digits being typed into a time/duration field */
} field_t;

typedef struct {
    const char *title;
    field_t f[12];
    int n, focus;
    bool require_title;     /* Enter refuses an empty first field (default for item forms) */
} form_t;

#define PANE_W 27
#define PANE_H 10
#define FIELDS_W 44

static field_t *form_add(form_t *fm, const char *label, ftype_t type)
{
    field_t *f = &fm->f[fm->n++];
    memset(f, 0, sizeof *f);
    f->label = label;
    f->type = type;
    f->day = -1;
    f->enabled = true;
    return f;
}

static const int dur_presets[] = { 15, 30, 45, 60, 90, 120, 180, 240, 360, 480, 1440 };
#define NDUR ((int)(sizeof dur_presets / sizeof dur_presets[0]))

static void field_value(app_t *a, const field_t *f, char *out, size_t n)
{
    const char *none = a->cfg.unicode ? "\xE2\x80\x94 none" : "- none";
    switch (f->type) {
    case F_TEXT: snprintf(out, n, "%s", f->text); break;
    case F_DATE:
        if (f->day < 0) snprintf(out, n, "%s", none);
        else {
            char ds[64];
            format_day(f->day, ds, sizeof ds, true);
            snprintf(out, n, "%s%s", ds, f->day == a->today ? "  (today)" : "");
        }
        break;
    case F_TIME:
        if (f->typed[0]) snprintf(out, n, "%s_", f->typed);
        else snprintf(out, n, "%02d:%02d", f->val / 60, f->val % 60);
        break;
    case F_DUR:
        if (f->typed[0]) snprintf(out, n, "%s_", f->typed);
        else model_format_dur(f->val, out, n);
        break;
    case F_TOGGLE: snprintf(out, n, "%s", f->val ? "yes" : "no"); break;
    case F_PRI: if (f->val) snprintf(out, n, "%d", f->val); else snprintf(out, n, "%s", none); break;
    case F_CHOICE: snprintf(out, n, "%s", f->text); break;
    }
}

static const char *field_hint(const field_t *f)
{
    switch (f->type) {
    case F_TEXT:   return f->complete ? "type a place; the list completes it" : "type the text · Ctrl-U clears";
    case F_DATE:   return f->optional ? "arrows move on the calendar · n/p month · t today · - none"
                                      : "arrows move on the calendar · n/p month · t today";
    case F_TIME:   return "up/down by slot · left/right by hour · n now";
    case F_DUR:    return "up/down presets · left/right by hour · or type 1h30";
    case F_TOGGLE: return "Space toggles";
    case F_PRI:    return "left/right or 1-5 · 0 none";
    case F_CHOICE: return f->contacts ? "up/down pick a contact · type to narrow · Ctrl-U none" : f->optional ? "up/down pick a place · type to narrow · Ctrl-U none" : "up/down pick a place · type to narrow · or Name | lat lon | Zone";
    }
    return "";
}

/* completion candidates for a place field: store places first, then the city table */
static int place_matches(app_t *a, const char *text, const char **out, int max)
{
    int n = 0;
    size_t len = strlen(text);
    for (int i = 0; i < a->st->nplaces && n < max; i++) {
        const char *nm = a->st->places[i].name;
        bool ok = true;
        for (size_t k = 0; k < len; k++) if (tolower((unsigned char)nm[k]) != tolower((unsigned char)text[k])) { ok = false; break; }
        if (ok) out[n++] = nm;
    }
    for (int i = 0; i < ncities && n < max; i++) {
        const char *nm = cities[i].name;
        bool ok = true, dup = false;
        for (size_t k = 0; k < len; k++) if (tolower((unsigned char)nm[k]) != tolower((unsigned char)text[k])) { ok = false; break; }
        for (int j = 0; j < n; j++) if (!strcmp(out[j], nm)) dup = true;
        if (ok && !dup) out[n++] = nm;
    }
    return n;
}

static int contact_matches(app_t *a, const char *text, const char **out, int max)
{
    int n = 0;
    for (int i = 0; i < a->cs->n && n < max; i++)
        if (a->cs->c[i].name && str_iprefix(text, a->cs->c[i].name)) out[n++] = a->cs->c[i].name;
    for (int i = 1; i < n; i++) {                 /* by name */
        const char *v = out[i]; int j = i - 1;
        while (j >= 0 && strcmp(out[j], v) > 0) { out[j + 1] = out[j]; j--; }
        out[j + 1] = v;
    }
    return n;
}

static int choice_matches(app_t *a, const field_t *f, const char **out, int max)
{
    if (f->list) {
        int n = 0;
        for (int i = 0; i < f->nlist && n < max; i++) if (str_iprefix(f->text, f->list[i])) out[n++] = f->list[i];
        return n;
    }
    return f->contacts ? contact_matches(a, f->text, out, max) : place_matches(a, f->text, out, max);
}

static void draw_pane(app_t *a, form_t *fm, int x, int y, int w, int h)
{
    field_t *f = &fm->f[fm->focus];
    grid_fill(&a->g, x, y, w, h, ' ', S_NORMAL);
    switch (f->type) {
    case F_DATE: {
        daynum_t d = f->day >= 0 ? f->day : a->today;
        civil_t c = days_to_civil(d);
        draw_month_box(a, x + 2, y, c.y, c.m, f->day);
        if (f->day < 0) grid_puts(&a->g, x + 2, y + 8, "none: arrows set a date", w - 2, S_DIM);
        break;
    }
    case F_TIME: {
        int rows = h - 1 < 9 ? h - 1 : 9;
        int cs = f->val / a->slot;
        int first = cs - rows / 2;
        int nslots = 1440 / a->slot;
        if (first < 0) first = 0;
        if (first > nslots - rows) first = nslots - rows;
        if (first < 0) first = 0;
        for (int i = 0; i < rows; i++) {
            int m = (first + i) * a->slot;
            if (m >= 1440) break;
            char t[16];
            snprintf(t, sizeof t, "%02d:%02d", m / 60, m % 60);
            bool cur = first + i == cs;
            grid_puts(&a->g, x + 6, y + i, t, 5, cur ? S_FOCUS : S_NORMAL);
            if (cur) grid_put(&a->g, x + 4, y + i, a->cfg.unicode ? 0x25B6 : '>', S_NOW);
        }
        break;
    }
    case F_DUR: {
        for (int i = 0; i < NDUR && i < h; i++) {
            char t[16];
            model_format_dur(dur_presets[i], t, sizeof t);
            bool cur = dur_presets[i] == f->val;
            int col = i < 6 ? 0 : 1;
            int row = i < 6 ? i : i - 6;
            grid_puts(&a->g, x + 6 + col * 10, y + row, t, 8, cur ? S_FOCUS : S_NORMAL);
            if (cur) grid_put(&a->g, x + 4 + col * 10, y + row, a->cfg.unicode ? 0x25B6 : '>', S_NOW);
        }
        bool preset = false;
        for (int i = 0; i < NDUR; i++) if (dur_presets[i] == f->val) preset = true;
        if (!preset) { char t[32]; model_format_dur(f->val, t, sizeof t); grid_puts(&a->g, x + 4, y + 7, t, w - 4, S_FOCUS); }
        break;
    }
    case F_PRI: {
        const char *labels[6] = { "none", "1", "2", "3", "4", "5" };
        int px = x + 3;
        for (int i = 0; i < 6; i++) {
            int lw = (int)strlen(labels[i]);
            grid_puts(&a->g, px, y + 1, labels[i], lw, i == f->val ? S_FOCUS : S_NORMAL);
            px += lw + 2;
        }
        grid_puts(&a->g, x + 3, y + 3, "1 is the most urgent", w - 3, S_DIM);
        break;
    }
    case F_TEXT:
        if (f->complete) {
            const char *m[10];
            int n = place_matches(a, f->text, m, 9);
            if (n == 0) grid_puts(&a->g, x + 2, y, "no match: new place, use", w - 2, S_DIM);
            if (n == 0) grid_puts(&a->g, x + 2, y + 1, "Name | lat lon | Zone", w - 2, S_DIM);
            for (int i = 0; i < n && i < h; i++) grid_field(&a->g, x + 2, y + i, m[i], w - 2, i == 0 ? S_FOCUS : S_NORMAL, a->cfg.unicode);
        }
        break;
    case F_CHOICE: {
        static const char *m[256];
        int n = choice_matches(a, f, m, 256);
        if (n == 0) {
            if (f->contacts) { grid_puts(&a->g, x + 2, y, "no such contact; the name is", w - 2, S_DIM); grid_puts(&a->g, x + 2, y + 1, "kept as typed (v: contacts table)", w - 2, S_DIM); }
            else { grid_puts(&a->g, x + 2, y, "no match. new place:", w - 2, S_DIM); grid_puts(&a->g, x + 2, y + 1, "Name | lat lon | Zone/Name", w - 2, S_DIM); }
            break;
        }
        int rows = h;
        int first = f->choice - rows / 2;
        if (first > n - rows) first = n - rows;
        if (first < 0) first = 0;
        for (int i = 0; i < rows && first + i < n; i++) {
            bool cur = first + i == f->choice;
            bool own = f->contacts || f->list || store_find_place(a->st, m[first + i]) != NULL;
            grid_field(&a->g, x + 2, y + i, m[first + i], w - 3, cur ? S_FOCUS : own ? S_BOLD : S_NORMAL, a->cfg.unicode);
            if (cur) grid_put(&a->g, x, y + i, a->cfg.unicode ? 0x25B6 : '>', S_NOW);
        }
        if (n > rows) { char c[32]; snprintf(c, sizeof c, "%d/%d", f->choice + 1, n); grid_puts(&a->g, x + w - (int)strlen(c) - 1, y + rows - 1, c, (int)strlen(c), S_DIM); }
        break;
    }
    case F_TOGGLE:
        break;
    }
}

/* pane placement: right of the fields when wide enough, below them when tall enough, else none */
static void form_geometry(app_t *a, form_t *fm, int *x, int *y, int *w, int *h, int *px, int *py, bool *has_pane)
{
    bool side = a->W - 2 >= FIELDS_W + PANE_W + 5;
    bool below = !side && a->H - TOP - 1 >= fm->n + PANE_H + 6;
    *has_pane = side || below;
    if (side) { *w = FIELDS_W + PANE_W + 5; *h = (fm->n > PANE_H ? fm->n : PANE_H) + 5; }
    else if (below) { *w = FIELDS_W + 4 < a->W - 2 ? FIELDS_W + 4 : a->W - 2; *h = fm->n + PANE_H + 6; }
    else { *w = FIELDS_W + 4 < a->W - 2 ? FIELDS_W + 4 : a->W - 2; *h = fm->n + 5; }
    if (*w < 20) *w = a->W;
    *x = (a->W - *w) / 2;
    *y = (a->H - *h) / 2;
    if (*x < 0) *x = 0;
    if (*y < TOP) *y = TOP;
    if (side) { *px = *x + 2 + FIELDS_W + 1; *py = *y + 2; }
    else { *px = *x + 2; *py = *y + 2 + fm->n + 1; }
}

static void draw_form(app_t *a, void *ctx)
{
    form_t *fm = ctx;
    int x, y, w, h, px, py;
    bool has_pane;
    form_geometry(a, fm, &x, &y, &w, &h, &px, &py, &has_pane);
    grid_fill(&a->g, x, y, w, h, ' ', S_NORMAL);
    grid_box(&a->g, x, y, w, h, S_BOX, a->cfg.unicode);
    char t[64];
    snprintf(t, sizeof t, " %s ", fm->title);
    grid_puts(&a->g, x + 2, y, t, w - 4, S_BOX);
    int lw = 11;
    int fw = has_pane && px > x + 2 + FIELDS_W ? FIELDS_W : w - 4;
    for (int i = 0; i < fm->n; i++) {
        field_t *f = &fm->f[i];
        int fy = y + 2 + i;
        if (fy >= y + h - 2) break;
        grid_field(&a->g, x + 2, fy, f->label, lw, f->enabled ? S_LABEL : S_DIM, a->cfg.unicode);
        char v[600];
        field_value(a, f, v, sizeof v);
        bool focus = i == fm->focus;
        style_t st = !f->enabled ? S_DIM : focus ? S_FOCUS : S_FIELD;
        int vw = fw - lw;
        if (f->type == F_TEXT) {
            const char *show = v;
            while (utf8_swidth(show) > vw - 1 && *show) show += utf8_decode(show, &(uint32_t){0});
            grid_fill(&a->g, x + 2 + lw, fy, vw, 1, ' ', st);
            int used = grid_puts(&a->g, x + 2 + lw, fy, show, vw, st);
            if (focus) { a->g.cur_x = x + 2 + lw + used; a->g.cur_y = fy; a->g.cur_show = true; }
        } else {
            int vlen = utf8_swidth(v) + 2;
            if (vlen > vw) vlen = vw;
            grid_fill(&a->g, x + 2 + lw, fy, vlen, 1, ' ', st);
            grid_puts(&a->g, x + 3 + lw, fy, v, vlen - 2, st);
        }
    }
    if (has_pane) {
        if (px > x + 2 + FIELDS_W) for (int j = y + 1; j < y + h - 1; j++) grid_put(&a->g, px - 1, j, a->cfg.unicode ? 0x2502 : '|', S_BOX);
        draw_pane(a, fm, px, py, PANE_W, PANE_H);
    }
    grid_field(&a->g, x + 2, y + h - 2, field_hint(&fm->f[fm->focus]), w - 4, S_DIM, a->cfg.unicode);
    grid_fill(&a->g, 0, STATUS_Y(a), a->W, 1, ' ', S_NORMAL);
    draw_menu(a, MENU_FORM);
}

static void form_after_change(form_t *fm)
{
    for (int i = 0; i < fm->n; i++) {
        if (fm->f[i].type != F_TOGGLE) continue;
        for (int j = 0; j < fm->n; j++) {
            if (fm->f[j].type == F_TIME || fm->f[j].type == F_DUR) fm->f[j].enabled = !fm->f[i].val;
            else if (fm->f[j].type == F_DATE && fm->f[j].optional) fm->f[j].enabled = fm->f[i].val != 0;
        }
    }
}

static void typed_time(field_t *f)
{
    int n = (int)strlen(f->typed);
    int v = atoi(f->typed);
    int h, m;
    if (n <= 2) { h = v; m = 0; }
    else { h = v / 100; m = v % 100; }
    if (h > 23) h = 23;
    if (m > 59) m = 59;
    f->val = h * 60 + m;
}

/* replace a partial place name with its first completion */
static void complete_field(app_t *a, field_t *f)
{
    if (f->type == F_CHOICE) {
        static const char *m[256];
        int n = choice_matches(a, f, m, 256);
        if (!f->text[0] && !f->val && f->optional) return;          /* untouched optional choice stays empty */
        if (f->list && !f->val && f->text[0]) { for (int i = 0; i < n; i++) if (!strcmp(m[i], f->text)) return; }   /* preset already exact */
        if (n > 0 && f->choice >= 0 && f->choice < n && !strchr(f->text, '|')) snprintf(f->text, sizeof f->text, "%s", m[f->choice]);
        return;
    }
    if (f->type != F_TEXT || !f->complete || !f->text[0]) return;
    const char *m[2];
    if (place_matches(a, f->text, m, 1) == 1) snprintf(f->text, sizeof f->text, "%s", m[0]);
}

static bool is_adjust_field(const field_t *f)
{
    return f->type == F_TIME || f->type == F_DUR || f->type == F_DATE || f->type == F_PRI || f->type == F_CHOICE;
}

static void form_focus_move(app_t *a, form_t *fm, int dir)
{
    fm->f[fm->focus].typed[0] = 0;
    complete_field(a, &fm->f[fm->focus]);
    for (int guard = 0; guard < fm->n; guard++) {
        fm->focus = (fm->focus + dir + fm->n) % fm->n;
        if (fm->f[fm->focus].enabled) return;
    }
}

static int dur_preset_index(int v)
{
    int best = 0;
    for (int i = 0; i < NDUR; i++) if (dur_presets[i] <= v) best = i;
    return best;
}

static void form_key(app_t *a, form_t *fm, int k)
{
    field_t *f = &fm->f[fm->focus];
    if (k == KEY_TAB || (!is_adjust_field(f) && k == KEY_DOWN)) { form_focus_move(a, fm, +1); return; }
    if (k == KEY_BTAB || (!is_adjust_field(f) && k == KEY_UP)) { form_focus_move(a, fm, -1); return; }

    switch (f->type) {
    case F_TEXT:
        edit_text_key(f->text, sizeof f->text, k);
        return;
    case F_CHOICE: {
        static const char *m[256];
        int n = choice_matches(a, f, m, 256);
        if (k == KEY_DOWN || (k == 'j' && !f->text[0])) { if (f->choice + 1 < n) f->choice++; f->val = 1; return; }
        if (k == KEY_UP || (k == 'k' && !f->text[0])) { if (f->choice > 0) f->choice--; f->val = 1; return; }
        if (k == KEY_PGDN) { f->choice = f->choice + PANE_H < n ? f->choice + PANE_H : (n ? n - 1 : 0); return; }
        if (k == KEY_PGUP) { f->choice = f->choice - PANE_H > 0 ? f->choice - PANE_H : 0; return; }
        if (k == KEY_CTRL('u')) f->val = 1;                 /* an explicit clear counts as touched */
        if (edit_text_key(f->text, sizeof f->text, k)) f->choice = 0;
        return;
    }
    case F_DATE: {
        /* an empty optional date starts from the form's main date, else today */
        daynum_t base = a->today;
        for (int i = 0; i < fm->n; i++) if (fm->f[i].type == F_DATE && fm->f[i].day >= 0) { base = fm->f[i].day; break; }
        daynum_t d = f->day >= 0 ? f->day : base;
        switch (k) {
        case 'h': case KEY_LEFT:  f->day = d - 1; break;
        case 'l': case KEY_RIGHT: f->day = d + 1; break;
        case 'k': case KEY_UP:    f->day = d - 7; break;
        case 'j': case KEY_DOWN:  f->day = d + 7; break;
        case KEY_PGUP: case 'p':  f->day = civil_add_months(d, -1); break;
        case KEY_PGDN: case 'n':  f->day = civil_add_months(d, 1); break;
        case 'P': f->day = civil_add_years(d, -1); break;
        case 'N': f->day = civil_add_years(d, 1); break;
        case 't': f->day = a->today; break;
        case ' ': if (f->day < 0) f->day = d; break;
        case '-': if (f->optional) f->day = -1; break;
        case 'g': { daynum_t g; if (prompt_date(a, "go to: ", &g)) f->day = g; break; }
        default: break;
        }
        return;
    }
    case F_TIME:
        if (k >= '0' && k <= '9') {
            size_t n = strlen(f->typed);
            if (n < 4) { f->typed[n] = (char)k; f->typed[n + 1] = 0; typed_time(f); }
            return;
        }
        if (k == KEY_BACKSPACE) { size_t n = strlen(f->typed); if (n) { f->typed[n - 1] = 0; if (n > 1) typed_time(f); } return; }
        f->typed[0] = 0;
        switch (k) {
        case 'j': case KEY_DOWN:  f->val = (f->val + a->slot) % 1440; break;
        case 'k': case KEY_UP:    f->val = (f->val - a->slot + 1440) % 1440; break;
        case 'l': case KEY_RIGHT: f->val = (f->val + 60) % 1440; break;
        case 'h': case KEY_LEFT:  f->val = (f->val - 60 + 1440) % 1440; break;
        case 'n': f->val = a->now_min - a->now_min % a->slot; break;
        default: break;
        }
        return;
    case F_DUR:
        if ((k >= '0' && k <= '9') || k == 'h' || k == 'm' || k == 'd') {
            size_t n = strlen(f->typed);
            if (n < 6) { f->typed[n] = (char)k; f->typed[n + 1] = 0; }
            int v;
            if (model_parse_dur(f->typed, &v)) f->val = v;
            return;
        }
        if (k == KEY_BACKSPACE) { size_t n = strlen(f->typed); if (n) f->typed[n - 1] = 0; return; }
        f->typed[0] = 0;
        switch (k) {
        case 'j': case KEY_DOWN: { int i = dur_preset_index(f->val); f->val = dur_presets[i + 1 < NDUR ? i + 1 : i]; break; }
        case 'k': case KEY_UP:   { int i = dur_preset_index(f->val); if (dur_presets[i] < f->val) f->val = dur_presets[i]; else f->val = dur_presets[i > 0 ? i - 1 : 0]; break; }
        case 'l': case KEY_RIGHT: f->val += 60; break;
        case KEY_LEFT:            f->val = f->val > 60 ? f->val - 60 : 5; break;
        default: break;
        }
        return;
    case F_TOGGLE:
        if (k == ' ' || k == KEY_LEFT || k == KEY_RIGHT || k == 'h' || k == 'l') f->val = !f->val;
        else if (k == 'y') f->val = 1;
        else if (k == 'n') f->val = 0;
        else return;
        form_after_change(fm);
        return;
    case F_PRI:
        if (k >= '0' && k <= '5') f->val = k - '0';
        else if (k == KEY_RIGHT || k == 'l' || k == KEY_DOWN || k == 'j') { if (f->val < 5) f->val++; }
        else if (k == KEY_LEFT || k == 'h' || k == KEY_UP || k == 'k') { if (f->val > 0) f->val--; }
        return;
    }
}

/* true when the user pressed Enter (title must be non-empty); false on Esc */
static bool run_form(app_t *a, form_t *fm)
{
    form_after_change(fm);
    for (;;) {
        draw_all(a);
        draw_form(a, fm);
        flush(a);
        a->g.cur_show = false;
        int k = term_read_key(1000);
        if (k == KEY_NONE) { refresh_clock(a); continue; }
        if (k == KEY_RESIZE) { resize(a); continue; }
        if (k == KEY_EOF) { a->quit = true; return false; }
        if (k == KEY_ESC) return false;
        if (k == KEY_ENTER) {
            complete_field(a, &fm->f[fm->focus]);
            if (fm->require_title && fm->f[0].type == F_TEXT && !fm->f[0].text[0]) { fm->focus = 0; set_msg(a, true, "a title is needed"); continue; }
            return true;
        }
        a->msg[0] = 0;
        form_key(a, fm, k);
    }
}

/* ---------- item forms ---------- */

static int default_start(app_t *a)
{
    if (a->view >= V_WEEK) return a->cur_min;
    if (a->cur == a->today) return ((a->now_min + a->slot - 1) / a->slot * a->slot) % 1440;
    return 9 * 60;
}

/* make sure a place named in a form exists in the store (adds a city-table entry if needed) */
static void ensure_place(app_t *a, const char *name)
{
    if (!name || !*name || store_find_place(a->st, name)) return;
    const city_t *c = city_find(name);
    if (c) store_add_place(a->st, c->name, c->lat, c->lon, c->zone);
}

static const char *const repeat_presets[] = {
    "none", "daily", "weekdays", "weekly", "every 2 weeks", "monthly", "monthly, same weekday", "yearly", "custom..."
};
#define N_REPEAT_PRESETS 9

/* which preset describes the rule, or "custom..." */
static const char *repeat_preset_of(const repeat_t *r)
{
    if (r->freq == REP_NONE) return repeat_presets[0];
    if (r->count || r->mday) return repeat_presets[8];
    if (r->freq == REP_DAILY && r->interval == 1) return repeat_presets[1];
    if (r->freq == REP_WEEKLY && r->interval == 1 && r->wdays == 0x1f) return repeat_presets[2];
    if (r->freq == REP_WEEKLY && r->interval == 1 && !r->wdays) return repeat_presets[3];
    if (r->freq == REP_WEEKLY && r->interval == 2 && !r->wdays) return repeat_presets[4];
    if (r->freq == REP_MONTHLY && r->interval == 1 && !r->nth) return repeat_presets[5];
    if (r->freq == REP_MONTHLY && r->interval == 1 && r->nth) return repeat_presets[6];
    if (r->freq == REP_YEARLY && r->interval == 1) return repeat_presets[7];
    return repeat_presets[8];
}

static void add_repeat_fields(form_t *fm, const item_t *it, field_t **rep, field_t **until)
{
    *rep = form_add(fm, "Repeat", F_CHOICE);
    (*rep)->list = repeat_presets; (*rep)->nlist = N_REPEAT_PRESETS;
    snprintf((*rep)->text, sizeof (*rep)->text, "%s", repeat_preset_of(&it->rep));
    for (int i = 0; i < N_REPEAT_PRESETS; i++) if (!strcmp(repeat_presets[i], (*rep)->text)) (*rep)->choice = i;
    *until = form_add(fm, "Repeat until", F_DATE);
    (*until)->optional = true;
    (*until)->day = it->rep.until;
}

/* apply the Repeat / Repeat until fields to the item; "custom..." asks for the grammar */
static void apply_repeat_fields(app_t *a, item_t *it, daynum_t day, const field_t *rep, const field_t *until)
{
    const char *v = rep->text;
    repeat_t r;
    memset(&r, 0, sizeof r);
    r.interval = 1;
    r.until = -1;
    if (!strcmp(v, "custom...")) {
        char buf[128];
        repeat_format(&it->rep, buf, sizeof buf);
        if (!buf[0]) snprintf(buf, sizeof buf, "weekly mon wed every 2");
        for (;;) {
            if (!prompt(a, "repeat (daily|weekly [mon..]|monthly [15|2nd wed|last fri]|yearly) [every N] [until DATE|count N]: ", buf, sizeof buf)) return;
            if (!buf[0]) { it->rep.freq = REP_NONE; return; }
            if (repeat_parse(buf, &it->rep)) return;
            set_msg(a, true, "cannot parse that rule");
            draw_all(a);
        }
    }
    if (!strcmp(v, "daily")) r.freq = REP_DAILY;
    else if (!strcmp(v, "weekdays")) { r.freq = REP_WEEKLY; r.wdays = 0x1f; }
    else if (!strcmp(v, "weekly")) r.freq = REP_WEEKLY;
    else if (!strcmp(v, "every 2 weeks")) { r.freq = REP_WEEKLY; r.interval = 2; }
    else if (!strcmp(v, "monthly")) r.freq = REP_MONTHLY;
    else if (!strcmp(v, "monthly, same weekday")) { r.freq = REP_MONTHLY; civil_t c = days_to_civil(day); r.nth = (c.d - 1) / 7 + 1; if (c.d + 7 > civil_days_in_month(c.y, c.m) && r.nth > 4) r.nth = -1; r.nth_wday = civil_weekday(day); }
    else if (!strcmp(v, "yearly")) r.freq = REP_YEARLY;
    else r.freq = REP_NONE;
    r.until = r.freq != REP_NONE ? until->day : -1;
    it->rep = r;
}

static bool event_form(app_t *a, item_t *it, bool is_new, bool allday_default)
{
    form_t fm;
    memset(&fm, 0, sizeof fm);
    fm.title = is_new ? "New event" : "Edit event";
    fm.require_title = true;
    field_t *title = form_add(&fm, "Title", F_TEXT);
    field_t *date = form_add(&fm, "Date", F_DATE);
    field_t *allday = form_add(&fm, "All-day", F_TOGGLE);
    field_t *time = form_add(&fm, "Time", F_TIME);
    field_t *dur = form_add(&fm, "Duration", F_DUR);
    field_t *until = form_add(&fm, "Until", F_DATE);
    field_t *at = form_add(&fm, "Place", F_CHOICE);
    field_t *who = form_add(&fm, "Who", F_CHOICE);
    field_t *tags = form_add(&fm, "Tags", F_TEXT);
    field_t *rep, *runtil;
    add_repeat_fields(&fm, it, &rep, &runtil);
    until->optional = true;
    at->optional = true;
    who->optional = true;
    who->contacts = true;
    snprintf(who->text, sizeof who->text, "%s", it->who ? it->who : "");

    snprintf(title->text, sizeof title->text, "%s", it->title ? it->title : "");
    date->day = it->day >= 0 ? it->day : a->cur;
    allday->val = allday_default || (!is_new && it->start < 0);
    time->val = it->start >= 0 ? it->start : default_start(a);
    dur->val = it->dur > 0 ? it->dur : 60;
    until->day = it->until;
    snprintf(at->text, sizeof at->text, "%s", it->at ? it->at : "");
    snprintf(tags->text, sizeof tags->text, "%s", it->tags ? it->tags : "");

    if (!run_form(a, &fm)) return false;

    store_set_str(&it->title, title->text);
    it->day = date->day;
    if (allday->val) { it->start = -1; it->dur = 0; }
    else { it->start = time->val; it->dur = dur->val > 0 ? dur->val : 60; }
    it->until = allday->val && until->day > it->day ? until->day : -1;
    ensure_place(a, at->text);
    store_set_str(&it->at, at->text);
    store_set_str(&it->who, who->text);
    store_set_str(&it->tags, tags->text);
    apply_repeat_fields(a, it, it->day, rep, runtil);
    return true;
}

static bool todo_form(app_t *a, item_t *it, bool is_new)
{
    form_t fm;
    memset(&fm, 0, sizeof fm);
    fm.title = is_new ? "New todo" : "Edit todo";
    fm.require_title = true;
    field_t *title = form_add(&fm, "Title", F_TEXT);
    field_t *date = form_add(&fm, "Scheduled", F_DATE);
    field_t *due = form_add(&fm, "Due", F_DATE);
    field_t *pri = form_add(&fm, "Priority", F_PRI);
    field_t *who = form_add(&fm, "Who", F_CHOICE);
    field_t *tags = form_add(&fm, "Tags", F_TEXT);
    field_t *rep, *runtil;
    add_repeat_fields(&fm, it, &rep, &runtil);
    date->optional = true;
    due->optional = true;
    who->optional = true;
    who->contacts = true;
    snprintf(who->text, sizeof who->text, "%s", it->who ? it->who : "");

    snprintf(title->text, sizeof title->text, "%s", it->title ? it->title : "");
    date->day = is_new ? a->cur : it->day;
    due->day = it->due;
    pri->val = it->pri;
    snprintf(tags->text, sizeof tags->text, "%s", it->tags ? it->tags : "");

    if (!run_form(a, &fm)) return false;

    store_set_str(&it->title, title->text);
    it->day = date->day;
    it->due = due->day;
    it->pri = pri->val;
    store_set_str(&it->who, who->text);
    store_set_str(&it->tags, tags->text);
    if (it->day >= 0) apply_repeat_fields(a, it, it->day, rep, runtil); else it->rep.freq = REP_NONE;
    return true;
}

/* Location form: a place, and the days it applies to. "Until" empty = from this day onward. */
static void location_form(app_t *a, bool home)
{
    form_t fm;
    memset(&fm, 0, sizeof fm);
    fm.title = home ? "Home location" : "Location";
    fm.require_title = true;
    field_t *place = form_add(&fm, "Place", F_CHOICE);
    field_t *from = NULL, *until = NULL;
    if (!home) {
        from = form_add(&fm, "From", F_DATE);
        until = form_add(&fm, "Until", F_DATE);
        until->optional = true;
        from->day = a->cur;
        until->day = a->cur;
    } else if (a->st->home) {
        snprintf(place->text, sizeof place->text, "%s", a->st->home);
    }
    if (!run_form(a, &fm)) return;

    const char *name = resolve_place(a, place->text);
    if (!name) return;
    if (home) {
        store_set_home(a->st, name);
        set_msg(a, false, "home: %s", name);
    } else {
        daynum_t f = from->day, u = until->day;
        if (u >= 0 && u < f) { daynum_t t = f; f = u; u = t; }
        store_set_loc_range(a->st, f, u, name);
        char fs[32], us[32];
        format_day(f, fs, sizeof fs, false);
        if (u >= 0) { format_day(u, us, sizeof us, false); set_msg(a, false, "%s: %s - %s", name, fs, us); }
        else set_msg(a, false, "%s from %s onward", name, fs);
    }
    save(a);
    refresh_clock(a);
}

/* ---------- actions ---------- */

static void item_free_strings(item_t *it)
{
    free(it->title); free(it->at); free(it->tags); free(it->note); free(it->who); free(it->status); free(it->extra); free(it->skips);
    it->title = it->at = it->tags = it->note = it->who = it->status = it->extra = NULL;
    it->skips = NULL; it->nskips = 0;
}

static void add_event(app_t *a)
{
    item_t it;
    item_blank(&it, ITEM_EVENT);
    it.day = a->cur;
    if (!event_form(a, &it, true, false)) { item_free_strings(&it); return; }
    int i = store_add_item(a->st, &it);
    item_free_strings(&it);
    if (i < 0) return;
    a->cur = a->st->items[i].day;
    if (a->st->items[i].start >= 0) a->cur_min = a->st->items[i].start;
    select_store_index(a, i);
    save(a);
}

static void add_todo(app_t *a)
{
    item_t it;
    item_blank(&it, ITEM_TODO);
    it.day = a->cur;
    if (!todo_form(a, &it, true)) { item_free_strings(&it); return; }
    int i = store_add_item(a->st, &it);
    item_free_strings(&it);
    if (i < 0) return;
    if (a->st->items[i].day >= 0) a->cur = a->st->items[i].day;
    else if (a->st->items[i].due >= 0) a->cur = a->st->items[i].due;
    select_store_index(a, i);
    save(a);
}

static void ensure_selection(app_t *a)
{
    if (selected(a) >= 0) return;
    int idx[MAX_DAY_ITEMS];
    if (day_items(a, a->cur, idx) > 0) a->sel = 0;
}

static void edit_selected(app_t *a)
{
    ensure_selection(a);
    int si = selected(a);
    if (si < 0) { set_msg(a, true, "nothing selected on this day"); return; }
    item_t *it = &a->st->items[si];
    item_t copy = *it;                       /* scalar fields; strings re-owned below */
    copy.title = copy.at = copy.tags = copy.note = copy.who = copy.status = copy.extra = NULL;
    store_set_str(&copy.title, it->title);
    store_set_str(&copy.at, it->at);
    store_set_str(&copy.tags, it->tags);
    store_set_str(&copy.who, it->who);
    copy.skips = NULL; copy.nskips = 0;
    bool ok = it->kind == ITEM_EVENT ? event_form(a, &copy, false, false) : todo_form(a, &copy, false);
    if (ok) {
        store_set_str(&it->title, copy.title);
        store_set_str(&it->at, copy.at);
        store_set_str(&it->tags, copy.tags);
        store_set_str(&it->who, copy.who);
        it->day = copy.day; it->start = copy.start; it->dur = copy.dur;
        it->until = copy.until; it->due = copy.due; it->pri = copy.pri; it->rep = copy.rep;
        a->st->dirty = true;
        if (it->day >= 0) a->cur = it->day;
        if (it->start >= 0) a->cur_min = it->start;
        select_store_index(a, si);
        save(a);
    }
    item_free_strings(&copy);
}

static void move_selected(app_t *a)
{
    ensure_selection(a);
    int si = selected(a);
    if (si < 0) { set_msg(a, true, "nothing selected on this day"); return; }
    daynum_t d = a->st->items[si].day >= 0 ? a->st->items[si].day : a->cur;
    if (!pick_date(a, &d, NULL, NULL)) return;
    item_t *it = &a->st->items[si];
    if (it->until >= 0 && it->day >= 0) it->until += d - it->day;
    it->day = d;
    a->st->dirty = true;
    a->cur = d;
    select_store_index(a, si);
    save(a);
}

static void toggle_done(app_t *a)
{
    ensure_selection(a);
    int si = selected(a);
    if (si < 0 || a->st->items[si].kind != ITEM_TODO) { set_msg(a, true, "select a todo first (Tab)"); return; }
    todo_toggle_on(&a->st->items[si], a->cur, a->today, a->now_min);
    a->st->dirty = true;
    save(a);
}

/* delete an item; for a repeating one, ask whether to skip this occurrence or drop the series */
static void delete_item_at(app_t *a, int si, daynum_t day)
{
    item_t *it = &a->st->items[si];
    char q[300];
    if (item_is_repeating(it)) {
        snprintf(q, sizeof q, "'%.120s' repeats: delete (o)nly this occurrence, (a)ll occurrences, or (n)o? ", it->title ? it->title : "");
        int y = STATUS_Y(a);
        grid_fill(&a->g, 0, y, a->W, 1, ' ', S_NORMAL);
        grid_puts(&a->g, 0, y, q, a->W, S_BOLD);
        flush(a);
        for (;;) {
            int k = term_read_key(-1);
            if (k == 'o' || k == 'O') { item_add_skip(it, day); a->st->dirty = true; save(a); return; }
            if (k == 'a' || k == 'A') break;
            if (k == KEY_RESIZE) { resize(a); draw_all(a); flush(a); continue; }
            if (k == KEY_EOF) { a->quit = true; return; }
            if (k >= 0) return;
        }
    } else {
        snprintf(q, sizeof q, "delete '%.200s'? (y/n) ", it->title ? it->title : "");
        if (!confirm(a, q)) return;
    }
    store_delete_item(a->st, si);
    save(a);
}

static void delete_selected(app_t *a)
{
    ensure_selection(a);
    int si = selected(a);
    if (si < 0) { set_msg(a, true, "nothing selected on this day"); return; }
    delete_item_at(a, si, a->cur);
    a->sel = -1;
}

static void set_location(app_t *a, bool home)
{
    location_form(a, home);
}

/* ---------- contact and place forms ---------- */

static bool contact_form(app_t *a, contact_t *c, bool is_new)
{
    form_t fm;
    memset(&fm, 0, sizeof fm);
    fm.title = is_new ? "New contact" : "Edit contact";
    fm.require_title = true;
    field_t *name = form_add(&fm, "Name", F_TEXT);
    field_t *phone = form_add(&fm, "Phone", F_TEXT);
    field_t *email = form_add(&fm, "Email", F_TEXT);
    field_t *place = form_add(&fm, "Place", F_CHOICE);
    field_t *tags = form_add(&fm, "Tags", F_TEXT);
    field_t *note = form_add(&fm, "Note", F_TEXT);
    place->optional = true;
    snprintf(name->text, sizeof name->text, "%s", c->name ? c->name : "");
    snprintf(phone->text, sizeof phone->text, "%s", c->phone ? c->phone : "");
    snprintf(email->text, sizeof email->text, "%s", c->email ? c->email : "");
    snprintf(place->text, sizeof place->text, "%s", c->place ? c->place : "");
    snprintf(tags->text, sizeof tags->text, "%s", c->tags ? c->tags : "");
    snprintf(note->text, sizeof note->text, "%s", c->note ? c->note : "");
    if (!run_form(a, &fm)) return false;
    contact_set(&c->name, name->text);
    contact_set(&c->phone, phone->text);
    contact_set(&c->email, email->text);
    contact_set(&c->place, place->text);
    contact_set(&c->tags, tags->text);
    contact_set(&c->note, note->text);
    return true;
}

static void save_contacts(app_t *a)
{
    if (contacts_save(a->cs) != 0) set_msg(a, true, "contacts save failed: %s (%s)", strerror(errno), a->cs->path);
}

/* ---------- table (spreadsheet) views ---------- */

typedef enum { CT_TEXT, CT_DATE, CT_TIME, CT_DUR, CT_PRI, CT_PLACE, CT_CONTACT, CT_DONE, CT_NUM, CT_ZONE, CT_RO } coltype_t;
typedef struct { const char *name; int width; coltype_t type; } col_t;

static const col_t cols_todos[] = { {"Done", 4, CT_DONE}, {"Title", 0, CT_TEXT}, {"Scheduled", 15, CT_DATE}, {"Due", 15, CT_DATE}, {"Pri", 3, CT_PRI}, {"Who", 16, CT_CONTACT}, {"Tags", 14, CT_TEXT} };
static const col_t cols_events[] = { {"Date", 15, CT_DATE}, {"Time", 5, CT_TIME}, {"Dur", 5, CT_DUR}, {"Title", 0, CT_TEXT}, {"Place", 14, CT_PLACE}, {"Who", 16, CT_CONTACT}, {"Tags", 12, CT_TEXT} };
static const col_t cols_places[] = { {"Name", 0, CT_TEXT}, {"Lat", 10, CT_NUM}, {"Lon", 10, CT_NUM}, {"Zone", 24, CT_ZONE}, {"Used", 5, CT_RO} };
static const col_t cols_contacts[] = { {"Name", 0, CT_TEXT}, {"Phone", 18, CT_TEXT}, {"Email", 24, CT_TEXT}, {"Place", 14, CT_PLACE}, {"Tags", 12, CT_TEXT} };


static const col_t *table_cols(table_t t, int *n)
{
    switch (t) {
    case TBL_TODOS:    *n = 7; return cols_todos;
    case TBL_EVENTS:   *n = 7; return cols_events;
    case TBL_PLACES:   *n = 5; return cols_places;
    case TBL_CONTACTS: *n = 5; return cols_contacts;
    default:           *n = 0; return NULL;
    }
}

/* rows: indices into the underlying array, sorted */
static int *table_rows(app_t *a, int *n)
{
    int cap = a->st->nitems + a->st->nlocs + a->st->nplaces + a->cs->n + 1;
    int *rows = malloc((size_t)cap * sizeof *rows);
    int k = 0;
    if (!rows) { *n = 0; return NULL; }
    switch (a->table) {
    case TBL_TODOS:
    case TBL_EVENTS: {
        item_kind_t kind = a->table == TBL_TODOS ? ITEM_TODO : ITEM_EVENT;
        for (int i = 0; i < a->st->nitems; i++) if (a->st->items[i].kind == kind) rows[k++] = i;
        /* insertion sort: todos by done, then scheduled/due; events by date and time */
        for (int i = 1; i < k; i++) {
            int v = rows[i], j = i - 1;
            const item_t *x = &a->st->items[v];
            while (j >= 0) {
                const item_t *y = &a->st->items[rows[j]];
                long kx, ky;
                if (kind == ITEM_TODO) {
                    kx = (x->done_day >= 0 ? 1L << 40 : 0) + (x->day >= 0 ? x->day : x->due >= 0 ? x->due : (1L << 30));
                    ky = (y->done_day >= 0 ? 1L << 40 : 0) + (y->day >= 0 ? y->day : y->due >= 0 ? y->due : (1L << 30));
                } else {
                    kx = x->day * 1440 + (x->start >= 0 ? x->start : 0);
                    ky = y->day * 1440 + (y->start >= 0 ? y->start : 0);
                }
                if (ky <= kx) break;
                rows[j + 1] = rows[j];
                j--;
            }
            rows[j + 1] = v;
        }
        break;
    }
    case TBL_PLACES:
        for (int i = 0; i < a->st->nplaces; i++) rows[k++] = i;
        break;
    case TBL_CONTACTS:
        for (int i = 0; i < a->cs->n; i++) rows[k++] = i;
        for (int i = 1; i < k; i++) {            /* by name */
            int v = rows[i], j = i - 1;
            const char *x = a->cs->c[v].name ? a->cs->c[v].name : "";
            while (j >= 0 && strcmp(a->cs->c[rows[j]].name ? a->cs->c[rows[j]].name : "", x) > 0) { rows[j + 1] = rows[j]; j--; }
            rows[j + 1] = v;
        }
        break;
    default: break;
    }
    *n = k;
    return rows;
}

/* the occurrence a todo row stands for: the first not-yet-done one for repeating todos */
static daynum_t todo_row_day(const item_t *it)
{
    if (!item_is_repeating(it)) return it->day;
    daynum_t d = it->done_day >= 0 ? item_next_occurrence(it, it->done_day) : it->day;
    return d >= 0 ? d : it->day;
}

static void cell_text(app_t *a, int ref, int col, char *out, size_t n)
{
    out[0] = 0;
    char ds[64];
    switch (a->table) {
    case TBL_TODOS: {
        const item_t *it = &a->st->items[ref];
        switch (col) {
        case 0: snprintf(out, n, "%s", todo_box(a, it, todo_row_day(it))); break;
        case 1: snprintf(out, n, "%s%s", it->title ? it->title : "", item_is_repeating(it) ? "  \xE2\x86\xBB" : ""); break;
        case 2: if (it->day >= 0) format_day(todo_row_day(it), out, n, true); break;
        case 3: if (it->due >= 0) format_day(it->due, out, n, true); break;
        case 4: if (it->pri) snprintf(out, n, "%d", it->pri); break;
        case 5: snprintf(out, n, "%s", it->who ? it->who : ""); break;
        case 6: snprintf(out, n, "%s", it->tags ? it->tags : ""); break;
        }
        break;
    }
    case TBL_EVENTS: {
        const item_t *it = &a->st->items[ref];
        switch (col) {
        case 0: format_day(it->day, out, n, true); break;
        case 1: if (it->start >= 0) snprintf(out, n, "%02d:%02d", it->start / 60, it->start % 60); else snprintf(out, n, "*"); break;
        case 2: if (it->start >= 0) model_format_dur(it->dur, out, n); else if (it->until > it->day) snprintf(out, n, "%ldd", (long)(it->until - it->day + 1)); break;
        case 3: snprintf(out, n, "%s", it->title ? it->title : ""); break;
        case 4: snprintf(out, n, "%s", it->at ? it->at : ""); break;
        case 5: snprintf(out, n, "%s", it->who ? it->who : ""); break;
        case 6: snprintf(out, n, "%s", it->tags ? it->tags : ""); break;
        }
        break;
    }
    case TBL_PLACES: {
        const place_t *p = &a->st->places[ref];
        switch (col) {
        case 0: snprintf(out, n, "%s%s", p->name, a->st->home && !strcmp(p->name, a->st->home) ? "  (home)" : ""); break;
        case 1: snprintf(out, n, "%.4f", p->lat); break;
        case 2: snprintf(out, n, "%.4f", p->lon); break;
        case 3: snprintf(out, n, "%s", place_zone(p)); break;
        case 4: snprintf(out, n, "%d", store_place_refs(a->st, p->name)); break;
        }
        break;
    }
    case TBL_CONTACTS: {
        const contact_t *c = &a->cs->c[ref];
        switch (col) {
        case 0: snprintf(out, n, "%s", c->name ? c->name : ""); break;
        case 1: snprintf(out, n, "%s", c->phone ? c->phone : ""); break;
        case 2: snprintf(out, n, "%s", c->email ? c->email : ""); break;
        case 3: snprintf(out, n, "%s", c->place ? c->place : ""); break;
        case 4: snprintf(out, n, "%s", c->tags ? c->tags : ""); break;
        }
        break;
    }
    default: (void)ds; break;
    }
}

static void draw_table(app_t *a)
{
    int ncol;
    const col_t *cols = table_cols(a->table, &ncol);
    int nrow;
    int *rows = table_rows(a, &nrow);
    if (a->trow >= nrow) a->trow = nrow - 1;
    if (a->trow < 0) a->trow = 0;
    if (a->tcol >= ncol) a->tcol = ncol - 1;
    if (a->tcol < 0) a->tcol = 0;

    /* column widths: the flex column takes what is left, at least 12 */
    int widths[8], fixed = 0, flex = -1;
    for (int c = 0; c < ncol; c++) { widths[c] = cols[c].width; if (cols[c].width == 0) flex = c; else fixed += cols[c].width + 1; }
    if (flex >= 0) { widths[flex] = a->W - 1 - fixed; if (widths[flex] < 12) widths[flex] = 12; }
    /* horizontal scroll so the focused column is visible */
    if (a->tcol < a->tcol0) a->tcol0 = a->tcol;
    for (;;) {
        int x = 1;
        for (int c = a->tcol0; c <= a->tcol; c++) x += widths[c] + 1;
        if (x <= a->W + 1 || a->tcol0 >= a->tcol) break;
        a->tcol0++;
    }

    int y0 = TOP, body = STATUS_Y(a) - y0 - 1;
    if (body < 1) body = 1;
    if (a->trow < a->trow0) a->trow0 = a->trow;
    if (a->trow >= a->trow0 + body) a->trow0 = a->trow - body + 1;
    if (a->trow0 < 0) a->trow0 = 0;

    int x = 1;
    for (int c = a->tcol0; c < ncol && x < a->W; c++) {
        grid_field(&a->g, x, y0, cols[c].name, widths[c], c == a->tcol ? S_TITLEBAR : S_BOLD, a->cfg.unicode);
        x += widths[c] + 1;
    }
    for (int r = 0; r < body; r++) {
        int ri = a->trow0 + r;
        int y = y0 + 1 + r;
        if (ri >= nrow) break;
        bool selrow = ri == a->trow;
        if (selrow) grid_fill(&a->g, 0, y, a->W, 1, ' ', S_DIM);
        x = 1;
        for (int c = a->tcol0; c < ncol && x < a->W; c++) {
            char v[600];
            cell_text(a, rows[ri], c, v, sizeof v);
            style_t st = selrow && c == a->tcol ? S_CURSOR : S_NORMAL;
            if (a->table == TBL_TODOS && !(selrow && c == a->tcol)) {
                const item_t *it = &a->st->items[rows[ri]];
                daynum_t rd = todo_row_day(it);
                if (todo_done_on(it, rd)) st = S_DIM;
                else if (todo_overdue_on(it, rd, a->today)) st = S_OVERDUE;
            }
            if (a->table == TBL_PLACES && c == 0 && a->st->home && !strcmp(a->st->places[rows[ri]].name, a->st->home) && !(selrow && c == a->tcol)) st = S_BOLD;
            grid_field(&a->g, x, y, v, widths[c], st, a->cfg.unicode);
            x += widths[c] + 1;
        }
    }
    if (nrow == 0) grid_puts(&a->g, 1, y0 + 1, "empty — a adds a row", a->W - 2, S_DIM);
    /* the cursor day follows the selected row so the info bar and clock stay meaningful */
    if (nrow) {
        int ref = rows[a->trow];
        if (a->table == TBL_TODOS) { const item_t *it = &a->st->items[ref]; if (it->day >= 0) a->cur = it->day; else if (it->due >= 0) a->cur = it->due; }
        else if (a->table == TBL_EVENTS) a->cur = a->st->items[ref].day;
    }
    free(rows);
}

/* one-field forms used by cell editing */
static bool edit_text_cell(app_t *a, const char *label, char *buf, size_t n)
{
    form_t fm; memset(&fm, 0, sizeof fm); fm.title = "Edit";
    field_t *f = form_add(&fm, label, F_TEXT);
    snprintf(f->text, sizeof f->text, "%s", buf);
    fm.require_title = false;
    if (!run_form(a, &fm)) return false;
    memcpy(buf, f->text, n < sizeof f->text ? n : sizeof f->text);
    buf[n - 1] = 0;
    return true;
}

static bool edit_date_cell(app_t *a, const char *label, daynum_t *d, bool optional)
{
    form_t fm; memset(&fm, 0, sizeof fm); fm.title = "Edit";
    field_t *f = form_add(&fm, label, F_DATE);
    f->day = *d; f->optional = optional;
    if (!run_form(a, &fm)) return false;
    *d = f->day;
    return true;
}

/* pick from the list; the current value is pre-selected, typing narrows, Ctrl-U clears.
 * Returns false when nothing was changed. */
static bool edit_choice_cell(app_t *a, const char *label, char *buf, size_t n, bool contacts, bool optional)
{
    form_t fm; memset(&fm, 0, sizeof fm); fm.title = "Edit";
    field_t *f = form_add(&fm, label, F_CHOICE);
    f->contacts = contacts; f->optional = optional;
    static const char *m[256];
    int cnt = choice_matches(a, f, m, 256);
    for (int i = 0; i < cnt; i++) if (!strcmp(m[i], buf)) f->choice = i;
    fm.require_title = false;
    if (!run_form(a, &fm)) return false;
    if (!f->text[0] && !f->val) return false;          /* untouched */
    memcpy(buf, f->text, n < sizeof f->text ? n : sizeof f->text);
    buf[n - 1] = 0;
    return true;
}

static bool edit_val_cell(app_t *a, const char *label, ftype_t type, int *val)
{
    form_t fm; memset(&fm, 0, sizeof fm); fm.title = "Edit";
    field_t *f = form_add(&fm, label, type);
    f->val = *val;
    if (!run_form(a, &fm)) return false;
    *val = f->val;
    return true;
}

static void table_edit_cell(app_t *a)
{
    int ncol, nrow;
    const col_t *cols = table_cols(a->table, &ncol);
    int *rows = table_rows(a, &nrow);
    if (!rows || nrow == 0) { free(rows); return; }
    int ref = rows[a->trow];
    free(rows);
    const col_t *col = &cols[a->tcol];
    char buf[512];
    switch (a->table) {
    case TBL_TODOS:
    case TBL_EVENTS: {
        item_t *it = &a->st->items[ref];
        char **strf = NULL;
        daynum_t *datef = NULL;
        int *valf = NULL;
        ftype_t vtype = F_TIME;
        bool contacts = false, choice = false, optional = true;
        if (a->table == TBL_TODOS) {
            switch (a->tcol) {
            case 0: todo_toggle_on(it, todo_row_day(it), a->today, a->now_min); a->st->dirty = true; save(a); return;
            case 1: strf = &it->title; break;
            case 2: datef = &it->day; break;
            case 3: datef = &it->due; break;
            case 4: valf = &it->pri; vtype = F_PRI; break;
            case 5: strf = &it->who; choice = contacts = true; break;
            case 6: strf = &it->tags; break;
            }
        } else {
            switch (a->tcol) {
            case 0: datef = &it->day; optional = false; break;
            case 1:
                if (it->start < 0) { it->start = 9 * 60; it->dur = 60; }
                valf = &it->start; vtype = F_TIME; break;
            case 2: if (it->start < 0) { set_msg(a, true, "all-day event: set a time first, or edit Until with e"); return; } valf = &it->dur; vtype = F_DUR; break;
            case 3: strf = &it->title; break;
            case 4: strf = &it->at; choice = true; break;
            case 5: strf = &it->who; choice = contacts = true; break;
            case 6: strf = &it->tags; break;
            }
        }
        bool ok = false;
        if (strf) {
            snprintf(buf, sizeof buf, "%s", *strf ? *strf : "");
            ok = choice ? edit_choice_cell(a, col->name, buf, sizeof buf, contacts, optional) : edit_text_cell(a, col->name, buf, sizeof buf);
            if (ok) {
                if (strf == &it->title && !buf[0]) { set_msg(a, true, "a title is needed"); ok = false; }
                else { if (strf == &it->at) ensure_place(a, buf); store_set_str(strf, buf); }
            }
        } else if (datef) {
            daynum_t d = *datef;
            ok = edit_date_cell(a, col->name, &d, optional);
            if (ok) { if (datef == &it->day && it->until >= 0 && it->day >= 0) it->until += d - it->day; *datef = d; }
        } else if (valf) {
            ok = edit_val_cell(a, col->name, vtype, valf);
        }
        if (ok) { a->st->dirty = true; save(a); }
        return;
    }
    case TBL_PLACES: {
        place_t *p = &a->st->places[ref];
        if (a->tcol == 0) {
            snprintf(buf, sizeof buf, "%s", p->name);
            if (!edit_text_cell(a, "Name", buf, sizeof buf) || !buf[0] || !strcmp(buf, p->name)) return;
            if (store_find_place(a->st, buf)) { set_msg(a, true, "a place named '%s' already exists", buf); return; }
            char old[256]; snprintf(old, sizeof old, "%s", p->name);
            store_rename_place(a->st, old, buf);
        } else if (a->tcol == 1 || a->tcol == 2) {
            snprintf(buf, sizeof buf, "%.4f", a->tcol == 1 ? p->lat : p->lon);
            if (!edit_text_cell(a, a->tcol == 1 ? "Latitude" : "Longitude", buf, sizeof buf)) return;
            char *end;
            double v = strtod(buf, &end);
            if (end == buf || *end || (a->tcol == 1 ? (v < -90 || v > 90) : (v < -180 || v > 180))) { set_msg(a, true, "not a valid coordinate"); return; }
            if (a->tcol == 1) p->lat = v; else p->lon = v;
            a->st->dirty = true;
        } else if (a->tcol == 3) {
            snprintf(buf, sizeof buf, "%s", place_zone(p));
            if (!edit_text_cell(a, "Zone (IANA)", buf, sizeof buf)) return;
            if (!tz_zone_exists(buf)) { set_msg(a, true, "unknown time zone '%s'", buf); return; }
            store_set_str(&p->zone, buf);
            a->st->dirty = true;
        } else return;
        save(a);
        return;
    }
    case TBL_CONTACTS: {
        contact_t *c = &a->cs->c[ref];
        char **f = a->tcol == 0 ? &c->name : a->tcol == 1 ? &c->phone : a->tcol == 2 ? &c->email : a->tcol == 3 ? &c->place : &c->tags;
        snprintf(buf, sizeof buf, "%s", *f ? *f : "");
        bool ok = a->tcol == 3 ? edit_choice_cell(a, "Place", buf, sizeof buf, false, true) : edit_text_cell(a, col->name, buf, sizeof buf);
        if (!ok) return;
        if (a->tcol == 0 && !buf[0]) { set_msg(a, true, "a name is needed"); return; }
        contact_set(f, buf);
        a->cs->dirty = true;
        save_contacts(a);
        return;
    }
    default: return;
    }
}

static void table_edit_row(app_t *a)
{
    int nrow;
    int *rows = table_rows(a, &nrow);
    if (!rows || nrow == 0) { free(rows); return; }
    int ref = rows[a->trow];
    free(rows);
    if (a->table == TBL_TODOS || a->table == TBL_EVENTS) {
        a->cur = a->st->items[ref].day >= 0 ? a->st->items[ref].day : a->cur;
        select_store_index(a, ref);
        if (a->sel < 0) { /* unscheduled todo: edit through a temporary selection */
            item_t *it = &a->st->items[ref];
            item_t copy = *it;
            copy.title = copy.at = copy.tags = copy.note = copy.who = copy.status = copy.extra = NULL;
            store_set_str(&copy.title, it->title); store_set_str(&copy.tags, it->tags); store_set_str(&copy.who, it->who);
            if (todo_form(a, &copy, false)) {
                store_set_str(&it->title, copy.title); store_set_str(&it->tags, copy.tags); store_set_str(&it->who, copy.who);
                it->day = copy.day; it->due = copy.due; it->pri = copy.pri; it->rep = copy.rep;
                a->st->dirty = true; save(a);
            }
            item_free_strings(&copy);
        } else edit_selected(a);
        a->sel = -1;
    } else if (a->table == TBL_CONTACTS) {
        contact_t *c = &a->cs->c[ref];
        contact_t copy; memset(&copy, 0, sizeof copy);
        contact_set(&copy.name, c->name); contact_set(&copy.phone, c->phone); contact_set(&copy.email, c->email);
        contact_set(&copy.place, c->place); contact_set(&copy.tags, c->tags); contact_set(&copy.note, c->note);
        if (contact_form(a, &copy, false)) {
            contact_set(&c->name, copy.name); contact_set(&c->phone, copy.phone); contact_set(&c->email, copy.email);
            contact_set(&c->place, copy.place); contact_set(&c->tags, copy.tags); contact_set(&c->note, copy.note);
            a->cs->dirty = true; save_contacts(a);
        }
        free(copy.name); free(copy.phone); free(copy.email); free(copy.place); free(copy.tags); free(copy.note);
    } else table_edit_cell(a);
}

static void table_add_row(app_t *a)
{
    switch (a->table) {
    case TBL_TODOS: add_todo(a); a->sel = -1; break;
    case TBL_EVENTS: add_event(a); a->sel = -1; break;
    case TBL_PLACES: {
        char buf[256] = "";
        if (!prompt(a, "new place  Name | lat lon | Zone/Name: ", buf, sizeof buf) || !buf[0]) return;
        if (resolve_place(a, buf)) save(a);
        break;
    }
    case TBL_CONTACTS: {
        contact_t c; memset(&c, 0, sizeof c);
        if (contact_form(a, &c, true) && c.name) { contacts_add(a->cs, &c); save_contacts(a); }
        free(c.name); free(c.phone); free(c.email); free(c.place); free(c.tags); free(c.note);
        break;
    }
    default: break;
    }
}

static void table_delete_row(app_t *a)
{
    int nrow;
    int *rows = table_rows(a, &nrow);
    if (!rows || nrow == 0) { free(rows); return; }
    int ref = rows[a->trow];
    free(rows);
    char q[300], v[200];
    cell_text(a, ref, a->table == TBL_EVENTS ? 3 : a->table == TBL_TODOS ? 1 : 0, v, sizeof v);
    if (a->table == TBL_PLACES) {
        int refs = store_place_refs(a->st, a->st->places[ref].name);
        if (refs) { set_msg(a, true, "'%s' is used %d time%s (timeline, home or items); reassign first", a->st->places[ref].name, refs, refs == 1 ? "" : "s"); return; }
    }
    if (a->table != TBL_TODOS && a->table != TBL_EVENTS) {
        snprintf(q, sizeof q, "delete '%.150s'? (y/n) ", v);
        if (!confirm(a, q)) return;
    }
    switch (a->table) {
    case TBL_TODOS: case TBL_EVENTS: delete_item_at(a, ref, a->st->items[ref].day); break;
    case TBL_PLACES: store_delete_place(a->st, a->st->places[ref].name); save(a); break;
    case TBL_CONTACTS: contacts_delete(a->cs, ref); save_contacts(a); break;
    default: break;
    }
    if (a->trow > 0 && a->trow >= nrow - 1) a->trow--;
}

/* ---------- day-based sheets: "days" (slots x days) and "locations by date" ---------- */

static void set_table(app_t *a, table_t t)
{
    a->table = t;
    a->itemmode = false;
    a->sel = -1;
    a->trow = a->tcol = a->trow0 = a->tcol0 = 0;
    a->tbase = a->cur;
    if (t == TBL_SLOTS) { a->trow = a->cur_min / a->sheet_slot + 1; a->trow0 = a->work_start / a->sheet_slot + 1; }
    if (t == TBL_DAYLOC) { a->tbase = a->cur - 3; a->trow = 3; }
}

/* pick one of several items by title; returns store index or -1 */
static int choose_item(app_t *a, const char *title, const int *idx, int n)
{
    if (n == 1) return idx[0];
    const char *names[64];
    char labels[64][80];
    for (int i = 0; i < n && i < 64; i++) {
        item_label(a, &a->st->items[idx[i]], labels[i], sizeof labels[i], true, a->cur);
        names[i] = labels[i];
    }
    form_t fm; memset(&fm, 0, sizeof fm); fm.title = title;
    field_t *f = form_add(&fm, "Item", F_CHOICE);
    f->list = names; f->nlist = n < 64 ? n : 64;
    fm.require_title = false;
    if (!run_form(a, &fm)) return -1;
    for (int i = 0; i < f->nlist; i++) if (!strcmp(names[i], f->text)) return idx[i];
    return -1;
}

/* items on `day` that cover the slot starting at `minute` (minute -1: the all-day row) */
static int slot_items(app_t *a, daynum_t day, int minute, int *out)
{
    int idx[MAX_DAY_ITEMS];
    int n = day_items(a, day, idx), k = 0;
    for (int i = 0; i < n; i++) {
        const item_t *it = &a->st->items[idx[i]];
        bool timed = it->kind == ITEM_EVENT && it->start >= 0;
        if (minute < 0) { if (!timed) out[k++] = idx[i]; continue; }
        if (!timed) continue;
        int end = it->start + (it->dur > 0 ? it->dur : a->sheet_slot);
        if (it->start < minute + a->sheet_slot && end > minute) out[k++] = idx[i];
    }
    return k;
}

static void draw_slots_sheet(app_t *a)
{
    const int gutter = 6, cw = 18;
    int ncols = (a->W - gutter) / cw;
    if (ncols < 1) ncols = 1;
    int nslots = 1440 / a->sheet_slot;
    int y0 = TOP, body = STATUS_Y(a) - y0 - 1;    /* header row, then rows */
    if (body < 1) body = 1;
    /* keep the cursor column and row visible */
    if (a->tcol < 0) { a->tbase += a->tcol; a->tcol = 0; }
    if (a->tcol >= ncols) { a->tbase += a->tcol - ncols + 1; a->tcol = ncols - 1; }
    if (a->trow < 0) a->trow = 0;
    if (a->trow > nslots) a->trow = nslots;
    if (a->trow < a->trow0) a->trow0 = a->trow;
    if (a->trow >= a->trow0 + body) a->trow0 = a->trow - body + 1;
    if (a->trow0 > nslots + 1 - body) a->trow0 = nslots + 1 - body;
    if (a->trow0 < 0) a->trow0 = 0;
    a->cur = a->tbase + a->tcol;
    if (a->trow > 0) a->cur_min = (a->trow - 1) * a->sheet_slot;

    for (int c = 0; c < ncols; c++) {
        daynum_t day = a->tbase + c;
        char h[64];
        civil_t cd = days_to_civil(day);
        snprintf(h, sizeof h, "%.3s %d %.3s", civil_weekday_names[civil_weekday(day)], cd.d, civil_month_names[cd.m]);
        style_t st = c == a->tcol ? S_TITLEBAR : day == a->today ? S_TODAY : civil_weekday(day) >= 5 ? S_DIM : S_BOLD;
        grid_field(&a->g, gutter + c * cw, y0, h, cw - 1, st, a->cfg.unicode);
    }
    for (int r = 0; r < body; r++) {
        int row = a->trow0 + r;           /* 0 = all-day, 1.. = slots */
        int y = y0 + 1 + r;
        if (row > nslots) break;
        int minute = row == 0 ? -1 : (row - 1) * a->sheet_slot;
        if (row == 0) grid_puts(&a->g, 0, y, "all", 5, S_GUTTER);
        else if (minute % 60 == 0) { char lab[16]; snprintf(lab, sizeof lab, "%02d:00", minute / 60); bool now = a->now_min >= minute && a->now_min < minute + a->sheet_slot; grid_puts(&a->g, 0, y, lab, 5, now ? S_NOW : in_work_hours(a, minute) ? S_NORMAL : S_GUTTER); }
        else if (a->sheet_slot <= 30 && minute % 30 == 0) grid_puts(&a->g, 0, y, "  :30", 5, S_GUTTER);
        for (int c = 0; c < ncols; c++) {
            daynum_t day = a->tbase + c;
            int x = gutter + c * cw;
            int items[MAX_DAY_ITEMS];
            int n = slot_items(a, day, minute, items);
            bool cursor = c == a->tcol && row == a->trow;
            style_t st = cursor ? S_CURSOR : S_NORMAL;
            grid_put(&a->g, x + cw - 1, y, a->cfg.unicode ? 0x2502 : '|', S_DIM);
            if (n == 0) {
                grid_fill(&a->g, x, y, cw - 1, 1, ' ', st);
                if (minute >= 0 && minute % 60 == 0 && !cursor) for (int i = 0; i < cw - 1; i++) grid_put(&a->g, x + i, y, a->cfg.unicode ? 0x2508 : '.', S_DIM);
                continue;
            }
            const item_t *it = &a->st->items[items[0]];
            char lab[512];
            bool first = minute < 0 || it->start >= minute;    /* the event starts in this slot: show its title */
            if (first) item_label(a, it, lab, sizeof lab, minute >= 0 && it->start % a->sheet_slot != 0, day);
            else snprintf(lab, sizeof lab, "%s", a->cfg.unicode ? "\xE2\x94\x86" : ":");
            if (n > 1) { size_t l = strlen(lab); snprintf(lab + l, sizeof lab - l, " +%d", n - 1); }
            style_t es = cursor ? S_CURSOR : it->kind == ITEM_TODO ? item_style(a, it, false, day) : minute < 0 ? S_ALLDAY : S_EVENT;
            if (!first && !cursor) es = S_EVENT;
            grid_fill(&a->g, x, y, cw - 1, 1, ' ', es);
            grid_field(&a->g, x, y, lab, cw - 1, es, a->cfg.unicode);
        }
    }
}

static void slots_sheet_enter(app_t *a, bool todo)
{
    daynum_t day = a->tbase + a->tcol;
    int minute = a->trow == 0 ? -1 : (a->trow - 1) * a->sheet_slot;
    int items[MAX_DAY_ITEMS];
    int n = slot_items(a, day, minute, items);
    a->cur = day;
    if (n > 0 && !todo) {
        int si = choose_item(a, "Edit which item?", items, n);
        if (si < 0) return;
        select_store_index(a, si);
        if (a->sel >= 0) edit_selected(a); else set_msg(a, true, "item not on this day");
        a->sel = -1;
        return;
    }
    if (todo) { add_todo(a); a->sel = -1; return; }
    item_t it;
    item_blank(&it, ITEM_EVENT);
    it.day = day;
    if (minute >= 0) { it.start = minute; it.dur = a->sheet_slot > 60 ? a->sheet_slot : 60; }
    if (!event_form(a, &it, true, minute < 0)) { item_free_strings(&it); return; }
    int i = store_add_item(a->st, &it);
    item_free_strings(&it);
    if (i >= 0) save(a);
}

static void slots_sheet_delete(app_t *a)
{
    daynum_t day = a->tbase + a->tcol;
    int minute = a->trow == 0 ? -1 : (a->trow - 1) * a->sheet_slot;
    int items[MAX_DAY_ITEMS];
    int n = slot_items(a, day, minute, items);
    if (n == 0) { set_msg(a, true, "empty cell"); return; }
    int si = choose_item(a, "Delete which item?", items, n);
    if (si < 0) return;
    delete_item_at(a, si, day);
}

static void slots_sheet_done(app_t *a)
{
    daynum_t day = a->tbase + a->tcol;
    int items[MAX_DAY_ITEMS];
    int n = slot_items(a, day, -1, items), k = 0;
    int todos[MAX_DAY_ITEMS];
    for (int i = 0; i < n; i++) if (a->st->items[items[i]].kind == ITEM_TODO) todos[k++] = items[i];
    if (k == 0 || a->trow != 0) { set_msg(a, true, "todos live in the all-day row"); return; }
    int si = choose_item(a, "Toggle which todo?", todos, k);
    if (si < 0) return;
    todo_toggle_on(&a->st->items[si], day, a->today, a->now_min);
    a->st->dirty = true;
    save(a);
}

static bool slots_sheet_key(app_t *a, int k)
{
    int nslots = 1440 / a->sheet_slot;
    switch (k) {
    case 'j': case KEY_DOWN:  if (a->trow < nslots) a->trow++; return true;
    case 'k': case KEY_UP:    if (a->trow > 0) a->trow--; return true;
    case 'l': case KEY_RIGHT: case KEY_TAB: a->tcol++; return true;
    case 'h': case KEY_LEFT:  case KEY_BTAB: a->tcol--; return true;
    case 'J': a->trow = a->trow + 60 / a->sheet_slot <= nslots ? a->trow + 60 / a->sheet_slot : nslots; return true;
    case 'K': a->trow = a->trow - 60 / a->sheet_slot >= 0 ? a->trow - 60 / a->sheet_slot : 0; return true;
    case KEY_PGDN: case 'n': a->tbase += 7; return true;
    case KEY_PGUP: case 'p': a->tbase -= 7; return true;
    case ']': a->tbase += 7; return true;
    case '[': a->tbase -= 7; return true;
    case '}': a->tbase = civil_add_months(a->tbase, 1); return true;
    case '{': a->tbase = civil_add_months(a->tbase, -1); return true;
    case KEY_HOME: a->trow = 0; return true;
    case KEY_END:  a->trow = nslots; return true;
    case '.': case 't': a->tbase = a->today; a->tcol = 0; a->trow = a->now_min / a->sheet_slot + 1; return true;
    case 'g': { daynum_t d; if (prompt_date(a, "go to: ", &d)) { a->tbase = d; a->tcol = 0; } return true; }
    case KEY_ENTER: case 'a': case 'e': slots_sheet_enter(a, false); return true;
    case 'T': slots_sheet_enter(a, true); return true;
    case 'd': case KEY_DEL: slots_sheet_delete(a); return true;
    case 'x': slots_sheet_done(a); return true;
    case 'z': { static const int sizes[] = { 5, 10, 15, 30, 60 }; int i = 0; int minute = a->trow > 0 ? (a->trow - 1) * a->sheet_slot : 0;
                for (int j = 0; j < 5; j++) if (sizes[j] == a->sheet_slot) i = j;
                a->sheet_slot = sizes[(i + 1) % 5]; a->trow = a->trow == 0 ? 0 : minute / a->sheet_slot + 1; a->trow0 = 0; set_msg(a, false, "slot size %d min", a->sheet_slot); return true; }
    case 'v': set_table(a, a->table == TBL_LAST ? TBL_SLOTS : a->table + 1); return true;
    case 'V': set_table(a, a->table == TBL_SLOTS ? TBL_LAST : a->table - 1); return true;
    case 'C': set_table(a, TBL_CONTACTS); return true;
    case KEY_ESC: case '-': case KEY_BACKSPACE: a->table = TBL_NONE; return true;
    case 'q': case 'Q': case '?': case 'M': case 's': case 'w': return false;
    default: return true;
    }
}

/* ----- locations by date ----- */

static const col_t cols_dayloc[] = { {"Date", 16, CT_RO}, {"Place", 0, CT_PLACE}, {"Zone", 22, CT_RO}, {"UTC", 6, CT_RO}, {"Rise", 5, CT_RO}, {"Set", 5, CT_RO}, {"Day", 6, CT_RO}, {"Items", 5, CT_RO} };
#define NCOL_DAYLOC 8

static void dayloc_cell(app_t *a, daynum_t day, int col, char *out, size_t n)
{
    out[0] = 0;
    const place_t *p = store_place_for_day(a->st, day);
    int off = day_offset(a, day);
    sun_t sun;
    if (p && col >= 4 && col <= 6) sun_compute(day, p->lat, p->lon, &sun);
    int hh, mm, dd;
    switch (col) {
    case 0: format_day(day, out, n, true); break;
    case 1: snprintf(out, n, "%s%s", p && a->st->home && strcmp(p->name, a->st->home) ? "@ " : "", p ? p->name : ""); break;
    case 2: snprintf(out, n, "%s", p ? place_zone(p) : ""); break;
    case 3: snprintf(out, n, "%+d%s", off / 60, off % 60 ? ":30" : ""); break;
    case 4: if (p) { if (sun.rise_set == SUN_NORMAL) { sun_local_hhmm(sun.rise, off, &hh, &mm, &dd); snprintf(out, n, "%02d:%02d", hh, mm); } else snprintf(out, n, sun.rise_set == SUN_ALWAYS_ABOVE ? "up" : "down"); } break;
    case 5: if (p) { if (sun.rise_set == SUN_NORMAL) { sun_local_hhmm(sun.set, off, &hh, &mm, &dd); snprintf(out, n, "%02d:%02d", hh, mm); } else out[0] = 0; } break;
    case 6: if (p) snprintf(out, n, "%dh%02d", (int)(sun.day_length / 60), (int)sun.day_length % 60); break;
    case 7: { int pend; int l = store_day_load(a->st, day, &pend); if (l) snprintf(out, n, "%d", l); break; }
    }
}

static void draw_dayloc_sheet(app_t *a)
{
    int widths[NCOL_DAYLOC], fixed = 0;
    for (int c = 0; c < NCOL_DAYLOC; c++) { widths[c] = cols_dayloc[c].width; if (widths[c]) fixed += widths[c] + 1; }
    widths[1] = a->W - 1 - fixed; if (widths[1] < 12) widths[1] = 12;
    int y0 = TOP, body = STATUS_Y(a) - y0 - 1;
    if (body < 1) body = 1;
    if (a->tcol < 0) a->tcol = 0;
    if (a->tcol >= NCOL_DAYLOC) a->tcol = NCOL_DAYLOC - 1;
    if (a->trow < 0) { a->tbase -= 28; a->trow += 28; a->trow0 += 28; }
    if (a->trow < a->trow0) a->trow0 = a->trow;
    if (a->trow >= a->trow0 + body) a->trow0 = a->trow - body + 1;
    if (a->trow0 < 0) a->trow0 = 0;
    a->cur = a->tbase + a->trow;

    int x = 1;
    for (int c = 0; c < NCOL_DAYLOC && x < a->W; c++) { grid_field(&a->g, x, y0, cols_dayloc[c].name, widths[c], c == a->tcol ? S_TITLEBAR : S_BOLD, a->cfg.unicode); x += widths[c] + 1; }
    for (int r = 0; r < body; r++) {
        int ri = a->trow0 + r;
        daynum_t day = a->tbase + ri;
        int y = y0 + 1 + r;
        bool selrow = ri == a->trow;
        const place_t *p = store_place_for_day(a->st, day);
        bool away = p && a->st->home && strcmp(p->name, a->st->home) != 0;
        bool prev_away = false;
        const place_t *pp = store_place_for_day(a->st, day - 1);
        prev_away = pp && p && strcmp(pp->name, p->name) != 0;
        if (selrow) grid_fill(&a->g, 0, y, a->W, 1, ' ', S_DIM);
        x = 1;
        for (int c = 0; c < NCOL_DAYLOC && x < a->W; c++) {
            char v[256];
            dayloc_cell(a, day, c, v, sizeof v);
            style_t st = selrow && c == a->tcol ? S_CURSOR : c == 1 && away ? S_ALLDAY : c == 0 && day == a->today ? S_TODAY : c == 0 && civil_weekday(day) >= 5 ? S_DIM : S_NORMAL;
            if (c == 1 && prev_away && !(selrow && c == a->tcol)) st.attr |= A_UL;
            grid_field(&a->g, x, y, v, widths[c], st, a->cfg.unicode);
            x += widths[c] + 1;
        }
    }
}

static void dayloc_set(app_t *a, daynum_t from, daynum_t until)
{
    char buf[256] = "";
    const place_t *p = store_place_for_day(a->st, from);
    if (p) snprintf(buf, sizeof buf, "%s", p->name);
    if (!edit_choice_cell(a, "Place", buf, sizeof buf, false, false) || !buf[0]) return;
    const char *name = resolve_place(a, buf);
    if (!name) return;
    store_set_loc_range(a->st, from, until, name);
    save(a);
}

static void dayloc_fill(app_t *a)
{
    daynum_t from = a->tbase + a->trow;
    const place_t *p = store_place_for_day(a->st, from);
    if (!p) { set_msg(a, true, "no place on this row"); return; }
    daynum_t until = from;
    form_t fm; memset(&fm, 0, sizeof fm); fm.title = "Fill down";
    field_t *f = form_add(&fm, "Until", F_DATE);
    f->day = from;
    if (!run_form(a, &fm) || f->day < 0) return;
    char name[256];
    snprintf(name, sizeof name, "%s", p->name);
    store_set_loc_range(a->st, from, f->day, name);
    set_msg(a, false, "%s through %ld more day%s", name, (long)(f->day - from), f->day - from == 1 ? "" : "s");
    (void)until;
    save(a);
}

static bool dayloc_key(app_t *a, int k)
{
    int body = STATUS_Y(a) - TOP - 1;
    if (body < 1) body = 1;
    switch (k) {
    case 'j': case KEY_DOWN:  a->trow++; return true;
    case 'k': case KEY_UP:    a->trow--; return true;
    case 'l': case KEY_RIGHT: case KEY_TAB: if (a->tcol + 1 < NCOL_DAYLOC) a->tcol++; return true;
    case 'h': case KEY_LEFT:  case KEY_BTAB: if (a->tcol > 0) a->tcol--; return true;
    case KEY_PGDN: case 'J': a->trow += body; return true;
    case KEY_PGUP: case 'K': a->trow -= body; return true;
    case ']': a->trow += 7; return true;
    case '[': a->trow -= 7; return true;
    case '}': a->trow += (int)(civil_add_months(a->tbase + a->trow, 1) - (a->tbase + a->trow)); return true;
    case '{': a->trow -= (int)((a->tbase + a->trow) - civil_add_months(a->tbase + a->trow, -1)); return true;
    case '.': case 't': a->trow += (int)(a->today - (a->tbase + a->trow)); return true;
    case 'g': { daynum_t d; if (prompt_date(a, "go to: ", &d)) a->trow += (int)(d - (a->tbase + a->trow)); return true; }
    case KEY_ENTER: case 'e': dayloc_set(a, a->tbase + a->trow, a->tbase + a->trow); return true;
    case 'f': dayloc_fill(a); return true;
    case 'a': case 'L': a->cur = a->tbase + a->trow; location_form(a, false); return true;
    case 'd': if (a->st->home) { store_set_loc_range(a->st, a->tbase + a->trow, a->tbase + a->trow, a->st->home); save(a); } return true;
    case 'v': set_table(a, a->table == TBL_LAST ? TBL_SLOTS : a->table + 1); return true;
    case 'V': set_table(a, a->table == TBL_SLOTS ? TBL_LAST : a->table - 1); return true;
    case 'C': set_table(a, TBL_CONTACTS); return true;
    case KEY_ESC: case '-': case KEY_BACKSPACE: a->table = TBL_NONE; return true;
    case 'q': case 'Q': case '?': case 'M': case 's': case 'w': return false;
    default: return true;
    }
}

/* ---------- search ---------- */

typedef struct { int kind; int ref; daynum_t day; } hit_t;   /* kind 0 item, 1 contact */

static int search_hits(app_t *a, hit_t *out, int max)
{
    int n = 0;
    const char *q = a->query;
    for (int i = 0; i < a->st->nitems && n < max; i++) {
        const item_t *it = &a->st->items[i];
        if (utf8_icontains(it->title, q) || utf8_icontains(it->tags, q) || utf8_icontains(it->note, q) || utf8_icontains(it->who, q) || utf8_icontains(it->at, q)) {
            daynum_t d = it->day >= 0 ? it->day : it->due;
            if (item_is_repeating(it)) { daynum_t nx = item_next_occurrence(it, a->today - 1); if (nx >= 0) d = nx; }
            out[n].kind = 0; out[n].ref = i; out[n].day = d; n++;
        }
    }
    for (int i = 1; i < n; i++) {             /* by day, unscheduled last */
        hit_t v = out[i]; int j = i - 1;
        long kv = v.day >= 0 ? v.day : (1L << 40);
        while (j >= 0 && (out[j].day >= 0 ? out[j].day : (1L << 40)) > kv) { out[j + 1] = out[j]; j--; }
        out[j + 1] = v;
    }
    for (int i = 0; i < a->cs->n && n < max; i++) {
        const contact_t *c = &a->cs->c[i];
        if (utf8_icontains(c->name, q) || utf8_icontains(c->phone, q) || utf8_icontains(c->email, q) || utf8_icontains(c->place, q) || utf8_icontains(c->tags, q) || utf8_icontains(c->note, q)) {
            out[n].kind = 1; out[n].ref = i; out[n].day = -1; n++;
        }
    }
    return n;
}

#define MAX_HITS 512

static void draw_search_sheet(app_t *a)
{
    static hit_t hits[MAX_HITS];
    int n = search_hits(a, hits, MAX_HITS);
    int y0 = TOP, body = STATUS_Y(a) - y0 - 1;
    if (body < 1) body = 1;
    if (a->trow >= n) a->trow = n - 1;
    if (a->trow < 0) a->trow = 0;
    if (a->trow < a->trow0) a->trow0 = a->trow;
    if (a->trow >= a->trow0 + body) a->trow0 = a->trow - body + 1;
    if (a->trow0 < 0) a->trow0 = 0;
    char head[256];
    snprintf(head, sizeof head, " search: %s   %d hit%s", a->query, n, n == 1 ? "" : "s");
    grid_fill(&a->g, 0, y0, a->W, 1, ' ', S_BOLD);
    grid_puts(&a->g, 0, y0, head, a->W, S_BOLD);
    for (int r = 0; r < body; r++) {
        int ri = a->trow0 + r;
        if (ri >= n) break;
        int y = y0 + 1 + r;
        bool sel = ri == a->trow;
        if (sel) grid_fill(&a->g, 0, y, a->W, 1, ' ', S_CURSOR);
        char left[32], right[700];
        if (hits[ri].kind == 0) {
            const item_t *it = &a->st->items[hits[ri].ref];
            if (hits[ri].day >= 0) format_day(hits[ri].day, left, sizeof left, true); else snprintf(left, sizeof left, "%s", "unscheduled");
            char lab[512], time[16] = "";
            item_label(a, it, lab, sizeof lab, false, hits[ri].day);
            if (it->kind == ITEM_EVENT && it->start >= 0) snprintf(time, sizeof time, "%02d:%02d  ", it->start / 60, it->start % 60);
            snprintf(right, sizeof right, "%s%s%s%s%s%s%s%s", it->kind == ITEM_EVENT ? "event  " : "todo   ", time, lab,
                     it->at ? "  @" : "", it->at ? it->at : "", it->who ? "  \xE2\x98\x8E " : "", it->who ? it->who : "", item_is_repeating(it) ? "  \xE2\x86\xBB" : "");
            style_t st = sel ? S_CURSOR : item_style(a, it, false, hits[ri].day);
            grid_field(&a->g, 1, y, left, 16, sel ? S_CURSOR : S_DIM, a->cfg.unicode);
            grid_field(&a->g, 18, y, right, a->W - 19, st, a->cfg.unicode);
        } else {
            const contact_t *c = &a->cs->c[hits[ri].ref];
            snprintf(right, sizeof right, "contact  %s%s%s%s%s%s%s", c->name ? c->name : "", c->phone ? "  \xE2\x98\x8E " : "", c->phone ? c->phone : "",
                     c->email ? "  " : "", c->email ? c->email : "", c->place ? "  @" : "", c->place ? c->place : "");
            grid_field(&a->g, 1, y, "", 16, sel ? S_CURSOR : S_DIM, a->cfg.unicode);
            grid_field(&a->g, 18, y, right, a->W - 19, sel ? S_CURSOR : S_ALLDAY, a->cfg.unicode);
        }
    }
    if (n == 0) grid_puts(&a->g, 1, y0 + 1, "no matches", a->W - 2, S_DIM);
}

static void start_search(app_t *a)
{
    char buf[128];
    snprintf(buf, sizeof buf, "%s", a->query);
    if (!prompt(a, "search: ", buf, sizeof buf) || !buf[0]) return;
    snprintf(a->query, sizeof a->query, "%s", buf);
    if (a->table != TBL_SEARCH) a->table_before_search = a->table;
    a->table = TBL_SEARCH;
    a->itemmode = false;
    a->sel = -1;
    a->trow = a->trow0 = 0;
}

static bool search_key(app_t *a, int k)
{
    static hit_t hits[MAX_HITS];
    int n = search_hits(a, hits, MAX_HITS);
    int body = STATUS_Y(a) - TOP - 1;
    if (body < 1) body = 1;
    switch (k) {
    case 'j': case KEY_DOWN:  if (a->trow + 1 < n) a->trow++; return true;
    case 'k': case KEY_UP:    if (a->trow > 0) a->trow--; return true;
    case KEY_PGDN: case 'J':  a->trow = a->trow + body < n ? a->trow + body : (n ? n - 1 : 0); return true;
    case KEY_PGUP: case 'K':  a->trow = a->trow - body > 0 ? a->trow - body : 0; return true;
    case KEY_HOME: case 'g':  a->trow = 0; return true;
    case KEY_END:  case 'G':  a->trow = n ? n - 1 : 0; return true;
    case '/': start_search(a); return true;
    case KEY_ENTER: case 'e':
        if (n == 0) return true;
        if (hits[a->trow].kind == 0) {
            int si = hits[a->trow].ref;
            if (hits[a->trow].day >= 0) a->cur = hits[a->trow].day;
            a->table = TBL_NONE;
            select_store_index(a, si);
            if (a->sel >= 0) { a->itemmode = true; if (k == 'e') { edit_selected(a); a->itemmode = false; a->sel = -1; } }
            else edit_selected(a);
        } else {
            a->table = TBL_CONTACTS;
            a->trow = 0;
            int nrow; int *rows = table_rows(a, &nrow);
            for (int i = 0; i < nrow; i++) if (rows[i] == hits[a->trow].ref) a->trow = i;
            free(rows);
            if (k == 'e') table_edit_row(a);
        }
        return true;
    case 'x':
        if (n && hits[a->trow].kind == 0 && a->st->items[hits[a->trow].ref].kind == ITEM_TODO) {
            item_t *it = &a->st->items[hits[a->trow].ref];
            todo_toggle_on(it, hits[a->trow].day >= 0 ? hits[a->trow].day : a->today, a->today, a->now_min);
            a->st->dirty = true; save(a);
        }
        return true;
    case KEY_ESC: case '-': case KEY_BACKSPACE: a->table = a->table_before_search == TBL_SEARCH ? TBL_NONE : a->table_before_search; return true;
    case 'v': set_table(a, TBL_SLOTS); return true;
    case 'C': set_table(a, TBL_CONTACTS); return true;
    case 'q': case 'Q': case '?': case 'M': case 's': case 'w': return false;
    default: return true;
    }
}

/* returns true when the key was consumed by the table view */
static bool table_key(app_t *a, int k)
{
    if (a->table == TBL_SEARCH) return search_key(a, k);
    if (a->table == TBL_SLOTS) return slots_sheet_key(a, k);
    if (a->table == TBL_DAYLOC) return dayloc_key(a, k);
    int ncol, nrow;
    table_cols(a->table, &ncol);
    int *rows = table_rows(a, &nrow);
    free(rows);
    int body = STATUS_Y(a) - TOP - 1;
    if (body < 1) body = 1;
    switch (k) {
    case 'j': case KEY_DOWN:  if (a->trow + 1 < nrow) a->trow++; return true;
    case 'k': case KEY_UP:    if (a->trow > 0) a->trow--; return true;
    case 'l': case KEY_RIGHT: case KEY_TAB: if (a->tcol + 1 < ncol) a->tcol++; return true;
    case 'h': case KEY_LEFT:  case KEY_BTAB: if (a->tcol > 0) a->tcol--; return true;
    case KEY_PGDN: case 'J':  a->trow = a->trow + body < nrow ? a->trow + body : (nrow ? nrow - 1 : 0); return true;
    case KEY_PGUP: case 'K':  a->trow = a->trow - body > 0 ? a->trow - body : 0; return true;
    case KEY_HOME: case 'g':  a->trow = 0; return true;
    case KEY_END:  case 'G':  a->trow = nrow ? nrow - 1 : 0; return true;
    case KEY_ENTER: table_edit_cell(a); return true;
    case 'e': table_edit_row(a); return true;
    case 'a': case 'T': table_add_row(a); return true;
    case 'd': case KEY_DEL: table_delete_row(a); return true;
    case 'x':
        if (a->table == TBL_TODOS) { int c = a->tcol; a->tcol = 0; table_edit_cell(a); a->tcol = c; }
        return true;
    case 'v': set_table(a, a->table == TBL_LAST ? TBL_SLOTS : a->table + 1); return true;
    case 'V': set_table(a, a->table == TBL_SLOTS ? TBL_LAST : a->table - 1); return true;
    case 'C': set_table(a, TBL_CONTACTS); return true;
    case '/': start_search(a); return true;
    case KEY_ESC: case '-': case KEY_BACKSPACE: a->table = TBL_NONE; return true;
    case 'q': case 'Q': case '?': case 'M': case 's': case 'w': return false;   /* global keys */
    default: return true;
    }
}

/* ---------- navigation ---------- */

static void set_view(app_t *a, view_t v)
{
    a->view = v;
    a->sel = -1;
    a->itemmode = false;
}

static void move_days(app_t *a, long n)
{
    a->cur += n;
    a->sel = -1;
}

static void next_period(app_t *a, int dir)
{
    switch (a->view) {
    case V_YEAR:    a->cur = civil_add_years(a->cur, dir); break;
    case V_QUARTER:
    case V_MONTH:   a->cur = civil_add_months(a->cur, dir); break;
    case V_WEEK:    a->cur += 7 * dir; break;
    case V_DAY:     a->cur += dir; break;
    }
    a->sel = -1;
}

/* Tab enters item mode: j/k/arrows walk the day's items, Enter edits, Esc leaves */
static void enter_itemmode(app_t *a, int dir)
{
    int idx[MAX_DAY_ITEMS];
    int n = day_items(a, a->cur, idx);
    if (n == 0) { set_msg(a, true, "no items on this day"); return; }
    if (!a->itemmode) { a->itemmode = true; a->sel = dir > 0 ? 0 : n - 1; return; }
    a->sel = (a->sel + dir + n) % n;
}

static void item_step(app_t *a, int dir)
{
    int idx[MAX_DAY_ITEMS];
    int n = day_items(a, a->cur, idx);
    if (n == 0) { a->sel = -1; return; }
    if (a->sel < 0) { a->sel = dir > 0 ? 0 : n - 1; return; }
    int ns = a->sel + dir;
    if (ns < 0) ns = 0;
    if (ns >= n) ns = n - 1;
    a->sel = ns;
}

/* after a day change in item mode, land on the first item of the new day */
static void itemmode_resync(app_t *a)
{
    if (!a->itemmode) return;
    int idx[MAX_DAY_ITEMS];
    a->sel = day_items(a, a->cur, idx) > 0 ? 0 : -1;
}

static void handle_key(app_t *a, int k)
{
    a->msg[0] = 0;
    if (a->show_help && k != KEY_RESIZE) { a->show_help = false; if (k == '?' || k == KEY_ESC || k == 'q') return; }
    if (a->table != TBL_NONE && table_key(a, k)) return;

    switch (k) {
    case 'q': save(a); a->quit = true; return;
    case 'Q': a->quit = true; return;
    case 'w': save(a); if (!a->msg[0]) set_msg(a, false, "saved %s", a->st->path); return;
    case '?': a->show_help = !a->show_help; return;
    case 's': a->show_sun = !a->show_sun; return;
    case KEY_ESC:
        if (a->show_sun) { a->show_sun = false; return; }
        if (a->itemmode || a->sel >= 0) { a->itemmode = false; a->sel = -1; return; }
        if (a->view > V_YEAR) set_view(a, a->view - 1);
        return;
    case 't': case '.': a->cur = a->today; a->cur_min = a->now_min - a->now_min % a->slot; a->sel = -1; itemmode_resync(a); return;
    case ']': a->cur += 7; a->sel = -1; itemmode_resync(a); return;
    case '[': a->cur -= 7; a->sel = -1; itemmode_resync(a); return;
    case '}': case KEY_ALT('}'): a->cur = civil_add_months(a->cur, 1); a->sel = -1; itemmode_resync(a); return;
    case '{': case KEY_ALT('{'): a->cur = civil_add_months(a->cur, -1); a->sel = -1; itemmode_resync(a); return;
    case ')': a->cur = civil_add_years(a->cur, 1); a->sel = -1; itemmode_resync(a); return;
    case '(': a->cur = civil_add_years(a->cur, -1); a->sel = -1; itemmode_resync(a); return;
    case 'M': a->menu_rows = a->menu_rows ? 0 : 2; return;
    case 'v': set_table(a, TBL_SLOTS); return;
    case 'C': set_table(a, TBL_CONTACTS); return;
    case '/': start_search(a); return;
    case 'g': {
        daynum_t d;
        if (prompt_date(a, "go to: ", &d)) { a->cur = d; a->sel = -1; itemmode_resync(a); }
        return;
    }
    case KEY_ENTER:
        if (a->itemmode && selected(a) >= 0) { edit_selected(a); return; }
        /* fall through */
    case '+': case '=':
        if (a->view < V_DAY) set_view(a, a->view + 1);
        return;
    case '-': case KEY_BACKSPACE:
        if (a->view > V_YEAR) set_view(a, a->view - 1);
        return;
    case 'n': case KEY_PGDN: next_period(a, +1); itemmode_resync(a); return;
    case 'p': case KEY_PGUP: next_period(a, -1); itemmode_resync(a); return;
    case 'z': {
        static const int sizes[] = { 5, 10, 15, 30, 60 };
        int i = 0;
        for (int j = 0; j < 5; j++) if (sizes[j] == a->slot) i = j;
        a->slot = sizes[(i + 1) % 5];
        a->cur_min -= a->cur_min % a->slot;
        set_msg(a, false, "slot size %d min", a->slot);
        return;
    }
    case KEY_TAB: case 'i': enter_itemmode(a, +1); return;
    case KEY_BTAB: case 'I': enter_itemmode(a, -1); return;
    case 'a': add_event(a); return;
    case 'T': add_todo(a); return;
    case 'x': toggle_done(a); return;
    case 'e': edit_selected(a); return;
    case 'm': move_selected(a); return;
    case 'd': case KEY_DEL: delete_selected(a); return;
    case 'L': set_location(a, false); return;
    case 'H': set_location(a, true); return;
    default: break;
    }

    bool slots = a->view >= V_WEEK;
    if (a->itemmode) {
        switch (k) {
        case 'j': case KEY_DOWN: item_step(a, +1); return;
        case 'k': case KEY_UP:   item_step(a, -1); return;
        case 'h': case KEY_LEFT:  move_days(a, -1); itemmode_resync(a); return;
        case 'l': case KEY_RIGHT: move_days(a, +1); itemmode_resync(a); return;
        default: break;
        }
    }
    switch (k) {
    case 'h': case KEY_LEFT:  move_days(a, -1); break;
    case 'l': case KEY_RIGHT: move_days(a, +1); break;
    case 'k': case KEY_UP:
        if (slots) { if (a->cur_min >= a->slot) a->cur_min -= a->slot; }
        else move_days(a, -7);
        break;
    case 'j': case KEY_DOWN:
        if (slots) { if (a->cur_min + a->slot < 1440) a->cur_min += a->slot; }
        else move_days(a, +7);
        break;
    case 'K': if (slots && a->cur_min >= 60) a->cur_min -= 60; break;
    case 'J': if (slots && a->cur_min + 60 < 1440) a->cur_min += 60; break;
    case KEY_HOME: if (slots) a->cur_min = 0; else a->cur = wk_start(a, a->cur); a->sel = -1; break;
    case KEY_END:  if (slots) a->cur_min = 1440 - a->slot; else a->cur = wk_start(a, a->cur) + 6; a->sel = -1; break;
    default: break;
    }
}

/* ---------- entry ---------- */

int app_run(const app_config_t *cfg, daynum_t start_day)
{
    app_t a;
    memset(&a, 0, sizeof a);
    a.st = cfg->st;
    a.cs = cfg->cs;
    a.cfg = *cfg;
    a.slot = cfg->slot > 0 ? cfg->slot : 15;
    a.sheet_slot = cfg->conf ? cfg->conf->sheet_slot : 30;
    a.first_wd = cfg->conf ? cfg->conf->first_weekday : 0;
    a.work_start = cfg->work_start;
    a.work_end = cfg->conf ? cfg->conf->work_end : 18 * 60;
    palette_init(cfg->conf);
    a.sel = -1;
    a.view = V_QUARTER;
    a.menu_rows = cfg->menu ? 2 : 0;
    grid_init(&a.g);
    refresh_clock(&a);
    a.cur = start_day >= 0 ? start_day : a.today;
    a.cur_min = cfg->work_start;
    if (a.cur == a.today) a.cur_min = a.now_min - a.now_min % a.slot;
    a.tscroll = cfg->work_start / a.slot;

    if (term_open() != 0) {
        fprintf(stderr, "soon: needs a terminal\n");
        return 1;
    }
    resize(&a);
    if (!a.st->home) set_msg(&a, true, "no home location: press H to set one (needed for sun times)");
    else set_msg(&a, false, "%d items · %s · ? for keys", a.st->nitems, a.st->path);

    while (!a.quit) {
        draw_all(&a);
        flush(&a);
        int k = term_read_key(1000);
        if (k == KEY_NONE) { refresh_clock(&a); continue; }
        if (k == KEY_RESIZE) { resize(&a); continue; }
        if (k == KEY_EOF) break;
        refresh_clock(&a);
        handle_key(&a, k);
    }
    term_close();
    grid_free(&a.g);
    free(a.out.s);
    return 0;
}
