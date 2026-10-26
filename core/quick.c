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

/* quick.c — the freeform capture parser. See quick.h for the grammar. */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quick.h"
#include "datejump.h"

#define MAX_QTOK 48
#define TOK_LEN 256

typedef struct {
    char text[TOK_LEN];
    bool quoted;        /* came in double quotes: always title text */
    bool claimed;
} tok_t;

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static bool ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

static bool all_digits(const char *s)
{
    if (!*s) return false;
    for (; *s; s++) if (!isdigit((unsigned char)*s)) return false;
    return true;
}

static void add_msg(char *msg, size_t n, const char *text)
{
    if (!msg || !n) return;
    size_t len = strlen(msg);
    snprintf(msg + len, n - len, "%s%s", len ? "; " : "", text);
}

/* split into tokens; "..." groups words and marks them as title text.
 * A sigil directly before a quote (@"Duluth GA") keeps the sigil. */
static int tokenize(const char *line, tok_t *t, int max)
{
    int n = 0;
    const char *p = line;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        tok_t *k = &t[n];
        memset(k, 0, sizeof *k);
        size_t i = 0;
        if (*p == '@' || *p == '&') {           /* sigil may be followed by a quote */
            if (i + 1 < TOK_LEN) k->text[i++] = *p;
            p++;
        }
        if (*p == '"') {
            p++;
            if (i == 0) k->quoted = true;
            while (*p && *p != '"') { if (i + 1 < TOK_LEN) k->text[i++] = *p; p++; }
            if (*p == '"') p++;
        } else {
            while (*p && *p != ' ' && *p != '\t') { if (i + 1 < TOK_LEN) k->text[i++] = *p; p++; }
        }
        k->text[i] = 0;
        if (k->text[0]) n++;
    }
    return n;
}

/* ---------- the repeat clause ---------- */

/* map natural frequency words onto the repeat_parse grammar */
static const char *freq_word(const char *w)
{
    if (ieq(w, "daily") || ieq(w, "day") || ieq(w, "days")) return "daily";
    if (ieq(w, "weekly") || ieq(w, "week") || ieq(w, "weeks")) return "weekly";
    if (ieq(w, "monthly") || ieq(w, "month") || ieq(w, "months")) return "monthly";
    if (ieq(w, "yearly") || ieq(w, "year") || ieq(w, "years") || ieq(w, "annually")) return "yearly";
    return NULL;
}

/* rewrite the tokens after "every" into repeat_parse grammar:
 *   mon wed            -> weekly mon wed
 *   weekdays           -> weekly mon tue wed thu fri
 *   2 weeks            -> weekly every 2
 *   day / monthly      -> daily / monthly
 *   until 20.12        -> until 2026-12-20  (any datejump form)
 */
static bool rewrite_repeat(tok_t *t, int from, int n, daynum_t today, repeat_t *rep, char *msg, size_t msglen)
{
    char buf[256] = "";
    size_t len = 0;
    int i = from;
    if (i >= n) { add_msg(msg, msglen, "'every' needs a rule after it"); return false; }
#define EMIT(w) (len += (size_t)snprintf(buf + len, sizeof buf - len, "%s%s", len ? " " : "", (w)))
    const char *fw = freq_word(t[i].text);
    if (fw) { EMIT(fw); i++; }
    else if (ieq(t[i].text, "weekdays")) { EMIT("weekly mon tue wed thu fri"); i++; }
    else if (datejump_weekday_from_name(t[i].text) >= 0) EMIT("weekly");
    else if (all_digits(t[i].text) && i + 1 < n && freq_word(t[i + 1].text)) {
        EMIT(freq_word(t[i + 1].text)); EMIT("every"); EMIT(t[i].text);
        i += 2;
    } else { add_msg(msg, msglen, "cannot read the repeat rule"); return false; }
    for (; i < n; i++) {
        if (ieq(t[i].text, "until")) {
            if (i + 1 >= n) { add_msg(msg, msglen, "'until' needs a date"); return false; }
            daynum_t d;
            char err[64], iso[16];
            if (datejump_parse(t[i + 1].text, today, &d, err, sizeof err) != 0) { add_msg(msg, msglen, "cannot read the 'until' date"); return false; }
            model_format_date(d, iso, sizeof iso);
            EMIT("until"); EMIT(iso);
            i++;
        } else EMIT(t[i].text);
    }
#undef EMIT
    if (!repeat_parse(buf, rep)) { add_msg(msg, msglen, "cannot read the repeat rule"); return false; }
    return true;
}

/* ---------- times and durations ---------- */

static bool parse_time_tok(const char *s, int *start, int *end)
{
    int h1, m1, h2, m2;
    char c;
    if (sscanf(s, "%d:%d-%d:%d%c", &h1, &m1, &h2, &m2, &c) == 4) {
        if (h1 > 23 || m1 > 59 || h2 > 23 || m2 > 59 || h1 < 0 || m1 < 0 || h2 < 0 || m2 < 0) return false;
        *start = h1 * 60 + m1;
        *end = h2 * 60 + m2;
        return true;
    }
    if (model_parse_time(s, start)) { *end = -1; return true; }
    return false;
}

/* ---------- dates ---------- */

/* try the unclaimed, unquoted window t[i..i+w) as one datejump expression */
static bool try_date_window(tok_t *t, int n, int i, int w, daynum_t today, daynum_t *out)
{
    if (i + w > n) return false;
    char buf[128] = "";
    size_t len = 0;
    bool nonnum = false;
    for (int j = i; j < i + w; j++) {
        if (t[j].claimed || t[j].quoted) return false;
        if (strchr("#@&!*", t[j].text[0]) || !strncmp(t[j].text, "due:", 4)) return false;
        if (!all_digits(t[j].text)) nonnum = true;
        len += (size_t)snprintf(buf + len, sizeof buf - len, "%s%s", len ? " " : "", t[j].text);
    }
    if (!nonnum) return false;              /* "Chapter 14" stays a title */
    char err[64];
    return datejump_parse(buf, today, out, err, sizeof err) == 0;
}

/* ---------- the parser ---------- */

int quick_parse(const char *line, daynum_t today, item_t *out, char *msg, size_t msglen)
{
    if (msg && msglen) msg[0] = 0;
    static tok_t t[MAX_QTOK];
    int n = tokenize(line, t, MAX_QTOK);
    if (n == 0) { add_msg(msg, msglen, "nothing on the line"); return -1; }

    daynum_t day = -1, due = -1;
    int start = -1, end = -1, dur = 0, pri = 0;
    bool allday = false, have_date = false, have_time = false;
    char tags[256] = "", place[256] = "", who[256] = "";
    repeat_t rep;
    memset(&rep, 0, sizeof rep);

    /* the repeat clause runs from "every" to the next sigiled token (or the end) */
    for (int i = 0; i < n; i++) {
        if (!t[i].quoted && ieq(t[i].text, "every")) {
            int e = i + 1;
            while (e < n && !t[e].quoted && !strchr("#@&!*", t[e].text[0]) && strncmp(t[e].text, "due:", 4) != 0) e++;
            if (!rewrite_repeat(t, i + 1, e, today, &rep, msg, msglen)) return -1;
            for (int j = i; j < e; j++) t[j].claimed = true;
            break;
        }
    }

    /* single-token fields */
    for (int i = 0; i < n; i++) {
        char *s = t[i].text;
        if (t[i].quoted || t[i].claimed) continue;
        if (s[0] == '#' && s[1]) {
            size_t len = strlen(tags);
            snprintf(tags + len, sizeof tags - len, "%s%s", len ? " " : "", s + 1);
            t[i].claimed = true;
        } else if (s[0] == '@' && s[1]) {
            snprintf(place, sizeof place, "%s", s + 1);
            t[i].claimed = true;
        } else if (s[0] == '&' && s[1]) {
            snprintf(who, sizeof who, "%s", s + 1);
            t[i].claimed = true;
        } else if (s[0] == '!' && s[1] >= '1' && s[1] <= '5' && !s[2]) {
            pri = s[1] - '0';
            t[i].claimed = true;
        } else if (!strcmp(s, "*")) {
            allday = true;
            t[i].claimed = true;
        } else if (!strncmp(s, "due:", 4) || !strncmp(s, "Due:", 4) || !strncmp(s, "DUE:", 4)) {
            char err[64];
            if (!s[4] || datejump_parse(s + 4, today, &due, err, sizeof err) != 0) { add_msg(msg, msglen, "cannot read the due: date"); return -1; }
            t[i].claimed = true;
        } else if (s[0] == '+' && s[1] && !all_digits(s + 1)) {
            int d2;
            if (model_parse_dur(s + 1, &d2)) {       /* +1h30; "+2w" falls through to the dates */
                dur = d2;
                t[i].claimed = true;
            }
        } else if (strchr(s, ':') && isdigit((unsigned char)s[0])) {
            int st2, en2;
            if (parse_time_tok(s, &st2, &en2)) {
                if (have_time) add_msg(msg, msglen, "second time ignored");
                else {
                    start = st2;
                    end = en2;
                    have_time = true;
                }
                t[i].claimed = true;
            }
        }
    }

    /* dates: longest unclaimed window first, first match wins */
    for (int i = 0; i < n && !have_date; i++) {
        for (int w = 3; w >= 1; w--) {
            if (w == 1 && all_digits(t[i].text)) continue;
            daynum_t d;
            if (try_date_window(t, n, i, w, today, &d)) {
                day = d;
                have_date = true;
                for (int j = i; j < i + w; j++) t[j].claimed = true;
                break;
            }
        }
    }

    /* what's left is the title */
    char title[512] = "";
    size_t tl = 0;
    for (int i = 0; i < n; i++) {
        if (t[i].claimed) continue;
        tl += (size_t)snprintf(title + tl, sizeof title - tl, "%s%s", tl ? " " : "", t[i].text);
    }
    if (!title[0]) { add_msg(msg, msglen, "no title left on the line"); return -1; }

    /* assemble */
    item_kind_t kind = (have_time || allday) ? ITEM_EVENT : ITEM_TODO;
    item_blank(out, kind);
    out->title = dup_str(title);
    if (tags[0]) out->tags = dup_str(tags);
    if (place[0]) out->at = dup_str(place);
    if (who[0]) out->who = dup_str(who);
    if (kind == ITEM_EVENT) {
        out->day = have_date ? day : today;
        if (have_time) {
            out->start = start;
            if (end > start) out->dur = end - start;
            else if (end >= 0) add_msg(msg, msglen, "end time not after the start; ignored");
            if (!out->dur || end <= start) out->dur = dur > 0 ? dur : 60;
        }
        if (due >= 0) add_msg(msg, msglen, "due: dropped (events have no deadline)");
        if (pri) add_msg(msg, msglen, "priority dropped (todos only)");
    } else {
        out->day = have_date ? day : -1;
        out->due = due;
        out->pri = pri;
        if (dur > 0) add_msg(msg, msglen, "duration dropped (no time given)");
    }
    if (rep.freq != REP_NONE) {
        out->rep = rep;
        if (out->day < 0) out->day = today;     /* a repeat needs a start */
    }
    return 0;
}

void quick_item_free(item_t *it)
{
    free(it->title); free(it->at); free(it->tags); free(it->note); free(it->who); free(it->status); free(it->extra);
    free(it->skips);
    memset(it, 0, sizeof *it);
}

void quick_describe(const item_t *it, char *out, size_t n)
{
    char when[64] = "";
    if (it->kind == ITEM_EVENT) {
        model_format_date(it->day, when, sizeof when);
        if (it->start >= 0) { size_t l = strlen(when); snprintf(when + l, sizeof when - l, " %02d:%02d", it->start / 60, it->start % 60); }
    } else if (it->due >= 0) {
        char d[16]; model_format_date(it->due, d, sizeof d);
        snprintf(when, sizeof when, "due %s", d);
    } else if (it->day >= 0) model_format_date(it->day, when, sizeof when);
    else snprintf(when, sizeof when, "unscheduled");
    char reps[80] = "";
    if (it->rep.freq != REP_NONE) { reps[0] = ' '; reps[1] = ' '; repeat_format(&it->rep, reps + 2, sizeof reps - 2); }
    snprintf(out, n, "%s  %s  %s%s%s%s%s%s", it->kind == ITEM_EVENT ? "event" : "todo ", it->title ? it->title : "", when,
             it->tags ? "  #" : "", it->tags ? it->tags : "", it->at ? "  @" : "", it->at ? it->at : "", reps);
}

const char *quick_cheatsheet(void)
{
    return
        "# one item per line; lines starting with '# ' are ignored\n"
        "#   Dentist cleaning thu 14:30 +45m @Clinic #medical\n"
        "#   Renew insurance due:5.11 !2 #financial #car\n"
        "#   Standup mon 9:30-9:45 every weekdays\n"
        "#   Pay HOA due:eom every monthly #financial\n"
        "# a time makes an event; otherwise a todo (undated without a date)\n"
        "# dates: thu, tomorrow, 5.11., sep 14, next fri, eom   time: 24h\n"
        "# #tag @place &who !1-5 due:DATE +45m * (all-day) every ... (last)\n"
        "# \"quoted words\" always stay in the title\n";
}
