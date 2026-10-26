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

/* test_quick.c — the freeform capture grammar */

#include <string.h>
#include "check.h"
#include "../core/quick.h"

/* Mon 26 Oct 2026 */
static daynum_t today;

static int parse(const char *line, item_t *it, char *msg, size_t n)
{
    return quick_parse(line, today, it, msg, n);
}

static void test_events(void)
{
    item_t it;
    char msg[128];

    CHECK_EQ(parse("Dentist cleaning thu 14:30 +45m @Clinic #medical", &it, msg, sizeof msg), 0);
    CHECK_EQ(it.kind, ITEM_EVENT);
    CHECK(!strcmp(it.title, "Dentist cleaning"));
    CHECK_EQ(it.day, civil_to_days(2026, 10, 29));      /* Thursday of this ISO week */
    CHECK_EQ(it.start, 14 * 60 + 30);
    CHECK_EQ(it.dur, 45);
    CHECK(!strcmp(it.at, "Clinic"));
    CHECK(!strcmp(it.tags, "medical"));
    CHECK_EQ(msg[0], 0);
    quick_item_free(&it);

    CHECK_EQ(parse("Blood work tomorrow 8:15 #medical", &it, msg, sizeof msg), 0);
    CHECK_EQ(it.kind, ITEM_EVENT);
    CHECK_EQ(it.day, civil_to_days(2026, 10, 27));
    CHECK_EQ(it.start, 8 * 60 + 15);
    CHECK_EQ(it.dur, 60);                               /* default */
    quick_item_free(&it);

    /* a time with no date: today */
    CHECK_EQ(parse("Standup 9:30-9:45", &it, msg, sizeof msg), 0);
    CHECK_EQ(it.day, today);
    CHECK_EQ(it.start, 9 * 60 + 30);
    CHECK_EQ(it.dur, 15);
    quick_item_free(&it);

    /* multi-word date, place and who with quotes */
    CHECK_EQ(parse("Review sep 14 10:00 @\"Duluth GA\" &\"Simon Podnar\"", &it, msg, sizeof msg), 0);
    CHECK_EQ(it.day, civil_to_days(2026, 9, 14));
    CHECK(!strcmp(it.at, "Duluth GA"));
    CHECK(!strcmp(it.who, "Simon Podnar"));
    CHECK(!strcmp(it.title, "Review"));
    quick_item_free(&it);

    /* * makes an all-day event */
    CHECK_EQ(parse("Conference * 2.11", &it, msg, sizeof msg), 0);
    CHECK_EQ(it.kind, ITEM_EVENT);
    CHECK_EQ(it.day, civil_to_days(2026, 11, 2));
    CHECK_EQ(it.start, -1);
    quick_item_free(&it);
}

static void test_todos(void)
{
    item_t it;
    char msg[128];

    CHECK_EQ(parse("Renew car insurance due:5.11 !2 #financial #car", &it, msg, sizeof msg), 0);
    CHECK_EQ(it.kind, ITEM_TODO);
    CHECK(!strcmp(it.title, "Renew car insurance"));
    CHECK_EQ(it.day, -1);
    CHECK_EQ(it.due, civil_to_days(2026, 11, 5));
    CHECK_EQ(it.pri, 2);
    CHECK(!strcmp(it.tags, "financial car"));
    quick_item_free(&it);

    CHECK_EQ(parse("Call Simon #family", &it, msg, sizeof msg), 0);
    CHECK_EQ(it.kind, ITEM_TODO);
    CHECK_EQ(it.day, -1);
    CHECK_EQ(it.due, -1);
    CHECK(!strcmp(it.title, "Call Simon"));
    quick_item_free(&it);

    /* a bare weekday schedules the todo */
    CHECK_EQ(parse("Pack for Ljubljana fri", &it, msg, sizeof msg), 0);
    CHECK_EQ(it.day, civil_to_days(2026, 10, 30));
    quick_item_free(&it);

    /* due:eom */
    CHECK_EQ(parse("Pay HOA due:eom #financial", &it, msg, sizeof msg), 0);
    CHECK_EQ(it.due, civil_to_days(2026, 10, 31));
    quick_item_free(&it);
}

static void test_repeats(void)
{
    item_t it;
    char msg[128];

    CHECK_EQ(parse("Standup mon 9:30-9:45 every weekdays", &it, msg, sizeof msg), 0);
    CHECK_EQ(it.kind, ITEM_EVENT);
    CHECK_EQ(it.day, civil_to_days(2026, 10, 26));
    CHECK_EQ(it.rep.freq, REP_WEEKLY);
    CHECK_EQ(it.rep.wdays, 0x1f);
    quick_item_free(&it);

    /* weekday list implies weekly; no date -> starts today */
    CHECK_EQ(parse("Trash out every mon", &it, msg, sizeof msg), 0);
    CHECK_EQ(it.kind, ITEM_TODO);
    CHECK_EQ(it.day, today);
    CHECK_EQ(it.rep.freq, REP_WEEKLY);
    CHECK_EQ(it.rep.wdays, 1);
    quick_item_free(&it);

    CHECK_EQ(parse("Pay HOA due:eom every monthly #financial", &it, msg, sizeof msg), 0);
    CHECK_EQ(it.rep.freq, REP_MONTHLY);
    CHECK_EQ(it.day, today);
    CHECK_EQ(it.due, civil_to_days(2026, 10, 31));
    quick_item_free(&it);

    /* "2 weeks" and a rewritten until date */
    CHECK_EQ(parse("Water plants every 2 weeks until 20.12", &it, msg, sizeof msg), 0);
    CHECK_EQ(it.rep.freq, REP_WEEKLY);
    CHECK_EQ(it.rep.interval, 2);
    CHECK_EQ(it.rep.until, civil_to_days(2026, 12, 20));
    quick_item_free(&it);

    CHECK_EQ(parse("Backup every day", &it, msg, sizeof msg), 0);
    CHECK_EQ(it.rep.freq, REP_DAILY);
    quick_item_free(&it);
}

static void test_ambiguity(void)
{
    item_t it;
    char msg[128];

    /* lone integers are never dates */
    CHECK_EQ(parse("Chapter 14 notes", &it, msg, sizeof msg), 0);
    CHECK_EQ(it.day, -1);
    CHECK(!strcmp(it.title, "Chapter 14 notes"));
    quick_item_free(&it);

    /* quoting protects a date-looking word */
    CHECK_EQ(parse("\"Fish fri\" sat", &it, msg, sizeof msg), 0);
    CHECK(!strcmp(it.title, "Fish fri"));
    CHECK_EQ(it.day, civil_to_days(2026, 10, 31));      /* Saturday of this week */
    quick_item_free(&it);

    /* the first date wins; the line keeps only one */
    CHECK_EQ(parse("Flights sep 14 #family &wife", &it, msg, sizeof msg), 0);
    CHECK_EQ(it.day, civil_to_days(2026, 9, 14));
    CHECK(!strcmp(it.who, "wife"));
    quick_item_free(&it);

    /* durations need units; +2 is a relative date, +45m a duration */
    CHECK_EQ(parse("Dentist +2 15:00", &it, msg, sizeof msg), 0);
    CHECK_EQ(it.day, today + 2);
    CHECK_EQ(it.dur, 60);
    quick_item_free(&it);

    /* warnings, not errors: due on an event */
    CHECK_EQ(parse("Dinner 19:00 due:eom", &it, msg, sizeof msg), 0);
    CHECK_EQ(it.kind, ITEM_EVENT);
    CHECK_EQ(it.due, -1);
    CHECK(msg[0] != 0);
    quick_item_free(&it);
}

static void test_errors(void)
{
    item_t it;
    char msg[128];

    CHECK_EQ(parse("", &it, msg, sizeof msg), -1);
    CHECK_EQ(parse("   ", &it, msg, sizeof msg), -1);
    CHECK_EQ(parse("#medical", &it, msg, sizeof msg), -1);          /* no title */
    CHECK_EQ(parse("due:nonsense thing", &it, msg, sizeof msg), -1);
    CHECK_EQ(parse("Gym every blurgh", &it, msg, sizeof msg), -1);
    CHECK_EQ(parse("every mon", &it, msg, sizeof msg), -1);         /* rule, no item */
}

int main(void)
{
    today = civil_to_days(2026, 10, 26);
    test_events();
    test_todos();
    test_repeats();
    test_ambiguity();
    test_errors();
    CHECK_DONE("quick");
}
