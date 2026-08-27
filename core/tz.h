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

/* tz.h — the only place the core touches time zone rules.
 *
 * Two implementations: platform/tz_posix.c (system zoneinfo via TZ + libc)
 * and, later, platform/tz_wasm.c (JS Intl API through an import). The core
 * never sees which one it got.
 *
 * Zones are IANA names ("Europe/Ljubljana"). Offsets are minutes east of UTC.
 */
#ifndef SOON_TZ_H
#define SOON_TZ_H

#include <stdbool.h>

/* True if the zone name is known to the platform. */
bool tz_zone_exists(const char *zone);

/* Offset of `zone` at the given UTC instant. Returns 0 on success, -1 on unknown zone. */
int tz_offset(const char *zone, long long utc_seconds, int *offset_minutes);

/* Wall clock in `zone` -> UTC seconds. Ambiguous times (fall-back hour) take the
 * earlier instant; nonexistent times (spring-forward gap) are moved forward.
 * Returns 0 on success, -1 on unknown zone. */
int tz_wall_to_utc(const char *zone, int y, int m, int d, int hh, int mm, long long *utc_seconds);

/* UTC seconds -> wall clock in `zone`. Returns 0 on success, -1 on unknown zone. */
int tz_utc_to_wall(const char *zone, long long utc_seconds,
                   int *y, int *m, int *d, int *hh, int *mm, int *offset_minutes);

/* Short label for the zone at the instant: "EDT", "CEST", "IST", or "" when the
 * database only has a numeric name (then the caller shows the offset alone). */
void tz_abbrev(const char *zone, long long utc_seconds, char *out, int n);

#endif
