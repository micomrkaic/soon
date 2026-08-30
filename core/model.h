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

/* model.h — items, places, the location timeline, and the store file.
 * File format: see FORMAT.md. Strings are UTF-8, owned by the store.
 */
#ifndef SOON_MODEL_H
#define SOON_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include "civil.h"

typedef enum { ITEM_EVENT = 0, ITEM_TODO = 1 } item_kind_t;

typedef struct {
    char *name;
    double lat, lon;
    char *zone;
} place_t;

typedef struct {
    daynum_t from;
    char *place;
} loc_t;

/* recurrence: the `repeat:` grammar of FORMAT.md */
typedef enum { REP_NONE = 0, REP_DAILY, REP_WEEKLY, REP_MONTHLY, REP_YEARLY } rep_freq_t;

typedef struct {
    rep_freq_t freq;
    int interval;        /* every N days/weeks/months/years; 1 = every */
    int wdays;           /* weekly: bitmask, bit 0 = Monday; 0 = the start's weekday */
    int mday;            /* monthly: day of month; 0 = the start's day */
    int nth, nth_wday;   /* monthly: nth (1..4, -1 = last) weekday (0 = Monday); nth = 0 unused */
    daynum_t until;      /* -1 = open */
    int count;           /* 0 = unlimited */
} repeat_t;

typedef struct {
    item_kind_t kind;
    char id[9];
    daynum_t day;        /* event date, or todo scheduled day; -1 = unscheduled todo */
    int start;           /* minutes after midnight; -1 = all-day / untimed */
    int dur;             /* minutes; 0 for all-day */
    daynum_t until;      /* last day of a multi-day all-day event; -1 otherwise */
    daynum_t due;        /* todo deadline; -1 = none */
    daynum_t done_day;   /* todo completion; -1 = not done */
    int done_min;
    int pri;             /* 1..5, 0 = unset */
    char *title, *at, *tags, *note, *who, *status;
    char *extra;         /* unknown field lines, kept verbatim, '\n'-joined */
    repeat_t rep;
    daynum_t *skips;     /* occurrence days removed from the repeat */
    int nskips;
} item_t;

typedef struct {
    place_t *places; int nplaces;
    char *home;
    loc_t *locs; int nlocs;
    item_t *items; int nitems, cap;
    char **raw; int nraw;   /* unparsed header lines, re-emitted on save */
    char *path;
    bool dirty;
} store_t;

void store_init(store_t *st, const char *path);
void store_free(store_t *st);
void store_parse(store_t *st, const char *text);   /* parse FORMAT.md text into the store */
char *store_serialize(store_t *st);                /* malloc'd text; caller frees */
int  store_load(store_t *st);   /* 0 ok, 1 file missing (empty store), -1 error */
int  store_save(store_t *st);   /* 0 ok, -1 error (errno set); atomic via rename */
void item_blank(item_t *it, item_kind_t kind);     /* zero an item with -1 sentinels */
bool model_parse_place_spec(const char *s, char *name, size_t nn, double *lat, double *lon, char *zone, size_t nz);

int  store_add_item(store_t *st, const item_t *tmpl); /* copies tmpl, assigns id; returns index */
void store_delete_item(store_t *st, int idx);
void store_set_str(char **field, const char *value);   /* strdup helper, NULL clears */

/* Indices of items shown on `day`, ordered: all-day events, timed events by start, todos. */
int  store_day_items(const store_t *st, daynum_t day, int *out, int max);
int  store_day_load(const store_t *st, daynum_t day, int *pending_todos);

place_t *store_find_place(store_t *st, const char *name);
place_t *store_add_place(store_t *st, const char *name, double lat, double lon, const char *zone);
const place_t *store_place_for_day(const store_t *st, daynum_t day);
void store_set_loc(store_t *st, daynum_t from, const char *place);
void store_remove_locs(store_t *st, daynum_t from, daynum_t to);   /* entries with from <= day <= to */
/* Assign `place` to the days from..until (until = -1: from that day onward). Days
 * outside the range keep their location: the one in effect before the range is
 * restored after it, unless a later entry already says otherwise. */
void store_set_loc_range(store_t *st, daynum_t from, daynum_t until, const char *place);
void store_compact_locs(store_t *st);                               /* drop entries that repeat the place in effect */
/* rename a place everywhere it is referenced (timeline, home, items) */
void store_rename_place(store_t *st, const char *from, const char *to);
int  store_place_refs(const store_t *st, const char *name);         /* timeline + home + item references */
void store_delete_place(store_t *st, const char *name);
void store_set_home(store_t *st, const char *place);

/* recurrence */
bool repeat_parse(const char *s, repeat_t *r);                 /* "weekly mon wed every 2 until 2026-12-31" */
void repeat_format(const repeat_t *r, char *out, size_t n);
bool item_occurs_on(const item_t *it, daynum_t day);           /* an occurrence (not an all-day span) starts on day */
bool item_is_repeating(const item_t *it);
bool item_skipped(const item_t *it, daynum_t day);
void item_add_skip(item_t *it, daynum_t day);
daynum_t item_next_occurrence(const item_t *it, daynum_t after);   /* first occurrence > after, or -1 (searches 10 years) */
daynum_t item_prev_occurrence(const item_t *it, daynum_t before);  /* last occurrence < before, or -1 */
/* todos: done state of the occurrence shown on `day` (non-repeating: any done) */
bool todo_done_on(const item_t *it, daynum_t day);
/* open on its occurrence `day` and past its deadline: the due date if it has one, else the day itself
 * (a repeating todo has no single due date; each occurrence's deadline is its own day) */
bool todo_overdue_on(const item_t *it, daynum_t day, daynum_t today);
/* open, with an explicit due date that is today or within `horizon` days; a scheduled day alone
 * is a plan, not a deadline, so it never counts (repeating todos have no due date) */
bool todo_due_soon_on(const item_t *it, daynum_t day, daynum_t today, int horizon);
/* the latest occurrence before today that is still open, or -1; for a repeating todo that is the one to
 * mark done to clear the backlog, since earlier occurrences count as done with it */
daynum_t todo_last_open_before(const item_t *it, daynum_t today);
void todo_toggle_on(item_t *it, daynum_t day, daynum_t today, int now_min);

/* parsing helpers shared with the UI */
bool model_parse_date(const char *s, daynum_t *out);       /* YYYY-MM-DD */
bool model_parse_time(const char *s, int *minutes);        /* H:MM or HH:MM */
bool model_parse_dur(const char *s, int *minutes);         /* 45m 1h 1h30 2d 90 */
void model_format_dur(int minutes, char *out, size_t n);
void model_format_date(daynum_t d, char *out, size_t n);

#endif
