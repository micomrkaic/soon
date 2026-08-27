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

/* Reference values (minutes after 00:00 UTC of the date) were produced with the
 * Python `astral` library (an independent Meeus implementation). Tolerance is
 * 1.5 minutes; the two implementations agree to within about 0.7 in practice. */
#include "check.h"
#include "../core/civil.h"
#include "../core/sun.h"

#define TOL 1.5

static sun_t S(int y, int m, int d, double lat, double lon)
{
    sun_t s;
    sun_compute(civil_to_days(y, m, d), lat, lon, &s);
    return s;
}

int main(void)
{
    sun_t s;

    /* Greenwich, summer solstice 2000 */
    s = S(2000, 6, 21, 51.4769, 0.0);
    CHECK_EQ(s.rise_set, SUN_NORMAL);
    CHECK_NEAR(s.rise, 223.1, TOL);
    CHECK_NEAR(s.set, 1220.5, TOL);
    CHECK_NEAR(s.noon, 721.7, TOL);
    CHECK_NEAR(s.civil_dawn, 174.5, TOL);
    CHECK_NEAR(s.civil_dusk, 1269.2, TOL);
    CHECK_NEAR(s.nautical_dawn, 100.1, TOL);
    CHECK_NEAR(s.nautical_dusk, 1343.5, TOL);
    CHECK_EQ(s.astro, SUN_ALWAYS_ABOVE);   /* no astronomical night in London in June */

    /* Alexandria VA, 2026-08-27 (dusks wrap past 24:00 UTC; only pre-noon values compared) */
    s = S(2026, 8, 27, 38.8048, -77.0469);
    CHECK_NEAR(s.rise, 633.1, TOL);
    CHECK_NEAR(s.set, 1425.6, TOL);
    CHECK_NEAR(s.noon, 1029.9, TOL);
    CHECK_NEAR(s.civil_dawn, 605.0, TOL);
    CHECK_NEAR(s.nautical_dawn, 572.1, TOL);
    CHECK_NEAR(s.astro_dawn, 537.7, TOL);
    CHECK(s.civil_dusk > 1440.0);          /* 00:13 UTC next day == 20:13 EDT */

    /* Ljubljana, both solstices */
    s = S(2026, 6, 21, 46.0569, 14.5058);
    CHECK_NEAR(s.rise, 191.2, TOL);
    CHECK_NEAR(s.set, 1136.4, TOL);
    CHECK_NEAR(s.noon, 663.7, TOL);
    CHECK_NEAR(s.civil_dawn, 151.8, TOL);
    CHECK_NEAR(s.civil_dusk, 1175.8, TOL);
    CHECK_NEAR(s.nautical_dawn, 99.7, TOL);
    CHECK_NEAR(s.nautical_dusk, 1227.9, TOL);
    CHECK_NEAR(s.astro_dawn, 26.4, TOL);
    CHECK_NEAR(s.astro_dusk, 1301.2, TOL);
    CHECK_NEAR(s.declination, 23.44, 0.05);

    s = S(2026, 12, 21, 46.0569, 14.5058);
    CHECK_NEAR(s.rise, 401.3, TOL);
    CHECK_NEAR(s.set, 918.7, TOL);
    CHECK_NEAR(s.noon, 659.8, TOL);
    CHECK_NEAR(s.civil_dawn, 366.0, TOL);
    CHECK_NEAR(s.civil_dusk, 954.1, TOL);
    CHECK_NEAR(s.nautical_dawn, 328.1, TOL);
    CHECK_NEAR(s.nautical_dusk, 992.0, TOL);
    CHECK_NEAR(s.astro_dawn, 291.6, TOL);
    CHECK_NEAR(s.astro_dusk, 1028.4, TOL);
    CHECK_NEAR(s.declination, -23.44, 0.05);

    /* Quito at the March equinox: day is a few minutes over 12 h because of refraction and the solar disc */
    s = S(2026, 3, 20, -0.1807, -78.4678);
    CHECK_NEAR(s.rise, 678.2, TOL);
    CHECK_NEAR(s.set, 1404.3, TOL);
    CHECK_NEAR(s.noon, 1041.5, TOL);
    CHECK_NEAR(s.civil_dawn, 657.1, TOL);
    CHECK_NEAR(s.civil_dusk, 1425.4, TOL);
    CHECK_NEAR(s.day_length, 726.0, 2.0);

    /* Sydney in December: sunrise is on the previous UTC day (negative minutes) */
    s = S(2026, 12, 21, -33.8688, 151.2093);
    CHECK(s.rise < 0);
    CHECK_NEAR(s.set, 545.2, TOL);
    CHECK_NEAR(s.noon, 113.0, TOL);
    CHECK_NEAR(s.civil_dusk, 574.9, TOL);
    CHECK_NEAR(s.nautical_dusk, 610.7, TOL);
    CHECK_NEAR(s.astro_dusk, 650.0, TOL);

    /* Duluth GA, mid-January */
    s = S(2026, 1, 15, 34.0029, -84.1446);
    CHECK_NEAR(s.rise, 762.0, TOL);
    CHECK_NEAR(s.set, 1370.4, TOL);
    CHECK_NEAR(s.noon, 1065.8, TOL);
    CHECK_NEAR(s.civil_dawn, 734.4, TOL);
    CHECK_NEAR(s.civil_dusk, 1398.0, TOL);
    CHECK_NEAR(s.nautical_dawn, 703.8, TOL);
    CHECK_NEAR(s.nautical_dusk, 1428.6, TOL);
    CHECK_NEAR(s.astro_dawn, 673.8, TOL);

    /* Reykjavik in June: sun sets, but never gets 6 degrees below the horizon */
    s = S(2026, 6, 21, 64.1466, -21.9426);
    CHECK_EQ(s.rise_set, SUN_NORMAL);
    CHECK_EQ(s.civil, SUN_ALWAYS_ABOVE);
    CHECK_EQ(s.nautical, SUN_ALWAYS_ABOVE);
    CHECK_EQ(s.astro, SUN_ALWAYS_ABOVE);

    /* Tromsø: midnight sun and polar night */
    s = S(2026, 6, 21, 69.65, 18.96);
    CHECK_EQ(s.rise_set, SUN_ALWAYS_ABOVE);
    CHECK_NEAR(s.day_length, 1440.0, 0.0);
    s = S(2026, 12, 21, 69.65, 18.96);
    CHECK_EQ(s.rise_set, SUN_ALWAYS_BELOW);
    CHECK_EQ(s.civil, SUN_NORMAL);          /* but civil twilight still happens */
    CHECK_NEAR(s.day_length, 0.0, 0.0);
    CHECK(s.civil_dawn < s.noon && s.noon < s.civil_dusk);

    /* structural properties */
    s = S(2026, 8, 27, 38.8048, -77.0469);
    CHECK(s.astro_dawn < s.nautical_dawn && s.nautical_dawn < s.civil_dawn && s.civil_dawn < s.rise);
    CHECK(s.rise < s.noon && s.noon < s.set);
    CHECK(s.set < s.civil_dusk && s.civil_dusk < s.nautical_dusk && s.nautical_dusk < s.astro_dusk);

    /* moving 15 degrees east shifts everything about an hour earlier */
    sun_t e = S(2026, 8, 27, 38.8048, -77.0469 + 15.0);
    CHECK_NEAR(s.noon - e.noon, 60.0, 0.1);
    CHECK_NEAR(s.rise - e.rise, 60.0, 0.5);

    /* equator: hemispheres swap day length across the solstice */
    sun_t n = S(2026, 6, 21, 40.0, 0.0), so = S(2026, 12, 21, -40.0, 0.0);
    CHECK_NEAR(n.day_length, so.day_length, 3.0);

    /* local time conversion with day carry */
    int hh, mm, dd;
    sun_local_hhmm(1453.4, -240, &hh, &mm, &dd);  CHECK_EQ(hh, 20); CHECK_EQ(mm, 13); CHECK_EQ(dd, 0);
    sun_local_hhmm(1453.4, 0, &hh, &mm, &dd);     CHECK_EQ(hh, 0);  CHECK_EQ(mm, 13); CHECK_EQ(dd, 1);
    sun_local_hhmm(-319.2, 660, &hh, &mm, &dd);   CHECK_EQ(hh, 5);  CHECK_EQ(mm, 41); CHECK_EQ(dd, 0);
    sun_local_hhmm(-319.2, 0, &hh, &mm, &dd);     CHECK_EQ(hh, 18); CHECK_EQ(mm, 41); CHECK_EQ(dd, -1);
    sun_local_hhmm(59.6, 0, &hh, &mm, &dd);       CHECK_EQ(hh, 1);  CHECK_EQ(mm, 0);  CHECK_EQ(dd, 0);

    CHECK_DONE("sun");
}
