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

/* model.c — see model.h and FORMAT.md */
#include "model.h"

#include <ctype.h>
#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------- small helpers ---------- */

static char *xstrdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

void store_set_str(char **field, const char *value)
{
    free(*field);
    *field = value && *value ? xstrdup(value) : NULL;
}

static void rtrim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

static const char *skipws(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* copy the next whitespace-delimited word into tok (max n), return pointer past it */
static const char *word(const char *s, char *tok, size_t n)
{
    s = skipws(s);
    size_t k = 0;
    while (*s && *s != ' ' && *s != '\t') { if (k < n - 1) tok[k++] = *s; s++; }
    tok[k] = 0;
    return s;
}

static unsigned long long rng_state;

static unsigned long long rng(void)
{
    if (!rng_state) rng_state = (unsigned long long)time(NULL) * 6364136223846793005ULL + (unsigned long long)(size_t)&rng_state;
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static void new_id(store_t *st, char out[9])
{
    for (;;) {
        snprintf(out, 9, "%08llx", rng() & 0xffffffffULL);
        bool clash = false;
        for (int i = 0; i < st->nitems; i++) if (!strcmp(st->items[i].id, out)) { clash = true; break; }
        if (!clash) return;
    }
}

/* ---------- parsing helpers ---------- */

bool model_parse_date(const char *s, daynum_t *out)
{
    int y, m, d;
    char c;
    if (sscanf(s, "%d-%d-%d%c", &y, &m, &d, &c) != 3) return false;
    if (!civil_valid(y, m, d)) return false;
    *out = civil_to_days(y, m, d);
    return true;
}

bool model_parse_time(const char *s, int *minutes)
{
    int h, m;
    char c;
    if (sscanf(s, "%d:%d%c", &h, &m, &c) != 2) return false;
    if (h < 0 || h > 23 || m < 0 || m > 59) return false;
    *minutes = h * 60 + m;
    return true;
}

bool model_parse_dur(const char *s, int *minutes)
{
    int a, b;
    char u, c;
    int n = sscanf(s, "%d%c%d%c", &a, &u, &b, &c);
    if (n == 1) { if (a <= 0) return false; *minutes = a; return true; }
    if (n == 2) {
        if (a <= 0) return false;
        if (u == 'm') *minutes = a;
        else if (u == 'h') *minutes = a * 60;
        else if (u == 'd') *minutes = a * 1440;
        else return false;
        return true;
    }
    if (n == 3 && u == 'h' && a >= 0 && b >= 0 && b < 60 && (a > 0 || b > 0)) { *minutes = a * 60 + b; return true; }
    return false;
}

void model_format_dur(int m, char *out, size_t n)
{
    if (m >= 1440 && m % 1440 == 0) snprintf(out, n, "%dd", m / 1440);
    else if (m % 60 == 0) snprintf(out, n, "%dh", m / 60);
    else if (m < 60) snprintf(out, n, "%dm", m);
    else snprintf(out, n, "%dh%02d", m / 60, m % 60);
}

void model_format_date(daynum_t d, char *out, size_t n)
{
    civil_t c = days_to_civil(d);
    snprintf(out, n, "%04d-%02d-%02d", c.y, c.m, c.d);
}

/* ---------- store lifecycle ---------- */

void store_init(store_t *st, const char *path)
{
    memset(st, 0, sizeof *st);
    st->path = xstrdup(path);
}

static void item_free(item_t *it)
{
    free(it->title); free(it->at); free(it->tags); free(it->note);
    free(it->who); free(it->status); free(it->extra); free(it->skips);
}

void store_free(store_t *st)
{
    for (int i = 0; i < st->nplaces; i++) { free(st->places[i].name); free(st->places[i].zone); }
    free(st->places);
    for (int i = 0; i < st->nlocs; i++) free(st->locs[i].place);
    free(st->locs);
    for (int i = 0; i < st->nitems; i++) item_free(&st->items[i]);
    free(st->items);
    for (int i = 0; i < st->nraw; i++) free(st->raw[i]);
    free(st->raw);
    free(st->home);
    free(st->path);
    memset(st, 0, sizeof *st);
}

/* ---------- items ---------- */

int store_add_item(store_t *st, const item_t *tmpl)
{
    if (st->nitems == st->cap) {
        int ncap = st->cap ? st->cap * 2 : 64;
        item_t *p = realloc(st->items, (size_t)ncap * sizeof *p);
        if (!p) return -1;
        st->items = p;
        st->cap = ncap;
    }
    item_t *it = &st->items[st->nitems];
    *it = *tmpl;
    it->title = xstrdup(tmpl->title); it->at = xstrdup(tmpl->at); it->tags = xstrdup(tmpl->tags);
    it->note = xstrdup(tmpl->note); it->who = xstrdup(tmpl->who); it->status = xstrdup(tmpl->status);
    it->extra = xstrdup(tmpl->extra);
    it->skips = NULL;
    it->nskips = 0;
    for (int i = 0; i < tmpl->nskips; i++) item_add_skip(it, tmpl->skips[i]);
    if (!it->id[0]) new_id(st, it->id);
    st->dirty = true;
    return st->nitems++;
}

void store_delete_item(store_t *st, int idx)
{
    if (idx < 0 || idx >= st->nitems) return;
    item_free(&st->items[idx]);
    memmove(&st->items[idx], &st->items[idx + 1], (size_t)(st->nitems - idx - 1) * sizeof st->items[0]);
    st->nitems--;
    st->dirty = true;
}

static bool item_on_day(const item_t *it, daynum_t day)
{
    if (it->kind == ITEM_EVENT) {
        if (it->rep.freq == REP_NONE) {
            if (it->day == day) return true;
            return it->start < 0 && it->until >= 0 && day > it->day && day <= it->until;
        }
        long span = it->start < 0 && it->until > it->day ? it->until - it->day : 0;
        for (long k = 0; k <= span; k++) if (item_occurs_on(it, day - k)) return true;
        return false;
    }
    if (it->day >= 0) return it->rep.freq == REP_NONE ? it->day == day : item_occurs_on(it, day);
    return it->due == day;      /* unscheduled todo shows on its deadline */
}

static int item_rank(const item_t *it)
{
    if (it->kind == ITEM_TODO) return 2 * 1440 + (it->done_day >= 0 ? 1440 : 0) + (it->pri ? it->pri : 9) * 10;
    if (it->start < 0) return 0;
    return it->start + 1;
}

int store_day_items(const store_t *st, daynum_t day, int *out, int max)
{
    int n = 0;
    for (int i = 0; i < st->nitems && n < max; i++)
        if (item_on_day(&st->items[i], day)) out[n++] = i;
    /* insertion sort by rank; n is small */
    for (int i = 1; i < n; i++) {
        int v = out[i], r = item_rank(&st->items[v]), j = i - 1;
        while (j >= 0 && item_rank(&st->items[out[j]]) > r) { out[j + 1] = out[j]; j--; }
        out[j + 1] = v;
    }
    return n;
}

int store_day_load(const store_t *st, daynum_t day, int *pending)
{
    int n = 0, p = 0;
    for (int i = 0; i < st->nitems; i++) {
        const item_t *it = &st->items[i];
        if (!item_on_day(it, day)) continue;
        if (it->kind == ITEM_TODO) { if (!todo_done_on(it, day)) p++; }
        else n++;
    }
    if (pending) *pending = p;
    return n + p;
}

/* ---------- places and timeline ---------- */

place_t *store_find_place(store_t *st, const char *name)
{
    for (int i = 0; i < st->nplaces; i++)
        if (!strcmp(st->places[i].name, name)) return &st->places[i];
    return NULL;
}

place_t *store_add_place(store_t *st, const char *name, double lat, double lon, const char *zone)
{
    place_t *p = store_find_place(st, name);
    if (!p) {
        place_t *arr = realloc(st->places, (size_t)(st->nplaces + 1) * sizeof *arr);
        if (!arr) return NULL;
        st->places = arr;
        p = &st->places[st->nplaces++];
        memset(p, 0, sizeof *p);
        p->name = xstrdup(name);
    }
    p->lat = lat;
    p->lon = lon;
    store_set_str(&p->zone, zone);
    st->dirty = true;
    return p;
}

const place_t *store_place_for_day(const store_t *st, daynum_t day)
{
    const char *name = st->home;
    daynum_t best = -1;
    for (int i = 0; i < st->nlocs; i++)
        if (st->locs[i].from <= day && st->locs[i].from > best) { best = st->locs[i].from; name = st->locs[i].place; }
    if (!name) return NULL;
    return store_find_place((store_t *)st, name);
}

void store_set_loc(store_t *st, daynum_t from, const char *place)
{
    for (int i = 0; i < st->nlocs; i++)
        if (st->locs[i].from == from) { store_set_str(&st->locs[i].place, place); st->dirty = true; return; }
    loc_t *arr = realloc(st->locs, (size_t)(st->nlocs + 1) * sizeof *arr);
    if (!arr) return;
    st->locs = arr;
    st->locs[st->nlocs].from = from;
    st->locs[st->nlocs].place = xstrdup(place);
    st->nlocs++;
    st->dirty = true;
}

void store_remove_locs(store_t *st, daynum_t from, daynum_t to)
{
    int k = 0;
    for (int i = 0; i < st->nlocs; i++) {
        if (st->locs[i].from >= from && st->locs[i].from <= to) { free(st->locs[i].place); st->dirty = true; }
        else st->locs[k++] = st->locs[i];
    }
    st->nlocs = k;
}

static int cmp_loc(const void *a, const void *b)
{
    const loc_t *x = a, *y = b;
    return x->from < y->from ? -1 : x->from > y->from;
}

/* the entry governing `day`, or NULL when only `home` applies */
static const loc_t *loc_governing(const store_t *st, daynum_t day)
{
    const loc_t *best = NULL;
    for (int i = 0; i < st->nlocs; i++)
        if (st->locs[i].from <= day && (!best || st->locs[i].from > best->from)) best = &st->locs[i];
    return best;
}

/* drop entries that repeat the place already in effect */
static void loc_compact(store_t *st)
{
    qsort(st->locs, (size_t)st->nlocs, sizeof st->locs[0], cmp_loc);
    const char *cur = st->home;
    int k = 0;
    for (int i = 0; i < st->nlocs; i++) {
        if (cur && !strcmp(st->locs[i].place, cur)) { free(st->locs[i].place); st->dirty = true; continue; }
        cur = st->locs[i].place;
        st->locs[k++] = st->locs[i];
    }
    st->nlocs = k;
}

void store_compact_locs(store_t *st)
{
    loc_compact(st);
}

void store_rename_place(store_t *st, const char *from, const char *to)
{
    if (!from || !to || !*to || !strcmp(from, to)) return;
    place_t *p = store_find_place(st, from);
    if (p) store_set_str(&p->name, to);
    if (st->home && !strcmp(st->home, from)) store_set_str(&st->home, to);
    for (int i = 0; i < st->nlocs; i++) if (!strcmp(st->locs[i].place, from)) store_set_str(&st->locs[i].place, to);
    for (int i = 0; i < st->nitems; i++) if (st->items[i].at && !strcmp(st->items[i].at, from)) store_set_str(&st->items[i].at, to);
    st->dirty = true;
}

int store_place_refs(const store_t *st, const char *name)
{
    int n = 0;
    if (st->home && !strcmp(st->home, name)) n++;
    for (int i = 0; i < st->nlocs; i++) if (!strcmp(st->locs[i].place, name)) n++;
    for (int i = 0; i < st->nitems; i++) if (st->items[i].at && !strcmp(st->items[i].at, name)) n++;
    return n;
}

void store_delete_place(store_t *st, const char *name)
{
    for (int i = 0; i < st->nplaces; i++) {
        if (strcmp(st->places[i].name, name)) continue;
        free(st->places[i].name);
        free(st->places[i].zone);
        memmove(&st->places[i], &st->places[i + 1], (size_t)(st->nplaces - i - 1) * sizeof st->places[0]);
        st->nplaces--;
        st->dirty = true;
        return;
    }
}

void store_set_loc_range(store_t *st, daynum_t from, daynum_t until, const char *place)
{
    if (until >= 0 && until < from) { daynum_t t = from; from = until; until = t; }
    if (until < 0) {
        store_set_loc(st, from, place);
        loc_compact(st);
        return;
    }
    /* what should follow the range: the location in effect on until+1, unless that
     * came from an entry inside the range (which is about to be overwritten), in
     * which case the location from before the range */
    const loc_t *g = loc_governing(st, until + 1);
    const place_t *after;
    if (g && g->from >= from) after = store_place_for_day(st, from - 1);
    else after = store_place_for_day(st, until + 1);
    char restore[128] = "";
    if (after) snprintf(restore, sizeof restore, "%s", after->name);
    store_remove_locs(st, from, until);
    store_set_loc(st, from, place);
    if (restore[0] && strcmp(restore, place) != 0) store_set_loc(st, until + 1, restore);
    loc_compact(st);
}

void store_set_home(store_t *st, const char *place)
{
    store_set_str(&st->home, place);
    st->dirty = true;
}

/* ---------- recurrence ---------- */

static const char *const wd_names[7] = { "mon", "tue", "wed", "thu", "fri", "sat", "sun" };

static int wd_from(const char *tok)
{
    for (int i = 0; i < 7; i++) if (!strncmp(tok, wd_names[i], 3) && (tok[3] == 0 || isalpha((unsigned char)tok[3]))) return i;
    return -1;
}

bool repeat_parse(const char *s, repeat_t *r)
{
    memset(r, 0, sizeof *r);
    r->interval = 1;
    r->until = -1;
    char tok[32];
    const char *p = word(s, tok, sizeof tok);
    if (!strcmp(tok, "daily")) r->freq = REP_DAILY;
    else if (!strcmp(tok, "weekly")) r->freq = REP_WEEKLY;
    else if (!strcmp(tok, "monthly")) r->freq = REP_MONTHLY;
    else if (!strcmp(tok, "yearly")) r->freq = REP_YEARLY;
    else return false;
    for (;;) {
        p = word(p, tok, sizeof tok);
        if (!tok[0]) break;
        int wd;
        daynum_t d;
        if (!strcmp(tok, "every")) { p = word(p, tok, sizeof tok); r->interval = atoi(tok); if (r->interval < 1 || r->interval > 1000) return false; }
        else if (!strcmp(tok, "until")) { p = word(p, tok, sizeof tok); if (!model_parse_date(tok, &d)) return false; r->until = d; }
        else if (!strcmp(tok, "count")) { p = word(p, tok, sizeof tok); r->count = atoi(tok); if (r->count < 1) return false; }
        else if (r->freq == REP_WEEKLY && (wd = wd_from(tok)) >= 0) r->wdays |= 1 << wd;
        else if (r->freq == REP_MONTHLY && isdigit((unsigned char)tok[0]) && !isalpha((unsigned char)tok[strlen(tok) - 1])) { r->mday = atoi(tok); if (r->mday < 1 || r->mday > 31) return false; }
        else if (r->freq == REP_MONTHLY && (!strcmp(tok, "last") || (isdigit((unsigned char)tok[0]) && isalpha((unsigned char)tok[strlen(tok) - 1])))) {
            int nth = !strcmp(tok, "last") ? -1 : atoi(tok);
            if (nth == 0 || nth > 4) return false;
            p = word(p, tok, sizeof tok);
            if ((wd = wd_from(tok)) < 0) return false;
            r->nth = nth; r->nth_wday = wd;
        }
        else return false;
    }
    return true;
}

void repeat_format(const repeat_t *r, char *out, size_t n)
{
    static const char *const names[] = { "", "daily", "weekly", "monthly", "yearly" };
    if (r->freq == REP_NONE) { out[0] = 0; return; }
    size_t k = (size_t)snprintf(out, n, "%s", names[r->freq]);
    if (r->freq == REP_WEEKLY) for (int i = 0; i < 7; i++) if (r->wdays & (1 << i)) k += (size_t)snprintf(out + k, n - k, " %s", wd_names[i]);
    if (r->freq == REP_MONTHLY && r->nth) {
        static const char *const ord[] = { "", "1st", "2nd", "3rd", "4th" };
        k += (size_t)snprintf(out + k, n - k, " %s %s", r->nth < 0 ? "last" : ord[r->nth], wd_names[r->nth_wday]);
    } else if (r->freq == REP_MONTHLY && r->mday) k += (size_t)snprintf(out + k, n - k, " %d", r->mday);
    if (r->interval > 1) k += (size_t)snprintf(out + k, n - k, " every %d", r->interval);
    if (r->until >= 0) { char ds[16]; model_format_date(r->until, ds, sizeof ds); k += (size_t)snprintf(out + k, n - k, " until %s", ds); }
    if (r->count > 0) snprintf(out + k, n - k, " count %d", r->count);
}

bool item_is_repeating(const item_t *it)
{
    return it->rep.freq != REP_NONE && it->day >= 0;
}

bool item_skipped(const item_t *it, daynum_t day)
{
    for (int i = 0; i < it->nskips; i++) if (it->skips[i] == day) return true;
    return false;
}

void item_add_skip(item_t *it, daynum_t day)
{
    if (item_skipped(it, day)) return;
    daynum_t *p = realloc(it->skips, (size_t)(it->nskips + 1) * sizeof *p);
    if (!p) return;
    it->skips = p;
    it->skips[it->nskips++] = day;
}

/* does the rule (before skips and count) produce an occurrence on `day`? */
static bool rule_hits(const item_t *it, daynum_t day)
{
    const repeat_t *r = &it->rep;
    if (day < it->day) return false;
    if (r->until >= 0 && day > r->until) return false;
    civil_t s = days_to_civil(it->day), c = days_to_civil(day);
    switch (r->freq) {
    case REP_DAILY:
        return (day - it->day) % r->interval == 0;
    case REP_WEEKLY: {
        int mask = r->wdays ? r->wdays : 1 << civil_weekday(it->day);
        if (!(mask & (1 << civil_weekday(day)))) return false;
        long weeks = (civil_week_monday(day) - civil_week_monday(it->day)) / 7;
        return weeks % r->interval == 0;
    }
    case REP_MONTHLY: {
        long months = (long)(c.y - s.y) * 12 + (c.m - s.m);
        if (months < 0 || months % r->interval) return false;
        if (r->nth) {
            if (civil_weekday(day) != r->nth_wday) return false;
            if (r->nth > 0) return (c.d - 1) / 7 + 1 == r->nth;
            return c.d + 7 > civil_days_in_month(c.y, c.m);
        }
        int md = r->mday ? r->mday : s.d;
        return c.d == md;
    }
    case REP_YEARLY: {
        if ((c.y - s.y) % r->interval) return false;
        return c.m == s.m && c.d == s.d;
    }
    default: return day == it->day;
    }
}

bool item_occurs_on(const item_t *it, daynum_t day)
{
    if (it->rep.freq == REP_NONE) return day == it->day;
    if (!rule_hits(it, day)) return false;
    if (it->rep.count > 0) {
        /* the count applies to the rule; skips do not extend it */
        int n = 0;
        for (daynum_t d = it->day; d <= day; d++) if (rule_hits(it, d) && ++n > it->rep.count) return false;
    }
    return !item_skipped(it, day);
}

daynum_t item_next_occurrence(const item_t *it, daynum_t after)
{
    daynum_t start = after + 1 > it->day ? after + 1 : it->day;
    for (daynum_t d = start; d < start + 3660; d++) if (item_occurs_on(it, d)) return d;
    return -1;
}

daynum_t item_prev_occurrence(const item_t *it, daynum_t before)
{
    for (daynum_t d = before - 1; d >= it->day && d > before - 3660; d--) if (item_occurs_on(it, d)) return d;
    return -1;
}

bool todo_done_on(const item_t *it, daynum_t day)
{
    if (it->done_day < 0) return false;
    if (!item_is_repeating(it)) return true;
    return day <= it->done_day;
}

bool todo_overdue_on(const item_t *it, daynum_t day, daynum_t today)
{
    if (it->kind != ITEM_TODO || todo_done_on(it, day)) return false;
    daynum_t deadline = it->due >= 0 && !item_is_repeating(it) ? it->due : day;
    return deadline >= 0 && deadline < today;
}

bool todo_due_soon_on(const item_t *it, daynum_t day, daynum_t today, int horizon)
{
    if (it->kind != ITEM_TODO || it->due < 0 || item_is_repeating(it) || todo_done_on(it, day)) return false;
    return it->due >= today && it->due - today <= horizon;
}

daynum_t todo_last_open_before(const item_t *it, daynum_t today)
{
    if (it->kind != ITEM_TODO || it->day < 0) return -1;
    if (!item_is_repeating(it)) return it->done_day < 0 && it->day < today ? it->day : -1;
    daynum_t d = item_prev_occurrence(it, today);
    return d >= 0 && !todo_done_on(it, d) ? d : -1;
}

void todo_toggle_on(item_t *it, daynum_t day, daynum_t today, int now_min)
{
    if (!item_is_repeating(it)) {
        if (it->done_day >= 0) it->done_day = -1;
        else { it->done_day = today; it->done_min = now_min; }
        return;
    }
    if (todo_done_on(it, day)) it->done_day = item_prev_occurrence(it, day);   /* undo this and later occurrences */
    else { it->done_day = day; it->done_min = now_min; }
}

/* ---------- parse (from text) ---------- */

static void add_raw(store_t *st, const char *line)
{
    char **arr = realloc(st->raw, (size_t)(st->nraw + 1) * sizeof *arr);
    if (!arr) return;
    st->raw = arr;
    st->raw[st->nraw++] = xstrdup(line);
}

static void append_line(char **field, const char *line)
{
    if (!*field) { *field = xstrdup(line); return; }
    size_t a = strlen(*field), b = strlen(line);
    char *p = realloc(*field, a + b + 2);
    if (!p) return;
    p[a] = '\n';
    memcpy(p + a + 1, line, b + 1);
    *field = p;
}

/* "NAME | LAT LON | ZONE" */
bool model_parse_place_spec(const char *s, char *name, size_t nn, double *lat, double *lon, char *zone, size_t nz)
{
    const char *b1 = strchr(s, '|');
    if (!b1) return false;
    const char *b2 = strchr(b1 + 1, '|');
    if (!b2) return false;
    const char *ns = skipws(s);
    size_t k = (size_t)(b1 - ns);
    while (k && (ns[k - 1] == ' ' || ns[k - 1] == '\t')) k--;
    if (k == 0 || k >= nn) return false;
    memcpy(name, ns, k);
    name[k] = 0;
    if (sscanf(b1 + 1, "%lf %lf", lat, lon) != 2) return false;
    if (*lat < -90 || *lat > 90 || *lon < -180 || *lon > 180) return false;
    word(b2 + 1, zone, nz);
    return zone[0] != 0;
}

static void apply_field(item_t *it, const char *key, const char *val, const char *line)
{
    daynum_t d;
    int hh, mm;
    if (!strcmp(key, "id")) { snprintf(it->id, sizeof it->id, "%.8s", val); }
    else if (!strcmp(key, "at")) store_set_str(&it->at, val);
    else if (!strcmp(key, "tags")) store_set_str(&it->tags, val);
    else if (!strcmp(key, "who")) store_set_str(&it->who, val);
    else if (!strcmp(key, "status")) store_set_str(&it->status, val);
    else if (!strcmp(key, "note")) append_line(&it->note, val);
    else if (!strcmp(key, "pri")) { it->pri = atoi(val); if (it->pri < 1 || it->pri > 5) it->pri = 0; }
    else if (!strcmp(key, "repeat")) { if (!repeat_parse(val, &it->rep)) append_line(&it->extra, line); }
    else if (!strcmp(key, "skip")) {
        char ds[16];
        const char *p = val;
        for (;;) { p = word(p, ds, sizeof ds); if (!ds[0]) break; if (model_parse_date(ds, &d)) item_add_skip(it, d); }
    }
    else if (!strcmp(key, "until") && model_parse_date(val, &d)) it->until = d;
    else if (!strcmp(key, "due") && model_parse_date(val, &d)) it->due = d;
    else if (!strcmp(key, "done")) {
        char ds[16], ts[16];
        const char *p = word(val, ds, sizeof ds);
        word(p, ts, sizeof ts);
        if (model_parse_date(ds, &d)) {
            it->done_day = d;
            it->done_min = (ts[0] && sscanf(ts, "%d:%d", &hh, &mm) == 2) ? hh * 60 + mm : 0;
        }
    }
    else append_line(&it->extra, line);
}

void item_blank(item_t *it, item_kind_t kind)
{
    memset(it, 0, sizeof *it);
    it->kind = kind;
    it->day = it->until = it->due = it->done_day = -1;
    it->start = -1;
    it->rep.until = -1;
    it->rep.interval = 1;
}

static void parse_line(store_t *st, char *line, item_t *cur, bool *have)
{
    rtrim(line);
    if (line[0] == '#' || (line[0] == 0 && !*have)) return;

    if (line[0] == ' ' || line[0] == '\t') {           /* field line */
        if (!*have) { add_raw(st, line); return; }
        const char *p = skipws(line);
        const char *colon = strchr(p, ':');
        char key[32];
        size_t kl = colon ? (size_t)(colon - p) : sizeof key;
        if (!colon || kl >= sizeof key) { append_line(&cur->extra, line); return; }
        memcpy(key, p, kl);
        key[kl] = 0;
        const char *val = colon + 1;
        if (*val == ' ') val++;
        apply_field(cur, key, val, line);
        return;
    }

    if (*have) { store_add_item(st, cur); item_free(cur); *have = false; }
    if (line[0] == 0) return;

    char tok[64];
    const char *p = word(line, tok, sizeof tok);

    if (!strcmp(tok, "place")) {
        char name[128], zone[64];
        double lat, lon;
        if (model_parse_place_spec(p, name, sizeof name, &lat, &lon, zone, sizeof zone))
            store_add_place(st, name, lat, lon, zone);
        else add_raw(st, line);
    } else if (!strcmp(tok, "home")) {
        store_set_str(&st->home, skipws(p));
    } else if (!strcmp(tok, "loc")) {
        char ds[16];
        daynum_t d;
        p = word(p, ds, sizeof ds);
        if (model_parse_date(ds, &d) && *skipws(p)) store_set_loc(st, d, skipws(p));
        else add_raw(st, line);
    } else if (!strcmp(tok, "E")) {
        char ds[16], ts[16], du[16];
        daynum_t d;
        int start = -1, dur = 0;
        p = word(p, ds, sizeof ds);
        p = word(p, ts, sizeof ts);
        bool ok = model_parse_date(ds, &d);
        if (ok && strcmp(ts, "*") == 0) start = -1;
        else if (ok && model_parse_time(ts, &start)) {
            p = word(p, du, sizeof du);
            ok = model_parse_dur(du, &dur);
        } else ok = false;
        if (!ok) { add_raw(st, line); return; }
        item_blank(cur, ITEM_EVENT);
        cur->day = d; cur->start = start; cur->dur = dur;
        store_set_str(&cur->title, skipws(p));
        *have = true;
    } else if (!strcmp(tok, "T")) {
        char ds[16];
        daynum_t d = -1;
        p = word(p, ds, sizeof ds);
        if (strcmp(ds, "-") != 0 && !model_parse_date(ds, &d)) { add_raw(st, line); return; }
        item_blank(cur, ITEM_TODO);
        cur->day = d;
        store_set_str(&cur->title, skipws(p));
        *have = true;
    } else {
        add_raw(st, line);
    }
}

void store_parse(store_t *st, const char *text)
{
    item_t cur;
    bool have = false;
    char line[4096];
    const char *p = text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, p, n);
        line[n] = 0;
        parse_line(st, line, &cur, &have);
        p += nl ? (size_t)(nl - p) + 1 : n;
    }
    if (have) { store_add_item(st, &cur); item_free(&cur); }
    st->dirty = false;
}

/* ---------- serialize (to text) ---------- */

typedef struct { char *s; size_t n, cap; } sbuf_t;

static void sb_put(sbuf_t *b, const char *s, size_t n)
{
    if (b->n + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 4096;
        while (nc < b->n + n + 1) nc *= 2;
        char *p = realloc(b->s, nc);
        if (!p) return;
        b->s = p;
        b->cap = nc;
    }
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = 0;
}

static void sb_printf(sbuf_t *b, const char *fmt, ...)
{
    char tmp[4200];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof tmp) n = (int)sizeof tmp - 1;
    sb_put(b, tmp, (size_t)n);
}

static int cmp_item(const void *a, const void *b)
{
    const item_t *x = a, *y = b;
    if (x->day != y->day) return x->day < y->day ? -1 : 1;
    int rx = item_rank(x), ry = item_rank(y);
    if (rx != ry) return rx < ry ? -1 : 1;
    return strcmp(x->id, y->id);
}

static void put_field(sbuf_t *b, const char *key, const char *val)
{
    if (!val || !*val) return;
    const char *p = val;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        sb_printf(b, "  %s: %.*s\n", key, (int)n, p);
        p += n + (nl ? 1 : 0);
    }
}

char *store_serialize(store_t *st)
{
    sbuf_t b = { 0, 0, 0 };
    sb_printf(&b, "# soon agenda file — see FORMAT.md\n\n");
    for (int i = 0; i < st->nplaces; i++)
        sb_printf(&b, "place %s | %.4f %.4f | %s\n", st->places[i].name, st->places[i].lat, st->places[i].lon,
                  st->places[i].zone ? st->places[i].zone : "UTC");
    if (st->home) sb_printf(&b, "home %s\n", st->home);
    if (st->nlocs > 1) qsort(st->locs, (size_t)st->nlocs, sizeof st->locs[0], cmp_loc);   /* locs may be NULL when empty */
    for (int i = 0; i < st->nlocs; i++) {
        char ds[16];
        model_format_date(st->locs[i].from, ds, sizeof ds);
        sb_printf(&b, "loc %s %s\n", ds, st->locs[i].place);
    }
    if (st->nplaces || st->home || st->nlocs) sb_put(&b, "\n", 1);

    qsort(st->items, (size_t)st->nitems, sizeof st->items[0], cmp_item);
    for (int i = 0; i < st->nitems; i++) {
        const item_t *it = &st->items[i];
        char ds[16], du[16];
        const char *title = it->title ? it->title : "";
        if (it->kind == ITEM_EVENT) {
            model_format_date(it->day, ds, sizeof ds);
            if (it->start < 0) sb_printf(&b, "E %s * %s\n", ds, title);
            else {
                model_format_dur(it->dur, du, sizeof du);
                sb_printf(&b, "E %s %02d:%02d %s %s\n", ds, it->start / 60, it->start % 60, du, title);
            }
        } else {
            if (it->day >= 0) model_format_date(it->day, ds, sizeof ds); else strcpy(ds, "-");
            sb_printf(&b, "T %s %s\n", ds, title);
        }
        sb_printf(&b, "  id: %s\n", it->id);
        put_field(&b, "at", it->at);
        if (it->until >= 0) { model_format_date(it->until, ds, sizeof ds); sb_printf(&b, "  until: %s\n", ds); }
        if (it->due >= 0) { model_format_date(it->due, ds, sizeof ds); sb_printf(&b, "  due: %s\n", ds); }
        if (it->done_day >= 0) {
            model_format_date(it->done_day, ds, sizeof ds);
            sb_printf(&b, "  done: %s %02d:%02d\n", ds, it->done_min / 60, it->done_min % 60);
        }
        if (it->pri) sb_printf(&b, "  pri: %d\n", it->pri);
        if (it->rep.freq != REP_NONE) { char rs[128]; repeat_format(&it->rep, rs, sizeof rs); sb_printf(&b, "  repeat: %s\n", rs); }
        if (it->nskips) {
            sb_put(&b, "  skip:", 7);
            for (int k = 0; k < it->nskips; k++) { model_format_date(it->skips[k], ds, sizeof ds); sb_printf(&b, " %s", ds); }
            sb_put(&b, "\n", 1);
        }
        put_field(&b, "tags", it->tags);
        put_field(&b, "who", it->who);
        put_field(&b, "status", it->status);
        put_field(&b, "note", it->note);
        if (it->extra) sb_printf(&b, "%s\n", it->extra);
        sb_put(&b, "\n", 1);
    }
    for (int i = 0; i < st->nraw; i++) sb_printf(&b, "%s\n", st->raw[i]);
    return b.s ? b.s : xstrdup("");
}

/* ---------- file wrappers (not compiled for wasm) ---------- */
#ifndef SOON_NO_FILE_IO
#include <sys/stat.h>

int store_load(store_t *st)
{
    FILE *f = fopen(st->path, "rb");
    if (!f) return errno == ENOENT ? 1 : -1;
    sbuf_t b = { 0, 0, 0 };
    char chunk[8192];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) sb_put(&b, chunk, n);
    int rc = ferror(f) ? -1 : 0;
    fclose(f);
    if (rc == 0) store_parse(st, b.s ? b.s : "");
    free(b.s);
    return rc;
}

static void mkdirs_for(const char *path)
{
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(buf, 0755); *p = '/'; }
}

int store_save(store_t *st)
{
    mkdirs_for(st->path);
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", st->path);
    char *text = store_serialize(st);
    if (!text) return -1;
    FILE *f = fopen(tmp, "wb");
    if (!f) { free(text); return -1; }
    size_t len = strlen(text);
    int rc = fwrite(text, 1, len, f) == len ? 0 : -1;
    free(text);
    if (fclose(f) != 0) rc = -1;
    if (rc == 0 && rename(tmp, st->path) != 0) rc = -1;
    if (rc == 0) st->dirty = false;
    return rc;
}
#endif
