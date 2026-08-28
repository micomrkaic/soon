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

/* main.c — soon: agenda and todo TUI. See README.md. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "core/cities.h"
#include "core/codecs.h"
#include "core/config.h"
#include "core/contacts.h"
#include "core/datejump.h"
#include "core/model.h"
#include "core/tz.h"
#include "ui/app.h"

#ifndef SOON_VERSION
#define SOON_VERSION "dev"
#endif

static long long now_utc(void)
{
    return (long long)time(NULL);
}

static void default_path(char *out, size_t n)
{
    const char *env = getenv("SOON_FILE");
    if (env && *env) { snprintf(out, n, "%s", env); return; }
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && *xdg) { snprintf(out, n, "%s/soon/agenda.soon", xdg); return; }
    const char *home = getenv("HOME");
    snprintf(out, n, "%s/.local/share/soon/agenda.soon", home ? home : ".");
}

/* IANA zone of the machine: $TZ, else the /etc/localtime symlink */
static const char *system_zone(char *buf, size_t n)
{
    const char *tz = getenv("TZ");
    if (tz && *tz) {
        if (*tz == ':') tz++;
        if (tz_zone_exists(tz)) { snprintf(buf, n, "%s", tz); return buf; }
    }
    char link[1024];
    ssize_t k = readlink("/etc/localtime", link, sizeof link - 1);
    if (k > 0) {
        link[k] = 0;
        const char *z = strstr(link, "zoneinfo/");
        if (z && tz_zone_exists(z + 9)) { snprintf(buf, n, "%s", z + 9); return buf; }
    }
    return NULL;
}

/* contacts.soon next to the agenda file unless overridden */
static void contacts_path(const char *agenda, char *out, size_t n)
{
    const char *env = getenv("SOON_CONTACTS");
    if (env && *env) { snprintf(out, n, "%s", env); return; }
    const char *slash = strrchr(agenda, '/');
    if (slash) snprintf(out, n, "%.*s/contacts.soon", (int)(slash - agenda), agenda);
    else snprintf(out, n, "contacts.soon");
}

static char *read_file(const char *path)
{
    FILE *f = strcmp(path, "-") == 0 ? stdin : fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 65536, n = 0;
    char *buf = malloc(cap);
    size_t k;
    while (buf && (k = fread(buf + n, 1, cap - n - 1, f)) > 0) {
        n += k;
        if (n + 1 >= cap) { cap *= 2; char *p = realloc(buf, cap); if (!p) { free(buf); buf = NULL; break; } buf = p; }
    }
    if (buf) buf[n] = 0;
    if (f != stdin) fclose(f);
    return buf;
}

static int write_file(const char *path, const char *text)
{
    FILE *f = strcmp(path, "-") == 0 ? stdout : fopen(path, "wb");
    if (!f) return -1;
    fputs(text, f);
    int rc = (f == stdout ? fflush(f) : fclose(f)) == 0 ? 0 : -1;
    return rc;
}

/* soon export org|ics|vcf [FILE]   /   soon import org|ics|vcf FILE */
static int transfer(store_t *st, contacts_t *cs, int argc, char **argv)
{
    bool ex = !strcmp(argv[0], "export");
    if (argc < 2) { fprintf(stderr, "usage: soon %s org|ics|vcf %s\n", argv[0], ex ? "[FILE]" : "FILE"); return 2; }
    const char *fmt = argv[1];
    const char *path = argc > 2 ? argv[2] : (ex ? "-" : NULL);
    if (!path) { fprintf(stderr, "soon import: a file is needed (- for stdin)\n"); return 2; }
    if (ex) {
        char *text;
        if (!strcmp(fmt, "org")) text = org_export(st);
        else if (!strcmp(fmt, "ics")) text = ics_export(st, now_utc());
        else if (!strcmp(fmt, "vcf")) text = vcf_export(cs);
        else { fprintf(stderr, "soon export: unknown format '%s' (org, ics, vcf)\n", fmt); return 2; }
        int rc = write_file(path, text);
        if (rc) perror(path);
        free(text);
        return rc ? 1 : 0;
    }
    char *text = read_file(path);
    if (!text) { perror(path); return 1; }
    import_report_t rep;
    memset(&rep, 0, sizeof rep);
    int rc = 0;
    if (!strcmp(fmt, "org") || !strcmp(fmt, "ics")) {
        if (!strcmp(fmt, "org")) org_import(st, text, &rep); else ics_import(st, text, &rep);
        fprintf(stderr, "%d added, %d updated, %d skipped\n", rep.added, rep.updated, rep.skipped);
        if (rep.warnings) fprintf(stderr, "%s\n", rep.warn);
        if ((rep.added || rep.updated) && store_save(st) != 0) { perror(st->path); rc = 1; }
    } else if (!strcmp(fmt, "vcf")) {
        int n = vcf_import(cs, text);
        fprintf(stderr, "%d contacts imported\n", n);
        if (n && contacts_save(cs) != 0) { perror(cs->path); rc = 1; }
    } else { fprintf(stderr, "soon import: unknown format '%s' (org, ics, vcf)\n", fmt); rc = 2; }
    free(text);
    return rc;
}

static void config_path(char *out, size_t n)
{
    const char *env = getenv("SOON_CONFIG");
    if (env && *env) { snprintf(out, n, "%s", env); return; }
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) { snprintf(out, n, "%s/soon/config", xdg); return; }
    const char *home = getenv("HOME");
    snprintf(out, n, "%s/.config/soon/config", home ? home : ".");
}

static bool locale_is_utf8(void)
{
    const char *vars[] = { "LC_ALL", "LC_CTYPE", "LANG" };
    for (int i = 0; i < 3; i++) {
        const char *v = getenv(vars[i]);
        if (!v || !*v) continue;
        for (const char *p = v; *p; p++)
            if ((p[0] == 'u' || p[0] == 'U') && (p[1] == 't' || p[1] == 'T') && (p[2] == 'f' || p[2] == 'F')) return true;
        return false;
    }
    return false;
}

static void usage(void)
{
    fprintf(stderr,
        "soon " SOON_VERSION " — keyboard-driven agenda and todo TUI (GPL-3.0-or-later)\n"
        "usage: soon [-f FILE] [-s SLOT] [-a] [-m] [-p] [-d DAYS] [DATE]\n"
        "  -f FILE   agenda file (default $SOON_FILE or ~/.local/share/soon/agenda.soon)\n"
        "  -s SLOT   minutes per slot in week/day views: 5 10 15 30 60 (default 15)\n"
        "  -a        ASCII glyphs only\n"
        "  -m        start without the key menu (M toggles it)\n"
        "  -V        print the version\n"
        "  -p        print what is coming up instead of opening the interface\n"
        "            (automatic when stdout is not a terminal, e.g. soon | head)\n"
        "  -d DAYS   how far ahead -p looks (default 7)\n"
        "  -c FILE   contacts file (default contacts.soon next to the agenda, or $SOON_CONTACTS)\n"
        "  -C FILE   config file (default ~/.config/soon/config, or $SOON_CONFIG)\n"
        "  DATE      open at a date: 2026-09-14, 14.9., sep 14, +3w, next mon, w38 ...\n"
        "  keys      ? inside the app\n"
        "\n"
        "  soon export org|ics|vcf [FILE]   write org-mode, iCalendar or vCard (default stdout)\n"
        "  soon import org|ics|vcf FILE     merge from a file (- for stdin); nothing is deleted\n"
        "  soon config                      print a commented default config file\n");
}

/* plain-text summary for pipes and shell prompts */
static void print_soon(store_t *st, daynum_t from, daynum_t today, int days)
{
    int idx[64];
    for (daynum_t d = from; d <= from + days; d++) {
        int n = store_day_items(st, d, idx, 64);
        int shown = 0;
        for (int k = 0; k < n; k++) if (!(st->items[idx[k]].kind == ITEM_EVENT && !item_occurs_on(&st->items[idx[k]], d))) shown++;
        if (shown == 0 && d != from) continue;
        civil_t c = days_to_civil(d);
        const place_t *p = store_place_for_day(st, d);
        printf("%s%s %d %s %d%s%s\n", d == today ? "today, " : "", civil_weekday_names[civil_weekday(d)], c.d,
               civil_month_names[c.m], c.y, p ? "  @ " : "", p ? p->name : "");
        if (shown == 0) printf("  nothing\n");
        for (int k = 0; k < n; k++) {
            const item_t *it = &st->items[idx[k]];
            if (it->kind == ITEM_EVENT && !item_occurs_on(it, d)) continue;     /* multi-day: once */
            if (it->kind == ITEM_TODO) {
                char due[32] = "";
                if (it->due >= 0) { civil_t dc = days_to_civil(it->due); snprintf(due, sizeof due, "  due %d %.3s", dc.d, civil_month_names[dc.m]); }
                printf("  [%c] %s%s\n", todo_done_on(it, d) ? 'x' : ' ', it->title ? it->title : "", due);
            } else if (it->start < 0) {
                char until[32] = "";
                if (it->until >= 0 && it->until != it->day) { civil_t uc = days_to_civil(it->until); snprintf(until, sizeof until, "  (until %d %.3s)", uc.d, civil_month_names[uc.m]); }
                printf("  all day      %s%s\n", it->title ? it->title : "", until);
            } else {
                int end = it->start + it->dur;
                printf("  %02d:%02d-%02d:%02d  %s%s%s\n", it->start / 60, it->start % 60, (end / 60) % 24, end % 60,
                       it->title ? it->title : "", it->at ? "  @ " : "", it->at ? it->at : "");
            }
        }
    }
    bool header = false;
    for (int i = 0; i < st->nitems; i++) {
        const item_t *it = &st->items[i];
        if (it->kind != ITEM_TODO) continue;
        daynum_t day = it->day >= 0 ? it->day : it->due;
        bool overdue;
        if (item_is_repeating(it)) {
            day = todo_last_open_before(it, today);
            if (day < 0) continue;
            overdue = true;
        } else {
            if (it->done_day >= 0) continue;
            bool open = (it->day >= 0 && it->day < today) || (it->due >= 0 && it->due < today);
            if (!open && day >= 0) continue;
            overdue = todo_overdue_on(it, day, today);
        }
        if (!header) { printf("open todos\n"); header = true; }
        printf("  [ ] %s%s\n", it->title ? it->title : "", overdue ? "  (overdue)" : "");
    }
}

int main(int argc, char **argv)
{
    char path[1024];
    default_path(path, sizeof path);
    char cpath[1024] = "", confpath[1024] = "";
    int slot = 0, days = 7;
    bool ascii = false, nomenu = false, print = !isatty(STDOUT_FILENO);
    int opt;
    while ((opt = getopt(argc, argv, "f:c:C:s:apd:mhV")) != -1) {
        switch (opt) {
        case 'f': snprintf(path, sizeof path, "%s", optarg); break;
        case 'c': snprintf(cpath, sizeof cpath, "%s", optarg); break;
        case 'C': snprintf(confpath, sizeof confpath, "%s", optarg); break;
        case 's': slot = atoi(optarg); if (slot != 5 && slot != 10 && slot != 15 && slot != 30 && slot != 60) { usage(); return 2; } break;
        case 'a': ascii = true; break;
        case 'm': nomenu = true; break;
        case 'V': printf("soon %s\n", SOON_VERSION); return 0;
        case 'p': print = true; break;
        case 'd': days = atoi(optarg); if (days < 0 || days > 3650) { usage(); return 2; } break;
        default: usage(); return opt == 'h' ? 0 : 2;
        }
    }

    if (optind < argc && !strcmp(argv[optind], "config")) { fputs(config_template(), stdout); return 0; }

    config_t conf;
    config_defaults(&conf);
    if (!confpath[0]) config_path(confpath, sizeof confpath);
    {
        char err[512];
        int crc = config_load(&conf, confpath, err, sizeof err);
        if (crc < 0) { perror(confpath); return 1; }
        if (crc == 0 && err[0]) fprintf(stderr, "soon: %s: %s\n", confpath, err);
    }
    if (slot == 0) slot = conf.slot;
    if (conf.menu == 0) nomenu = true;

    store_t st;
    store_init(&st, path);
    int rc = store_load(&st);
    if (rc < 0) { perror(path); return 1; }
    if (!cpath[0]) contacts_path(path, cpath, sizeof cpath);
    contacts_t cs;
    contacts_init(&cs, cpath);
    if (contacts_load(&cs) < 0) { perror(cpath); return 1; }

    if (optind < argc && (!strcmp(argv[optind], "export") || !strcmp(argv[optind], "import"))) {
        rc = transfer(&st, &cs, argc - optind, argv + optind);
        contacts_free(&cs);
        store_free(&st);
        return rc;
    }

    /* first run: invent a home from the system zone so sun times work immediately */
    if (!st.home) {
        char zone[128];
        const char *z = system_zone(zone, sizeof zone);
        const city_t *c = z ? city_for_zone(z) : NULL;
        if (c) { store_add_place(&st, c->name, c->lat, c->lon, c->zone); store_set_home(&st, c->name); }
        else if (z) { store_add_place(&st, "Home", 0, 0, z); store_set_home(&st, "Home"); }
    }

    /* today in the home zone */
    daynum_t today;
    {
        const place_t *home = st.home ? store_find_place(&st, st.home) : NULL;
        int y, m, d, hh, mm, off;
        if (tz_utc_to_wall(home && home->zone ? home->zone : "UTC", now_utc(), &y, &m, &d, &hh, &mm, &off) == 0)
            today = civil_to_days(y, m, d);
        else today = (daynum_t)(now_utc() / 86400);
    }
    daynum_t start = -1;
    if (optind < argc) {
        char expr[256] = "";
        for (int i = optind; i < argc; i++) { if (expr[0]) strncat(expr, " ", sizeof expr - strlen(expr) - 1); strncat(expr, argv[i], sizeof expr - strlen(expr) - 1); }
        char err[64];
        if (datejump_parse(expr, today, &start, err, sizeof err) != 0) { fprintf(stderr, "soon: %s: %s\n", expr, err); return 2; }
    }

    if (print) {
        print_soon(&st, start >= 0 ? start : today, today, days);
        contacts_free(&cs);
        store_free(&st);
        return 0;
    }
    bool uni = conf.unicode < 0 ? locale_is_utf8() : conf.unicode == 1;
    app_config_t cfg = { &st, &cs, &conf, now_utc, slot, conf.work_start, !ascii && uni, !nomenu };
    rc = app_run(&cfg, start);
    contacts_free(&cs);
    store_free(&st);
    return rc;
}
