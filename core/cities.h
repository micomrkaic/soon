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

/* cities.h — small built-in gazetteer: name, latitude, longitude, IANA zone.
 * Used to complete a place from a typed name, and to give a zone a
 * representative coordinate when the app has to invent a home from the
 * system's time zone. Extend freely; keep it sorted by name.
 */
#ifndef SOON_CITIES_H
#define SOON_CITIES_H

typedef struct {
    const char *name;
    double lat, lon;
    const char *zone;
} city_t;

extern const city_t cities[];
extern const int ncities;

/* Case-insensitive exact match, then unique prefix match; NULL if none or ambiguous. */
const city_t *city_find(const char *name);
/* First city in the given IANA zone; NULL if none. */
const city_t *city_for_zone(const char *zone);

#endif
