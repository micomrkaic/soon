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

/* sun.h — sunrise, sunset, solar noon and twilights for one civil day.
 *
 * Algorithm: NOAA Solar Calculator equations (Meeus, Astronomical Algorithms),
 * with one refinement pass per event. Accuracy is about a minute for latitudes
 * below ~70°, and degrades gracefully toward the poles.
 *
 * All times are minutes after 00:00 UTC of the given civil date, so they may be
 * negative or exceed 1440 far from Greenwich. Add the zone offset to obtain
 * local wall clock; sun_local_hhmm() does that and reports a day carry.
 */
#ifndef SOON_SUN_H
#define SOON_SUN_H

#include "civil.h"

typedef enum {
    SUN_NORMAL = 0,       /* the event happens on this day */
    SUN_ALWAYS_ABOVE = 1, /* sun never goes below this horizon (midnight sun) */
    SUN_ALWAYS_BELOW = -1 /* sun never rises above this horizon (polar night) */
} sun_state_t;

typedef struct {
    double noon;                  /* solar noon, minutes after 00:00 UTC */
    double declination;           /* degrees, at solar noon */
    double eqtime;                /* equation of time, minutes, at solar noon */

    double rise, set;             /* geometric zenith 90.833° (refraction + disc) */
    double civil_dawn, civil_dusk;         /* 96°  */
    double nautical_dawn, nautical_dusk;   /* 102° */
    double astro_dawn, astro_dusk;         /* 108° */

    sun_state_t rise_set, civil, nautical, astro;
    double day_length;            /* minutes; 0 or 1440 in the polar cases */
} sun_t;

/* lat in degrees north, lon in degrees east */
void sun_compute(daynum_t day, double lat, double lon, sun_t *out);

/* Convert a UTC-minute value to local hh:mm; *day_delta is -1/0/+1 relative to the date. */
void sun_local_hhmm(double utc_minutes, int offset_minutes, int *hh, int *mm, int *day_delta);

#endif
