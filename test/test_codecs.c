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
#include "../core/codecs.h"
#include "../core/contacts.h"

static const char *agenda =
    "place Alexandria VA | 38.8048 -77.0469 | America/New_York\n"
    "place Kranj | 46.2389 14.3556 | Europe/Ljubljana\n"
    "home Alexandria VA\n"
    "loc 2026-09-14 Kranj\n"
    "loc 2026-09-28 Alexandria VA\n\n"
    "E 2026-09-15 09:00 1h30 Kava z Aljošo\n  id: aaaa0001\n  at: Kranj\n  tags: friends paper\n  note: Cankarjeva, first floor.\n  note: Ask about the draft.\n  who: Aljoša\n\n"
    "E 2026-08-28 * Duluth weekend\n  id: aaaa0002\n  until: 2026-08-30\n\n"
    "E 2026-08-27 14:00 45m Review; with, commas\\backslash\n  id: aaaa0003\n\n"
    "T 2026-09-20 Renew passport\n  id: aaaa0004\n  due: 2026-10-01\n  pri: 1\n  tags: admin\n\n"
    "T - Read Thain ch. 6\n  id: aaaa0005\n  done: 2026-08-25 22:10\n\n"
    "T 2026-09-16 Cancelled thing\n  id: aaaa0006\n  status: cancelled\n";

static item_t *by_id(store_t *st, const char *id)
{
    for (int i = 0; i < st->nitems; i++) if (!strcmp(st->items[i].id, id)) return &st->items[i];
    return NULL;
}

static void load(store_t *st)
{
    store_init(st, "/nonexistent/a.soon");
    store_parse(st, agenda);
}

int main(void)
{
    /* ---- org ---- */
    {
        store_t st;
        load(&st);
        char *org = org_export(&st);
        CHECK(strstr(org, "* Kava z Aljošo  :friends:paper:\n  <2026-09-15 Tue 09:00-10:30>\n") != NULL);
        CHECK(strstr(org, "* Duluth weekend\n  <2026-08-28 Fri>--<2026-08-30 Sun>\n") != NULL);
        CHECK(strstr(org, "* TODO [#A] Renew passport  :admin:\n  SCHEDULED: <2026-09-20 Sun> DEADLINE: <2026-10-01 Thu>\n") != NULL);
        CHECK(strstr(org, "* DONE Read Thain ch. 6\n  CLOSED: [2026-08-25 Tue 22:10]\n") != NULL);
        CHECK(strstr(org, "* CANCELLED Cancelled thing\n") != NULL);
        CHECK(strstr(org, "  :ID: aaaa0001\n  :LOCATION: Kranj\n  :WHO: Aljoša\n") != NULL);
        CHECK(strstr(org, "  Cankarjeva, first floor.\n  Ask about the draft.\n") != NULL);

        /* re-import into an empty store reproduces the items */
        store_t st2;
        store_init(&st2, "/nonexistent/b.soon");
        import_report_t rep;
        int n = org_import(&st2, org, &rep);
        CHECK_EQ(n, 6);
        CHECK_EQ(rep.added, 6);
        CHECK_EQ(rep.warnings, 0);
        item_t *e = by_id(&st2, "aaaa0001");
        CHECK(e && e->kind == ITEM_EVENT && e->day == civil_to_days(2026, 9, 15) && e->start == 540 && e->dur == 90);
        CHECK(e && !strcmp(e->title, "Kava z Aljošo") && !strcmp(e->at, "Kranj") && !strcmp(e->who, "Aljoša"));
        CHECK(e && !strcmp(e->tags, "friends paper"));
        CHECK(e && !strcmp(e->note, "Cankarjeva, first floor.\nAsk about the draft."));
        item_t *w = by_id(&st2, "aaaa0002");
        CHECK(w && w->start < 0 && w->until == civil_to_days(2026, 8, 30));
        item_t *t = by_id(&st2, "aaaa0004");
        CHECK(t && t->kind == ITEM_TODO && t->day == civil_to_days(2026, 9, 20) && t->due == civil_to_days(2026, 10, 1) && t->pri == 1 && t->done_day < 0);
        item_t *d = by_id(&st2, "aaaa0005");
        CHECK(d && d->day < 0 && d->done_day == civil_to_days(2026, 8, 25) && d->done_min == 22 * 60 + 10);
        item_t *c = by_id(&st2, "aaaa0006");
        CHECK(c && c->status && !strcmp(c->status, "cancelled"));

        /* importing again updates, never duplicates */
        n = org_import(&st2, org, &rep);
        CHECK_EQ(rep.added, 0);
        CHECK_EQ(rep.updated, 6);
        CHECK_EQ(st2.nitems, 6);

        /* the export of the re-import is identical */
        char *org2 = org_export(&st2);
        CHECK(!strcmp(org, org2));

        /* foreign org: keywords, priorities, repeater warning, plain headline skipped, tags */
        const char *foreign =
            "* Notes\nsome text\n"
            "** TODO [#B] Buy milk :home:errand:\n   SCHEDULED: <2026-09-01 Tue +1w>\n"
            "** NEXT Call the bank\n   DEADLINE: <2026-09-03 Thu>\n"
            "* Dentist <2026-09-02 Wed 11:00-13:00>\n"
            "* Conference\n  <2026-10-05 Mon>--<2026-10-07 Wed>\n";
        n = org_import(&st2, foreign, &rep);
        CHECK_EQ(rep.added, 4);
        CHECK_EQ(rep.skipped, 1);
        CHECK_EQ(rep.warnings, 0);
        int found = 0;
        for (int i = 0; i < st2.nitems; i++) {
            item_t *x = &st2.items[i];
            if (x->title && !strcmp(x->title, "Buy milk")) { found++; CHECK(x->kind == ITEM_TODO && x->pri == 2 && !strcmp(x->tags, "home errand") && x->day == civil_to_days(2026, 9, 1)); CHECK(x->rep.freq == REP_WEEKLY && x->rep.interval == 1); }
            if (x->title && !strcmp(x->title, "Call the bank")) { found++; CHECK(x->kind == ITEM_TODO && x->due == civil_to_days(2026, 9, 3)); }
            if (x->title && !strcmp(x->title, "Dentist")) { found++; CHECK(x->kind == ITEM_EVENT && x->start == 660 && x->dur == 120); }
            if (x->title && !strcmp(x->title, "Conference")) { found++; CHECK(x->kind == ITEM_EVENT && x->start < 0 && x->until == civil_to_days(2026, 10, 7)); }
        }
        CHECK_EQ(found, 4);
        free(org); free(org2);
        store_free(&st); store_free(&st2);
    }

    /* ---- ics ---- */
    {
        store_t st;
        load(&st);
        long long now = (long long)civil_to_days(2026, 8, 27) * 86400 + 12 * 3600;
        char *ics = ics_export(&st, now);
        CHECK(strstr(ics, "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n") != NULL);
        /* 09:00 in Kranj (CEST) is 07:00Z; the event is in Kranj so its zone applies */
        CHECK(strstr(ics, "DTSTART:20260915T070000Z\r\nDTEND:20260915T083000Z\r\n") != NULL);
        /* 14:00 in Alexandria (EDT) is 18:00Z */
        CHECK(strstr(ics, "DTSTART:20260827T180000Z\r\nDTEND:20260827T184500Z\r\n") != NULL);
        CHECK(strstr(ics, "DTSTART;VALUE=DATE:20260828\r\nDTEND;VALUE=DATE:20260831\r\n") != NULL);
        CHECK(strstr(ics, "SUMMARY:Review\\; with\\, commas\\\\backslash\r\n") != NULL);
        CHECK(strstr(ics, "DESCRIPTION:Cankarjeva\\, first floor.\\nAsk about the draft.\r\n") != NULL);
        CHECK(strstr(ics, "CATEGORIES:friends,paper\r\n") != NULL);
        CHECK(strstr(ics, "BEGIN:VTODO\r\nUID:soon-aaaa0004\r\n") != NULL);
        CHECK(strstr(ics, "DUE;VALUE=DATE:20261001\r\n") != NULL);
        CHECK(strstr(ics, "PRIORITY:1\r\n") != NULL);
        CHECK(strstr(ics, "STATUS:COMPLETED\r\n") != NULL);
        CHECK(strstr(ics, "STATUS:CANCELLED\r\n") != NULL);
        /* no line longer than 75 octets */
        int longest = 0, cur = 0;
        for (const char *p = ics; *p; p++) { if (*p == '\n') { if (cur > longest) longest = cur; cur = 0; } else if (*p != '\r') cur++; }
        CHECK(longest <= 75);

        store_t st2;
        store_init(&st2, "/nonexistent/c.soon");
        store_parse(&st2, "place Alexandria VA | 38.8048 -77.0469 | America/New_York\nplace Kranj | 46.2389 14.3556 | Europe/Ljubljana\nhome Alexandria VA\nloc 2026-09-14 Kranj\nloc 2026-09-28 Alexandria VA\n");
        import_report_t rep;
        int n = ics_import(&st2, ics, &rep);
        CHECK_EQ(n, 6);
        CHECK_EQ(rep.added, 6);
        item_t *e = by_id(&st2, "aaaa0001");
        CHECK(e && e->day == civil_to_days(2026, 9, 15) && e->start == 540 && e->dur == 90);   /* back to 09:00 Kranj wall clock */
        CHECK(e && !strcmp(e->note, "Cankarjeva, first floor.\nAsk about the draft.") && !strcmp(e->tags, "friends paper"));
        item_t *r = by_id(&st2, "aaaa0003");
        CHECK(r && r->start == 840 && r->dur == 45 && !strcmp(r->title, "Review; with, commas\\backslash"));
        item_t *w = by_id(&st2, "aaaa0002");
        CHECK(w && w->start < 0 && w->day == civil_to_days(2026, 8, 28) && w->until == civil_to_days(2026, 8, 30));
        item_t *t = by_id(&st2, "aaaa0004");
        CHECK(t && t->kind == ITEM_TODO && t->due == civil_to_days(2026, 10, 1) && t->pri == 1 && t->done_day < 0);
        item_t *d = by_id(&st2, "aaaa0005");
        CHECK(d && d->done_day == civil_to_days(2026, 8, 25) && d->done_min == 22 * 60 + 10);
        n = ics_import(&st2, ics, &rep);
        CHECK_EQ(rep.updated, 6);
        CHECK_EQ(st2.nitems, 6);
        char *ics2 = ics_export(&st2, now);
        CHECK(!strcmp(ics, ics2));

        /* foreign ics: TZID, DURATION, floating, RRULE kept with a warning, long UID preserved for re-import */
        const char *foreign =
            "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:x\r\n"
            "BEGIN:VEVENT\r\nUID:1234567890@google.com\r\nDTSTART;TZID=Europe/Berlin:20260916T100000\r\nDURATION:PT2H\r\n"
            "SUMMARY:Berlin call\r\nRRULE:FREQ=WEEKLY;COUNT=4\r\nEND:VEVENT\r\n"
            "BEGIN:VEVENT\r\nUID:float@x\r\nDTSTART:20260918T083000\r\nDTEND:20260918T090000\r\nSUMMARY:Floating\r\nEND:VEVENT\r\n"
            "BEGIN:VTODO\r\nUID:todo@x\r\nSUMMARY:Pay tax\r\nDUE;VALUE=DATE:20261015\r\nPRIORITY:5\r\nSTATUS:NEEDS-ACTION\r\nEND:VTODO\r\n"
            "END:VCALENDAR\r\n";
        n = ics_import(&st2, foreign, &rep);
        CHECK_EQ(rep.added, 3);
        CHECK_EQ(rep.warnings, 0);
        int found = 0;
        for (int i = 0; i < st2.nitems; i++) {
            item_t *x = &st2.items[i];
            if (x->title && !strcmp(x->title, "Berlin call")) {
                found++;
                /* 10:00 Berlin (CEST) on a Kranj day = 10:00 Kranj (same zone), 2 h */
                CHECK(x->day == civil_to_days(2026, 9, 16) && x->start == 600 && x->dur == 120);
                CHECK(x->rep.freq == REP_WEEKLY && x->rep.count == 4 && x->rep.interval == 1);
                CHECK(x->extra && strstr(x->extra, "  uid: 1234567890@google.com") && !strstr(x->extra, "rrule"));
            }
            if (x->title && !strcmp(x->title, "Floating")) { found++; CHECK(x->start == 510 && x->dur == 30); }
            if (x->title && !strcmp(x->title, "Pay tax")) { found++; CHECK(x->kind == ITEM_TODO && x->due == civil_to_days(2026, 10, 15) && x->pri == 3); }
        }
        CHECK_EQ(found, 3);
        n = ics_import(&st2, foreign, &rep);
        CHECK_EQ(rep.added, 0);
        CHECK_EQ(rep.updated, 3);
        free(ics); free(ics2);
        store_free(&st); store_free(&st2);
    }

    /* ---- recurrence through both codecs ---- */
    {
        store_t st;
        store_init(&st, "/nonexistent/rr.soon");
        store_parse(&st, "place Kranj | 46.2389 14.3556 | Europe/Ljubljana\nhome Kranj\n"
                         "E 2026-09-02 10:00 1h Weekly sync\n  id: rrrr0001\n  repeat: weekly mon wed every 2 until 2026-12-31\n  skip: 2026-09-16\n\n"
                         "E 2026-09-09 * Board\n  id: rrrr0002\n  repeat: monthly 2nd wed count 6\n\n"
                         "T 2026-09-07 Water plants\n  id: rrrr0003\n  repeat: daily every 3\n");
        long long now = (long long)civil_to_days(2026, 8, 27) * 86400;
        char *ics = ics_export(&st, now);
        CHECK(strstr(ics, "RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,WE;UNTIL=20261231\r\n") != NULL);
        CHECK(strstr(ics, "EXDATE:20260916T080000Z\r\n") != NULL);          /* 10:00 CEST */
        CHECK(strstr(ics, "RRULE:FREQ=MONTHLY;BYDAY=2WE;COUNT=6\r\n") != NULL);
        CHECK(strstr(ics, "RRULE:FREQ=DAILY;INTERVAL=3\r\n") != NULL);
        store_t st2;
        store_init(&st2, "/nonexistent/rr2.soon");
        store_parse(&st2, "place Kranj | 46.2389 14.3556 | Europe/Ljubljana\nhome Kranj\n");
        import_report_t rep;
        ics_import(&st2, ics, &rep);
        CHECK_EQ(rep.added, 3);
        item_t *e = by_id(&st2, "rrrr0001");
        CHECK(e && e->rep.freq == REP_WEEKLY && e->rep.interval == 2 && e->rep.wdays == 0x5 && e->rep.until == civil_to_days(2026, 12, 31));
        CHECK(e && e->nskips == 1 && e->skips[0] == civil_to_days(2026, 9, 16));
        item_t *bd = by_id(&st2, "rrrr0002");
        CHECK(bd && bd->rep.freq == REP_MONTHLY && bd->rep.nth == 2 && bd->rep.nth_wday == 2 && bd->rep.count == 6);
        char *ics2 = ics_export(&st2, now);
        CHECK(!strcmp(ics, ics2));
        /* a rule we cannot express is kept as text, with a warning */
        const char *complex = "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:c@x\r\nDTSTART:20260901T100000Z\r\nSUMMARY:Complex\r\nRRULE:FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
        ics_import(&st2, complex, &rep);
        CHECK_EQ(rep.warnings, 1);
        int found = 0;
        for (int i = 0; i < st2.nitems; i++) if (st2.items[i].title && !strcmp(st2.items[i].title, "Complex")) { found++; CHECK(st2.items[i].rep.freq == REP_NONE && st2.items[i].extra && strstr(st2.items[i].extra, "  rrule: FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1")); }
        CHECK_EQ(found, 1);
        /* org: simple repeaters in the stamp, the full rule as a property; round trip */
        char *org = org_export(&st);
        CHECK(strstr(org, "  <2026-09-09 Wed>\n") != NULL);                          /* nth-weekday rule has no org repeater */
        CHECK(strstr(org, "  :REPEAT: monthly 2nd wed count 6\n") != NULL);
        CHECK(strstr(org, "SCHEDULED: <2026-09-07 Mon +3d>\n") != NULL);
        CHECK(strstr(org, "  :REPEAT: weekly mon wed every 2 until 2026-12-31\n  :SKIP: 2026-09-16 \n") != NULL);
        store_t st3;
        store_init(&st3, "/nonexistent/rr3.soon");
        org_import(&st3, org, &rep);
        CHECK_EQ(rep.added, 3);
        e = by_id(&st3, "rrrr0001");
        CHECK(e && e->rep.freq == REP_WEEKLY && e->rep.interval == 2 && e->rep.wdays == 0x5 && e->nskips == 1);
        item_t *w = by_id(&st3, "rrrr0003");
        CHECK(w && w->rep.freq == REP_DAILY && w->rep.interval == 3);
        char *org2 = org_export(&st3);
        CHECK(!strcmp(org, org2));
        free(ics); free(ics2); free(org); free(org2);
        store_free(&st); store_free(&st2); store_free(&st3);
    }

    /* ---- contacts and vCard ---- */
    {
        contacts_t cs;
        contacts_init(&cs, "/nonexistent/c.soon");
        contacts_parse(&cs, "C Simon Podnar\n  id: cccc0001\n  phone: +386 41 123 456\n  email: simon@example.si\n  place: Ljubljana\n  tags: friend neurology\n  note: Dentist referral.\n\nC Aljoša\n  phone: +386 31 000 000\n");
        CHECK_EQ(cs.n, 2);
        const contact_t *c = contacts_find(&cs, "simon");
        CHECK(c && !strcmp(c->name, "Simon Podnar") && !strcmp(c->phone, "+386 41 123 456"));
        CHECK(contacts_find(&cs, "ALJ") != NULL);
        CHECK(contacts_find(&cs, "x") == NULL);
        char *txt = contacts_serialize(&cs);
        CHECK(strstr(txt, "C Aljoša\n  id: ") != NULL);       /* sorted, id assigned */
        CHECK(strstr(txt, "C Simon Podnar\n  id: cccc0001\n  phone: +386 41 123 456\n  email: simon@example.si\n  place: Ljubljana\n  tags: friend neurology\n  note: Dentist referral.\n") != NULL);
        char *vcf = vcf_export(&cs);
        CHECK(strstr(vcf, "BEGIN:VCARD\r\nVERSION:3.0\r\n") != NULL);
        CHECK(strstr(vcf, "FN:Simon Podnar\r\nN:Podnar;Simon;;;\r\nTEL;TYPE=CELL:+386 41 123 456\r\nEMAIL:simon@example.si\r\n") != NULL);
        CHECK(strstr(vcf, "UID:soon-cccc0001\r\n") != NULL);
        contacts_t cs2;
        contacts_init(&cs2, "/nonexistent/d.soon");
        CHECK_EQ(vcf_import(&cs2, vcf), 2);
        CHECK_EQ(cs2.n, 2);
        c = contacts_find(&cs2, "Simon Podnar");
        CHECK(c && !strcmp(c->id, "cccc0001") && !strcmp(c->email, "simon@example.si") && !strcmp(c->place, "Ljubljana") && !strcmp(c->tags, "friend neurology"));
        CHECK_EQ(vcf_import(&cs2, vcf), 2);
        CHECK_EQ(cs2.n, 2);                                    /* updated, not duplicated */
        /* a phone-exported card: N only, folded line, escaped comma */
        const char *phone = "BEGIN:VCARD\r\nVERSION:3.0\r\nN:Prosen;Tomaž;;;\r\nTEL;TYPE=CELL,VOICE:+386 1 999\r\nNOTE:Physicist\\, Ljubljana. Long note that will surely get folded by a phone at sev\r\n enty five octets\r\nEND:VCARD\r\n";
        CHECK_EQ(vcf_import(&cs2, phone), 1);
        c = contacts_find(&cs2, "Tomaž Prosen");
        CHECK(c && !strcmp(c->phone, "+386 1 999") && strstr(c->note, "Physicist, Ljubljana. Long note that will surely get folded by a phone at seventy five octets"));
        free(txt); free(vcf);
        contacts_free(&cs); contacts_free(&cs2);
    }

    CHECK_DONE("codecs");
}
