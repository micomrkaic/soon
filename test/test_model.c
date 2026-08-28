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

#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "../core/model.h"

static const char *sample =
    "# comment\n"
    "place Home | 38.8048 -77.0469 | America/New_York\n"
    "place Ljubljana | 46.0569 14.5058 | Europe/Ljubljana\n"
    "home Home\n"
    "loc 2026-09-14 Ljubljana\n"
    "loc 2026-09-28 Home\n"
    "\n"
    "E 2026-09-15 09:00 1h30 Coffee with Aljoša\n"
    "  id: 7f3a9c1e\n"
    "  at: Ljubljana\n"
    "  tags: friends paper\n"
    "  pri: 2\n"
    "  note: Cankarjeva.\n"
    "  note: Ask about the draft.\n"
    "  color: purple\n"
    "\n"
    "E 2026-09-14 * Slovenia trip\n"
    "  until: 2026-09-27\n"
    "\n"
    "T 2026-09-20 Renew passport\n"
    "  due: 2026-10-01\n"
    "  pri: 1\n"
    "\n"
    "T - Read Thain ch. 5\n"
    "  done: 2026-08-25 22:10\n"
    "\n"
    "T - Unscheduled with deadline\n"
    "  due: 2026-09-16\n"
    "\n"
    "X something unknown\n";

int main(void)
{
    store_t st;
    store_init(&st, "/nonexistent/x.soon");
    store_parse(&st, sample);

    CHECK_EQ(st.nplaces, 2);
    CHECK(st.home && !strcmp(st.home, "Home"));
    CHECK_EQ(st.nlocs, 2);
    CHECK_EQ(st.nitems, 5);
    CHECK_EQ(st.nraw, 1);

    /* location timeline */
    const place_t *p;
    p = store_place_for_day(&st, civil_to_days(2026, 9, 1));  CHECK(p && !strcmp(p->name, "Home"));
    p = store_place_for_day(&st, civil_to_days(2026, 9, 14)); CHECK(p && !strcmp(p->name, "Ljubljana"));
    p = store_place_for_day(&st, civil_to_days(2026, 9, 27)); CHECK(p && !strcmp(p->name, "Ljubljana"));
    p = store_place_for_day(&st, civil_to_days(2026, 9, 28)); CHECK(p && !strcmp(p->name, "Home"));
    p = store_place_for_day(&st, civil_to_days(2030, 1, 1));  CHECK(p && !strcmp(p->name, "Home"));

    /* the event */
    item_t *e = NULL;
    for (int i = 0; i < st.nitems; i++) if (!strcmp(st.items[i].id, "7f3a9c1e")) e = &st.items[i];
    CHECK(e != NULL);
    if (e) {
        CHECK_EQ(e->kind, ITEM_EVENT);
        CHECK_EQ(e->day, civil_to_days(2026, 9, 15));
        CHECK_EQ(e->start, 540);
        CHECK_EQ(e->dur, 90);
        CHECK(!strcmp(e->title, "Coffee with Aljoša"));
        CHECK(!strcmp(e->at, "Ljubljana"));
        CHECK(!strcmp(e->tags, "friends paper"));
        CHECK_EQ(e->pri, 2);
        CHECK(!strcmp(e->note, "Cankarjeva.\nAsk about the draft."));
        CHECK(e->extra && !strcmp(e->extra, "  color: purple"));   /* unknown field preserved */
    }

    /* every item got an id */
    for (int i = 0; i < st.nitems; i++) CHECK_EQ(strlen(st.items[i].id), 8);

    /* day queries */
    int idx[16];
    CHECK_EQ(store_day_items(&st, civil_to_days(2026, 9, 15), idx, 16), 2);   /* trip (all-day) + coffee */
    CHECK_EQ(st.items[idx[0]].start, -1);                                     /* all-day first */
    CHECK_EQ(store_day_items(&st, civil_to_days(2026, 9, 20), idx, 16), 2);   /* trip + passport */
    CHECK_EQ(store_day_items(&st, civil_to_days(2026, 9, 16), idx, 16), 2);   /* trip + unscheduled-with-deadline */
    CHECK_EQ(store_day_items(&st, civil_to_days(2026, 9, 28), idx, 16), 0);
    int pend;
    CHECK_EQ(store_day_load(&st, civil_to_days(2026, 9, 20), &pend), 2);
    CHECK_EQ(pend, 1);

    /* round trip: serialize, reparse, serialize again must be identical */
    char *t1 = store_serialize(&st);
    store_t st2;
    store_init(&st2, "/nonexistent/y.soon");
    store_parse(&st2, t1);
    char *t2 = store_serialize(&st2);
    CHECK(t1 && t2 && !strcmp(t1, t2));
    CHECK(strstr(t1, "E 2026-09-15 09:00 1h30 Coffee with Aljoša\n") != NULL);
    CHECK(strstr(t1, "  note: Cankarjeva.\n  note: Ask about the draft.\n") != NULL);
    CHECK(strstr(t1, "  color: purple\n") != NULL);
    CHECK(strstr(t1, "X something unknown\n") != NULL);
    CHECK(strstr(t1, "loc 2026-09-14 Ljubljana\nloc 2026-09-28 Home\n") != NULL);
    CHECK(strstr(t1, "T - Read Thain ch. 5\n") != NULL);
    CHECK(strstr(t1, "  done: 2026-08-25 22:10\n") != NULL);
    free(t1); free(t2);

    /* add, delete */
    item_t it;
    item_blank(&it, ITEM_TODO);
    it.day = civil_to_days(2026, 9, 1);
    it.title = "Nov todo";
    int i = store_add_item(&st, &it);
    CHECK(i >= 0);
    CHECK_EQ(st.nitems, 6);
    CHECK(st.dirty);
    store_delete_item(&st, i);
    CHECK_EQ(st.nitems, 5);

    /* parsers */
    int m;
    CHECK(model_parse_dur("45m", &m) && m == 45);
    CHECK(model_parse_dur("1h", &m) && m == 60);
    CHECK(model_parse_dur("1h30", &m) && m == 90);
    CHECK(model_parse_dur("2d", &m) && m == 2880);
    CHECK(model_parse_dur("90", &m) && m == 90);
    CHECK(!model_parse_dur("abc", &m));
    CHECK(!model_parse_dur("0", &m));
    CHECK(model_parse_time("9:05", &m) && m == 545);
    CHECK(model_parse_time("23:59", &m) && m == 1439);
    CHECK(!model_parse_time("24:00", &m));
    CHECK(!model_parse_time("9", &m));
    char buf[16];
    model_format_dur(90, buf, sizeof buf); CHECK(!strcmp(buf, "1h30"));
    model_format_dur(60, buf, sizeof buf); CHECK(!strcmp(buf, "1h"));
    model_format_dur(45, buf, sizeof buf); CHECK(!strcmp(buf, "45m"));
    model_format_dur(2880, buf, sizeof buf); CHECK(!strcmp(buf, "2d"));

    store_free(&st);
    store_free(&st2);

    /* location timeline editing */
    {
        store_t t;
        store_init(&t, "/nonexistent/z.soon");
        store_add_place(&t, "Alexandria VA", 38.8, -77.0, "America/New_York");
        store_add_place(&t, "Duluth GA", 34.0, -84.1, "America/New_York");
        store_add_place(&t, "Kranj", 46.2, 14.4, "Europe/Ljubljana");
        store_set_home(&t, "Alexandria VA");
        #define AT(y, m, d) store_place_for_day(&t, civil_to_days(y, m, d))->name

        /* fresh range */
        store_set_loc_range(&t, civil_to_days(2026, 8, 27), civil_to_days(2026, 9, 5), "Duluth GA");
        CHECK(!strcmp(AT(2026, 8, 26), "Alexandria VA"));
        CHECK(!strcmp(AT(2026, 8, 27), "Duluth GA"));
        CHECK(!strcmp(AT(2026, 9, 5), "Duluth GA"));
        CHECK(!strcmp(AT(2026, 9, 6), "Alexandria VA"));
        CHECK_EQ(t.nlocs, 2);

        /* shrink the same range: the bug report */
        store_set_loc_range(&t, civil_to_days(2026, 8, 27), civil_to_days(2026, 9, 3), "Duluth GA");
        CHECK(!strcmp(AT(2026, 9, 3), "Duluth GA"));
        CHECK(!strcmp(AT(2026, 9, 4), "Alexandria VA"));
        CHECK(!strcmp(AT(2026, 9, 5), "Alexandria VA"));
        CHECK_EQ(t.nlocs, 2);

        /* extend it */
        store_set_loc_range(&t, civil_to_days(2026, 8, 27), civil_to_days(2026, 9, 8), "Duluth GA");
        CHECK(!strcmp(AT(2026, 9, 8), "Duluth GA"));
        CHECK(!strcmp(AT(2026, 9, 9), "Alexandria VA"));
        CHECK_EQ(t.nlocs, 2);

        /* a later trip is untouched by an earlier one */
        store_set_loc_range(&t, civil_to_days(2026, 9, 14), civil_to_days(2026, 9, 27), "Kranj");
        store_set_loc_range(&t, civil_to_days(2026, 8, 27), civil_to_days(2026, 9, 1), "Duluth GA");
        CHECK(!strcmp(AT(2026, 9, 2), "Alexandria VA"));
        CHECK(!strcmp(AT(2026, 9, 14), "Kranj"));
        CHECK(!strcmp(AT(2026, 9, 27), "Kranj"));
        CHECK(!strcmp(AT(2026, 9, 28), "Alexandria VA"));
        CHECK_EQ(t.nlocs, 4);

        /* a range inside a range: the outer one resumes afterwards */
        store_set_loc_range(&t, civil_to_days(2026, 9, 18), civil_to_days(2026, 9, 19), "Duluth GA");
        CHECK(!strcmp(AT(2026, 9, 17), "Kranj"));
        CHECK(!strcmp(AT(2026, 9, 18), "Duluth GA"));
        CHECK(!strcmp(AT(2026, 9, 20), "Kranj"));
        CHECK(!strcmp(AT(2026, 9, 28), "Alexandria VA"));

        /* a single day, then the same day set back to home: entries collapse */
        store_set_loc_range(&t, civil_to_days(2026, 10, 10), civil_to_days(2026, 10, 10), "Kranj");
        CHECK(!strcmp(AT(2026, 10, 10), "Kranj"));
        CHECK(!strcmp(AT(2026, 10, 11), "Alexandria VA"));
        int before = t.nlocs;
        store_set_loc_range(&t, civil_to_days(2026, 10, 10), civil_to_days(2026, 10, 10), "Alexandria VA");
        CHECK(!strcmp(AT(2026, 10, 10), "Alexandria VA"));
        CHECK_EQ(t.nlocs, before - 2);

        /* onward */
        store_set_loc_range(&t, civil_to_days(2026, 11, 1), -1, "Kranj");
        CHECK(!strcmp(AT(2026, 11, 1), "Kranj"));
        CHECK(!strcmp(AT(2027, 6, 1), "Kranj"));
        #undef AT
        store_free(&t);
    }

    /* recurrence */
    {
        repeat_t r;
        char out[128];
        CHECK(repeat_parse("weekly mon wed every 2 until 2026-12-31", &r));
        CHECK(r.freq == REP_WEEKLY && r.wdays == 0x5 && r.interval == 2 && r.until == civil_to_days(2026, 12, 31));
        repeat_format(&r, out, sizeof out);
        CHECK(!strcmp(out, "weekly mon wed every 2 until 2026-12-31"));
        CHECK(repeat_parse("monthly 2nd wed count 6", &r));
        CHECK(r.freq == REP_MONTHLY && r.nth == 2 && r.nth_wday == 2 && r.count == 6);
        repeat_format(&r, out, sizeof out); CHECK(!strcmp(out, "monthly 2nd wed count 6"));
        CHECK(repeat_parse("monthly last fri", &r)); CHECK(r.nth == -1 && r.nth_wday == 4);
        CHECK(repeat_parse("monthly 15", &r)); CHECK(r.mday == 15 && r.nth == 0);
        CHECK(repeat_parse("daily every 3", &r)); CHECK(r.freq == REP_DAILY && r.interval == 3);
        CHECK(repeat_parse("yearly", &r)); CHECK(r.freq == REP_YEARLY);
        CHECK(!repeat_parse("fortnightly", &r));
        CHECK(!repeat_parse("weekly every 0", &r));
        CHECK(!repeat_parse("monthly 5th mon", &r));

        item_t ev;
        item_blank(&ev, ITEM_EVENT);
        ev.day = civil_to_days(2026, 9, 2);           /* a Wednesday */
        ev.start = 600; ev.dur = 60;
        repeat_parse("weekly", &ev.rep);
        CHECK(item_occurs_on(&ev, civil_to_days(2026, 9, 2)));
        CHECK(item_occurs_on(&ev, civil_to_days(2026, 9, 9)));
        CHECK(!item_occurs_on(&ev, civil_to_days(2026, 9, 8)));
        CHECK(!item_occurs_on(&ev, civil_to_days(2026, 8, 26)));   /* before the start */
        repeat_parse("weekly mon fri every 2", &ev.rep);           /* from Wed 2 Sep: Fri 4 Sep, then Mon 14, Fri 18 */
        CHECK(item_occurs_on(&ev, civil_to_days(2026, 9, 4)));
        CHECK(!item_occurs_on(&ev, civil_to_days(2026, 9, 7)));
        CHECK(item_occurs_on(&ev, civil_to_days(2026, 9, 14)));
        CHECK(item_occurs_on(&ev, civil_to_days(2026, 9, 18)));
        CHECK(!item_occurs_on(&ev, civil_to_days(2026, 9, 2)));    /* Wednesday is not in the mask */
        repeat_parse("monthly", &ev.rep);
        CHECK(item_occurs_on(&ev, civil_to_days(2026, 10, 2)));
        CHECK(!item_occurs_on(&ev, civil_to_days(2026, 10, 3)));
        ev.day = civil_to_days(2026, 1, 31);
        repeat_parse("monthly", &ev.rep);
        CHECK(!item_occurs_on(&ev, civil_to_days(2026, 2, 28)));   /* no 31 February: skipped, like RFC 5545 */
        CHECK(item_occurs_on(&ev, civil_to_days(2026, 3, 31)));
        ev.day = civil_to_days(2026, 9, 9);                          /* 2nd Wednesday of September */
        repeat_parse("monthly 2nd wed", &ev.rep);
        CHECK(item_occurs_on(&ev, civil_to_days(2026, 10, 14)));
        CHECK(!item_occurs_on(&ev, civil_to_days(2026, 10, 7)));
        repeat_parse("monthly last fri", &ev.rep);
        CHECK(item_occurs_on(&ev, civil_to_days(2026, 9, 25)));
        CHECK(item_occurs_on(&ev, civil_to_days(2026, 10, 30)));
        CHECK(!item_occurs_on(&ev, civil_to_days(2026, 10, 23)));
        ev.day = civil_to_days(2024, 2, 29);
        repeat_parse("yearly", &ev.rep);
        CHECK(item_occurs_on(&ev, civil_to_days(2028, 2, 29)));
        CHECK(!item_occurs_on(&ev, civil_to_days(2026, 2, 28)));
        ev.day = civil_to_days(2026, 9, 1);
        repeat_parse("daily every 3 count 4", &ev.rep);              /* 1, 4, 7, 10 Sep */
        CHECK(item_occurs_on(&ev, civil_to_days(2026, 9, 10)));
        CHECK(!item_occurs_on(&ev, civil_to_days(2026, 9, 13)));
        item_add_skip(&ev, civil_to_days(2026, 9, 7));
        CHECK(!item_occurs_on(&ev, civil_to_days(2026, 9, 7)));
        CHECK(item_occurs_on(&ev, civil_to_days(2026, 9, 10)));     /* a skip does not extend the count */
        CHECK_EQ(item_next_occurrence(&ev, civil_to_days(2026, 9, 4)), civil_to_days(2026, 9, 10));
        CHECK_EQ(item_next_occurrence(&ev, civil_to_days(2026, 9, 10)), -1);
        CHECK_EQ(item_prev_occurrence(&ev, civil_to_days(2026, 9, 10)), civil_to_days(2026, 9, 4));
        repeat_parse("daily until 2026-09-05", &ev.rep);
        CHECK(item_occurs_on(&ev, civil_to_days(2026, 9, 5)));
        CHECK(!item_occurs_on(&ev, civil_to_days(2026, 9, 6)));
        free(ev.skips); ev.skips = NULL; ev.nskips = 0;

        /* overdue: the due date if set, else the scheduled day; never once done */
        {
            daynum_t today = civil_to_days(2026, 9, 10);
            item_t o;
            item_blank(&o, ITEM_TODO);
            CHECK(!todo_overdue_on(&o, o.day, today));                       /* undated: never overdue */
            o.day = civil_to_days(2026, 9, 8);
            CHECK(todo_overdue_on(&o, o.day, today));                        /* scheduled day passed */
            o.due = civil_to_days(2026, 9, 12);
            CHECK(!todo_overdue_on(&o, o.day, today));                       /* ...but the deadline has not */
            o.due = civil_to_days(2026, 9, 9);
            CHECK(todo_overdue_on(&o, o.day, today));
            CHECK(!todo_overdue_on(&o, o.day, o.due));                       /* due today is not overdue */
            o.done_day = today;
            CHECK(!todo_overdue_on(&o, o.day, today));
            o.done_day = -1; o.day = -1;
            CHECK(todo_overdue_on(&o, o.due, today));                        /* unscheduled with a deadline */
            CHECK_EQ(todo_last_open_before(&o, today), -1);
            o.day = civil_to_days(2026, 9, 8);
            CHECK_EQ(todo_last_open_before(&o, today), o.day);
            o.done_day = today;
            CHECK_EQ(todo_last_open_before(&o, today), -1);
        }

        /* repeating todo: each occurrence has its own done state */
        item_t t;
        item_blank(&t, ITEM_TODO);
        t.day = civil_to_days(2026, 9, 7);
        repeat_parse("weekly", &t.rep);
        CHECK(!todo_done_on(&t, civil_to_days(2026, 9, 7)));
        todo_toggle_on(&t, civil_to_days(2026, 9, 7), civil_to_days(2026, 9, 7), 600);
        CHECK(todo_done_on(&t, civil_to_days(2026, 9, 7)));
        CHECK(!todo_done_on(&t, civil_to_days(2026, 9, 14)));
        CHECK(!todo_overdue_on(&t, civil_to_days(2026, 9, 7), civil_to_days(2026, 9, 20)));    /* done occurrence */
        CHECK(todo_overdue_on(&t, civil_to_days(2026, 9, 14), civil_to_days(2026, 9, 20)));    /* missed one */
        CHECK(!todo_overdue_on(&t, civil_to_days(2026, 9, 21), civil_to_days(2026, 9, 20)));   /* future one */
        CHECK_EQ(todo_last_open_before(&t, civil_to_days(2026, 9, 20)), civil_to_days(2026, 9, 14));   /* the missed one */
        CHECK_EQ(todo_last_open_before(&t, civil_to_days(2026, 9, 10)), -1);                          /* nothing missed yet */
        todo_toggle_on(&t, civil_to_days(2026, 9, 14), civil_to_days(2026, 9, 14), 600);
        CHECK(todo_done_on(&t, civil_to_days(2026, 9, 14)));
        todo_toggle_on(&t, civil_to_days(2026, 9, 14), civil_to_days(2026, 9, 14), 600);   /* undo */
        CHECK(!todo_done_on(&t, civil_to_days(2026, 9, 14)));
        CHECK(todo_done_on(&t, civil_to_days(2026, 9, 7)));
        CHECK_EQ(t.done_day, civil_to_days(2026, 9, 7));

        /* the store shows occurrences and keeps repeat/skip through a round trip */
        store_t s;
        store_init(&s, "/nonexistent/r.soon");
        store_parse(&s, "E 2026-09-02 10:00 1h Weekly sync\n  id: rrrr0001\n  repeat: weekly wed every 2\n  skip: 2026-09-30\n\nT 2026-09-07 Water plants\n  id: rrrr0002\n  repeat: weekly mon thu\n  done: 2026-09-07 08:00\n");
        int ix[8];
        CHECK_EQ(store_day_items(&s, civil_to_days(2026, 9, 16), ix, 8), 1);
        CHECK_EQ(store_day_items(&s, civil_to_days(2026, 9, 23), ix, 8), 0);
        CHECK_EQ(store_day_items(&s, civil_to_days(2026, 9, 30), ix, 8), 0);   /* skipped */
        CHECK_EQ(store_day_items(&s, civil_to_days(2026, 10, 14), ix, 8), 1);
        CHECK_EQ(store_day_items(&s, civil_to_days(2026, 9, 10), ix, 8), 1);   /* Thursday todo */
        int pd;
        store_day_load(&s, civil_to_days(2026, 9, 7), &pd);  CHECK_EQ(pd, 0);   /* done that day */
        store_day_load(&s, civil_to_days(2026, 9, 10), &pd); CHECK_EQ(pd, 1);
        char *txt = store_serialize(&s);
        CHECK(strstr(txt, "  repeat: weekly wed every 2\n  skip: 2026-09-30\n") != NULL);
        CHECK(strstr(txt, "  repeat: weekly mon thu\n") != NULL);
        free(txt);
        store_free(&s);
    }
    CHECK_DONE("model");
}
