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

#include <string.h>
#include "check.h"
#include "../core/utf8.h"

int main(void)
{
    uint32_t cp;
    CHECK_EQ(utf8_decode("a", &cp), 1); CHECK_EQ(cp, 'a');
    CHECK_EQ(utf8_decode("\xC4\x8D", &cp), 2); CHECK_EQ(cp, 0x10D);          /* č */
    CHECK_EQ(utf8_decode("\xE6\x9D\xB1", &cp), 3); CHECK_EQ(cp, 0x6771);      /* 東 */
    CHECK_EQ(utf8_decode("\xF0\x9F\x8C\x8D", &cp), 4); CHECK_EQ(cp, 0x1F30D); /* 🌍 */
    CHECK_EQ(utf8_decode("\xC0\x80", &cp), 1); CHECK_EQ(cp, UTF8_INVALID);    /* overlong */
    CHECK_EQ(utf8_decode("\xED\xA0\x80", &cp), 1); CHECK_EQ(cp, UTF8_INVALID);/* surrogate */
    CHECK_EQ(utf8_decode("\xFF", &cp), 1); CHECK_EQ(cp, UTF8_INVALID);
    CHECK_EQ(utf8_decode("", &cp), 0);

    char b[4];
    CHECK_EQ(utf8_encode(0x10D, b), 2); CHECK(memcmp(b, "\xC4\x8D", 2) == 0);
    CHECK_EQ(utf8_encode(0x1F30D, b), 4); CHECK(memcmp(b, "\xF0\x9F\x8C\x8D", 4) == 0);

    CHECK_EQ(utf8_width('a'), 1);
    CHECK_EQ(utf8_width(0x10D), 1);        /* č */
    CHECK_EQ(utf8_width(0x6771), 2);       /* 東 */
    CHECK_EQ(utf8_width(0xAC00), 2);       /* Hangul */
    CHECK_EQ(utf8_width(0x1F30D), 2);      /* emoji */
    CHECK_EQ(utf8_width(0x301), 0);        /* combining acute */
    CHECK_EQ(utf8_width(0x200B), 0);       /* zero width space */
    CHECK_EQ(utf8_width(0x2026), 1);       /* … */
    CHECK_EQ(utf8_width(0x2500), 1);       /* box drawing */
    CHECK_EQ(utf8_width(0x9), 0);

    CHECK_EQ(utf8_swidth("Zürich"), 6);
    CHECK_EQ(utf8_swidth("Ljubljana"), 9);
    CHECK_EQ(utf8_swidth("東京"), 4);
    CHECK_EQ(utf8_swidth("Kraków 東京"), 11);
    CHECK_EQ(utf8_swidth("e\xCC\x81"), 1);     /* e + combining acute (NFD) */
    CHECK_EQ(utf8_len("Zürich"), 6);
    CHECK_EQ(utf8_len("東京"), 2);

    CHECK_EQ(utf8_prev("Zürich", 3), 1);       /* byte 3 is after ü (2 bytes) */
    CHECK_EQ(utf8_prev("abc", 3), 2);
    CHECK_EQ(utf8_prev("abc", 0), 0);

    int used;
    CHECK_EQ(utf8_fit("東京都", 5, &used), 6); CHECK_EQ(used, 4);   /* 3rd wide char does not fit */
    CHECK_EQ(utf8_fit("Zürich", 3, &used), 4); CHECK_EQ(used, 3);

    CHECK(utf8_icontains("Kava z Aljošo", "aljošo"));
    CHECK(utf8_icontains("Kava z Aljošo", "ALJOŠO"));
    CHECK(utf8_icontains("Čevapčiči", "čevap"));
    CHECK(utf8_icontains("ČEVAPČIČI", "čevap"));
    CHECK(utf8_icontains("Zürich", "ZÜRICH"));
    CHECK(utf8_icontains("Łódź", "łódź"));
    CHECK(!utf8_icontains("Kranj", "kranjska"));
    CHECK(utf8_icontains("anything", ""));
    CHECK(!utf8_icontains(NULL, "x"));
    CHECK_EQ(utf8_fold(0x160), 0x161);   /* Š -> š */
    CHECK_EQ(utf8_fold(0x17D), 0x17E);   /* Ž -> ž */
    CHECK_EQ(0x10C | 1, 0x10D);          /* Č -> č */
    CHECK_DONE("utf8");
}
