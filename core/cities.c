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

/* cities.c — see cities.h */
#include "cities.h"

#include <ctype.h>
#include <string.h>

const city_t cities[] = {
    { "Accra",          5.6037,   -0.1870, "Africa/Accra" },
    { "Addis Ababa",    9.0320,   38.7469, "Africa/Addis_Ababa" },
    { "Alexandria VA", 38.8048,  -77.0469, "America/New_York" },
    { "Amsterdam",     52.3676,    4.9041, "Europe/Amsterdam" },
    { "Anchorage",     61.2181, -149.9003, "America/Anchorage" },
    { "Ankara",        39.9334,   32.8597, "Europe/Istanbul" },
    { "Arlington VA",  38.8816,  -77.0910, "America/New_York" },
    { "Athens",        37.9838,   23.7275, "Europe/Athens" },
    { "Atlanta",       33.7490,  -84.3880, "America/New_York" },
    { "Auckland",     -36.8485,  174.7633, "Pacific/Auckland" },
    { "Bangkok",       13.7563,  100.5018, "Asia/Bangkok" },
    { "Beijing",       39.9042,  116.4074, "Asia/Shanghai" },
    { "Belgrade",      44.7866,   20.4489, "Europe/Belgrade" },
    { "Berlin",        52.5200,   13.4050, "Europe/Berlin" },
    { "Bethesda MD",   38.9847,  -77.0947, "America/New_York" },
    { "Bled",          46.3683,   14.1146, "Europe/Ljubljana" },
    { "Bogotá",         4.7110,  -74.0721, "America/Bogota" },
    { "Boston",        42.3601,  -71.0589, "America/New_York" },
    { "Brasília",     -15.7975,  -47.8919, "America/Sao_Paulo" },
    { "Brussels",      50.8503,    4.3517, "Europe/Brussels" },
    { "Bucharest",     44.4268,   26.1025, "Europe/Bucharest" },
    { "Budapest",      47.4979,   19.0402, "Europe/Budapest" },
    { "Buenos Aires", -34.6037,  -58.3816, "America/Argentina/Buenos_Aires" },
    { "Cairo",         30.0444,   31.2357, "Africa/Cairo" },
    { "Cape Town",    -33.9249,   18.4241, "Africa/Johannesburg" },
    { "Celje",         46.2311,   15.2683, "Europe/Ljubljana" },
    { "Charlottesville VA", 38.0293, -78.4767, "America/New_York" },
    { "Chicago",       41.8781,  -87.6298, "America/Chicago" },
    { "Copenhagen",    55.6761,   12.5683, "Europe/Copenhagen" },
    { "Dakar",         14.7167,  -17.4677, "Africa/Dakar" },
    { "Delhi",         28.6139,   77.2090, "Asia/Kolkata" },
    { "Denver",        39.7392, -104.9903, "America/Denver" },
    { "Dubai",         25.2048,   55.2708, "Asia/Dubai" },
    { "Dublin",        53.3498,   -6.2603, "Europe/Dublin" },
    { "Duluth GA",     34.0029,  -84.1446, "America/New_York" },
    { "Frankfurt",     50.1109,    8.6821, "Europe/Berlin" },
    { "Geneva",        46.2044,    6.1432, "Europe/Zurich" },
    { "Graz",          47.0707,   15.4395, "Europe/Vienna" },
    { "Helsinki",      60.1699,   24.9384, "Europe/Helsinki" },
    { "Hong Kong",     22.3193,  114.1694, "Asia/Hong_Kong" },
    { "Honolulu",      21.3069, -157.8583, "Pacific/Honolulu" },
    { "Istanbul",      41.0082,   28.9784, "Europe/Istanbul" },
    { "Jakarta",       -6.2088,  106.8456, "Asia/Jakarta" },
    { "Johannesburg", -26.2041,   28.0473, "Africa/Johannesburg" },
    { "Klagenfurt",    46.6247,   14.3053, "Europe/Vienna" },
    { "Koper",         45.5481,   13.7302, "Europe/Ljubljana" },
    { "Kranj",         46.2389,   14.3556, "Europe/Ljubljana" },
    { "Kyiv",          50.4501,   30.5234, "Europe/Kyiv" },
    { "Lagos",          6.5244,    3.3792, "Africa/Lagos" },
    { "Lima",         -12.0464,  -77.0428, "America/Lima" },
    { "Lisbon",        38.7223,   -9.1393, "Europe/Lisbon" },
    { "Ljubljana",     46.0569,   14.5058, "Europe/Ljubljana" },
    { "London",        51.5074,   -0.1278, "Europe/London" },
    { "Los Angeles",   34.0522, -118.2437, "America/Los_Angeles" },
    { "Madrid",        40.4168,   -3.7038, "Europe/Madrid" },
    { "Manila",        14.5995,  120.9842, "Asia/Manila" },
    { "Maribor",       46.5547,   15.6459, "Europe/Ljubljana" },
    { "Mexico City",   19.4326,  -99.1332, "America/Mexico_City" },
    { "Miami",         25.7617,  -80.1918, "America/New_York" },
    { "Milan",         45.4642,    9.1900, "Europe/Rome" },
    { "Montréal",      45.5017,  -73.5673, "America/Toronto" },
    { "Moscow",        55.7558,   37.6173, "Europe/Moscow" },
    { "Mumbai",        19.0760,   72.8777, "Asia/Kolkata" },
    { "Munich",        48.1351,   11.5820, "Europe/Berlin" },
    { "Murska Sobota", 46.6625,   16.1664, "Europe/Ljubljana" },
    { "Nairobi",       -1.2921,   36.8219, "Africa/Nairobi" },
    { "New York",      40.7128,  -74.0060, "America/New_York" },
    { "Nova Gorica",   45.9560,   13.6484, "Europe/Ljubljana" },
    { "Novo Mesto",    45.8010,   15.1710, "Europe/Ljubljana" },
    { "Oslo",          59.9139,   10.7522, "Europe/Oslo" },
    { "Paris",         48.8566,    2.3522, "Europe/Paris" },
    { "Phoenix",       33.4484, -112.0740, "America/Phoenix" },
    { "Pittsburgh",    40.4406,  -79.9959, "America/New_York" },
    { "Portorož",      45.5147,   13.5931, "Europe/Ljubljana" },
    { "Prague",        50.0755,   14.4378, "Europe/Prague" },
    { "Reykjavík",     64.1466,  -21.9426, "Atlantic/Reykjavik" },
    { "Richmond VA",   37.5407,  -77.4360, "America/New_York" },
    { "Rio de Janeiro",-22.9068, -43.1729, "America/Sao_Paulo" },
    { "Rome",          41.9028,   12.4964, "Europe/Rome" },
    { "San Francisco", 37.7749, -122.4194, "America/Los_Angeles" },
    { "Santiago",     -33.4489,  -70.6693, "America/Santiago" },
    { "Savannah GA",   32.0809,  -81.0912, "America/New_York" },
    { "Seattle",       47.6062, -122.3321, "America/Los_Angeles" },
    { "Seoul",         37.5665,  126.9780, "Asia/Seoul" },
    { "Shanghai",      31.2304,  121.4737, "Asia/Shanghai" },
    { "Singapore",      1.3521,  103.8198, "Asia/Singapore" },
    { "Stockholm",     59.3293,   18.0686, "Europe/Stockholm" },
    { "Sydney",       -33.8688,  151.2093, "Australia/Sydney" },
    { "São Paulo",    -23.5505,  -46.6333, "America/Sao_Paulo" },
    { "Taipei",        25.0330,  121.5654, "Asia/Taipei" },
    { "Tallinn",       59.4370,   24.7536, "Europe/Tallinn" },
    { "Tehran",        35.6892,   51.3890, "Asia/Tehran" },
    { "Tel Aviv",      32.0853,   34.7818, "Asia/Jerusalem" },
    { "Tokyo",         35.6762,  139.6503, "Asia/Tokyo" },
    { "Toronto",       43.6532,  -79.3832, "America/Toronto" },
    { "Trieste",       45.6495,   13.7768, "Europe/Rome" },
    { "Vancouver",     49.2827, -123.1207, "America/Vancouver" },
    { "Venice",        45.4408,   12.3155, "Europe/Rome" },
    { "Vienna",        48.2082,   16.3738, "Europe/Vienna" },
    { "Warsaw",        52.2297,   21.0122, "Europe/Warsaw" },
    { "Washington DC", 38.9072,  -77.0369, "America/New_York" },
    { "Zagreb",        45.8150,   15.9819, "Europe/Zagreb" },
    { "Zürich",        47.3769,    8.5417, "Europe/Zurich" },
};

const int ncities = (int)(sizeof cities / sizeof cities[0]);

static int ci_prefix(const char *pre, const char *s)
{
    for (; *pre; pre++, s++)
        if (tolower((unsigned char)*pre) != tolower((unsigned char)*s)) return 0;
    return 1;
}

const city_t *city_find(const char *name)
{
    if (!name || !*name) return 0;
    const city_t *hit = 0;
    int n = 0;
    for (int i = 0; i < ncities; i++) {
        if (ci_prefix(name, cities[i].name) && ci_prefix(cities[i].name, name)) return &cities[i];
        if (ci_prefix(name, cities[i].name)) { hit = &cities[i]; n++; }
    }
    return n == 1 ? hit : 0;
}

const city_t *city_for_zone(const char *zone)
{
    if (!zone) return 0;
    for (int i = 0; i < ncities; i++)
        if (!strcmp(cities[i].zone, zone)) return &cities[i];
    return 0;
}
