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

/* org.c — org-mode codec, see codecs.h */
#include "codecs.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"

static void warn(import_report_t *rep, const char *fmt, ...)
{
    if (!rep) return;
    rep->warnings++;
    size_t n = strlen(rep->warn);
    if (n + 2 >= sizeof rep->warn) return;
    if (n) { rep->warn[n++] = '\n'; rep->warn[n] = 0; }
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    snprintf(rep->warn + n, sizeof rep->warn - n, "%s", tmp);
}

/* ---------- export ---------- */

/* org repeater for the rules org can express: daily/weekly(one weekday)/monthly(same day)/yearly, every N */
static void org_repeater(const repeat_t *r, char *out, size_t n)
{
    out[0] = 0;
    switch (r->freq) {
    case REP_DAILY:   snprintf(out, n, " +%dd", r->interval); break;
    case REP_WEEKLY:  if (!r->wdays || (r->wdays & (r->wdays - 1)) == 0) snprintf(out, n, " +%dw", r->interval); break;
    case REP_MONTHLY: if (!r->nth) snprintf(out, n, " +%dm", r->interval); break;
    case REP_YEARLY:  snprintf(out, n, " +%dy", r->interval); break;
    default: break;
    }
}

static void org_stamp(char *out, size_t n, daynum_t d, int start, int dur, bool active)
{
    civil_t c = days_to_civil(d);
    const char *wd = civil_weekday_names[civil_weekday(d)];
    char o = active ? '<' : '[', e = active ? '>' : ']';
    if (start < 0) snprintf(out, n, "%c%04d-%02d-%02d %.3s%c", o, c.y, c.m, c.d, wd, e);
    else if (dur > 0) {
        int end = start + dur;
        if (end >= 1440) end = 1439;
        snprintf(out, n, "%c%04d-%02d-%02d %.3s %02d:%02d-%02d:%02d%c", o, c.y, c.m, c.d, wd, start / 60, start % 60, end / 60, end % 60, e);
    } else snprintf(out, n, "%c%04d-%02d-%02d %.3s %02d:%02d%c", o, c.y, c.m, c.d, wd, start / 60, start % 60, e);
}

/* insert the repeater before the closing bracket */
static void org_stamp_rep(char *stamp, size_t n, const repeat_t *r)
{
    char rp[16];
    org_repeater(r, rp, sizeof rp);
    if (!rp[0]) return;
    size_t k = strlen(stamp);
    if (k + strlen(rp) + 1 >= n) return;
    char close = stamp[k - 1];
    stamp[k - 1] = 0;
    strcat(stamp, rp);
    size_t m = strlen(stamp);
    stamp[m] = close;
    stamp[m + 1] = 0;
}

/* org tags allow letters, digits, _ @ # % */
static void org_tags(sbuf_t *b, const char *tags)
{
    if (!tags || !*tags) return;
    sb_puts(b, "  :");
    for (const char *p = tags; *p; p++) {
        if (*p == ' ') { if (p[1]) sb_put(b, ":", 1); }
        else if (isalnum((unsigned char)*p) || *p == '_' || *p == '@' || *p == '#' || *p == '%' || (unsigned char)*p >= 0x80) sb_put(b, p, 1);
        else sb_put(b, "_", 1);
    }
    sb_puts(b, ":");
}

static void org_prop(sbuf_t *b, const char *key, const char *val)
{
    if (!val || !*val) return;
    sb_printf(b, "  :%s: %s\n", key, val);
}

char *org_export(store_t *st)
{
    sbuf_t b = { 0, 0, 0 };
    sb_puts(&b, "#+TITLE: soon agenda\n#+STARTUP: overview\n\n");
    for (int i = 0; i < st->nitems; i++) {
        const item_t *it = &st->items[i];
        const char *title = it->title ? it->title : "(untitled)";
        char stamp[64], stamp2[64];
        if (it->kind == ITEM_TODO) {
            const char *kw = it->done_day >= 0 ? "DONE" : (it->status && str_ieq(it->status, "cancelled")) ? "CANCELLED" : "TODO";
            sb_printf(&b, "* %s ", kw);
            if (it->pri >= 1 && it->pri <= 5) sb_printf(&b, "[#%c] ", 'A' + it->pri - 1);
            sb_puts(&b, title);
            org_tags(&b, it->tags);
            sb_put(&b, "\n", 1);
            bool plan = it->done_day >= 0 || it->day >= 0 || it->due >= 0;
            if (plan) {
                sb_puts(&b, " ");
                if (it->done_day >= 0) { org_stamp(stamp, sizeof stamp, it->done_day, it->done_min, 0, false); sb_printf(&b, " CLOSED: %s", stamp); }
                if (it->day >= 0) { org_stamp(stamp, sizeof stamp, it->day, -1, 0, true); org_stamp_rep(stamp, sizeof stamp, &it->rep); sb_printf(&b, " SCHEDULED: %s", stamp); }
                if (it->due >= 0) { org_stamp(stamp, sizeof stamp, it->due, -1, 0, true); sb_printf(&b, " DEADLINE: %s", stamp); }
                sb_put(&b, "\n", 1);
            }
        } else {
            sb_puts(&b, "* ");
            sb_puts(&b, title);
            org_tags(&b, it->tags);
            sb_put(&b, "\n", 1);
            org_stamp(stamp, sizeof stamp, it->day, it->start, it->dur, true);
            org_stamp_rep(stamp, sizeof stamp, &it->rep);
            if (it->start < 0 && it->until > it->day) {
                org_stamp(stamp2, sizeof stamp2, it->until, -1, 0, true);
                sb_printf(&b, "  %s--%s\n", stamp, stamp2);
            } else sb_printf(&b, "  %s\n", stamp);
        }
        sb_puts(&b, "  :PROPERTIES:\n");
        org_prop(&b, "ID", it->id);
        org_prop(&b, "LOCATION", it->at);
        org_prop(&b, "WHO", it->who);
        if (it->rep.freq != REP_NONE) { char rs[128]; repeat_format(&it->rep, rs, sizeof rs); org_prop(&b, "REPEAT", rs); }
        if (it->nskips) {
            char sk[512] = "";
            for (int k = 0; k < it->nskips; k++) { char ds[16]; model_format_date(it->skips[k], ds, sizeof ds); strncat(sk, ds, sizeof sk - strlen(sk) - 1); strncat(sk, " ", sizeof sk - strlen(sk) - 1); }
            org_prop(&b, "SKIP", sk);
        }
        sb_puts(&b, "  :END:\n");
        if (it->note) {
            const char *p = it->note;
            while (*p) {
                const char *nl = strchr(p, '\n');
                size_t n = nl ? (size_t)(nl - p) : strlen(p);
                sb_printf(&b, "  %.*s\n", (int)n, p);
                p += n + (nl ? 1 : 0);
            }
        }
        sb_put(&b, "\n", 1);
    }
    return sb_take(&b);
}

/* ---------- import ---------- */

static repeat_t last_repeater;   /* set by parse_stamp when a repeater is present */

/* parse "<2026-09-15 Tue 09:00-10:30 +1w>" or "[...]"; returns chars consumed or 0 */
static int parse_stamp(const char *s, daynum_t *day, int *start, int *end, bool *active, bool *repeater)
{
    if (*s != '<' && *s != '[') return 0;
    char close = *s == '<' ? '>' : ']';
    const char *e = strchr(s, close);
    if (!e) return 0;
    char body[128];
    size_t n = (size_t)(e - s - 1);
    if (n >= sizeof body) return 0;
    memcpy(body, s + 1, n);
    body[n] = 0;
    int y, m, d;
    char tok[32];
    const char *p = word(body, tok, sizeof tok);
    if (sscanf(tok, "%d-%d-%d", &y, &m, &d) != 3 || !civil_valid(y, m, d)) return 0;
    *day = civil_to_days(y, m, d);
    *start = *end = -1;
    *active = *s == '<';
    if (repeater) *repeater = false;
    while (*(p = word(p, tok, sizeof tok)) || tok[0]) {
        int h1, m1, h2, m2;
        if (sscanf(tok, "%d:%d-%d:%d", &h1, &m1, &h2, &m2) == 4) { *start = h1 * 60 + m1; *end = h2 * 60 + m2; }
        else if (sscanf(tok, "%d:%d", &h1, &m1) == 2) *start = h1 * 60 + m1;
        else if (tok[0] == '+' || tok[0] == '.') {
            const char *q = tok;
            while (*q == '+' || *q == '.') q++;
            int iv = atoi(q);
            while (isdigit((unsigned char)*q)) q++;
            memset(&last_repeater, 0, sizeof last_repeater);
            last_repeater.interval = iv > 0 ? iv : 1;
            last_repeater.until = -1;
            last_repeater.freq = *q == 'd' ? REP_DAILY : *q == 'w' ? REP_WEEKLY : *q == 'm' ? REP_MONTHLY : *q == 'y' ? REP_YEARLY : REP_NONE;
            if (last_repeater.freq != REP_NONE && repeater) *repeater = true;
        }
        if (!tok[0]) break;
    }
    return (int)(e - s + 1);
}

/* find the first active timestamp in a line; optional "--<...>" range end */
static bool find_stamp(const char *line, daynum_t *day, int *start, int *end, daynum_t *until, bool *rep)
{
    for (const char *p = line; *p; p++) {
        if (*p != '<') continue;
        bool active;
        int n = parse_stamp(p, day, start, end, &active, rep);
        if (n && active) {
            *until = -1;
            if (p[n] == '-' && p[n + 1] == '-' && p[n + 2] == '<') {
                daynum_t d2; int s2, e2; bool a2, r2;
                if (parse_stamp(p + n + 2, &d2, &s2, &e2, &a2, &r2)) *until = d2;
            }
            return true;
        }
    }
    return false;
}

typedef struct {
    item_t it;
    bool have, keyword, stamp, repeater;
    char kw[16];
    bool in_props;
} org_state_t;

static int find_existing(store_t *st, const item_t *it)
{
    if (it->id[0]) for (int i = 0; i < st->nitems; i++) if (!strcmp(st->items[i].id, it->id)) return i;
    for (int i = 0; i < st->nitems; i++) {
        const item_t *x = &st->items[i];
        if (x->kind == it->kind && x->day == it->day && x->title && it->title && !strcmp(x->title, it->title)) return i;
    }
    return -1;
}

static void item_free_strings(item_t *it)
{
    free(it->title); free(it->at); free(it->tags); free(it->note); free(it->who); free(it->status); free(it->extra); free(it->skips);
    it->title = it->at = it->tags = it->note = it->who = it->status = it->extra = NULL;
    it->skips = NULL; it->nskips = 0;
}

/* copy fields from src into an existing item, keeping the target's id */
static void merge_into(item_t *dst, const item_t *src)
{
    char id[9];
    memcpy(id, dst->id, sizeof id);
    item_free_strings(dst);
    *dst = *src;
    dst->title = xstrdup(src->title); dst->at = xstrdup(src->at); dst->tags = xstrdup(src->tags);
    dst->note = xstrdup(src->note); dst->who = xstrdup(src->who); dst->status = xstrdup(src->status); dst->extra = xstrdup(src->extra);
    dst->skips = NULL; dst->nskips = 0;
    for (int i = 0; i < src->nskips; i++) item_add_skip(dst, src->skips[i]);
    memcpy(dst->id, id, sizeof id);
}

static void flush_item(store_t *st, org_state_t *s, import_report_t *rep)
{
    if (!s->have) return;
    item_t *it = &s->it;
    bool ok = true;
    if (s->keyword) {
        it->kind = ITEM_TODO;
        if (!strcmp(s->kw, "DONE")) { if (it->done_day < 0) { it->done_day = it->day >= 0 ? it->day : 0; it->done_min = 0; } }
        else if (!strcmp(s->kw, "CANCELLED") || !strcmp(s->kw, "CANCELED")) str_set(&it->status, "cancelled");
        else it->done_day = -1;
    } else if (s->stamp) {
        it->kind = ITEM_EVENT;
    } else {
        ok = false;
    }
    if (ok && it->kind == ITEM_EVENT && it->day < 0) ok = false;
    if (ok) {
        if (s->repeater && it->rep.freq == REP_NONE) it->rep = last_repeater;
        int idx = find_existing(st, it);
        if (idx >= 0) { merge_into(&st->items[idx], it); st->dirty = true; rep->updated++; }
        else { store_add_item(st, it); rep->added++; }
    } else rep->skipped++;
    item_free_strings(it);
    s->have = false;
}

/* headline: "*+ [KEYWORD] [[#P]] title [:tags:]" */
static bool parse_headline(const char *line, org_state_t *s)
{
    if (line[0] != '*') return false;
    const char *p = line;
    while (*p == '*') p++;
    if (*p != ' ') return false;
    p = skipws(p);
    item_blank(&s->it, ITEM_EVENT);
    s->have = true;
    s->keyword = s->stamp = s->repeater = false;
    s->kw[0] = 0;
    s->in_props = false;
    static const char *const kws[] = { "TODO", "DONE", "NEXT", "WAITING", "CANCELLED", "CANCELED", "STARTED" };
    for (size_t i = 0; i < sizeof kws / sizeof kws[0]; i++) {
        size_t n = strlen(kws[i]);
        if (!strncmp(p, kws[i], n) && (p[n] == ' ' || p[n] == 0)) { snprintf(s->kw, sizeof s->kw, "%s", kws[i]); s->keyword = true; p = skipws(p + n); break; }
    }
    if (p[0] == '[' && p[1] == '#' && p[2] >= 'A' && p[2] <= 'E' && p[3] == ']') { s->it.pri = p[2] - 'A' + 1; p = skipws(p + 4); }
    char title[1024];
    snprintf(title, sizeof title, "%s", p);
    rtrim(title);
    /* trailing :tags: */
    size_t n = strlen(title);
    if (n > 2 && title[n - 1] == ':') {
        const char *t = title + n - 2;
        while (t > title && *t != ' ') t--;
        if (*t == ' ' && t[1] == ':') {
            char tags[256] = "";
            for (const char *q = t + 2; *q; q++) {
                if (*q == ':') { if (q[1]) strncat(tags, " ", sizeof tags - strlen(tags) - 1); }
                else { size_t l = strlen(tags); if (l < sizeof tags - 1) { tags[l] = *q; tags[l + 1] = 0; } }
            }
            rtrim(tags);
            str_set(&s->it.tags, tags);
            title[t - title] = 0;
            rtrim(title);
        }
    }
    /* a timestamp in the headline itself */
    daynum_t d, u; int st_, en; bool rep;
    if (find_stamp(title, &d, &st_, &en, &u, &rep)) {
        s->it.day = d; s->it.start = st_; s->it.dur = (st_ >= 0 && en > st_) ? en - st_ : (st_ >= 0 ? 60 : 0);
        s->it.until = u; s->stamp = true; s->repeater |= rep;
        char *lt = strchr(title, '<');
        if (lt) { *lt = 0; rtrim(title); }
    }
    str_set(&s->it.title, title);
    return true;
}

static void parse_planning(const char *line, org_state_t *s)
{
    const char *p = line;
    while (*p) {
        const char *key = NULL;
        if (!strncmp(p, "SCHEDULED:", 10)) { key = "S"; p += 10; }
        else if (!strncmp(p, "DEADLINE:", 9)) { key = "D"; p += 9; }
        else if (!strncmp(p, "CLOSED:", 7)) { key = "C"; p += 7; }
        if (!key) { p++; continue; }
        p = skipws(p);
        daynum_t d; int st_, en; bool active, rep;
        int n = parse_stamp(p, &d, &st_, &en, &active, &rep);
        if (!n) continue;
        if (*key == 'S') { s->it.day = d; s->repeater |= rep; }
        else if (*key == 'D') { s->it.due = d; s->repeater |= rep; }
        else { s->it.done_day = d; s->it.done_min = st_ >= 0 ? st_ : 0; }
        p += n;
    }
}

int org_import(store_t *st, const char *text, import_report_t *rep)
{
    import_report_t local;
    if (!rep) rep = &local;
    memset(rep, 0, sizeof *rep);
    org_state_t s;
    memset(&s, 0, sizeof s);
    char line[4096];
    const char *p = text;
    while (next_line(&p, line, sizeof line)) {
        if (line[0] == '*') { flush_item(st, &s, rep); parse_headline(line, &s); continue; }
        if (!s.have) continue;
        const char *b = skipws(line);
        if (str_ieq(b, ":PROPERTIES:")) { s.in_props = true; continue; }
        if (str_ieq(b, ":END:")) { s.in_props = false; continue; }
        if (s.in_props) {
            if (b[0] == ':') {
                const char *c = strchr(b + 1, ':');
                if (c) {
                    char key[32];
                    size_t kl = (size_t)(c - b - 1);
                    if (kl < sizeof key) {
                        memcpy(key, b + 1, kl); key[kl] = 0;
                        const char *v = skipws(c + 1);
                        if (str_ieq(key, "ID")) snprintf(s.it.id, sizeof s.it.id, "%.8s", v);
                        else if (str_ieq(key, "LOCATION")) str_set(&s.it.at, v);
                        else if (str_ieq(key, "WHO")) str_set(&s.it.who, v);
                        else if (str_ieq(key, "REPEAT")) { if (!repeat_parse(v, &s.it.rep)) warn(rep, "bad :REPEAT: '%s'", v); }
                        else if (str_ieq(key, "SKIP")) { char ds[16]; const char *q = v; for (;;) { q = word(q, ds, sizeof ds); if (!ds[0]) break; daynum_t d; if (model_parse_date(ds, &d)) item_add_skip(&s.it, d); } }
                    }
                }
            }
            continue;
        }
        if (strstr(b, "SCHEDULED:") || strstr(b, "DEADLINE:") || strstr(b, "CLOSED:")) { parse_planning(b, &s); continue; }
        daynum_t d, u; int st_, en; bool rp;
        if (!s.stamp && !s.keyword && find_stamp(b, &d, &st_, &en, &u, &rp)) {
            s.it.day = d; s.it.start = st_; s.it.dur = (st_ >= 0 && en > st_) ? en - st_ : (st_ >= 0 ? 60 : 0);
            s.it.until = u; s.stamp = true; s.repeater |= rp;
            continue;
        }
        if (s.keyword && !s.stamp && find_stamp(b, &d, &st_, &en, &u, &rp)) { s.stamp = true; continue; } /* stray stamp on a todo: ignore */
        if (b[0] == '#' && b[1] == '+') continue;
        if (*b) str_append_line(&s.it.note, b);
    }
    flush_item(st, &s, rep);
    return rep->added + rep->updated;
}
