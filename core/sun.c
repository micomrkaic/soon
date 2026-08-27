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

/* sun.c — see sun.h */
#include "sun.h"

#include <math.h>

#define PI 3.14159265358979323846
#define RAD(x) ((x) * PI / 180.0)
#define DEG(x) ((x) * 180.0 / PI)

/* Solar position quantities at a given Julian century since J2000. */
typedef struct {
    double declination; /* degrees */
    double eqtime;      /* minutes */
} solpos_t;

static double norm360(double x)
{
    x = fmod(x, 360.0);
    return x < 0 ? x + 360.0 : x;
}

static solpos_t solpos(double jc)
{
    double L0 = norm360(280.46646 + jc * (36000.76983 + jc * 0.0003032));   /* geometric mean longitude */
    double M  = 357.52911 + jc * (35999.05029 - 0.0001537 * jc);             /* geometric mean anomaly */
    double e  = 0.016708634 - jc * (0.000042037 + 0.0000001267 * jc);        /* eccentricity of Earth's orbit */

    double C  = sin(RAD(M)) * (1.914602 - jc * (0.004817 + 0.000014 * jc))   /* equation of centre */
              + sin(RAD(2 * M)) * (0.019993 - 0.000101 * jc)
              + sin(RAD(3 * M)) * 0.000289;

    double omega   = 125.04 - 1934.136 * jc;
    double lambda  = L0 + C - 0.00569 - 0.00478 * sin(RAD(omega));            /* apparent longitude */
    double eps0    = 23.0 + (26.0 + (21.448 - jc * (46.815 + jc * (0.00059 - jc * 0.001813))) / 60.0) / 60.0;
    double eps     = eps0 + 0.00256 * cos(RAD(omega));                        /* corrected obliquity */

    solpos_t p;
    p.declination = DEG(asin(sin(RAD(eps)) * sin(RAD(lambda))));

    double y = tan(RAD(eps / 2.0));
    y *= y;
    double L0r = RAD(L0), Mr = RAD(M);
    double Etime = y * sin(2 * L0r)
                 - 2.0 * e * sin(Mr)
                 + 4.0 * e * y * sin(Mr) * cos(2 * L0r)
                 - 0.5 * y * y * sin(4 * L0r)
                 - 1.25 * e * e * sin(2 * Mr);
    p.eqtime = 4.0 * DEG(Etime);
    return p;
}

/* Hour angle (degrees) at which the sun's centre reaches the given zenith.
 * Returns +1 if the sun never gets that low, -1 if it never gets that high. */
static sun_state_t hour_angle(double lat, double decl, double zenith, double *ha)
{
    double latr = RAD(lat), decr = RAD(decl);
    double cosh = cos(RAD(zenith)) / (cos(latr) * cos(decr)) - tan(latr) * tan(decr);
    if (cosh > 1.0) { *ha = 0.0; return SUN_ALWAYS_BELOW; }
    if (cosh < -1.0) { *ha = 180.0; return SUN_ALWAYS_ABOVE; }
    *ha = DEG(acos(cosh));
    return SUN_NORMAL;
}

/* One event pair for a zenith: t = noon -/+ 4*HA, refined once with the
 * declination at the event's own time instead of at noon. */
static sun_state_t event_pair(double jd0, double noon, double lat, double zenith,
                              double decl_noon, double *dawn, double *dusk)
{
    double ha;
    sun_state_t st = hour_angle(lat, decl_noon, zenith, &ha);
    if (st != SUN_NORMAL) {
        *dawn = *dusk = noon;
        return st;
    }
    double t1 = noon - 4.0 * ha;
    double t2 = noon + 4.0 * ha;

    solpos_t p1 = solpos((jd0 + t1 / 1440.0 - 2451545.0) / 36525.0);
    solpos_t p2 = solpos((jd0 + t2 / 1440.0 - 2451545.0) / 36525.0);
    double ha1, ha2;
    sun_state_t s1 = hour_angle(lat, p1.declination, zenith, &ha1);
    sun_state_t s2 = hour_angle(lat, p2.declination, zenith, &ha2);
    if (s1 != SUN_NORMAL || s2 != SUN_NORMAL) {
        /* the refinement crossed a polar boundary; keep the first estimate */
        *dawn = t1;
        *dusk = t2;
        return SUN_NORMAL;
    }
    *dawn = noon - 4.0 * ha1;
    *dusk = noon + 4.0 * ha2;
    return SUN_NORMAL;
}

void sun_compute(daynum_t day, double lat, double lon, sun_t *out)
{
    double jd0 = (double)day + 2440587.5;          /* Julian day at 00:00 UTC */

    /* solar noon: first with values at 12:00 UTC, then refined at the estimate */
    solpos_t p = solpos((jd0 + 0.5 - 2451545.0) / 36525.0);
    double noon = 720.0 - 4.0 * lon - p.eqtime;
    p = solpos((jd0 + noon / 1440.0 - 2451545.0) / 36525.0);
    noon = 720.0 - 4.0 * lon - p.eqtime;

    out->noon = noon;
    out->declination = p.declination;
    out->eqtime = p.eqtime;

    out->rise_set = event_pair(jd0, noon, lat, 90.833, p.declination, &out->rise, &out->set);
    out->civil    = event_pair(jd0, noon, lat, 96.0,   p.declination, &out->civil_dawn, &out->civil_dusk);
    out->nautical = event_pair(jd0, noon, lat, 102.0,  p.declination, &out->nautical_dawn, &out->nautical_dusk);
    out->astro    = event_pair(jd0, noon, lat, 108.0,  p.declination, &out->astro_dawn, &out->astro_dusk);

    switch (out->rise_set) {
    case SUN_NORMAL:       out->day_length = out->set - out->rise; break;
    case SUN_ALWAYS_ABOVE: out->day_length = 1440.0; break;
    default:               out->day_length = 0.0; break;
    }
}

void sun_local_hhmm(double utc_minutes, int offset_minutes, int *hh, int *mm, int *day_delta)
{
    double local = utc_minutes + offset_minutes;
    int m = (int)floor(local + 0.5);     /* round to the nearest minute */
    int dd = 0;
    while (m < 0) { m += 1440; dd--; }
    while (m >= 1440) { m -= 1440; dd++; }
    if (hh) *hh = m / 60;
    if (mm) *mm = m % 60;
    if (day_delta) *day_delta = dd;
}
