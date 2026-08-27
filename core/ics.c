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

/* ics.c — iCalendar (RFC 5545) codec, see codecs.h
 *
 * Timed events are exported in UTC, converted from the wall clock of the
 * event's place (or of the day's location), which every calendar client
 * imports correctly. All-day events and todo dates use VALUE=DATE.
 * On import, instants are converted back into the wall clock of the day's
 * location, so a meeting keeps its local time wherever it was entered.
 */
#include "codecs.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"
#include "tz.h"

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

static const char *zone_for(store_t *st, const item_t *it, daynum_t day)
{
    const place_t *p = it && it->at ? store_find_place(st, it->at) : NULL;
    if (!p) p = store_place_for_day(st, day);
    return p && p->zone ? p->zone : "UTC";
}

/* ---------- export ---------- */

static void ics_escape(sbuf_t *b, const char *s)
{
    for (; *s; s++) {
        if (*s == '\\' || *s == ',' || *s == ';') { sb_put(b, "\\", 1); sb_put(b, s, 1); }
        else if (*s == '\n') sb_put(b, "\\n", 2);
        else if (*s != '\r') sb_put(b, s, 1);
    }
}

static void ics_fold(sbuf_t *out, const char *line)
{
    size_t col = 0;
    for (const char *p = line; *p;) {
        size_t clen = 1;
        unsigned char c = (unsigned char)*p;
        if (c >= 0xF0) clen = 4; else if (c >= 0xE0) clen = 3; else if (c >= 0xC0) clen = 2;
        if (col + clen > 75) { sb_put(out, "\r\n ", 3); col = 1; }
        sb_put(out, p, clen);
        col += clen;
        p += clen;
    }
    sb_put(out, "\r\n", 2);
}

static void ics_prop(sbuf_t *out, const char *name, const char *val)
{
    if (!val || !*val) return;
    sbuf_t l = { 0, 0, 0 };
    sb_puts(&l, name);
    sb_put(&l, ":", 1);
    ics_escape(&l, val);
    ics_fold(out, l.s ? l.s : "");
    free(l.s);
}

static void ics_raw(sbuf_t *out, const char *line)
{
    ics_fold(out, line);
}

static void utc_stamp(char *out, size_t n, long long utc)
{
    daynum_t d = (daynum_t)(utc >= 0 ? utc / 86400 : (utc - 86399) / 86400);
    long long sec = utc - (long long)d * 86400;
    civil_t c = days_to_civil(d);
    snprintf(out, n, "%04d%02d%02dT%02d%02d%02dZ", c.y, c.m, c.d, (int)(sec / 3600), (int)(sec / 60 % 60), (int)(sec % 60));
}

static void date_stamp(char *out, size_t n, daynum_t d)
{
    civil_t c = days_to_civil(d);
    snprintf(out, n, "%04d%02d%02d", c.y, c.m, c.d);
}

static void categories(sbuf_t *b, const char *tags)
{
    if (!tags || !*tags) return;
    sbuf_t l = { 0, 0, 0 };
    sb_puts(&l, "CATEGORIES:");
    for (const char *p = tags; *p; p++) sb_put(&l, *p == ' ' ? "," : p, 1);
    ics_fold(b, l.s);
    free(l.s);
}

static const char *const ics_wd[7] = { "MO", "TU", "WE", "TH", "FR", "SA", "SU" };

static void rrule_out(sbuf_t *b, store_t *st, const item_t *it)
{
    const repeat_t *r = &it->rep;
    if (r->freq == REP_NONE) return;
    static const char *const freq[] = { "", "DAILY", "WEEKLY", "MONTHLY", "YEARLY" };
    char line[256];
    size_t k = (size_t)snprintf(line, sizeof line, "RRULE:FREQ=%s", freq[r->freq]);
    if (r->interval > 1) k += (size_t)snprintf(line + k, sizeof line - k, ";INTERVAL=%d", r->interval);
    if (r->freq == REP_WEEKLY && r->wdays) {
        k += (size_t)snprintf(line + k, sizeof line - k, ";BYDAY=");
        bool first = true;
        for (int i = 0; i < 7; i++) if (r->wdays & (1 << i)) { k += (size_t)snprintf(line + k, sizeof line - k, "%s%s", first ? "" : ",", ics_wd[i]); first = false; }
    }
    if (r->freq == REP_MONTHLY && r->nth) k += (size_t)snprintf(line + k, sizeof line - k, ";BYDAY=%d%s", r->nth, ics_wd[r->nth_wday]);
    else if (r->freq == REP_MONTHLY && r->mday) k += (size_t)snprintf(line + k, sizeof line - k, ";BYMONTHDAY=%d", r->mday);
    if (r->until >= 0) { char ds[16]; date_stamp(ds, sizeof ds, r->until); k += (size_t)snprintf(line + k, sizeof line - k, ";UNTIL=%s", ds); }
    if (r->count > 0) snprintf(line + k, sizeof line - k, ";COUNT=%d", r->count);
    ics_raw(b, line);
    for (int i = 0; i < it->nskips; i++) {
        char ds[32];
        if (it->kind == ITEM_EVENT && it->start >= 0) {
            civil_t c = days_to_civil(it->skips[i]);
            long long t0;
            if (tz_wall_to_utc(zone_for(st, it, it->skips[i]), c.y, c.m, c.d, it->start / 60, it->start % 60, &t0) != 0) t0 = (long long)it->skips[i] * 86400 + it->start * 60;
            utc_stamp(ds, sizeof ds, t0);
            snprintf(line, sizeof line, "EXDATE:%s", ds);
        } else {
            date_stamp(ds, sizeof ds, it->skips[i]);
            snprintf(line, sizeof line, "EXDATE;VALUE=DATE:%s", ds);
        }
        ics_raw(b, line);
    }
}

char *ics_export(store_t *st, long long now_utc)
{
    sbuf_t b = { 0, 0, 0 };
    char stamp[32], s2[32], line[128], uid[32];
    utc_stamp(stamp, sizeof stamp, now_utc);
    sb_puts(&b, "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//soon//agenda//EN\r\nCALSCALE:GREGORIAN\r\n");
    for (int i = 0; i < st->nitems; i++) {
        const item_t *it = &st->items[i];
        snprintf(uid, sizeof uid, "soon-%s", it->id);
        if (it->kind == ITEM_EVENT) {
            sb_puts(&b, "BEGIN:VEVENT\r\n");
            ics_prop(&b, "UID", uid);
            snprintf(line, sizeof line, "DTSTAMP:%s", stamp); ics_raw(&b, line);
            if (it->start < 0) {
                date_stamp(s2, sizeof s2, it->day);
                snprintf(line, sizeof line, "DTSTART;VALUE=DATE:%s", s2); ics_raw(&b, line);
                date_stamp(s2, sizeof s2, (it->until > it->day ? it->until : it->day) + 1);
                snprintf(line, sizeof line, "DTEND;VALUE=DATE:%s", s2); ics_raw(&b, line);
            } else {
                const char *zone = zone_for(st, it, it->day);
                civil_t c = days_to_civil(it->day);
                long long t0;
                if (tz_wall_to_utc(zone, c.y, c.m, c.d, it->start / 60, it->start % 60, &t0) != 0)
                    t0 = (long long)it->day * 86400 + it->start * 60;
                utc_stamp(s2, sizeof s2, t0);
                snprintf(line, sizeof line, "DTSTART:%s", s2); ics_raw(&b, line);
                utc_stamp(s2, sizeof s2, t0 + (long long)(it->dur > 0 ? it->dur : 60) * 60);
                snprintf(line, sizeof line, "DTEND:%s", s2); ics_raw(&b, line);
                snprintf(line, sizeof line, "X-SOON-TZID:%s", zone); ics_raw(&b, line);
            }
            ics_prop(&b, "SUMMARY", it->title);
            ics_prop(&b, "LOCATION", it->at);
            ics_prop(&b, "DESCRIPTION", it->note);
            ics_prop(&b, "X-SOON-WHO", it->who);
            categories(&b, it->tags);
            rrule_out(&b, st, it);
            sb_puts(&b, "END:VEVENT\r\n");
        } else {
            sb_puts(&b, "BEGIN:VTODO\r\n");
            ics_prop(&b, "UID", uid);
            snprintf(line, sizeof line, "DTSTAMP:%s", stamp); ics_raw(&b, line);
            ics_prop(&b, "SUMMARY", it->title);
            if (it->day >= 0) { date_stamp(s2, sizeof s2, it->day); snprintf(line, sizeof line, "DTSTART;VALUE=DATE:%s", s2); ics_raw(&b, line); }
            if (it->due >= 0) { date_stamp(s2, sizeof s2, it->due); snprintf(line, sizeof line, "DUE;VALUE=DATE:%s", s2); ics_raw(&b, line); }
            if (it->done_day >= 0) {
                civil_t c = days_to_civil(it->done_day);
                long long t0;
                if (tz_wall_to_utc(zone_for(st, NULL, it->done_day), c.y, c.m, c.d, it->done_min / 60, it->done_min % 60, &t0) != 0)
                    t0 = (long long)it->done_day * 86400;
                utc_stamp(s2, sizeof s2, t0);
                snprintf(line, sizeof line, "COMPLETED:%s", s2); ics_raw(&b, line);
                ics_raw(&b, "STATUS:COMPLETED");
                ics_raw(&b, "PERCENT-COMPLETE:100");
            } else if (it->status && str_ieq(it->status, "cancelled")) ics_raw(&b, "STATUS:CANCELLED");
            else ics_raw(&b, "STATUS:NEEDS-ACTION");
            if (it->pri >= 1 && it->pri <= 5) { snprintf(line, sizeof line, "PRIORITY:%d", it->pri * 2 - 1); ics_raw(&b, line); }
            ics_prop(&b, "DESCRIPTION", it->note);
            ics_prop(&b, "X-SOON-WHO", it->who);
            categories(&b, it->tags);
            rrule_out(&b, st, it);
            sb_puts(&b, "END:VTODO\r\n");
        }
    }
    sb_puts(&b, "END:VCALENDAR\r\n");
    return sb_take(&b);
}

/* ---------- import ---------- */

static char *unfold(const char *text)
{
    sbuf_t b = { 0, 0, 0 };
    for (const char *p = text; *p; p++) {
        if (*p == '\r') continue;
        if (*p == '\n' && (p[1] == ' ' || p[1] == '\t')) { p++; continue; }
        sb_put(&b, p, 1);
    }
    return sb_take(&b);
}

static void unescape(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '\\' && s[1]) { s++; *o++ = (*s == 'n' || *s == 'N') ? '\n' : *s; }
        else *o++ = *s;
    }
    *o = 0;
}

typedef struct {
    bool is_date;      /* VALUE=DATE */
    daynum_t day;
    int hh, mm, ss;
    bool utc;          /* trailing Z */
    char tzid[64];
} dt_t;

static bool parse_dt(const char *params, const char *val, dt_t *out)
{
    memset(out, 0, sizeof *out);
    int y, m, d, hh = 0, mi = 0, ss = 0;
    size_t n = strlen(val);
    if (n < 8 || sscanf(val, "%4d%2d%2d", &y, &m, &d) != 3 || !civil_valid(y, m, d)) return false;
    out->day = civil_to_days(y, m, d);
    if (n == 8 || strstr(params, "VALUE=DATE")) { out->is_date = true; return true; }
    if (val[8] != 'T' || sscanf(val + 9, "%2d%2d%2d", &hh, &mi, &ss) < 2) return false;
    out->hh = hh; out->mm = mi; out->ss = ss;
    out->utc = val[n - 1] == 'Z';
    const char *tz = strstr(params, "TZID=");
    if (tz) {
        tz += 5;
        size_t k = 0;
        while (tz[k] && tz[k] != ';' && tz[k] != ':' && k < sizeof out->tzid - 1) { out->tzid[k] = tz[k]; k++; }
        out->tzid[k] = 0;
    }
    return true;
}

/* instant -> wall clock of the day's location */
static void to_local(store_t *st, const dt_t *dt, daynum_t *day, int *minute)
{
    if (dt->is_date) { *day = dt->day; *minute = -1; return; }
    long long utc;
    if (dt->utc) utc = (long long)dt->day * 86400 + dt->hh * 3600 + dt->mm * 60 + dt->ss;
    else if (dt->tzid[0] && tz_zone_exists(dt->tzid)) {
        civil_t c = days_to_civil(dt->day);
        if (tz_wall_to_utc(dt->tzid, c.y, c.m, c.d, dt->hh, dt->mm, &utc) != 0) { *day = dt->day; *minute = dt->hh * 60 + dt->mm; return; }
    } else { *day = dt->day; *minute = dt->hh * 60 + dt->mm; return; }   /* floating */
    daynum_t approx = (daynum_t)(utc / 86400);
    const char *zone = zone_for(st, NULL, approx);
    int y, m, d, hh, mm, off;
    if (tz_utc_to_wall(zone, utc, &y, &m, &d, &hh, &mm, &off) == 0) { *day = civil_to_days(y, m, d); *minute = hh * 60 + mm; }
    else { *day = approx; *minute = (int)((utc % 86400) / 60); }
}

static int parse_duration(const char *s)
{
    int total = 0, num = 0;
    bool time = false, neg = false;
    if (*s == '-') { neg = true; s++; }
    if (*s != 'P') return -1;
    for (s++; *s; s++) {
        if (isdigit((unsigned char)*s)) { num = num * 10 + (*s - '0'); continue; }
        switch (*s) {
        case 'T': time = true; break;
        case 'W': total += num * 7 * 1440; break;
        case 'D': total += num * 1440; break;
        case 'H': total += num * 60; break;
        case 'M': total += time ? num : 0; break;
        case 'S': break;
        default: return -1;
        }
        num = 0;
    }
    return neg ? -total : total;
}

static int find_by_uid(store_t *st, const char *uid)
{
    if (!strncmp(uid, "soon-", 5) && strlen(uid) == 13) {
        for (int i = 0; i < st->nitems; i++) if (!strcmp(st->items[i].id, uid + 5)) return i;
    }
    char needle[600];
    snprintf(needle, sizeof needle, "  uid: %s", uid);
    for (int i = 0; i < st->nitems; i++) {
        const char *x = st->items[i].extra;
        if (!x) continue;
        const char *h = strstr(x, needle);
        if (h && (h == x || h[-1] == '\n') && (h[strlen(needle)] == 0 || h[strlen(needle)] == '\n')) return i;
    }
    return -1;
}

static void item_free_strings(item_t *it)
{
    free(it->title); free(it->at); free(it->tags); free(it->note); free(it->who); free(it->status); free(it->extra); free(it->skips);
    it->title = it->at = it->tags = it->note = it->who = it->status = it->extra = NULL;
    it->skips = NULL; it->nskips = 0;
}

/* FREQ, INTERVAL, BYDAY (weekly list, or one nth-weekday for monthly), BYMONTHDAY, UNTIL, COUNT.
 * Anything else (BYMONTH, BYSETPOS, hourly...) is refused so the rule is kept as text. */
static bool rrule_in(const char *val, repeat_t *r)
{
    repeat_t t;
    memset(&t, 0, sizeof t);
    t.interval = 1;
    t.until = -1;
    char buf[512];
    snprintf(buf, sizeof buf, "%s", val);
    for (char *part = strtok(buf, ";"); part; part = strtok(NULL, ";")) {
        char *eq = strchr(part, '=');
        if (!eq) return false;
        *eq = 0;
        const char *v = eq + 1;
        if (!strcmp(part, "FREQ")) {
            if (!strcmp(v, "DAILY")) t.freq = REP_DAILY;
            else if (!strcmp(v, "WEEKLY")) t.freq = REP_WEEKLY;
            else if (!strcmp(v, "MONTHLY")) t.freq = REP_MONTHLY;
            else if (!strcmp(v, "YEARLY")) t.freq = REP_YEARLY;
            else return false;
        } else if (!strcmp(part, "INTERVAL")) { t.interval = atoi(v); if (t.interval < 1) return false; }
        else if (!strcmp(part, "COUNT")) { t.count = atoi(v); if (t.count < 1) return false; }
        else if (!strcmp(part, "UNTIL")) {
            int y, m, d;
            if (sscanf(v, "%4d%2d%2d", &y, &m, &d) != 3 || !civil_valid(y, m, d)) return false;
            t.until = civil_to_days(y, m, d);
        } else if (!strcmp(part, "BYDAY")) {
            char days[128];
            snprintf(days, sizeof days, "%s", v);
            for (char *q = days; *q;) {
                char *c = strchr(q, ',');
                if (c) *c = 0;
                int nth = 0;
                const char *w = q;
                if (*w == '-' || *w == '+' || isdigit((unsigned char)*w)) { nth = atoi(w); while (*w == '-' || *w == '+' || isdigit((unsigned char)*w)) w++; }
                int wd = -1;
                for (int i = 0; i < 7; i++) if (!strcmp(w, ics_wd[i])) wd = i;
                if (wd < 0) return false;
                if (nth) { if (t.nth) return false; t.nth = nth; t.nth_wday = wd; }
                else t.wdays |= 1 << wd;
                if (!c) break;
                q = c + 1;
            }
        } else if (!strcmp(part, "BYMONTHDAY")) { t.mday = atoi(v); if (t.mday < 1 || t.mday > 31 || strchr(v, ',')) return false; }
        else if (!strcmp(part, "WKST")) { /* ignored: weeks start on Monday here */ }
        else return false;
    }
    if (t.freq == REP_NONE) return false;
    if (t.freq != REP_WEEKLY && t.wdays) return false;
    if (t.freq != REP_MONTHLY && (t.nth || t.mday)) return false;
    if (t.nth && (t.nth > 4 || t.nth < -1)) return false;
    *r = t;
    return true;
}

int ics_import(store_t *st, const char *text, import_report_t *rep)
{
    import_report_t local;
    if (!rep) rep = &local;
    memset(rep, 0, sizeof *rep);
    char *flat = unfold(text);
    const char *p = flat;
    char line[8192];
    item_t it;
    bool in = false;
    dt_t dtstart, dtend, due, completed;
    bool have_start = false, have_end = false, have_due = false, have_completed = false;
    int duration = -1;
    char uid[512] = "";
    char status[32] = "";

    while (next_line(&p, line, sizeof line)) {
        if (str_ieq(line, "BEGIN:VEVENT") || str_ieq(line, "BEGIN:VTODO")) {
            item_blank(&it, str_ieq(line, "BEGIN:VEVENT") ? ITEM_EVENT : ITEM_TODO);
            in = true;
            have_start = have_end = have_due = have_completed = false;
            duration = -1;
            uid[0] = status[0] = 0;
            continue;
        }
        if (str_ieq(line, "END:VEVENT") || str_ieq(line, "END:VTODO")) {
            if (!in) continue;
            in = false;
            if (it.kind == ITEM_EVENT) {
                if (!have_start) { rep->skipped++; item_free_strings(&it); continue; }
                int minute;
                to_local(st, &dtstart, &it.day, &minute);
                it.start = minute;
                if (minute >= 0) {
                    if (have_end) {
                        daynum_t d2; int m2;
                        to_local(st, &dtend, &d2, &m2);
                        long dm = (d2 - it.day) * 1440 + (m2 >= 0 ? m2 : 0) - minute;
                        it.dur = dm > 0 && dm < 1440 * 366 ? (int)dm : 60;
                    } else if (duration > 0) it.dur = duration;
                    else it.dur = 60;
                } else {
                    it.dur = 0;
                    if (have_end && dtend.is_date && dtend.day - 1 > it.day) it.until = dtend.day - 1;
                    else if (duration >= 1440 && duration / 1440 > 1) it.until = it.day + duration / 1440 - 1;
                }
            } else {
                if (have_start) { int m; to_local(st, &dtstart, &it.day, &m); }
                if (have_due) { int m; to_local(st, &due, &it.due, &m); }
                if (have_completed) { int m; to_local(st, &completed, &it.done_day, &m); it.done_min = m >= 0 ? m : 0; }
                else if (str_ieq(status, "COMPLETED")) { it.done_day = it.due >= 0 ? it.due : it.day >= 0 ? it.day : 0; it.done_min = 0; }
                if (str_ieq(status, "CANCELLED")) str_set(&it.status, "cancelled");
            }
            int idx = uid[0] ? find_by_uid(st, uid) : -1;
            if (idx < 0) {
                for (int i = 0; i < st->nitems; i++) {
                    const item_t *x = &st->items[i];
                    if (x->kind == it.kind && x->day == it.day && x->title && it.title && !strcmp(x->title, it.title)) { idx = i; break; }
                }
            }
            if (uid[0] && !(!strncmp(uid, "soon-", 5) && strlen(uid) == 13)) {
                char extra[600];
                snprintf(extra, sizeof extra, "  uid: %s", uid);
                str_append_line(&it.extra, extra);
            } else if (uid[0]) snprintf(it.id, sizeof it.id, "%.8s", uid + 5);
            if (idx >= 0) {
                item_t *dst = &st->items[idx];
                char id[9]; memcpy(id, dst->id, sizeof id);
                item_free_strings(dst);
                *dst = it;
                memcpy(dst->id, id, sizeof id);
                st->dirty = true;
                rep->updated++;
            } else {
                store_add_item(st, &it);
                item_free_strings(&it);
                rep->added++;
            }
            continue;
        }
        if (!in) continue;
        char *colon = strchr(line, ':');
        if (!colon) continue;
        /* property name, parameters (kept in `params` with their leading ';'), value */
        char *val = colon + 1;
        char params[256] = "";
        char *semi = strchr(line, ';');
        if (semi && semi < colon) { snprintf(params, sizeof params, "%.*s", (int)(colon - semi), semi); *semi = 0; }
        else *colon = 0;
        for (char *q = line; *q; q++) *q = (char)toupper((unsigned char)*q);
        if (!strcmp(line, "SUMMARY")) { unescape(val); str_set(&it.title, val); }
        else if (!strcmp(line, "DESCRIPTION")) { unescape(val); str_set(&it.note, val); }
        else if (!strcmp(line, "LOCATION")) { unescape(val); str_set(&it.at, val); }
        else if (!strcmp(line, "X-SOON-WHO")) { unescape(val); str_set(&it.who, val); }
        else if (!strcmp(line, "CATEGORIES")) { unescape(val); for (char *q = val; *q; q++) if (*q == ',') *q = ' '; str_set(&it.tags, val); }
        else if (!strcmp(line, "UID")) snprintf(uid, sizeof uid, "%s", val);
        else if (!strcmp(line, "DTSTART")) have_start = parse_dt(params, val, &dtstart);
        else if (!strcmp(line, "DTEND")) have_end = parse_dt(params, val, &dtend);
        else if (!strcmp(line, "DUE")) have_due = parse_dt(params, val, &due);
        else if (!strcmp(line, "COMPLETED")) have_completed = parse_dt(params, val, &completed);
        else if (!strcmp(line, "DURATION")) duration = parse_duration(val);
        else if (!strcmp(line, "STATUS")) snprintf(status, sizeof status, "%s", val);
        else if (!strcmp(line, "PRIORITY")) { int pr = atoi(val); it.pri = pr <= 0 ? 0 : pr <= 2 ? 1 : pr <= 4 ? 2 : pr <= 6 ? 3 : pr <= 8 ? 4 : 5; }
        else if (!strcmp(line, "RRULE")) {
            if (!rrule_in(val, &it.rep)) {
                char extra[600];
                snprintf(extra, sizeof extra, "  rrule: %s", val);
                str_append_line(&it.extra, extra);
                warn(rep, "'%s': RRULE too complex, kept as text: %s", it.title ? it.title : uid, val);
            }
        }
        else if (!strcmp(line, "EXDATE")) {
            /* one or more dates, comma separated */
            for (char *q = strtok(val, ","); q; q = strtok(NULL, ",")) {
                dt_t x;
                if (parse_dt(params, q, &x)) { daynum_t d; int m; to_local(st, &x, &d, &m); item_add_skip(&it, d); }
            }
        }
    }
    if (in) item_free_strings(&it);
    free(flat);
    return rep->added + rep->updated;
}
