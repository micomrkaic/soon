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

/* check.h — minimal assertion harness; each test binary returns nonzero on failure */
#ifndef SOON_CHECK_H
#define SOON_CHECK_H

#include <stdio.h>
#include <math.h>

static int check_pass = 0, check_fail = 0;

#define CHECK(cond) do { \
    if (cond) check_pass++; \
    else { check_fail++; fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a == _b) check_pass++; \
    else { check_fail++; fprintf(stderr, "%s:%d: FAIL %s == %s  (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } \
} while (0)

#define CHECK_NEAR(a, b, tol) do { \
    double _a = (a), _b = (b); \
    if (fabs(_a - _b) <= (tol)) check_pass++; \
    else { check_fail++; fprintf(stderr, "%s:%d: FAIL |%s - %s| <= %g  (%g vs %g)\n", __FILE__, __LINE__, #a, #b, (double)(tol), _a, _b); } \
} while (0)

#define CHECK_DONE(name) do { \
    printf("%-12s %3d passed, %d failed\n", name, check_pass, check_fail); \
    return check_fail ? 1 : 0; \
} while (0)

#endif
