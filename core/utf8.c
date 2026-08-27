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

/* utf8.c — see utf8.h */
#include "utf8.h"

#include <string.h>

int utf8_decode(const char *s, uint32_t *cp)
{
    const unsigned char *p = (const unsigned char *)s;
    if (p[0] == 0) { *cp = 0; return 0; }
    if (p[0] < 0x80) { *cp = p[0]; return 1; }
    int n;
    uint32_t c;
    if ((p[0] & 0xE0) == 0xC0) { n = 2; c = p[0] & 0x1F; }
    else if ((p[0] & 0xF0) == 0xE0) { n = 3; c = p[0] & 0x0F; }
    else if ((p[0] & 0xF8) == 0xF0) { n = 4; c = p[0] & 0x07; }
    else { *cp = UTF8_INVALID; return 1; }
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) { *cp = UTF8_INVALID; return 1; }
        c = (c << 6) | (p[i] & 0x3F);
    }
    /* reject overlongs, surrogates, out of range */
    if ((n == 2 && c < 0x80) || (n == 3 && c < 0x800) || (n == 4 && c < 0x10000)
        || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
        *cp = UTF8_INVALID;
        return 1;
    }
    *cp = c;
    return n;
}

int utf8_encode(uint32_t c, char *out)
{
    unsigned char *o = (unsigned char *)out;
    if (c < 0x80) { o[0] = (unsigned char)c; return 1; }
    if (c < 0x800) { o[0] = (unsigned char)(0xC0 | (c >> 6)); o[1] = (unsigned char)(0x80 | (c & 0x3F)); return 2; }
    if (c < 0x10000) {
        o[0] = (unsigned char)(0xE0 | (c >> 12)); o[1] = (unsigned char)(0x80 | ((c >> 6) & 0x3F));
        o[2] = (unsigned char)(0x80 | (c & 0x3F)); return 3;
    }
    o[0] = (unsigned char)(0xF0 | (c >> 18)); o[1] = (unsigned char)(0x80 | ((c >> 12) & 0x3F));
    o[2] = (unsigned char)(0x80 | ((c >> 6) & 0x3F)); o[3] = (unsigned char)(0x80 | (c & 0x3F)); return 4;
}

typedef struct { uint32_t lo, hi; } range_t;

/* zero-width: combining marks, format characters, variation selectors */
static const range_t zero[] = {
    {0x0300, 0x036F}, {0x0483, 0x0489}, {0x0591, 0x05BD}, {0x05BF, 0x05BF}, {0x05C1, 0x05C2},
    {0x05C4, 0x05C5}, {0x05C7, 0x05C7}, {0x0610, 0x061A}, {0x064B, 0x065F}, {0x0670, 0x0670},
    {0x06D6, 0x06DC}, {0x06DF, 0x06E4}, {0x06E7, 0x06E8}, {0x06EA, 0x06ED}, {0x0711, 0x0711},
    {0x0730, 0x074A}, {0x07A6, 0x07B0}, {0x0816, 0x082D}, {0x0859, 0x085B}, {0x08D3, 0x08E1},
    {0x08E3, 0x0902}, {0x093A, 0x093A}, {0x093C, 0x093C}, {0x0941, 0x0948}, {0x094D, 0x094D},
    {0x0951, 0x0957}, {0x0962, 0x0963}, {0x0981, 0x0981}, {0x09BC, 0x09BC}, {0x09C1, 0x09C4},
    {0x09CD, 0x09CD}, {0x0A01, 0x0A02}, {0x0A3C, 0x0A3C}, {0x0A41, 0x0A51}, {0x0A70, 0x0A71},
    {0x0E31, 0x0E31}, {0x0E34, 0x0E3A}, {0x0E47, 0x0E4E}, {0x0EB1, 0x0EB1}, {0x0EB4, 0x0EBC},
    {0x0EC8, 0x0ECD}, {0x0F18, 0x0F19}, {0x0F35, 0x0F35}, {0x0F37, 0x0F37}, {0x0F39, 0x0F39},
    {0x0F71, 0x0F7E}, {0x0F80, 0x0F84}, {0x0F86, 0x0F87}, {0x1AB0, 0x1AFF}, {0x1DC0, 0x1DFF},
    {0x200B, 0x200F}, {0x202A, 0x202E}, {0x2060, 0x2064}, {0x20D0, 0x20FF}, {0x302A, 0x302D},
    {0x3099, 0x309A}, {0xFE00, 0xFE0F}, {0xFE20, 0xFE2F}, {0xFEFF, 0xFEFF}, {0x1F3FB, 0x1F3FF},
    {0xE0100, 0xE01EF},
};

/* double-width: East Asian Wide/Fullwidth and emoji presentation blocks */
static const range_t wide[] = {
    {0x1100, 0x115F}, {0x231A, 0x231B}, {0x2329, 0x232A}, {0x23E9, 0x23EC}, {0x23F0, 0x23F0},
    {0x23F3, 0x23F3}, {0x25FD, 0x25FE}, {0x2614, 0x2615}, {0x2648, 0x2653}, {0x267F, 0x267F},
    {0x2693, 0x2693}, {0x26A1, 0x26A1}, {0x26AA, 0x26AB}, {0x26BD, 0x26BE}, {0x26C4, 0x26C5},
    {0x26CE, 0x26CE}, {0x26D4, 0x26D4}, {0x26EA, 0x26EA}, {0x26F2, 0x26F3}, {0x26F5, 0x26F5},
    {0x26FA, 0x26FA}, {0x26FD, 0x26FD}, {0x2705, 0x2705}, {0x270A, 0x270B}, {0x2728, 0x2728},
    {0x274C, 0x274C}, {0x274E, 0x274E}, {0x2753, 0x2755}, {0x2757, 0x2757}, {0x2795, 0x2797},
    {0x27B0, 0x27B0}, {0x27BF, 0x27BF}, {0x2B1B, 0x2B1C}, {0x2B50, 0x2B50}, {0x2B55, 0x2B55},
    {0x2E80, 0x303E}, {0x3041, 0x33FF}, {0x3400, 0x4DBF}, {0x4E00, 0x9FFF}, {0xA000, 0xA4CF},
    {0xA960, 0xA97F}, {0xAC00, 0xD7A3}, {0xF900, 0xFAFF}, {0xFE10, 0xFE19}, {0xFE30, 0xFE6F},
    {0xFF00, 0xFF60}, {0xFFE0, 0xFFE6}, {0x16FE0, 0x16FE3}, {0x17000, 0x18AFF}, {0x1B000, 0x1B2FF},
    {0x1F004, 0x1F004}, {0x1F0CF, 0x1F0CF}, {0x1F18E, 0x1F18E}, {0x1F191, 0x1F19A}, {0x1F200, 0x1F251},
    {0x1F300, 0x1F320}, {0x1F32D, 0x1F335}, {0x1F337, 0x1F37C}, {0x1F37E, 0x1F393}, {0x1F3A0, 0x1F3CA},
    {0x1F3CF, 0x1F3D3}, {0x1F3E0, 0x1F3F0}, {0x1F3F4, 0x1F3F4}, {0x1F3F8, 0x1F43E}, {0x1F440, 0x1F440},
    {0x1F442, 0x1F4FC}, {0x1F4FF, 0x1F53D}, {0x1F54B, 0x1F54E}, {0x1F550, 0x1F567}, {0x1F57A, 0x1F57A},
    {0x1F595, 0x1F596}, {0x1F5A4, 0x1F5A4}, {0x1F5FB, 0x1F64F}, {0x1F680, 0x1F6C5}, {0x1F6CC, 0x1F6CC},
    {0x1F6D0, 0x1F6D2}, {0x1F6D5, 0x1F6D7}, {0x1F6EB, 0x1F6EC}, {0x1F6F4, 0x1F6FC}, {0x1F7E0, 0x1F7EB},
    {0x1F90C, 0x1F93A}, {0x1F93C, 0x1F945}, {0x1F947, 0x1F9FF}, {0x1FA70, 0x1FAFF}, {0x20000, 0x2FFFD},
    {0x30000, 0x3FFFD},
};

static int in_ranges(uint32_t c, const range_t *r, size_t n)
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (c < r[mid].lo) hi = mid;
        else if (c > r[mid].hi) lo = mid + 1;
        else return 1;
    }
    return 0;
}

int utf8_width(uint32_t c)
{
    if (c == 0) return 0;
    if (c < 0x20 || (c >= 0x7F && c < 0xA0)) return 0;
    if (c < 0x300) return 1;
    if (in_ranges(c, zero, sizeof zero / sizeof zero[0])) return 0;
    if (in_ranges(c, wide, sizeof wide / sizeof wide[0])) return 2;
    return 1;
}

int utf8_swidth(const char *s)
{
    int w = 0;
    uint32_t c;
    int n;
    while ((n = utf8_decode(s, &c)) > 0) { w += utf8_width(c); s += n; }
    return w;
}

size_t utf8_len(const char *s)
{
    size_t k = 0;
    uint32_t c;
    int n;
    while ((n = utf8_decode(s, &c)) > 0) { k++; s += n; }
    return k;
}

size_t utf8_prev(const char *s, size_t pos)
{
    if (pos == 0) return 0;
    pos--;
    while (pos > 0 && ((unsigned char)s[pos] & 0xC0) == 0x80) pos--;
    return pos;
}

size_t utf8_fit(const char *s, int width, int *used)
{
    size_t i = 0;
    int w = 0;
    uint32_t c;
    int n;
    while ((n = utf8_decode(s + i, &c)) > 0) {
        int cw = utf8_width(c);
        if (w + cw > width) break;
        w += cw;
        i += (size_t)n;
    }
    if (used) *used = w;
    return i;
}

uint32_t utf8_fold(uint32_t c)
{
    if (c < 0x80) return (c >= 'A' && c <= 'Z') ? c + 32 : c;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;          /* Latin-1 capitals (except ×) */
    if (c >= 0x100 && c <= 0x137) return c | 1;                        /* Ā..ķ: even upper, odd lower */
    if (c >= 0x139 && c <= 0x148) return (c & 1) ? c + 1 : c;          /* Ĺ..ň: odd upper, even lower */
    if (c >= 0x14A && c <= 0x177) return c | 1;                        /* Ŋ..ŷ */
    if (c == 0x178) return 0xFF;                                       /* Ÿ */
    if (c >= 0x179 && c <= 0x17E) return (c & 1) ? c + 1 : c;          /* Ź..ž */
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 32;         /* Greek capitals */
    if (c >= 0x410 && c <= 0x42F) return c + 32;                       /* Cyrillic capitals */
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}

bool utf8_icontains(const char *hay, const char *needle)
{
    if (!needle || !*needle) return true;
    if (!hay) return false;
    for (const char *h = hay; *h; ) {
        const char *hp = h, *np = needle;
        for (;;) {
            uint32_t nc, hc;
            int nn = utf8_decode(np, &nc);
            if (nn == 0) return true;
            int hn = utf8_decode(hp, &hc);
            if (hn == 0 || utf8_fold(hc) != utf8_fold(nc)) break;
            hp += hn;
            np += nn;
        }
        uint32_t c;
        int n = utf8_decode(h, &c);
        if (n == 0) break;
        h += n;
    }
    return false;
}
