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

/* contacts.c — see contacts.h */
#include "contacts.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "text.h"

void contact_set(char **field, const char *value)
{
    str_set(field, value);
}

void contacts_init(contacts_t *cs, const char *path)
{
    memset(cs, 0, sizeof *cs);
    cs->path = xstrdup(path);
}

static void contact_free(contact_t *c)
{
    free(c->name); free(c->phone); free(c->email); free(c->place); free(c->tags); free(c->note); free(c->extra);
}

void contacts_free(contacts_t *cs)
{
    for (int i = 0; i < cs->n; i++) contact_free(&cs->c[i]);
    free(cs->c);
    free(cs->path);
    memset(cs, 0, sizeof *cs);
}

static unsigned long long rng_state;

static void new_id(contacts_t *cs, char out[9])
{
    if (!rng_state) rng_state = (unsigned long long)time(NULL) * 2862933555777941757ULL + 3037000493ULL;
    for (;;) {
        rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17;
        snprintf(out, 9, "%08llx", rng_state & 0xffffffffULL);
        bool clash = false;
        for (int i = 0; i < cs->n; i++) if (!strcmp(cs->c[i].id, out)) clash = true;
        if (!clash) return;
    }
}

int contacts_add(contacts_t *cs, const contact_t *tmpl)
{
    if (cs->n == cs->cap) {
        int ncap = cs->cap ? cs->cap * 2 : 32;
        contact_t *p = realloc(cs->c, (size_t)ncap * sizeof *p);
        if (!p) return -1;
        cs->c = p;
        cs->cap = ncap;
    }
    contact_t *c = &cs->c[cs->n];
    memset(c, 0, sizeof *c);
    memcpy(c->id, tmpl->id, sizeof c->id);
    c->name = xstrdup(tmpl->name); c->phone = xstrdup(tmpl->phone); c->email = xstrdup(tmpl->email);
    c->place = xstrdup(tmpl->place); c->tags = xstrdup(tmpl->tags); c->note = xstrdup(tmpl->note); c->extra = xstrdup(tmpl->extra);
    if (!c->id[0]) new_id(cs, c->id);
    cs->dirty = true;
    return cs->n++;
}

void contacts_delete(contacts_t *cs, int idx)
{
    if (idx < 0 || idx >= cs->n) return;
    contact_free(&cs->c[idx]);
    memmove(&cs->c[idx], &cs->c[idx + 1], (size_t)(cs->n - idx - 1) * sizeof cs->c[0]);
    cs->n--;
    cs->dirty = true;
}

const contact_t *contacts_find(const contacts_t *cs, const char *name)
{
    if (!name || !*name) return NULL;
    const contact_t *hit = NULL;
    int n = 0;
    for (int i = 0; i < cs->n; i++) {
        if (!cs->c[i].name) continue;
        if (str_ieq(cs->c[i].name, name)) return &cs->c[i];
        if (str_iprefix(name, cs->c[i].name)) { hit = &cs->c[i]; n++; }
    }
    return n == 1 ? hit : NULL;
}

/* ---------- native text format ---------- */

static void apply_field(contact_t *c, const char *key, const char *val, const char *line)
{
    if (!strcmp(key, "id")) snprintf(c->id, sizeof c->id, "%.8s", val);
    else if (!strcmp(key, "phone")) str_set(&c->phone, val);
    else if (!strcmp(key, "email")) str_set(&c->email, val);
    else if (!strcmp(key, "place")) str_set(&c->place, val);
    else if (!strcmp(key, "tags")) str_set(&c->tags, val);
    else if (!strcmp(key, "note")) str_append_line(&c->note, val);
    else str_append_line(&c->extra, line);
}

void contacts_parse(contacts_t *cs, const char *text)
{
    contact_t cur;
    memset(&cur, 0, sizeof cur);
    bool have = false;
    char line[2048];
    const char *p = text;
    while (next_line(&p, line, sizeof line)) {
        if (line[0] == '#') continue;
        if (line[0] == ' ' || line[0] == '\t') {
            if (!have) continue;
            const char *f = skipws(line);
            const char *colon = strchr(f, ':');
            char key[32];
            if (!colon || (size_t)(colon - f) >= sizeof key) { str_append_line(&cur.extra, line); continue; }
            memcpy(key, f, (size_t)(colon - f));
            key[colon - f] = 0;
            const char *val = colon + 1;
            if (*val == ' ') val++;
            apply_field(&cur, key, val, line);
            continue;
        }
        if (have) { contacts_add(cs, &cur); contact_free(&cur); memset(&cur, 0, sizeof cur); have = false; }
        if (line[0] == 'C' && (line[1] == ' ' || line[1] == '\t')) {
            str_set(&cur.name, skipws(line + 1));
            have = true;
        }
    }
    if (have) { contacts_add(cs, &cur); contact_free(&cur); }
    cs->dirty = false;
}

static int cmp_contact(const void *a, const void *b)
{
    const contact_t *x = a, *y = b;
    return strcmp(x->name ? x->name : "", y->name ? y->name : "");
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

char *contacts_serialize(contacts_t *cs)
{
    sbuf_t b = { 0, 0, 0 };
    sb_puts(&b, "# soon contacts — C NAME, then indented fields: phone email place tags note\n\n");
    qsort(cs->c, (size_t)cs->n, sizeof cs->c[0], cmp_contact);
    for (int i = 0; i < cs->n; i++) {
        const contact_t *c = &cs->c[i];
        sb_printf(&b, "C %s\n  id: %s\n", c->name ? c->name : "", c->id);
        put_field(&b, "phone", c->phone);
        put_field(&b, "email", c->email);
        put_field(&b, "place", c->place);
        put_field(&b, "tags", c->tags);
        put_field(&b, "note", c->note);
        if (c->extra) sb_printf(&b, "%s\n", c->extra);
        sb_put(&b, "\n", 1);
    }
    return sb_take(&b);
}

#ifndef SOON_NO_FILE_IO
#include <sys/stat.h>

int contacts_load(contacts_t *cs)
{
    FILE *f = fopen(cs->path, "rb");
    if (!f) return errno == ENOENT ? 1 : -1;
    sbuf_t b = { 0, 0, 0 };
    char chunk[8192];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) sb_put(&b, chunk, n);
    int rc = ferror(f) ? -1 : 0;
    fclose(f);
    if (rc == 0) contacts_parse(cs, b.s ? b.s : "");
    free(b.s);
    return rc;
}

int contacts_save(contacts_t *cs)
{
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", cs->path);
    for (char *p = buf + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(buf, 0755); *p = '/'; }
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", cs->path);
    char *text = contacts_serialize(cs);
    FILE *f = fopen(tmp, "wb");
    if (!f) { free(text); return -1; }
    size_t len = strlen(text);
    int rc = fwrite(text, 1, len, f) == len ? 0 : -1;
    free(text);
    if (fclose(f) != 0) rc = -1;
    if (rc == 0 && rename(tmp, cs->path) != 0) rc = -1;
    if (rc == 0) cs->dirty = false;
    return rc;
}
#endif

/* ---------- vCard 3.0 ---------- */

static void vcf_escape(sbuf_t *b, const char *s)
{
    for (; *s; s++) {
        if (*s == '\\' || *s == ',' || *s == ';') { sb_put(b, "\\", 1); sb_put(b, s, 1); }
        else if (*s == '\n') sb_put(b, "\\n", 2);
        else sb_put(b, s, 1);
    }
}

/* fold at 75 octets without splitting a UTF-8 sequence */
static void vcf_fold(sbuf_t *out, const char *line)
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

static void vcf_prop(sbuf_t *out, const char *name, const char *val)
{
    if (!val || !*val) return;
    sbuf_t l = { 0, 0, 0 };
    sb_puts(&l, name);
    sb_put(&l, ":", 1);
    vcf_escape(&l, val);
    vcf_fold(out, l.s ? l.s : "");
    free(l.s);
}

char *vcf_export(const contacts_t *cs)
{
    sbuf_t b = { 0, 0, 0 };
    for (int i = 0; i < cs->n; i++) {
        const contact_t *c = &cs->c[i];
        sb_puts(&b, "BEGIN:VCARD\r\nVERSION:3.0\r\nPRODID:-//soon//agenda//EN\r\n");
        vcf_prop(&b, "FN", c->name);
        /* N is required by the spec: last name first when there are two words */
        if (c->name) {
            const char *sp = strrchr(c->name, ' ');
            sbuf_t n = { 0, 0, 0 };
            sb_puts(&n, "N:");
            if (sp) { vcf_escape(&n, sp + 1); sb_put(&n, ";", 1); char first[256]; snprintf(first, sizeof first, "%.*s", (int)(sp - c->name), c->name); vcf_escape(&n, first); sb_puts(&n, ";;;"); }
            else { vcf_escape(&n, c->name); sb_puts(&n, ";;;;"); }
            vcf_fold(&b, n.s);
            free(n.s);
        }
        vcf_prop(&b, "TEL;TYPE=CELL", c->phone);
        vcf_prop(&b, "EMAIL", c->email);
        vcf_prop(&b, "ADR;TYPE=HOME", c->place ? c->place : "");
        vcf_prop(&b, "CATEGORIES", c->tags);
        vcf_prop(&b, "NOTE", c->note);
        char uid[32];
        snprintf(uid, sizeof uid, "soon-%s", c->id);
        vcf_prop(&b, "UID", uid);
        sb_puts(&b, "END:VCARD\r\n");
    }
    return sb_take(&b);
}

static void vcf_unescape(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '\\' && s[1]) {
            s++;
            *o++ = (*s == 'n' || *s == 'N') ? '\n' : *s;
        } else *o++ = *s;
    }
    *o = 0;
}

/* unfold continuation lines into a single string per property */
static char *vcf_unfold(const char *text)
{
    sbuf_t b = { 0, 0, 0 };
    for (const char *p = text; *p; p++) {
        if (*p == '\r') continue;
        if (*p == '\n' && (p[1] == ' ' || p[1] == '\t')) { p++; continue; }
        sb_put(&b, p, 1);
    }
    return sb_take(&b);
}

int vcf_import(contacts_t *cs, const char *text)
{
    char *flat = vcf_unfold(text);
    const char *p = flat;
    char line[4096];
    contact_t cur;
    memset(&cur, 0, sizeof cur);
    bool in = false;
    int count = 0;
    while (next_line(&p, line, sizeof line)) {
        if (str_ieq(line, "BEGIN:VCARD")) { memset(&cur, 0, sizeof cur); in = true; continue; }
        if (str_ieq(line, "END:VCARD")) {
            if (in && cur.name) {
                int idx = -1;
                for (int i = 0; i < cs->n; i++) if (!strcmp(cs->c[i].id, cur.id) && cur.id[0]) idx = i;
                if (idx < 0) for (int i = 0; i < cs->n; i++) if (cs->c[i].name && str_ieq(cs->c[i].name, cur.name)) idx = i;
                if (idx >= 0) {
                    contact_t *c = &cs->c[idx];
                    if (cur.phone) str_set(&c->phone, cur.phone);
                    if (cur.email) str_set(&c->email, cur.email);
                    if (cur.place) str_set(&c->place, cur.place);
                    if (cur.tags) str_set(&c->tags, cur.tags);
                    if (cur.note) str_set(&c->note, cur.note);
                    cs->dirty = true;
                } else contacts_add(cs, &cur);
                count++;
            }
            contact_free(&cur);
            memset(&cur, 0, sizeof cur);
            in = false;
            continue;
        }
        if (!in) continue;
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = 0;
        char *val = colon + 1;
        char *semi = strchr(line, ';');
        if (semi) *semi = 0;                  /* drop parameters */
        vcf_unescape(val);
        if (str_ieq(line, "FN")) str_set(&cur.name, val);
        else if (str_ieq(line, "N") && !cur.name) {
            /* "Last;First;;;" -> "First Last" */
            char *s2 = strchr(val, ';');
            char name[512];
            if (s2) {
                *s2 = 0;
                char *first = s2 + 1, *e = strchr(first, ';');
                if (e) *e = 0;
                snprintf(name, sizeof name, "%s%s%s", first, *first && *val ? " " : "", val);
            } else snprintf(name, sizeof name, "%s", val);
            rtrim(name);
            str_set(&cur.name, name);
        }
        else if (str_ieq(line, "TEL")) { if (!cur.phone) str_set(&cur.phone, val); }
        else if (str_ieq(line, "EMAIL")) { if (!cur.email) str_set(&cur.email, val); }
        else if (str_ieq(line, "ADR")) {
            /* pick the first non-empty component after the PO box and extended fields (usually the city) */
            char *parts[8] = { 0 }; int np = 0;
            for (char *q = strtok(val, ";"); q && np < 8; q = strtok(NULL, ";")) parts[np++] = q;
            const char *city = NULL;
            for (int i = 0; i < np; i++) if (parts[i] && parts[i][0]) { city = parts[i]; if (i >= 3) break; }
            if (city && !cur.place) str_set(&cur.place, city);
        }
        else if (str_ieq(line, "CATEGORIES")) { for (char *q = val; *q; q++) if (*q == ',') *q = ' '; str_set(&cur.tags, val); }
        else if (str_ieq(line, "NOTE")) str_set(&cur.note, val);
        else if (str_ieq(line, "UID")) { if (!strncmp(val, "soon-", 5) && strlen(val) == 13) snprintf(cur.id, sizeof cur.id, "%.8s", val + 5); }
    }
    contact_free(&cur);
    free(flat);
    return count;
}
