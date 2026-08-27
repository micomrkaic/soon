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

/* codecs.h — import/export to standard formats. Every codec works on text in
 * memory, so the same code serves the CLI, the app and the browser build.
 *
 *   org    Emacs org-mode: TODO/DONE headlines with SCHEDULED/DEADLINE/CLOSED,
 *          events as active timestamps, :ID:/:LOCATION:/:WHO: properties,
 *          body lines as notes.
 *   ics    iCalendar (RFC 5545): VEVENT and VTODO, UTC times, UID round trip.
 *
 * Import merges: an item whose id (org :ID:, ics UID) is already in the store
 * is updated in place; otherwise an item with the same kind, title and day is
 * updated; otherwise it is added. Nothing is ever deleted by an import.
 */
#ifndef SOON_CODECS_H
#define SOON_CODECS_H

#include "model.h"

typedef struct {
    int added, updated, skipped;
    int warnings;
    char warn[1024];        /* newline-separated messages, truncated */
} import_report_t;

char *org_export(store_t *st);
int   org_import(store_t *st, const char *text, import_report_t *rep);

char *ics_export(store_t *st, long long now_utc);   /* now_utc: for DTSTAMP */
int   ics_import(store_t *st, const char *text, import_report_t *rep);

#endif
