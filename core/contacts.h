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

/* contacts.h — the address book: a second small store next to the agenda.
 * File format mirrors FORMAT.md:  C NAME  followed by indented fields.
 * vCard 3.0 import/export for interchange with phones and mail clients.
 */
#ifndef SOON_CONTACTS_H
#define SOON_CONTACTS_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char id[9];
    char *name, *phone, *email, *place, *tags, *note, *extra;
} contact_t;

typedef struct {
    contact_t *c;
    int n, cap;
    char *path;
    bool dirty;
} contacts_t;

void contacts_init(contacts_t *cs, const char *path);
void contacts_free(contacts_t *cs);
void contacts_parse(contacts_t *cs, const char *text);
char *contacts_serialize(contacts_t *cs);          /* malloc'd; sorted by name */
int  contacts_load(contacts_t *cs);                /* 0 ok, 1 missing, -1 error */
int  contacts_save(contacts_t *cs);

int  contacts_add(contacts_t *cs, const contact_t *tmpl);   /* copies; assigns id; returns index */
void contacts_delete(contacts_t *cs, int idx);
void contact_set(char **field, const char *value);
/* exact (case-insensitive) match, else unique prefix; NULL otherwise */
const contact_t *contacts_find(const contacts_t *cs, const char *name);

/* vCard 3.0 */
char *vcf_export(const contacts_t *cs);
int   vcf_import(contacts_t *cs, const char *text);          /* returns contacts added or updated */

#endif
